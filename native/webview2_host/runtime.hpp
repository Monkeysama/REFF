#pragma once
#include "services.hpp"
#include "preview_frame.hpp"
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
    bool test_disk_shell{};
    std::string test_script;
    static void configure_options(ICoreWebView2EnvironmentOptions* options);
    void attach(ICoreWebView2* web,ICoreWebView2Environment* environment,std::string session,
                std::filesystem::path assets,std::filesystem::path manifests,std::string game);
    void dispatch(const Json& message);
    void tick();
    void suspend_input();
    void close();
    void viewport(int width,int height,std::uint64_t generation);
    PreviewFrame preview_frame(const unsigned char* pixels,unsigned stride,int width,int height,std::uint64_t generation) const;
private:
    struct Document {
        HostServices services;
        Microsoft::WRL::ComPtr<ICoreWebView2Frame2> frame;
        std::string id,url,ticket;
        std::uint64_t navigation{};
        bool main{},dead{};
        std::set<std::string> preview_ids;
    };
    void initialize(const std::shared_ptr<Document>& document);
    void frame_created(ICoreWebView2Frame* frame);
    void starting(const std::shared_ptr<Document>& document,ICoreWebView2NavigationStartingEventArgs* args);
    void completed(const std::shared_ptr<Document>& document,ICoreWebView2NavigationCompletedEventArgs* args);
    void message(const std::shared_ptr<Document>& document,ICoreWebView2WebMessageReceivedEventArgs* args);
    void post(const std::shared_ptr<Document>& document,Json value);
    void resource(ICoreWebView2WebResourceRequestedEventArgs* args);
    void guard(const std::function<void()>& action) noexcept;
    bool preview_query(const std::shared_ptr<Document>& document,const Json& request,const std::shared_ptr<HostServices::Reply>& reply);
    void clear_previews();
    void publish_previews();
    PreviewFrame preview_;
    int preview_width_{},preview_height_{};
    std::uint64_t preview_generation_{};
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
