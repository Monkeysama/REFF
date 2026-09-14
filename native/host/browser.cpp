#include "browser.hpp"
#include "include/cef_command_line.h"
#include "include/cef_parser.h"
#include "include/cef_scheme.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_stream_resource_handler.h"
#include "include/wrapper/cef_helpers.h"
#include <set>
#include <fstream>
#include <mutex>
#include <algorithm>
// 预览候选构建标识：日志目录规范化版本。

#ifndef REFF_ENABLE_RESIZE_DIAGNOSTICS
#define REFF_ENABLE_RESIZE_DIAGNOSTICS 0
#endif

namespace reff {
namespace {
std::mutex resize_log_mutex;
std::uint64_t resize_log_tick{};
// REFF 诊断日志统一写入游戏数据目录，避免污染 reframework 根目录及官方日志。
std::filesystem::path reff_log_dir(const std::filesystem::path& assets) {
    // assets 位于 reframework/reff/ui，向上两级回到 reframework 根目录。
    return assets / L".." / L".." / L"data" / L"REFF" / L"log";
}
#if defined(REFF_ENABLE_IME_DIAGNOSTICS)
std::mutex ime_log_mutex;
#endif
// 宿主只验证订阅句柄的有限字符集；事件是否声明由游戏侧 Lua 注册表最终确认。
bool valid_subscription_id(std::string_view value) {
    if (value.empty() || value.size() > 160) return false;
    for (const char character : value)
        if (!((character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '.' || character == '-' || character == ':')) return false;
    return true;
}

}

void BrowserClient::resize_log(const std::string& message, bool sampled) {
#if REFF_ENABLE_RESIZE_DIAGNOSTICS
    const auto now = GetTickCount64();
    if (sampled && resize_log_tick && now - resize_log_tick < 500) return;
    if (sampled) resize_log_tick = now;
    std::lock_guard lock(resize_log_mutex);
    try {
        const auto dir = reff_log_dir(assets_);
        std::filesystem::create_directories(dir);
        std::ofstream file(dir / L"reff_resize.log", std::ios::app);
        file << now << " " << message << "\n";
    } catch (...) {}
#else
    (void)message; (void)sampled;
#endif
}

// 每次创建新的 CEF 会话时清空上一轮游戏的专用诊断日志；本轮运行期间继续追加。
void BrowserClient::reset_resize_log() {
    try {
        const auto dir = reff_log_dir(assets_);
        std::filesystem::create_directories(dir);
        std::ofstream file(dir / L"reff_resize.log", std::ios::trunc);
    } catch (...) {}
}

// 每次创建新的 CEF 会话时清空宿主侧 IME 诊断日志，避免不同游戏运行混在一起。
void BrowserClient::reset_ime_log() {
#if defined(REFF_ENABLE_IME_DIAGNOSTICS)
    try {
        const auto dir = reff_log_dir(assets_);
        std::filesystem::create_directories(dir);
        std::ofstream file(dir / L"reff_host_ime.log", std::ios::trunc);
    } catch (...) {}
#endif
}

// 宿主 IME 诊断使用独立文件，记录线程安全的短文本和坐标摘要。
void BrowserClient::ime_log(const std::string& message) {
#if defined(REFF_ENABLE_IME_DIAGNOSTICS)
    std::lock_guard lock(ime_log_mutex);
    try {
        const auto dir = reff_log_dir(assets_);
        std::filesystem::create_directories(dir);
        std::ofstream file(dir / L"reff_host_ime.log", std::ios::app);
        file << GetTickCount64() << " " << message << "\n";
    } catch (...) {}
#else
    (void)message;
#endif
}

// M3 路由只接受本地、未编码的相对资源名；拒绝目录穿越、协议跳转和外部网络回退。
static bool safe_resource_name(std::string_view value) {
    return !value.empty() && value.size() <= 512 && value.front() != '/' && value.back() != '/' &&
        value.find("..") == std::string_view::npos && value.find('\\') == std::string_view::npos &&
        value.find(':') == std::string_view::npos && value.find('%') == std::string_view::npos &&
        value.find('?') == std::string_view::npos && value.find('#') == std::string_view::npos;
}

// 运行时只用确定性的绝对路径规范化检查资源根边界；URL 层已拒绝 ..、反斜杠、绝对路径和编码绕过。
static bool beneath_without_reparse(const std::filesystem::path& root, const std::filesystem::path& candidate) {
    const auto absolute_root = std::filesystem::absolute(root).lexically_normal();
    const auto absolute_candidate = std::filesystem::absolute(candidate).lexically_normal();
    const auto relative = absolute_candidate.lexically_relative(absolute_root);
    if (relative.empty() || relative.is_absolute() || *relative.begin() == "..") return false;
    for (const auto& component : relative)
        if (component == "." || component == "..") return false;
    return true;
}

BrowserClient::BrowserClient(std::wstring session, std::filesystem::path assets, std::filesystem::path manifests, bool self_test)
    : assets_(std::move(assets)), session_(narrow(session)), self_test_(self_test) {
    reset_resize_log();
    reset_ime_log();
    gate_.open();
    manifests_ = scan_manifests(manifests, manifest_errors_);
    frame_.open(session, false);
    router_ = CefMessageRouterBrowserSide::Create(CefMessageRouterConfig{});
    router_->AddHandler(this, false);
    channel_.start(session, false, [this](Json value) {
        if (!incoming_.push(std::move(value))) { /* 有界收包队列，超量由请求超时暴露。 */ }
    });
}

// 查找已通过 manifest 校验的插件；网页不能凭 URL 自行制造插件身份。
const PluginManifest* BrowserClient::manifest_for_id(std::string_view id) const {
    const auto it = std::find_if(manifests_.begin(), manifests_.end(), [&](const auto& manifest) { return manifest.id == id; });
    return it == manifests_.end() ? nullptr : &*it;
}

// 解析 Shell/插件页 URL；插件页的 entry 仍必须落在对应 manifest 资源根内。
std::optional<BrowserClient::PageRoute> BrowserClient::route_for_url(const std::string& url) const {
    constexpr std::string_view shell_prefix = "reff://shell/";
    constexpr std::string_view plugin_prefix = "reff://plugin/";
    if (url.starts_with(shell_prefix)) {
        const auto path = url.substr(shell_prefix.size());
        // Shell 的入口及构建产物均位于同一资源根；允许安全相对路径，禁止目录穿越。
        if (path == "index.html" || safe_resource_name(path)) return PageRoute{true, nullptr, path};
        return std::nullopt;
    }
    if (!url.starts_with(plugin_prefix)) return std::nullopt;
    const auto rest = url.substr(plugin_prefix.size());
    const auto slash = rest.find('/');
    if (slash == std::string::npos) return std::nullopt;
    const auto plugin_id = rest.substr(0, slash);
    const auto relative = rest.substr(slash + 1);
    const auto* manifest = manifest_for_id(plugin_id);
    if (!manifest || !safe_resource_name(relative)) return std::nullopt;
    return PageRoute{false, manifest, relative};
}

// 将受限 URL 映射到本地资源；不存在或越界的文件统一返回空值。
std::optional<std::filesystem::path> BrowserClient::resource_path(const std::string& url) const {
    const auto route = route_for_url(url);
    if (!route) return std::nullopt;
    const auto root = route->shell ? assets_ : route->manifest->root;
    const auto candidate = root / std::filesystem::u8path(route->relative_path);
    if (!beneath_without_reparse(root, candidate)) return std::nullopt;
    std::error_code error;
    if (!std::filesystem::is_regular_file(candidate, error)) return std::nullopt;
    return candidate;
}

// 导航只允许框架 Shell 或已发现插件的页面入口，其他请求由 CEF 取消。
bool BrowserClient::page_url_allowed(const std::string& url) const {
    const auto route = route_for_url(url);
    if (!route) return false;
    if (route->shell) return true;
    return route->relative_path == route->manifest->entry;
}

// 页面生命周期建立后启动轻量轮询；CEF 持有引用，关闭标记阻止再次调度。
void BrowserClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD(); browser_ = browser; browser_->GetHost()->SetFocus(true); tick();
}
void BrowserClient::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    // 先设置关闭屏障，再撤销路由和挂起请求；之后到达的 IPC 只能被丢弃。
    closed_ = true;
    gate_.begin_close();
    release_page();
    router_->OnBeforeClose(browser);
    fail_pending("HOST_CLOSED");
    subscriptions_.clear();
    channel_.stop();
    browser_ = nullptr;
    CefQuitMessageLoop();
}
void BrowserClient::OnPaint(CefRefPtr<CefBrowser>, PaintElementType type, const RectList&, const void* buffer, int width, int height) {
    if (type == PET_VIEW) {
        const bool matches_viewport = width == viewport_width_ && height == viewport_height_;
        // 延迟到达的旧尺寸帧不能冒充当前 generation；仍写入共享帧供渐进显示，但标记为过期。
        frame_.write(buffer, width, height, matches_viewport ? viewport_generation_ : 0);
        // 记录首帧像素特征，区分页面空白与 DX12 提交异常；不保存图像数据。
        if (!ready_) {
            const auto* pixels = static_cast<const unsigned char*>(buffer);
            std::uint64_t sample = 0;
            for (int i = 0; i < 16 && pixels; ++i) sample = sample * 131 + pixels[i * 4];
            resize_log("cef_first_frame pixels=" + std::to_string(sample) + " size=" +
                       std::to_string(width) + "x" + std::to_string(height));
        }
        resize_log("cef_paint generation=" + std::to_string(matches_viewport ? viewport_generation_ : 0) +
                   " actual=" + std::to_string(width) + "x" + std::to_string(height) +
                   " expected=" + std::to_string(viewport_width_) + "x" + std::to_string(viewport_height_), true);
        if (viewport_resize_in_flight_ && matches_viewport) {
            viewport_resize_in_flight_ = false;
            pump_viewport_resize();
        }
    }
}

void BrowserClient::pump_viewport_resize() {
    CEF_REQUIRE_UI_THREAD();
    if (!browser_ || !viewport_resize_pending_) return;
    const auto now = GetTickCount64();
    // Wilds 的 CEF 离屏大画布重排耗时较高；100ms 上限降低拖动延迟，同时等待当前帧完成避免请求堆积。
    if (viewport_resize_in_flight_ || (last_viewport_resize_tick_ && now - last_viewport_resize_tick_ < 100)) return;
    viewport_width_ = pending_viewport_width_; viewport_height_ = pending_viewport_height_;
    viewport_generation_ = pending_viewport_generation_;
    viewport_resize_pending_ = false; viewport_resize_in_flight_ = true; last_viewport_resize_tick_ = now;
    resize_log("viewport_apply generation=" + std::to_string(viewport_generation_) + " size=" +
               std::to_string(viewport_width_) + "x" + std::to_string(viewport_height_));
    browser_->GetHost()->WasResized();
    // 静态页面可能不会因 WasResized 单独触发 OSR OnPaint；显式失效视图，保证新尺寸尽快产出一帧。
    browser_->GetHost()->Invalidate(PET_VIEW);
}

// 所有导航限定为本地入口，阻止新页面借用已授权的主 frame。
bool BrowserClient::OnBeforeBrowse(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                  CefRefPtr<CefRequest> request, bool, bool) {
    CEF_REQUIRE_UI_THREAD();
    const auto url = request->GetURL().ToString();
    const auto route = route_for_url(url);
    if (!frame->IsMain()) {
        // Isolated Page 由 Shell 的 iframe 承载；只允许已登记插件的入口文档，禁止子 frame 越权导航。
        const bool allowed = route && !route->shell && route->manifest->ui_mode == "isolated-page" &&
            route->relative_path == route->manifest->entry;
        if (!allowed) resize_log("navigation_reject subframe url=" + url);
        return !allowed;
    }
    if (!page_url_allowed(url)) {
        resize_log("navigation_reject mainframe url=" + url);
        return true;
    }
    release_page();
    router_->OnBeforeBrowse(browser, frame);
    return false;
}

// 导航提交后才授予身份：重载可更换 CEF frame 标识，不能沿用 OnBeforeBrowse 的旧 frame。
void BrowserClient::OnLoadStart(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, TransitionType) {
    CEF_REQUIRE_UI_THREAD();
    if (!frame->IsMain()) return;
    const auto route = route_for_url(frame->GetURL().ToString());
    if (!route || (!route->shell && route->relative_path != route->manifest->entry)) return;
    page_ = {browser->GetIdentifier(), frame->GetIdentifier().ToString(), ++document_generation_,
             route->shell ? "shell:" + std::to_string(document_generation_)
                          : "plugin:" + route->manifest->id + ":" + std::to_string(document_generation_),
             route->shell ? std::string{} : route->manifest->id};
}
CefRefPtr<CefResourceRequestHandler> BrowserClient::GetResourceRequestHandler(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
    CefRefPtr<CefRequest>, bool, bool, const CefString&, bool& disable_default_handling) {
    disable_default_handling = true; return this;
}
BrowserClient::ReturnValue BrowserClient::OnBeforeResourceLoad(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
    CefRefPtr<CefRequest> request, CefRefPtr<CefCallback>) {
    const auto path = resource_path(request->GetURL().ToString());
    if (!path) resize_log("resource_reject url=" + request->GetURL().ToString());
    return path ? RV_CONTINUE : RV_CANCEL;
}

// CEF OSR 的组合输入范围使用网页逻辑坐标；转发到游戏侧后再按面板缩放映射到屏幕。
void BrowserClient::OnImeCompositionRangeChanged(CefRefPtr<CefBrowser>, const CefRange&, const RectList& character_bounds) {
    CEF_REQUIRE_UI_THREAD();
    if (character_bounds.empty() || closed_) return;
    const auto& rect = character_bounds.front();
    ime_log("composition_range x=" + std::to_string(rect.x) + " y=" + std::to_string(rect.y) +
            " w=" + std::to_string(rect.width) + " h=" + std::to_string(rect.height));
    channel_.send({{"type", "ime_bounds"}, {"sessionId", session_}, {"x", rect.x}, {"y", rect.y}, {"width", rect.width}, {"height", rect.height}});
}
CefRefPtr<CefResourceHandler> BrowserClient::GetResourceHandler(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefRequest> request) {
    const auto path = resource_path(request->GetURL().ToString());
    if (!path || request->GetMethod() != "GET") return nullptr;
    const auto name = path->filename().string();
    auto stream = CefStreamReader::CreateForFile(path->wstring());
    if (!stream) return nullptr;
    std::string mime = name.ends_with(".js") || name.ends_with(".mjs") ? "text/javascript" : name.ends_with(".css") ? "text/css" : name.ends_with(".svg") ? "image/svg+xml" : "text/html";
    CefResponse::HeaderMap headers;
    const auto route = route_for_url(request->GetURL().ToString());
    const auto frame_policy = route && route->shell
        ? "frame-ancestors 'none'; frame-src reff:"
        : route && !route->shell && route->manifest->ui_mode == "isolated-page"
        ? "frame-ancestors reff://shell" : "frame-ancestors 'none'";
    // 隔离页只可额外加载 Shell 下的公共 UI 目录；其他 Shell 脚本、外部网络和任意样式仍被 CSP 拒绝。
    const auto shared_sources = route && !route->shell
        ? " script-src 'self' reff://shell/shared/; style-src 'self' reff://shell/shared/;"
        : " script-src 'self'; style-src 'self';";
    headers.insert({"Content-Security-Policy", std::string("default-src 'none';") + shared_sources +
        " img-src 'self'; connect-src 'none'; base-uri 'none'; " + frame_policy + "; form-action 'none'"});
    // Vite 产物带 crossorigin 属性；允许同一自定义协议的本地资源完成模块加载，不开放任何网络来源。
    headers.insert({"Access-Control-Allow-Origin", "*"});
    headers.insert({"X-Content-Type-Options", "nosniff"});
    // 本地 Shell/插件资源随 REFF 部署更新；禁止 CEF 复用旧 CSP 和旧构建产物。
    headers.insert({"Cache-Control", "no-store, no-cache, must-revalidate"});
    resize_log("resource_open url=" + request->GetURL().ToString() + " mime=" + mime);
    return new CefStreamResourceHandler(200, "OK", mime, headers, stream);
}
bool BrowserClient::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
    CefProcessId source, CefRefPtr<CefProcessMessage> message) {
    return router_->OnProcessMessageReceived(browser, frame, source, message);
}

// 自检只由本机启动参数启用，真实发布页面不会自动修改测试状态。
void BrowserClient::OnLoadEnd(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, int) {
    if (self_test_ && frame->IsMain()) frame->ExecuteJavaScript(
        CefCommandLine::GetGlobalCommandLine()->HasSwitch("reff-stress-test")
            ? "window.reffSmokeReloadTarget = 20;" : "window.reffSmokeReloadTarget = 1;", frame->GetURL(), 0);
    if (self_test_ && frame->IsMain() && frame->GetURL() == "reff://shell/index.html") frame->ExecuteJavaScript(R"JS(
        // 仅独立 smoke 启用：从真实 renderer 发起身份查询和伪造请求，完成后才允许业务回环。
        (async () => {
            const query = request => new Promise((resolve, reject) => window.cefQuery({
                request: JSON.stringify(request), onSuccess: raw => resolve(JSON.parse(raw)),
                onFailure: (code, reason) => reject({code, reason})
            }));
            const identity = await query({method: 'ui.identity', params: {}});
            if (identity.kind !== 'shell' || identity.pluginId !== null || !identity.pageId.startsWith('shell:')) throw new Error('identity mismatch');
            for (const field of ['pluginId', 'pageId', 'frameId', 'sessionId', 'epoch', 'id', 'type']) {
                let rejected = false;
                try { await query({method: 'ui.status', params: {}, [field]: 'forged'}); }
                catch (error) { if (error.code !== 400 || error.reason !== 'INVALID_PAGE_REQUEST') throw error; rejected = true; }
                if (!rejected) throw new Error('forged identity accepted: ' + field);
            }
            let ready = false;
            for (let attempt = 0; attempt < 100; ++attempt) {
                if ((await query({method: 'ui.status', params: {}})).ready) { ready = true; break; }
                await new Promise(resolve => setTimeout(resolve, 50));
            }
            if (!ready) throw new Error('backend readiness timeout');
            let rejected = false;
            try { await query({method: 'ui.unsubscribe', params: {subscriptionId: 'unowned-handle'}}); }
            catch (error) { if (error.code !== 403 || error.reason !== 'SUBSCRIPTION_NOT_OWNED') throw error; rejected = true; }
            if (!rejected) throw new Error('unowned subscription accepted');
            // 同一真实 frame 重载（压力模式 20 次）：旧订阅清理后固定探针句柄才能重新建立。
            const previousPage = sessionStorage.getItem('reff-smoke-page');
            if (previousPage === identity.pageId) throw new Error('document identity reused after reload');
            await query({method: 'ui.subscribe', params: {
                eventName: 'example.vue.refreshed', subscriptionId: 'smoke-page-probe'
            }});
            const reloads = Number(sessionStorage.getItem('reff-smoke-reloads') || 0);
            if (reloads < window.reffSmokeReloadTarget) {
                sessionStorage.setItem('reff-smoke-page', identity.pageId);
                sessionStorage.setItem('reff-smoke-reloads', String(reloads + 1));
                location.reload();
                return;
            }
            await query({method: 'ui.unsubscribe', params: {subscriptionId: 'smoke-page-probe'}});
            sessionStorage.removeItem('reff-smoke-page');
            sessionStorage.removeItem('reff-smoke-reloads');
            const before = await query({method: 'example.vue.get', params: {}});
            const after = await query({method: 'example.vue.refresh', params: {}});
            window.reffSelfTest = {passed: after.refreshCount === before.refreshCount + 1};
        })().catch(error => window.cefQuery({
            request: JSON.stringify({method: 'test.finished', params: {passed: false, identityError: String(error.message || JSON.stringify(error))}}),
            onSuccess: () => {}, onFailure: () => {}
        }));
    )JS", frame->GetURL(), 0);
}

// 页面加载失败时写入专用日志，避免污染 REFramework 主日志。
void BrowserClient::OnLoadError(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, ErrorCode code,
                                const CefString& text, const CefString& failed_url) {
    resize_log(std::string("load_error frame=") + (frame->IsMain() ? "main" : "subframe") +
        " code=" + std::to_string(static_cast<int>(code)) +
        " text=" + text.ToString() + " url=" + failed_url.ToString());
}

// 捕获 Chromium 控制台错误，定位脚本执行或模块加载失败。
bool BrowserClient::OnConsoleMessage(CefRefPtr<CefBrowser>, cef_log_severity_t level,
                                     const CefString& message, const CefString& source, int line) {
    resize_log("console level=" + std::to_string(static_cast<int>(level)) +
        " line=" + std::to_string(line) + " source=" + source.ToString() +
        " message=" + message.ToString());
    return false;
}

// 网页请求绑定真实主 frame；查询 ID 由 CEF 分配，不信任页面伪造身份。
bool BrowserClient::OnQuery(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int64_t query,
                           const CefString& raw, bool persistent, CefRefPtr<Callback> callback) {
    try {
        CEF_REQUIRE_UI_THREAD();
        const auto route = route_for_url(frame->GetURL().ToString());
        if (!frame->IsMain() || !route || persistent || raw.length() > max_message ||
            (route->shell ? !page_.plugin_id.empty() : page_.plugin_id != route->manifest->id) ||
            !page_.matches(browser->GetIdentifier(), frame->GetIdentifier().ToString(), document_generation_)) {
            callback->Failure(403, "FORBIDDEN"); return true;
        }
        if (closed_ || gate_.closing()) { callback->Failure(503, "HOST_CLOSED"); return true; }
        auto value = Json::parse(raw.ToString());
        if (!valid_page_request(value)) { callback->Failure(400, "INVALID_PAGE_REQUEST"); return true; }
        auto method = value.value("method", std::string{});
        if (method == "ui.identity") {
            callback->Success(Json{{"pageId", page_.page_id}, {"kind", page_.plugin_id.empty() ? "shell" : "plugin"},
                                   {"pluginId", page_.plugin_id.empty() ? Json(nullptr) : Json(page_.plugin_id)}}.dump()); return true;
        }
        if (method == "ui.status") {
            callback->Success(Json{{"ready", ready_}, {"epoch", epoch_}}.dump()); return true;
        }
        if (method == "ui.plugins") {
            if (!page_.plugin_id.empty()) { callback->Failure(403, "FORBIDDEN"); return true; }
            Json plugins = Json::array();
            for (const auto& manifest : manifests_) plugins.push_back(manifest_summary(manifest));
            callback->Success(Json{{"plugins", plugins}, {"errors", manifest_errors_}}.dump()); return true;
        }
        if (method == "ui.close") {
            channel_.send({{"type", "close"}}); callback->Success("{}"); return true;
        }
        if (method == "ui.input.focus") {
            const auto params = value.value("params", Json::object());
            // 原生代理只接受有限长度的 UTF-8 文本和整数坐标，避免页面消息导致跨进程控件分配异常。
            const bool valid_focus = params.is_object() && params.contains("active") && params["active"].is_boolean();
            const bool valid_text = !params.value("active", false) ||
                (params.value("text", std::string{}).size() <= 8192 &&
                 params.value("inputId", std::string{}).size() <= 128);
            if (!valid_focus || !valid_text) { callback->Failure(400, "INVALID_ARGUMENT"); return true; }
            ime_log("focus_request page=" + page_.page_id + " active=" +
                    std::string(params.value("active", false) ? "1" : "0") +
                    " x=" + std::to_string(params.value("x", 0)) + " y=" + std::to_string(params.value("y", 0)));
            const auto input_id = params.value("inputId", std::string{});
            if (params.value("active", false)) active_ime_input_id_ = input_id;
            else if (input_id.empty() || input_id == active_ime_input_id_) active_ime_input_id_.clear();
            // 隔离 iframe 的输入焦点经过 Shell 转发后，显式恢复 CEF 宿主焦点，确保 ImeCommitText 仍投递到当前网页控件。
            // 原生透明 EDIT 代理只负责接收 Windows IME 消息，不应取代 CEF renderer 的焦点归属。
            if (params.value("active", false) && browser_) browser_->GetHost()->SetFocus(true);
            Json request = params; request["type"] = "ime_focus"; request["sessionId"] = session_;
            if (!channel_.send(std::move(request))) { callback->Failure(429, "QUEUE_FULL"); return true; }
            callback->Success("{}"); return true;
        }
        // 内置设置页由 Shell 承载，Core 方法可在 Lua 未就绪时使用；第三方插件页不能取得该特权。
        const bool core_settings = page_.plugin_id.empty() &&
            (method == "reff.settings.get" || method == "reff.settings.set" || method == "reff.settings.reset");
        if (self_test_ && method == "test.finished") {
            value["params"]["documentGeneration"] = document_generation_;
            value["params"]["subscriptions"] = subscriptions_.size();
            value["params"]["pendingRequests"] = pending_.size();
            value["params"]["subscriptionCleanup"] = subscription_cleanup_.size();
            channel_.send({{"type", "test_result"}, {"result", value.at("params")}});
            callback->Success("{}"); return true;
        }
        if (closed_ || gate_.closing() || (!ready_ && !core_settings)) { callback->Failure(503, closed_ ? "HOST_CLOSED" : "NOT_READY"); return true; }
        value["id"] = std::to_string(query);
        if (!valid_request(value)) { callback->Failure(400, "INVALID_ARGUMENT"); return true; }
        Pending pending{callback, std::chrono::steady_clock::now() + std::chrono::seconds(5)};
        pending.owner = page_;
        const auto params = value.value("params", Json::object());
        if (method == "ui.subscribe") {
            if (!params.is_object() || !valid_name(params.value("eventName", std::string{})) ||
                !valid_subscription_id(params.value("subscriptionId", std::string{}))) {
                callback->Failure(400, "INVALID_ARGUMENT"); return true;
            }
            const auto subscription_id = params["subscriptionId"].get<std::string>();
            if (!page_.plugin_id.empty() &&
                std::find(route->manifest->events.begin(), route->manifest->events.end(), params["eventName"].get<std::string>()) == route->manifest->events.end()) {
                callback->Failure(403, "FORBIDDEN"); return true;
            }
            const bool pending_subscription = std::any_of(pending_.begin(), pending_.end(), [&](const auto& item) {
                return item.second.kind == Pending::Kind::subscribe && item.second.subscription_id == subscription_id;
            });
            if (subscriptions_.contains(subscription_id) || pending_subscription) {
                callback->Failure(409, "SUBSCRIPTION_EXISTS"); return true;
            }
            pending.kind = Pending::Kind::subscribe;
            pending.subscription_id = subscription_id;
            pending.event_name = params["eventName"].get<std::string>();
        } else if (method == "ui.unsubscribe") {
            if (!params.is_object() || !valid_subscription_id(params.value("subscriptionId", std::string{}))) {
                callback->Failure(400, "INVALID_ARGUMENT"); return true;
            }
            pending.kind = Pending::Kind::unsubscribe;
            pending.subscription_id = params["subscriptionId"].get<std::string>();
            const auto subscription = subscriptions_.find(pending.subscription_id);
            if (subscription == subscriptions_.end() || !subscription->second.owner.matches(
                    page_.browser_id, page_.frame_id, page_.generation)) {
                callback->Failure(403, "SUBSCRIPTION_NOT_OWNED"); return true;
            }
        } else if (!page_.plugin_id.empty()) {
            if (std::find(route->manifest->methods.begin(), route->manifest->methods.end(), method) == route->manifest->methods.end() ||
                !method.starts_with(page_.plugin_id + ".")) {
                callback->Failure(403, "FORBIDDEN"); return true;
            }
        }
        if (pending_.size() >= queue_limit) { callback->Failure(429, "QUEUE_FULL"); return true; }
        value["type"] = "request"; value["sessionId"] = session_; value["epoch"] = epoch_;
        value["pageId"] = page_.page_id;
        if (!page_.plugin_id.empty()) value["pluginId"] = page_.plugin_id;
        if (!channel_.send(value)) { callback->Failure(429, "QUEUE_FULL"); return true; }
        pending_.emplace(value["id"].get<std::string>(), std::move(pending));
    } catch (const std::exception&) { callback->Failure(400, "INVALID_ARGUMENT"); }
    return true;
}
void BrowserClient::OnQueryCanceled(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int64_t query) {
    // 保留取消中的订阅握手直至响应/超时，供文档卸载时清理 Lua 端句柄。
    auto it = pending_.find(std::to_string(query));
    if (it != pending_.end()) it->second.callback = nullptr;
}

// UI 线程导航前撤销本页订阅，包括尚未确认的握手；IPC FIFO 保证取消排在建立之后。
// 清空旧 Promise 和身份，迟到响应不能进入新文档；不持有 Lua 状态或额外页面引用。
void BrowserClient::release_page() {
    CEF_REQUIRE_UI_THREAD();
    for (const auto& [id, subscription] : subscriptions_) subscription_cleanup_.insert(id);
    for (const auto& [id, pending] : pending_)
        if (pending.kind == Pending::Kind::subscribe) subscription_cleanup_.insert(pending.subscription_id);
    flush_subscription_cleanup();
    pump_viewport_resize();
    subscriptions_.clear();
    fail_pending("PAGE_UNLOADED");
    page_ = {};
}

// UI 线程重试清理消息；发送队列暂满时保留句柄，下轮 tick 再提交，不静默遗失撤销请求。
void BrowserClient::flush_subscription_cleanup() {
    while (!subscription_cleanup_.empty()) {
        const auto it = subscription_cleanup_.begin();
        if (!channel_.send({{"type", "request"}, {"id", "cleanup:" + std::to_string(++cleanup_sequence_)},
            {"sessionId", session_}, {"epoch", epoch_}, {"method", "ui.unsubscribe"},
            {"params", {{"subscriptionId", *it}}}})) break;
        subscription_cleanup_.erase(it);
    }
}

// 失效会话中的 Promise 一次性失败，禁止宿主恢复后重放写请求。
void BrowserClient::fail_pending(const std::string& reason) {
    auto pending = std::move(pending_); pending_.clear();
    for (auto& [id, value] : pending) if (value.callback) value.callback->Failure(503, reason);
}

// UI 线程消费 IPC：把鼠标键盘事件发送给离屏浏览器，业务结果返回原 Promise。
void BrowserClient::dispatch(const Json& message) {
    auto type = message.value("type", std::string{});
    if (type == "ready") {
        auto epoch = message.at("epoch").get<std::uint64_t>();
        if (epoch != epoch_) fail_pending("SCRIPT_RESET");
        epoch_ = epoch; ready_ = message.value("ready", false); gate_.set_connected(ready_); return;
    }
    if (type == "response") {
        if (message.value("epoch", std::uint64_t{}) != epoch_) return;
        auto it = pending_.find(message.value("id", std::string{}));
        if (it == pending_.end()) return;
        auto pending = std::move(it->second); pending_.erase(it);
        if (!pending.owner.matches(page_.browser_id, page_.frame_id, page_.generation)) return;
        auto callback = pending.callback;
        if (!message.contains("error")) {
            if (pending.kind == Pending::Kind::subscribe) {
                if (callback) subscriptions_[pending.subscription_id] = Subscription{pending.event_name, pending.owner};
                else subscription_cleanup_.insert(pending.subscription_id);
            }
            else if (pending.kind == Pending::Kind::unsubscribe)
                subscriptions_.erase(pending.subscription_id);
        }
        if (callback) {
            if (message.contains("error")) callback->Failure(500, message["error"].dump());
            else callback->Success(message.at("result").dump());
        }
    } else if (type == "event") {
        // 事件消息已经由 Lua 注册表校验；宿主只向本页面已确认的订阅句柄派发。
        if (message.value("epoch", std::uint64_t{}) != epoch_ || !browser_) return;
        const auto event_name = message.value("eventName", std::string{});
        if (!valid_name(event_name) || !message.contains("payload")) return;
        const auto payload = message["payload"].dump();
        if (payload.size() > max_message) return;
        for (const auto& [subscription_id, subscription] : subscriptions_) {
            if (subscription.event_name != event_name || !subscription.owner.matches(
                    page_.browser_id, page_.frame_id, page_.generation)) continue;
            const auto dom_event = Json("reff:" + subscription_id).dump();
            browser_->GetMainFrame()->ExecuteJavaScript(
                "window.dispatchEvent(new CustomEvent(" + dom_event + ",{detail:" + payload + "}));",
                browser_->GetMainFrame()->GetURL(), 0);
        }
    } else if (type == "visible") {
        // OnBeforeClose 之后仍可能有一个已排队的显隐消息；此时不能解引用 browser_。
        if (!browser_) return;
        bool visible = message.value("value", false); browser_->GetHost()->WasHidden(!visible);
        browser_->GetHost()->SetFocus(visible);
        // 仅独立自检回执：确认 UI 线程已处理显隐，不把管道发送成功当作执行完成。
        if (self_test_ && message.contains("testSequence")) channel_.send({{"type", "visibility_ack"},
            {"testSequence", message.at("testSequence")}, {"value", visible},
            {"subscriptions", subscriptions_.size()}, {"pendingRequests", pending_.size()},
            {"subscriptionCleanup", subscription_cleanup_.size()}});
    } else if (type == "viewport") {
        if (!browser_) return;
        const int width = message.value("width", 0), height = message.value("height", 0);
        if (width < 1 || height < 1 || width > frame_max_width || height > frame_max_height) return;
        // 游戏侧 generation 是唯一请求序号；宿主只保留最新待应用尺寸，避免 CEF 重排请求堆积。
        const auto generation = message.value("generation", viewport_generation_ + 1ULL);
        if (width == viewport_width_ && height == viewport_height_ && generation == viewport_generation_ && !viewport_resize_in_flight_) return;
        pending_viewport_width_ = width; pending_viewport_height_ = height; pending_viewport_generation_ = generation;
        viewport_resize_pending_ = true;
        resize_log("viewport_receive generation=" + std::to_string(generation) + " size=" + std::to_string(width) + "x" + std::to_string(height));
        pump_viewport_resize();
    } else if (type == "shutdown") {
        if (browser_) browser_->GetHost()->CloseBrowser(true);
    } else if (type == "mouse") {
        if (!browser_) return;
        CefMouseEvent event; event.x = message.at("x").get<int>(); event.y = message.at("y").get<int>();
        event.modifiers = message.value("modifiers", 0);
        auto action = message.value("action", std::string{});
        if (action == "move") browser_->GetHost()->SendMouseMoveEvent(event, message.value("leave", false));
        else if (action == "wheel") browser_->GetHost()->SendMouseWheelEvent(event, 0, message.value("delta", 0));
        else {
            // clickCount 由游戏窗口线程按 Win32 DOWN/DBLCLK/UP 配对生成；限制为浏览器支持的首版范围。
            const int click_count = std::clamp(message.value("clickCount", 1), 1, 2);
            browser_->GetHost()->SendMouseClickEvent(event,
                message.value("button", 0) == 1 ? MBT_RIGHT : MBT_LEFT, action == "up", click_count);
        }
    } else if (type == "key") {
        if (!browser_) return;
        CefKeyEvent key; key.windows_key_code = message.at("key").get<int>();
        key.native_key_code = message.value("native", 0); key.modifiers = message.value("modifiers", 0);
        auto action = message.value("action", std::string{});
        key.type = action == "up" ? KEYEVENT_KEYUP : action == "char" ? KEYEVENT_CHAR : KEYEVENT_RAWKEYDOWN;
        browser_->GetHost()->SendKeyEvent(key);
    } else if (type == "ime") {
        if (!browser_) return;
        auto host = browser_->GetHost();
        auto action = message.value("action", std::string{});
        auto text = message.value("text", std::string{});
        ime_log("proxy_message action=" + action + " text_bytes=" + std::to_string(text.size()));
        if (active_ime_input_id_.starts_with("embedded:")) {
            // CEF 的浏览器级 IME API 在当前 OSR 版本不能可靠命中子 frame；
            // 由主 frame 向 Shell 派发受限事件，再定向交给当前隔离页面的聚焦输入框。
            const auto detail = Json{{"action", action}, {"text", text}, {"inputId", active_ime_input_id_}}.dump();
            browser_->GetMainFrame()->ExecuteJavaScript(
                "window.dispatchEvent(new CustomEvent('reff:embedded-ime',{detail:" + detail + "}));",
                browser_->GetMainFrame()->GetURL(), 0);
        } else if (action == "composition") {
            std::vector<CefCompositionUnderline> underlines;
            const CefString composition_text(text);
            const auto length = static_cast<std::uint32_t>(composition_text.length());
            host->ImeSetComposition(composition_text, underlines, CefRange::InvalidRange(), CefRange(length, length));
        } else if (action == "commit") {
            host->ImeCommitText(CefString(text), CefRange::InvalidRange(), 0);
        } else if (action == "cancel") {
            host->ImeCancelComposition();
        }
    }
}

// 只进行短任务：断线释放 pending，超时逐项失败，每轮处理消息数有上限。
void BrowserClient::tick() {
    if (closed_ || gate_.closing()) return;
    if (channel_.connected() && !hello_) {
        channel_.send({{"type", "hello"}, {"protocol", protocol_version}, {"sessionId", session_}}); hello_ = true;
    }
    if (hello_ && !channel_.connected()) {
        ready_ = false; fail_pending("HOST_DISCONNECTED"); browser_->GetHost()->CloseBrowser(true); return;
    }
    if (!hello_ && std::chrono::steady_clock::now() - started_ > std::chrono::seconds(15)) {
        browser_->GetHost()->CloseBrowser(true); return;
    }
    try { for (int i = 0; i < 64; ++i) { auto message = incoming_.pop(); if (!message) break; dispatch(*message); } }
    catch (...) { ready_ = false; fail_pending("INVALID_MESSAGE"); }
    auto now = std::chrono::steady_clock::now();
    for (auto it = pending_.begin(); it != pending_.end();) {
        if (it->second.deadline < now) {
            if (it->second.kind == Pending::Kind::subscribe) subscription_cleanup_.insert(it->second.subscription_id);
            auto callback = it->second.callback; it = pending_.erase(it); if (callback) callback->Failure(408, "TIMEOUT");
        }
        else ++it;
    }
    flush_subscription_cleanup();
    CefPostDelayedTask(TID_UI, base::BindOnce(&BrowserClient::tick, CefRefPtr<BrowserClient>(this)), 8);
}

// 默认关闭后台网络特性，页面层再执行来源白名单；不启用远程调试端口。
void BrowserApp::OnBeforeCommandLineProcessing(const CefString&, CefRefPtr<CefCommandLine> command) {
    command->AppendSwitch("disable-background-networking");
    command->AppendSwitch("disable-component-update");
    command->AppendSwitch("disable-sync");
    // REFF 只加载受限本地页面，不允许 CEF 扫描或装入系统 Chrome 的外部扩展。
    command->AppendSwitch("disable-extensions");
    command->AppendSwitch("no-first-run");
    command->AppendSwitch("disable-domain-reliability");
}
void BrowserApp::OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) {
    registrar->AddCustomScheme("reff", CEF_SCHEME_OPTION_STANDARD | CEF_SCHEME_OPTION_SECURE | CEF_SCHEME_OPTION_CORS_ENABLED);
}
void BrowserApp::OnContextInitialized() {
    CEF_REQUIRE_UI_THREAD();
    auto command = CefCommandLine::GetGlobalCommandLine();
    try {
        CefRefPtr<BrowserClient> client = new BrowserClient(command->GetSwitchValue("reff-session").ToWString(),
            command->GetSwitchValue("reff-assets").ToWString(), command->GetSwitchValue("reff-manifests").ToWString(), command->HasSwitch("reff-self-test"));
        CefWindowInfo window; window.SetAsWindowless(nullptr); window.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
        CefBrowserSettings settings; settings.windowless_frame_rate = 30;
        // Shell 自己绘制可调半透明表面；OSR 背景必须透明，圆角外像素才能显示游戏画面。
        settings.background_color = CefColorSetARGB(0, 0, 0, 0);
        CefBrowserHost::CreateBrowser(window, client, "reff://shell/index.html", settings, nullptr, nullptr);
    } catch (...) { CefQuitMessageLoop(); }
}

// renderer 进程注册 CEF message router，只给受控主页面提供 cefQuery。
void BrowserApp::OnWebKitInitialized() { router_ = CefMessageRouterRendererSide::Create(CefMessageRouterConfig{}); }
void BrowserApp::OnContextCreated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context) {
    const auto url = frame->GetURL().ToString();
    if (frame->IsMain() && (url == "reff://shell/index.html" || url.starts_with("reff://plugin/")))
        router_->OnContextCreated(browser, frame, context);
}
void BrowserApp::OnContextReleased(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context) {
    if (router_) router_->OnContextReleased(browser, frame, context);
}
bool BrowserApp::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
    CefProcessId source, CefRefPtr<CefProcessMessage> message) {
    return router_ && router_->OnProcessMessageReceived(browser, frame, source, message);
}
}
