#include "services.hpp"
#include "ime_session.hpp"
#include <algorithm>
#include <fstream>
namespace reff {
namespace {
bool valid_subscription_id(std::string_view value) {
    if (value.empty() || value.size() > 160) return false;
    for (const char character : value)
        if (!((character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '.' || character == '-' || character == ':')) return false;
    return true;
}
// URL 不允许编码路径或相对跳转；逐级检查重解析点，拒绝资源根及目录中的 junction/symlink。
bool safe_path(std::string_view s){return !s.empty() && s.size()<=512 && s.front()!='/' && s.back()!='/' &&
    s.find("..") == s.npos && s.find_first_of("\\:%?#") == s.npos;}
bool safe_file(const std::filesystem::path& root,const std::filesystem::path& file){
    const auto r=std::filesystem::absolute(root).lexically_normal(),f=std::filesystem::absolute(file).lexically_normal();
    const auto rel=f.lexically_relative(r);if(rel.empty() || rel.is_absolute() || *rel.begin()=="..")return false;
    std::filesystem::path current;
    for(const auto& part:f){current/=part;const auto flags=GetFileAttributesW(current.c_str());
        if(flags==INVALID_FILE_ATTRIBUTES || (flags&FILE_ATTRIBUTE_REPARSE_POINT))return false;}
    std::error_code e;return std::filesystem::is_regular_file(f,e);
}
}
// 开发开关只读用户数据目录中的小型 JSON，默认关闭；不把插件标记当作开启权限，也不随包分发。
bool development_enabled(const std::filesystem::path& directory){
    try{
        const auto path=directory/L"development.json";
        if(!safe_file(directory,path)||std::filesystem::file_size(path)>1024)return false;
        std::ifstream stream(path);const auto value=Json::parse(stream,nullptr,false);
        return value.is_object()&&value.size()==1&&value.contains("enabled")&&value["enabled"].is_boolean()&&value["enabled"]==true;
    }catch(...){return false;}
}
// 清单只扫描本次会话已指定的目录，页面不获知本地路径；线程与资源所有权留在宿主。
void HostServices::configure(std::string session,std::filesystem::path assets,std::filesystem::path manifests,std::string game){
    session_=std::move(session);assets_=std::move(assets);manifests_=scan_manifests(manifests,manifest_errors_,game);
}
const PluginManifest* HostServices::manifest_for_id(std::string_view id)const{
    for(const auto& m:manifests_)if(m.id==id)return &m;return nullptr;
}
// 每个插件独立 authority；兼容现有 manifest/SDK，避免同源插件互相读取文档。
std::optional<HostServices::Route> HostServices::route_for_url(const std::string& url)const{
    if(!url.starts_with("reff://"))return {};const auto slash=url.find('/',7);if(slash==url.npos)return {};
    const auto host=url.substr(7,slash-7);auto relative=url.substr(slash+1);
    if(auto q=relative.find_first_of("?#");q!=relative.npos)relative.resize(q);
    if(!safe_path(relative))return {};
    if(host=="shell")return Route{true,nullptr,relative};
    if(!host.ends_with(".plugin"))return {};const auto* m=manifest_for_id(host.substr(0,host.size()-7));
    if(!m)return {};return Route{false,m,relative};
}
std::optional<std::filesystem::path> HostServices::resource_path(const std::string& url)const{
    const auto route=route_for_url(url);if(!route)return {};
    const auto root=route->shell?assets_:route->manifest->root;const auto path=root/std::filesystem::u8path(route->relative_path);
    if(!safe_file(root,path))return {};return path;
}
// 主文档更换先撤销旧请求与订阅；文档代次由宿主产生，页面字段不能改变身份。
bool HostServices::navigate(const std::string& url,const std::string& frame){
    release_page();const auto route=route_for_url(url);
    if(!route || (route->shell?route->relative_path!="index.html":route->relative_path!=route->manifest->entry))return false;
    bound_url_=url;frame_=frame;++document_generation_;
    const auto plugin=route->shell?std::string{}:route->manifest->id;
    page_={1,frame,document_generation_,frame+":"+std::to_string(document_generation_),plugin};return true;
}
// 仅绑定的当前文档可调用；外层先校验消息来源与当前文档随机票据。
void HostServices::query(const std::string& raw,std::shared_ptr<Reply> callback){
    try {
        if(raw.size()>max_message || page_.page_id.empty()){callback->Failure(403,"FORBIDDEN");return;}
        const auto route=route_for_url(bound_url_);
        if(!route){callback->Failure(403,"FORBIDDEN");return;}
        auto value = Json::parse(raw);
        if (!valid_page_request(value)) { callback->Failure(400, "INVALID_PAGE_REQUEST"); return; }
        auto method = value.value("method", std::string{});
        if (method == "ui.identity") {
            callback->Success(Json{{"pageId", page_.page_id}, {"kind", page_.plugin_id.empty() ? "shell" : "plugin"},
                                   {"pluginId", page_.plugin_id.empty() ? Json(nullptr) : Json(page_.plugin_id)}}.dump()); return;
        }
        if (method == "ui.status") {
            callback->Success(Json{{"ready", ready_}, {"epoch", epoch_}}.dump()); return;
        }
        // 受控开发接口：当前页面只能刷新自身，不能操作 Shell 或其他插件页面。
        if (method == "ui.reload") {
            if(!dev_enabled){callback->Failure(403,"DEV_DISABLED");return;}
            if (page_.plugin_id.empty()) { callback->Failure(403, "FORBIDDEN"); return; }
            callback->Success("{}");
            if(reload) reload();
            return;
        }
        if (method == "ui.dev.version") {
            if(!dev_enabled){callback->Failure(403,"DEV_DISABLED");return;}
            // Shell 可查询当前已选插件，隔离插件页只能查询自身，避免借接口枚举或刷新其他插件。
            std::string requested_id = page_.plugin_id;
            if (requested_id.empty()) {
                const auto params = value.value("params", Json::object());
                if (!params.is_object() || params.value("pluginId", std::string{}).empty()) {
                    callback->Failure(400, "INVALID_ARGUMENT"); return;
                }
                requested_id = params.value("pluginId", std::string{});
            }
            const auto* manifest = manifest_for_id(requested_id);
            if (!manifest) { callback->Failure(403, "FORBIDDEN"); return; }
            std::error_code error;
            const auto stamp = std::filesystem::last_write_time(manifest->root / L".reff-dev-version", error);
            if (error) { callback->Failure(404, "DEV_MARKER_NOT_FOUND"); return; }
            callback->Success(Json{{"version", std::to_string(stamp.time_since_epoch().count())}}.dump()); return;
        }
        if (method == "ui.plugins") {
            if (!page_.plugin_id.empty()) { callback->Failure(403, "FORBIDDEN"); return; }
            Json plugins = Json::array();
            for (const auto& manifest : manifests_) {auto summary=manifest_summary(manifest);
                summary["url"]="reff://"+manifest.id+".plugin/"+manifest.entry;plugins.push_back(std::move(summary));}
            callback->Success(Json{{"plugins", plugins}, {"errors", manifest_errors_}}.dump()); return;
        }
        if (method == "ui.close") {
            send({{"type", "close"}}); callback->Success("{}"); return;
        }
        if (method == "ui.input.focus") {
            const auto params = value.value("params", Json::object());
            // 原生代理只接受有限长度的 UTF-8 文本和整数坐标，避免页面消息导致跨进程控件分配异常。
            const bool valid_focus = params.is_object() && params.contains("active") && params["active"].is_boolean();
            const bool valid_text = !params.value("active", false) ||
                (params.value("text", std::string{}).size() <= 8192 &&
                 params.value("inputId", std::string{}).size() <= 128);
            if (!valid_focus || !valid_text) { callback->Failure(400, "INVALID_ARGUMENT"); return; }

            const auto input_id = params.value("inputId", std::string{});
            const bool active = params.value("active", false);
            const bool new_session = starts_new_ime_session(active_ime_input_id_, input_id, active);
            // 只更新 IME 会话信息；系统焦点由游戏侧透明 EDIT 管理，重复上报不能争抢焦点。
            (void)new_session;
            if (active) active_ime_input_id_ = input_id;
            else if (input_id.empty() || input_id == active_ime_input_id_) active_ime_input_id_.clear();
            Json request = params; request["type"] = "ime_focus"; request["sessionId"] = session_;
            if (!send(std::move(request))) { callback->Failure(429, "QUEUE_FULL"); return; }
            callback->Success("{}"); return;
        }
        // 内置设置页由 Shell 承载，Core 方法可在 Lua 未就绪时使用；第三方插件页不能取得该特权。
        const bool core_settings = page_.plugin_id.empty() &&
            (method == "reff.settings.get" || method == "reff.settings.set" || method == "reff.settings.reset" || method == "reff.settings.hotkey.capture");
        if (self_test_ && method == "test.finished") {
            value["params"]["documentGeneration"] = document_generation_;
            value["params"]["subscriptions"] = subscriptions_.size();
            value["params"]["pendingRequests"] = pending_.size();
            value["params"]["subscriptionCleanup"] = subscription_cleanup_.size();
            send({{"type", "test_result"}, {"result", value.at("params")}});
            callback->Success("{}"); return;
        }
        if (closed_ || (!ready_ && !core_settings)) { callback->Failure(503, closed_ ? "HOST_CLOSED" : "NOT_READY"); return; }
        value["id"] = frame_+":"+std::to_string(++query_sequence_);
        if (!valid_request(value)) { callback->Failure(400, "INVALID_ARGUMENT"); return; }
        Pending pending{callback, std::chrono::steady_clock::now() + std::chrono::seconds(5)};
        pending.owner = page_;
        const auto params = value.value("params", Json::object());
        if (method == "ui.subscribe") {
            if (!params.is_object() || !valid_name(params.value("eventName", std::string{})) ||
                !valid_subscription_id(params.value("subscriptionId", std::string{}))) {
                callback->Failure(400, "INVALID_ARGUMENT"); return;
            }
            const auto subscription_id = params["subscriptionId"].get<std::string>();
            if (!page_.plugin_id.empty() &&
                std::find(route->manifest->events.begin(), route->manifest->events.end(), params["eventName"].get<std::string>()) == route->manifest->events.end()) {
                callback->Failure(403, "FORBIDDEN"); return;
            }
            const bool pending_subscription = std::any_of(pending_.begin(), pending_.end(), [&](const auto& item) {
                return item.second.kind == Pending::Kind::subscribe && item.second.subscription_id == subscription_id;
            });
            if (subscriptions_.contains(subscription_id) || pending_subscription) {
                callback->Failure(409, "SUBSCRIPTION_EXISTS"); return;
            }
            pending.kind = Pending::Kind::subscribe;
            pending.subscription_id = subscription_id;
            pending.event_name = params["eventName"].get<std::string>();
            // Lua 注册表是会话级；内部句柄由文档命名，插件不能猜测或撤销其他 frame 的句柄。
            pending.native_subscription_id="wv2:"+frame_+":"+std::to_string(document_generation_)+":"+std::to_string(query_sequence_);
            value["params"]["subscriptionId"]=pending.native_subscription_id;
        } else if (method == "ui.unsubscribe") {
            if (!params.is_object() || !valid_subscription_id(params.value("subscriptionId", std::string{}))) {
                callback->Failure(400, "INVALID_ARGUMENT"); return;
            }
            pending.kind = Pending::Kind::unsubscribe;
            pending.subscription_id = params["subscriptionId"].get<std::string>();
            const auto subscription = subscriptions_.find(pending.subscription_id);
            if (subscription == subscriptions_.end() || !subscription->second.owner.matches(
                    page_.browser_id, page_.frame_id, page_.generation)) {
                callback->Failure(403, "SUBSCRIPTION_NOT_OWNED"); return;
            }
            pending.native_subscription_id=subscriptions_.at(pending.subscription_id).native_id;
            value["params"]["subscriptionId"]=pending.native_subscription_id;
        } else if (!page_.plugin_id.empty()) {
            if (std::find(route->manifest->methods.begin(), route->manifest->methods.end(), method) == route->manifest->methods.end() ||
                !method.starts_with(page_.plugin_id + ".")) {
                callback->Failure(403, "FORBIDDEN"); return;
            }
        }
        if (pending_.size() >= queue_limit) { callback->Failure(429, "QUEUE_FULL"); return; }
        value["type"] = "request"; value["sessionId"] = session_; value["epoch"] = epoch_;
        value["pageId"] = page_.page_id;
        if (!page_.plugin_id.empty()) value["pluginId"] = page_.plugin_id;
        if (!send(value)) { callback->Failure(429, "QUEUE_FULL"); return; }
        pending_.emplace(value["id"].get<std::string>(), std::move(pending));
    } catch (const std::exception&) { callback->Failure(400, "INVALID_ARGUMENT"); }
    return;
}
void HostServices::release_page() {
    for (const auto& [id, subscription] : subscriptions_) subscription_cleanup_.insert(subscription.native_id);
    for (const auto& [id, pending] : pending_)
        if (pending.kind == Pending::Kind::subscribe) subscription_cleanup_.insert(pending.native_subscription_id);
    flush_subscription_cleanup();
    subscriptions_.clear();
    fail_pending("PAGE_UNLOADED");
    page_ = {};
}

// UI 线程重试清理消息；发送队列暂满时保留句柄，下轮 tick 再提交，不静默遗失撤销请求。
void HostServices::flush_subscription_cleanup() {
    while (!subscription_cleanup_.empty()) {
        const auto it = subscription_cleanup_.begin();
        if (!send({{"type", "request"}, {"id", "cleanup:" + frame_+":"+std::to_string(++cleanup_sequence_)},
            {"sessionId", session_}, {"epoch", epoch_}, {"method", "ui.unsubscribe"},
            {"params", {{"subscriptionId", *it}}}})) break;
        subscription_cleanup_.erase(it);
    }
}

// 失效会话中的 Promise 一次性失败，禁止宿主恢复后重放写请求。
void HostServices::fail_pending(const std::string& reason) {
    auto pending = std::move(pending_); pending_.clear();
    for (auto& [id, value] : pending) if (value.callback) value.callback->Failure(503, reason);
}


// IPC epoch 与宿主页面身份共同过滤响应和事件；脚本重置不重放写操作。
bool HostServices::dispatch(const Json& message){
    auto type = message.value("type", std::string{});
    if (type == "ready") {
        auto epoch = message.at("epoch").get<std::uint64_t>();
        if (epoch != epoch_) {fail_pending("SCRIPT_RESET");subscriptions_.clear();subscription_cleanup_.clear();}
        epoch_ = epoch; ready_ = message.value("ready", false); return true;
    }
    if (type == "response") {
        if (message.value("epoch", std::uint64_t{}) != epoch_) return true;
        auto it = pending_.find(message.value("id", std::string{}));
        if (it == pending_.end()) return true;
        auto pending = std::move(it->second); pending_.erase(it);
        if (!pending.owner.matches(page_.browser_id, page_.frame_id, page_.generation)) return true;
        auto callback = pending.callback;
        if (!message.contains("error")) {
            if (pending.kind == Pending::Kind::subscribe) {
                if (callback) subscriptions_[pending.subscription_id] = Subscription{pending.event_name, pending.owner,pending.native_subscription_id};
                else subscription_cleanup_.insert(pending.native_subscription_id);
            }
            else if (pending.kind == Pending::Kind::unsubscribe)
                subscriptions_.erase(pending.subscription_id);
        }
        if (callback) {
            if (message.contains("error")) callback->Failure(500, message["error"].dump());
            else {auto result=message.at("result");
                if(pending.kind!=Pending::Kind::normal && result.is_object())result["subscriptionId"]=pending.subscription_id;
                callback->Success(result.dump());}
        }
    } else if (type == "event") {
        // 事件消息已经由 Lua 注册表校验；宿主只向本页面已确认的订阅句柄派发。
        if (message.value("epoch", std::uint64_t{}) != epoch_) return true;
        const auto event_name = message.value("eventName", std::string{});
        if (!valid_name(event_name) || !message.contains("payload")) return true;
        const auto payload = message["payload"].dump();
        if (payload.size() > max_message) return true;
        for (const auto& [subscription_id, subscription] : subscriptions_) {
            if (subscription.event_name != event_name || !subscription.owner.matches(
                    page_.browser_id, page_.frame_id, page_.generation)) continue;
            if(emit)emit(subscription_id,message["payload"]);
        }

    } else return false;
    return true;
}

// UI 周期只做有界超时和清理，Lua 执行仍在游戏侧回调中。
void HostServices::tick(){
    const auto now=std::chrono::steady_clock::now();
    for(auto it=pending_.begin();it!=pending_.end();){if(it->second.deadline<now){
        if(it->second.kind==Pending::Kind::subscribe)subscription_cleanup_.insert(it->second.native_subscription_id);
        const auto cb=it->second.callback;it=pending_.erase(it);if(cb)cb->Failure(408,"TIMEOUT");
    }else ++it;}flush_subscription_cleanup();
}
Json HostServices::counts()const{return {{"subscriptions",subscriptions_.size()},{"pendingRequests",pending_.size()},{"subscriptionCleanup",subscription_cleanup_.size()},{"documentGeneration",document_generation_}};}
}
