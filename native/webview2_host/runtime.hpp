#pragma once
#include "services.hpp"
#include <wrl.h>
#include <WebView2.h>
#include <WebView2EnvironmentOptions.h>

namespace reff {
// 所有方法及回调仅在宿主 STA 调用；浏览器、文档和 IPC 回调不跨线程共享 COM 对象。
class WebViewRuntime {
public:
    HostServices::Send send;
    std::function<void(const std::string&)> failure;
    bool self_test{},dev_enabled{};
    static void configure_options(ICoreWebView2EnvironmentOptions* options);
    void attach(ICoreWebView2* web,ICoreWebView2Environment* environment,std::string session,
                std::filesystem::path assets,std::filesystem::path manifests,std::string game);
    void dispatch(const Json& message);
    void tick();
    void suspend_input();
    void close();
private:
    struct Document {
        HostServices services;
        Microsoft::WRL::ComPtr<ICoreWebView2Frame2> frame;
        std::string id,url,ticket;
        std::uint64_t navigation{};
        bool main{},dead{};
    };
    void initialize(const std::shared_ptr<Document>& document);
    void frame_created(ICoreWebView2Frame* frame);
    void starting(const std::shared_ptr<Document>& document,ICoreWebView2NavigationStartingEventArgs* args);
    void completed(const std::shared_ptr<Document>& document,ICoreWebView2NavigationCompletedEventArgs* args);
    void message(const std::shared_ptr<Document>& document,ICoreWebView2WebMessageReceivedEventArgs* args);
    void post(const std::shared_ptr<Document>& document,Json value);
    void resource(ICoreWebView2WebResourceRequestedEventArgs* args);
    void guard(const std::function<void()>& action) noexcept;
    Microsoft::WRL::ComPtr<ICoreWebView2> web_;
    Microsoft::WRL::ComPtr<ICoreWebView2Environment> environment_;
    std::vector<std::shared_ptr<Document>> documents_;
    std::string session_,game_;
    std::filesystem::path assets_,manifests_;
    std::uint64_t frame_sequence_{};
    Json ready_={{"type","ready"},{"epoch",0},{"ready",false}};
    bool closed_{};
};
}
