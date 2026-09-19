#pragma once
#include "frame.hpp"
#include "manifest.hpp"
#include "lifecycle.hpp"
#include "page_identity.hpp"
#include "include/cef_app.h"
#include "include/cef_client.h"
#include "include/cef_render_handler.h"
#include "include/cef_resource_request_handler.h"
#include "include/wrapper/cef_message_router.h"
#include <filesystem>
#include <map>
#include <set>

namespace reff {
// 浏览器 UI 线程上的页面控制器；IPC 收包仅排队，由 tick 转回 CEF UI 线程。
class BrowserClient : public CefClient, public CefLifeSpanHandler, public CefRenderHandler,
                      public CefRequestHandler, public CefResourceRequestHandler,
                      public CefLoadHandler, public CefDisplayHandler,
                      public CefMessageRouterBrowserSide::Handler {
public:
    BrowserClient(std::wstring session, std::filesystem::path assets, std::filesystem::path manifests,
                  std::string game, bool self_test);
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
    CefRefPtr<CefRenderHandler> GetRenderHandler() override { return this; }
    CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
    CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
    CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;
    bool OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int, const CefString&, const CefString&,
        CefLifeSpanHandler::WindowOpenDisposition, bool, const CefPopupFeatures&, CefWindowInfo&, CefRefPtr<CefClient>&,
        CefBrowserSettings&, CefRefPtr<CefDictionaryValue>&, bool*) override { return true; }
    // CEF UI 线程读取当前逻辑视口；尺寸由游戏侧窗口缩放消息更新。
    void GetViewRect(CefRefPtr<CefBrowser>, CefRect& rect) override { rect = CefRect(0, 0, viewport_width_, viewport_height_); }
    void OnPaint(CefRefPtr<CefBrowser>, PaintElementType type, const RectList&, const void* buffer, int width, int height) override;
    // 将 CEF 文本插入点回传游戏代理线程，用于定位系统输入法候选框。
    void OnImeCompositionRangeChanged(CefRefPtr<CefBrowser>, const CefRange&, const RectList&) override;
    bool OnBeforeBrowse(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefRequest>, bool, bool) override;
    CefRefPtr<CefResourceRequestHandler> GetResourceRequestHandler(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
        CefRefPtr<CefRequest>, bool, bool, const CefString&, bool& disable_default_handling) override;
    ReturnValue OnBeforeResourceLoad(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefRequest>, CefRefPtr<CefCallback>) override;
    CefRefPtr<CefResourceHandler> GetResourceHandler(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefRequest>) override;
    bool OnProcessMessageReceived(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefProcessId, CefRefPtr<CefProcessMessage>) override;
    void OnLoadEnd(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int) override;
    void OnLoadError(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, ErrorCode, const CefString&, const CefString&) override;
    bool OnConsoleMessage(CefRefPtr<CefBrowser>, cef_log_severity_t, const CefString&, const CefString&, int) override;
    void OnLoadStart(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, TransitionType) override;
    bool OnQuery(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int64_t, const CefString&, bool, CefRefPtr<Callback>) override;
    void OnQueryCanceled(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int64_t) override;
    void tick();
private:
    // 解析 Shell 与插件页路由；只返回已扫描 manifest 对应的插件资源。
    struct PageRoute {
        bool shell{};
        const PluginManifest* manifest{};
        std::string relative_path;
    };
    std::optional<PageRoute> route_for_url(const std::string& url) const;
    std::optional<std::filesystem::path> resource_path(const std::string& url) const;
    bool page_url_allowed(const std::string& url) const;
    const PluginManifest* manifest_for_id(std::string_view id) const;
    // CEF 宿主进程独立记录 viewport/OnPaint 诊断，不写入 REFramework 主日志。
    void reset_resize_log();
    void resize_log(const std::string& message, bool sampled = false);
    // IME 专用诊断只记录焦点请求和组合消息，不写入 REFramework 主日志或缩放日志。
    void reset_ime_log();
    void ime_log(const std::string& message);
    void dispatch(const Json& message);
    void fail_pending(const std::string& reason);
    void release_page();
    void flush_subscription_cleanup();
    // UI 线程合并 viewport 请求；CEF 正在处理一个尺寸时只保留最新请求。
    void pump_viewport_resize();
    struct Pending {
        enum class Kind { normal, subscribe, unsubscribe };
        CefRefPtr<Callback> callback;
        std::chrono::steady_clock::time_point deadline;
        Kind kind{Kind::normal};
        std::string subscription_id;
        std::string event_name;
        PageIdentity owner;
    };
    struct Subscription { std::string event_name; PageIdentity owner; };
    PageIdentity page_;
    std::uint64_t document_generation_{};
    std::uint64_t cleanup_sequence_{};
    std::set<std::string> subscription_cleanup_;
    CefRefPtr<CefBrowser> browser_;
    CefRefPtr<CefMessageRouterBrowserSide> router_;
    SharedFrame frame_;
    Channel channel_;
    Queue<Json> incoming_;
    std::filesystem::path assets_;
    std::vector<PluginManifest> manifests_;
    std::vector<std::string> manifest_errors_;
    SessionGate gate_;
    std::map<std::string, Pending> pending_;
    std::map<std::string, Subscription> subscriptions_;
    // 当前 IME 输入会话来自宿主校验后的 inputId；embedded: 前缀表示提交文本需定向转发到隔离 iframe。
    std::string active_ime_input_id_;
    std::string session_;
    std::uint64_t epoch_{};
    int viewport_width_{panel_width}, viewport_height_{panel_height};
    // UI 线程递增 viewport 代次；OnPaint 把生成时的代次写入共享帧，阻止旧尺寸帧覆盖新布局。
    std::uint64_t viewport_generation_{1};
    int pending_viewport_width_{}, pending_viewport_height_{};
    std::uint64_t pending_viewport_generation_{};
    bool viewport_resize_pending_{}, viewport_resize_in_flight_{};
    std::uint64_t last_viewport_resize_tick_{};
    bool ready_{}, closed_{}, hello_{}, self_test_{};
    std::chrono::steady_clock::time_point started_{std::chrono::steady_clock::now()};
    IMPLEMENT_REFCOUNTING(BrowserClient);
};

// 同一宿主 DLL 服务浏览器进程与 renderer 子进程，JS 路由在各自正确线程注册。
class BrowserApp : public CefApp, public CefBrowserProcessHandler, public CefRenderProcessHandler {
public:
    CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override { return this; }
    CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override { return this; }
    void OnBeforeCommandLineProcessing(const CefString&, CefRefPtr<CefCommandLine>) override;
    void OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar>) override;
    void OnContextInitialized() override;
    void OnWebKitInitialized() override;
    void OnContextCreated(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefV8Context>) override;
    void OnContextReleased(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefV8Context>) override;
    bool OnProcessMessageReceived(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefProcessId, CefRefPtr<CefProcessMessage>) override;
private:
    CefRefPtr<CefMessageRouterRendererSide> router_;
    IMPLEMENT_REFCOUNTING(BrowserApp);
};
}
