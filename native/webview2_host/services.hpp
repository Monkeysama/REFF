#pragma once
#include "ipc.hpp"
#include "manifest.hpp"
#include "page_identity.hpp"
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <set>

namespace reff {
bool development_enabled(const std::filesystem::path& directory);
// STA 线程独占的单文档控制器；身份由原生 frame/navigation 绑定，不访问 WebView2 或 Lua 对象。
class HostServices {
public:
    struct Reply { std::function<void(int,const std::string&)> complete;
        void Success(const std::string& raw){complete(0,raw);}
        void Failure(int code,const std::string& reason){complete(code,reason);} };
    struct Route { bool shell{}; const PluginManifest* manifest{}; std::string relative_path; };
    using Send=std::function<bool(Json)>;
    Send send;
    std::function<void(const std::string&,const Json&)> emit;
    std::function<void()> reload;
    std::string active_ime_input_id_;
    bool self_test_{};
    bool dev_enabled{};
    void configure(std::string session,std::filesystem::path assets,std::filesystem::path manifests,std::string game);
    std::optional<Route> route_for_url(const std::string& url) const;
    std::optional<std::filesystem::path> resource_path(const std::string& url) const;
    bool navigate(const std::string& url,const std::string& frame);
    void query(const std::string& raw,std::shared_ptr<Reply> callback);
    bool dispatch(const Json& message);
    void tick();
    void release_page();
    void fail_pending(const std::string& reason);
    Json counts() const;
private:
    struct Pending { enum class Kind {normal,subscribe,unsubscribe}; std::shared_ptr<Reply> callback;
        std::chrono::steady_clock::time_point deadline; Kind kind{Kind::normal};
        std::string subscription_id,event_name,native_subscription_id; PageIdentity owner; };
    struct Subscription {std::string event_name; PageIdentity owner;std::string native_id;};
    const PluginManifest* manifest_for_id(std::string_view id) const;
    void flush_subscription_cleanup();
    std::filesystem::path assets_;
    std::vector<PluginManifest> manifests_;
    std::vector<std::string> manifest_errors_;
    std::string session_;
    std::string bound_url_,frame_;
    std::uint64_t epoch_{},document_generation_{},query_sequence_{},cleanup_sequence_{};
    PageIdentity page_;
    std::map<std::string,Pending> pending_;
    std::map<std::string,Subscription> subscriptions_;
    std::set<std::string> subscription_cleanup_;
    bool ready_{},closed_{};
};
}
