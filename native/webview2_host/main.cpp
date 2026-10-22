#include <Windows.h>
#include <shellapi.h>
#include <DispatcherQueue.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <wrl.h>
#include <WebView2.h>
#include <WebView2EnvironmentOptions.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include "frame.hpp"
#include <json.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <deque>
#include <string>
#include <set>
#include "keyboard.hpp"
#include "runtime.hpp"

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
using nlohmann::json;
namespace comp = winrt::Windows::UI::Composition;
namespace capture = winrt::Windows::Graphics::Capture;
namespace direct = winrt::Windows::Graphics::DirectX;
using Clock = std::chrono::steady_clock;
// 以实测正式 Runtime 作为保守下限；不把 SDK 版本或 API 理论下限冒充已验证环境。
constexpr wchar_t minimum_runtime[]=L"155.0.4283.62";
constexpr wchar_t runtime_download[]=L"https://developer.microsoft.com/microsoft-edge/webview2/";

// 宿主的 COM、WinRT 和图形资源归 UI 线程所有；捕获采用无窗口 Visual，不捕获用户屏幕内容。
class WebViewHost {
public:
    HWND window{};
    std::filesystem::path output;
    std::wstring game_session;
    std::filesystem::path cache_root;
    int result = 2;
    bool selftest=false;
    std::filesystem::path assets,manifests;
    std::string game;
    std::unique_ptr<reff::WebViewRuntime> runtime;

    // 仅从 UI 消息循环调用；异常集中记录为失败，避免回调越过 Win32 ABI。
    void start() {
        winrt::check_hresult(CreateDispatcherQueueController(
            {sizeof(DispatcherQueueOptions), DQTYPE_THREAD_CURRENT, DQTAT_COM_STA},
            reinterpret_cast<ABI::Windows::System::IDispatcherQueueController**>(winrt::put_abi(dispatcher))));
        log.open(output / "events.jsonl", std::ios::trunc);
        if(!game_session.empty()) {
            shared.open(game_session,false);
            channel.start(game_session,false,[this](reff::Json message){incoming.push(std::move(message));});
        }
        // 只检测本机正式 Runtime 和系统能力；不安装或修改系统组件。
        OSVERSIONINFOW os{sizeof(os)};
        using RtlVersion=LONG(WINAPI*)(OSVERSIONINFOW*);
        const auto get_version=reinterpret_cast<RtlVersion>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"RtlGetVersion"));
        if(!get_version || get_version(&os)!=0)throw std::runtime_error("OS version detection failed");
        report["osBuild"]=os.dwBuildNumber;report["minimumOsBuild"]=19045;
        report["minimumRuntime"]=winrt::to_string(minimum_runtime);
        report["runtimeDownload"]=winrt::to_string(runtime_download);
        if(os.dwMajorVersion<10 || os.dwBuildNumber<19045)
            throw std::runtime_error("Windows 10 22H2 (19045) or later required; other OS builds not validated");
        options=Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
        reff::WebViewRuntime::configure_options(options.Get());
        winrt::check_hresult(options->put_TargetCompatibleBrowserVersion(minimum_runtime));
        ComPtr<ICoreWebView2EnvironmentOptions7> options7;winrt::check_hresult(options.As(&options7));
        winrt::check_hresult(options7->put_ReleaseChannels(COREWEBVIEW2_RELEASE_CHANNELS_STABLE));
        LPWSTR version{};
        HRESULT detected = GetAvailableCoreWebView2BrowserVersionStringWithOptions(nullptr, options.Get(), &version);
        std::wstring detected_version=version?version:L"";CoTaskMemFree(version);
        report["runtimeDetectionHresult"] = static_cast<long>(detected);
        if (FAILED(detected) || detected_version.empty()) throw std::runtime_error("Stable WebView2 Runtime missing; install Evergreen Runtime from the runtimeDownload URL");
        check_runtime(detected_version);
        if (!capture::GraphicsCaptureSession::IsSupported()) throw std::runtime_error("GraphicsCapture unsupported");
        using winrt::Windows::Foundation::Metadata::ApiInformation;
        if(!ApiInformation::IsMethodPresent(L"Windows.Graphics.Capture.GraphicsCaptureItem",L"CreateFromVisual") ||
            !ApiInformation::IsPropertyPresent(L"Windows.Graphics.Capture.GraphicsCaptureSession",L"IsCursorCaptureEnabled"))
            throw std::runtime_error("Required stable Windows Graphics Capture APIs unavailable");
        compositor = comp::Compositor();
        root = compositor.CreateContainerVisual();
        root.Size({800,600});
        create_capture();
        const auto cache = cache_root.empty()?output/"userdata":cache_root;
        winrt::check_hresult(CreateCoreWebView2EnvironmentWithOptions(nullptr, cache.c_str(), options.Get(),
            Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
                guard([&] {
                    winrt::check_hresult(hr); environment = env;
                    LPWSTR actual{};winrt::check_hresult(environment->get_BrowserVersionString(&actual));
                    const std::wstring owned=actual?actual:L"";CoTaskMemFree(actual);check_runtime(owned);
                    ComPtr<ICoreWebView2Environment3> env3; winrt::check_hresult(environment.As(&env3));
                    winrt::check_hresult(env3->CreateCoreWebView2CompositionController(window,
                        Callback<ICoreWebView2CreateCoreWebView2CompositionControllerCompletedHandler>(
                        [this](HRESULT created, ICoreWebView2CompositionController* value) -> HRESULT {
                            guard([&] { initialize(created, value); }); return S_OK;
                        }).Get()));
                }); return S_OK;
            }).Get()));
        begun = Clock::now();
        SetTimer(window, 1, 16, nullptr);
        event("started", {{"mode","detached"}, {"runtime",report["runtime"]}});
    }

    // 图形捕获资源由 STA 线程独占；独立重建检查释放旧池、会话及 D3D11 设备，不触碰游戏设备。
    void create_capture() {
        if(session){session.Close();session=nullptr;}
        if(pool){pool.Close();pool=nullptr;}
        item=nullptr;capture_device=nullptr;staging.Reset();context.Reset();device.Reset();
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        D3D_FEATURE_LEVEL level{};
        winrt::check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0,
            D3D11_SDK_VERSION, &device, &level, &context));
        ComPtr<IDXGIDevice> dxgi; winrt::check_hresult(device.As(&dxgi));
        winrt::com_ptr<IInspectable> inspectable;
        winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(), inspectable.put()));
        capture_device = inspectable.as<direct::Direct3D11::IDirect3DDevice>();
        item = capture::GraphicsCaptureItem::CreateFromVisual(root);
        pool = capture::Direct3D11CaptureFramePool::CreateFreeThreaded(capture_device,
            direct::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, item.Size());
        session = pool.CreateCaptureSession(item);
        session.IsCursorCaptureEnabled(false);
        session.StartCapture();
    }

    // 异步环境创建完成后关联 Visual；禁止把浏览器导航到外部来源。
    void initialize(HRESULT hr, ICoreWebView2CompositionController* value) {
        winrt::check_hresult(hr);
        composition = value; winrt::check_hresult(composition.As(&controller));
        winrt::check_hresult(controller->get_CoreWebView2(&web));
        // 共享帧与游戏输入协议都使用物理像素；不让隐藏父窗口的监视器 DPI 再次缩放网页。
        ComPtr<ICoreWebView2Controller3> c3;winrt::check_hresult(controller.As(&c3));
        winrt::check_hresult(c3->put_ShouldDetectMonitorScaleChanges(FALSE));
        winrt::check_hresult(c3->put_RasterizationScale(1.0));
        winrt::check_hresult(c3->put_BoundsMode(COREWEBVIEW2_BOUNDS_MODE_USE_RAW_PIXELS));
        winrt::check_hresult(composition->put_RootVisualTarget(winrt::get_unknown(root)));
        ComPtr<ICoreWebView2Controller2> c2; winrt::check_hresult(controller.As(&c2));
        winrt::check_hresult(c2->put_DefaultBackgroundColor({0,0,0,0}));
        ComPtr<ICoreWebView2Settings> settings; winrt::check_hresult(web->get_Settings(&settings));
        settings->put_AreDefaultContextMenusEnabled(FALSE);
        settings->put_AreDevToolsEnabled(FALSE);
        settings->put_AreDefaultScriptDialogsEnabled(FALSE);
        ComPtr<ICoreWebView2Settings3> settings3;winrt::check_hresult(settings.As(&settings3));
        winrt::check_hresult(settings3->put_AreBrowserAcceleratorKeysEnabled(FALSE));
        EventRegistrationToken token{};
        // 致命浏览器故障终止本次会话，由现有游戏服务释放输入并等待用户重新打开；不重放任何操作。
        winrt::check_hresult(web->add_ProcessFailed(Callback<ICoreWebView2ProcessFailedEventHandler>(
            [this](ICoreWebView2*,ICoreWebView2ProcessFailedEventArgs* args)->HRESULT {
                guard([&]{
                    COREWEBVIEW2_PROCESS_FAILED_KIND kind{};winrt::check_hresult(args->get_ProcessFailedKind(&kind));
                    event("process-failed",{{"kind",int(kind)}});
                    if(kind==COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED ||
                       kind==COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED ||
                       kind==COREWEBVIEW2_PROCESS_FAILED_KIND_FRAME_RENDER_PROCESS_EXITED ||
                       kind==COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_UNRESPONSIVE)
                        fail("WebView2 process failed; close session and reopen REFF",E_FAIL);
                    // GPU/utility 子进程由 Runtime 自行恢复；捕获错误或视口超时仍会安全关闭宿主。
                });return S_OK;
            }).Get(),&token));
        runtime=std::make_unique<reff::WebViewRuntime>();
        runtime->send=[this](json value){return channel.send(std::move(value));};
        runtime->failure=[this](const std::string& error){fail(error,E_FAIL);};
        runtime->self_test=selftest;
        runtime->dev_enabled=reff::development_enabled(cache_root.parent_path().parent_path());
        event("development-mode",{{"enabled",runtime->dev_enabled}});
        runtime->attach(web.Get(),environment.Get(),reff::narrow(game_session),assets,manifests,game);
        // 环境异步创建期间 IPC 已可能就绪；新 Runtime 必须继承最新脚本代次，避免丢失首次 ready。
        if(!runtime_ready.is_null())runtime->dispatch(runtime_ready);
        web->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [this](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                BOOL ok{}; COREWEBVIEW2_WEB_ERROR_STATUS error{};
                args->get_IsSuccess(&ok); args->get_WebErrorStatus(&error);
                event("navigation", {{"success",bool(ok)},{"webError",int(error)}});
                if (ok) { ready = true; ready_at = Clock::now();
                    report["navigationReadyMs"]=std::chrono::duration<double,std::milli>(ready_at-launched).count();
                    pump_viewport(); stamp_viewport(); }
                return S_OK;
            }).Get(), &token);
        resize(width,height);
        winrt::check_hresult(controller->put_IsVisible(visible));
        // 桥脚本注册完成后由 Runtime 导航，防止首个文档缺少注入。
    }

    // 仅应用一项已调度视口；完成其实际捕获前不允许新请求反复重建捕获池。
    void resize(int w, int h) {
        width=w; height=h; staging.Reset();
        root.Size({float(w),float(h)});
        if (controller) winrt::check_hresult(controller->put_Bounds({0,0,w,h}));
        pool.Recreate(capture_device,direct::DirectXPixelFormat::B8G8R8A8UIntNormalized,2,{w,h});
        if(ready) stamp_viewport();
        event("resize", {{"width",w},{"height",h}});
    }

    // STA 定时器消费 IPC 和最新捕获帧；BGRA 通过共享内存交给游戏侧 DX12，不访问屏幕。
    void tick() {
        if(runtime)runtime->tick();
        const auto now=Clock::now();
        if(!game_session.empty()) {
            if(channel.connected() && !hello) {
                channel.send({{"type","hello"},{"protocol",reff::protocol_version},{"sessionId",reff::narrow(game_session)}}); hello=true;
            }
            if(hello && !channel.connected()) { result=0;report["exitReason"]="IPC disconnected";DestroyWindow(window);return; }
            for(int i=0;i<64;++i) {auto message=incoming.pop();if(!message)break;dispatch(*message);}
            if(!IsWindow(window)) return;
            pump_viewport();
        }
        if ((!ready && now-begun>std::chrono::seconds(15)) ||
            (visible && viewport_in_flight && now-viewport_started>std::chrono::seconds(5))) {
            throw std::runtime_error("initialization or capture timed out");
        }
        const auto capture_started=Clock::now();
        auto frame=pool.TryGetNextFrame(); if(!frame) return;
        // 仅回读当前可取得的最新完整帧，防止诊断轮询积累旧的组合结果。
        while(auto newer=pool.TryGetNextFrame()) {frame.Close();frame=std::move(newer);}
        const auto size=frame.ContentSize();
        if(size.Width!=width || size.Height!=height) return;
        auto access=frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        ComPtr<ID3D11Texture2D> texture;
        winrt::check_hresult(access->GetInterface(IID_PPV_ARGS(&texture)));
        D3D11_TEXTURE2D_DESC desc{}; texture->GetDesc(&desc);
        if(!staging) {
            desc.BindFlags=0; desc.MiscFlags=0; desc.Usage=D3D11_USAGE_STAGING; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
            winrt::check_hresult(device->CreateTexture2D(&desc,nullptr,&staging));
        }
        context->CopyResource(staging.Get(),texture.Get());
        const auto map_started=Clock::now();
        D3D11_MAPPED_SUBRESOURCE map{};
        winrt::check_hresult(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map));
        const auto map_us=std::chrono::duration<double,std::micro>(Clock::now()-map_started).count();
        const auto* bytes=static_cast<const unsigned char*>(map.pData);
        if(!game_session.empty()) {
            // 页面绘制在同一帧中的代次色块决定帧归属；不能把 resize 请求时刻当作页面重排完成。
            const auto* stamp=bytes+22*map.RowPitch+98*4;
            const auto generation=std::uint32_t(stamp[2])|(std::uint32_t(stamp[1])<<8)|(std::uint32_t(stamp[0])<<16);
            if(game_capture_count++<4)event("captured-generation",{{"actual",generation},{"expected",viewport_generation},{"bgra",{stamp[0],stamp[1],stamp[2],stamp[3]}}});
            bool published=false;
            if(ready && generation==(viewport_generation&0xffffffu)) {
                packed.resize(size_t(width)*height*4);
                for(int y=0;y<height;++y) memcpy(packed.data()+size_t(y)*width*4,bytes+y*map.RowPitch,width*4);
                published=shared.write(packed.data(),width,height,viewport_generation);
            }
            context->Unmap(staging.Get(),0);
            frame.Close();frame=nullptr;texture.Reset();access=nullptr;
            if(published && viewport_in_flight) {
                event("viewport-complete",{{"generation",viewport_generation},{"width",width},{"height",height},
                    {"layoutMs",std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-viewport_started).count()},
                    {"pendingGeneration",pending_generation}});
                viewport_in_flight=false;
                pump_viewport();
            }
            if(published) {
                if(!published_count)report["firstPublishedMs"]=std::chrono::duration<double,std::milli>(Clock::now()-launched).count();
                ++published_count;published_bytes+=size_t(size.Width)*size.Height*4;
                if(map_samples.size()<16384) {map_samples.push_back(map_us);
                    capture_samples.push_back(std::chrono::duration<double,std::micro>(Clock::now()-capture_started).count());}
            }
            return;
        }
    }

    // 按资源依赖顺序在 STA 线程关闭；不接触游戏进程或其他浏览器实例。
    void close() {
        KillTimer(window,1);
        if(runtime)runtime->close();
        channel.stop();
        if(session) {session.Close(); session=nullptr;}
        if(pool) {pool.Close(); pool=nullptr;}
        if(composition) composition->put_RootVisualTarget(nullptr);
        if(controller) controller->Close();
        web.Reset(); controller.Reset(); composition.Reset(); environment.Reset();
        runtime.reset();
        root=nullptr; compositor=nullptr;
        report["exitCode"]=result;report["publishedFrames"]=published_count;report["publishedBytes"]=published_bytes;
        report["captureAndPublishCpuWallUs"]=percentiles(capture_samples);
        report["gpuReadbackMapWallUs"]=percentiles(map_samples);
        report["gameValidated"]=false;
        std::ofstream(output/"summary.json")<<report.dump(2);
    }
    template<class F> void guard(F&& action) noexcept {
        try { action(); }
        catch(const winrt::hresult_error& e) { fail(winrt::to_string(e.message()),e.code()); }
        catch(const std::exception& e) { fail(e.what(),E_FAIL); }
    }
private:
    // 同时校验预检测与实际创建的环境，避免注册表或环境变量覆盖通道选择后静默使用预览版。
    void check_runtime(const std::wstring& version) {
        report["runtime"]=winrt::to_string(version);
        if(version.empty() || version.find_first_not_of(L"0123456789.")!=std::wstring::npos)
            throw std::runtime_error("Stable Evergreen Runtime required; preview channels are not supported");
        int comparison{};winrt::check_hresult(CompareBrowserVersions(version.c_str(),minimum_runtime,&comparison));
        if(comparison<0)throw std::runtime_error("WebView2 Runtime too old; update Evergreen Runtime from the runtimeDownload URL");
    }
    // 诊断仅保存耗时分布，不保存输入；上限防止长时间运行无限积累样本。
    static json percentiles(std::vector<double> samples) {
        if(samples.empty())return {{"samples",0}};
        std::sort(samples.begin(),samples.end());
        return {{"samples",samples.size()},{"p50",samples[(samples.size()-1)/2]},
            {"p95",samples[size_t((samples.size()-1)*0.95)]},{"max",samples.back()}};
    }
    // 单项在途、仅保留最新待办：先交付当前真实重排帧，再追赶拖动中的最新尺寸，杜绝精确追逐导致饿死。
    void pump_viewport() {
        if(game_session.empty() || !ready || !controller || viewport_in_flight || !pending_generation)return;
        const auto generation=pending_generation;
        const int next_width=pending_width,next_height=pending_height;
        pending_generation=0;
        if(generation==viewport_generation && next_width==width && next_height==height)return;
        viewport_generation=generation;
        viewport_in_flight=true;viewport_started=Clock::now();
        resize(next_width,next_height);
    }

    // 页面用一个不透明像素标记实际绘制代次，隔离尺寸变化与异步浏览器合成之间的竞态。
    void stamp_viewport() {
        const auto g=viewport_generation&0xffffffu;
        // Bounds 的 COM 返回不代表网页已使用新视口；只在真实 innerWidth/innerHeight 匹配后标记该代画面。
        const auto code="(()=>{const ticket={w:"+std::to_string(width)+",h:"+std::to_string(height)+"};"
            "window.__reffViewport=ticket;function ack(){if(window.__reffViewport!==ticket)return;"
            "if(innerWidth!==ticket.w||innerHeight!==ticket.h){requestAnimationFrame(ack);return;}"
            "let stamp=document.getElementById('reff-capture-stamp');if(!stamp){stamp=document.createElement('div');"
            "stamp.id='reff-capture-stamp';stamp.style.cssText='position:fixed!important;left:98px!important;top:22px!important;"
            "width:1px!important;height:1px!important;z-index:2147483647!important;pointer-events:none!important';"
            "document.documentElement.appendChild(stamp);}stamp.style.backgroundColor='rgb("+
            std::to_string(g&255)+","+std::to_string((g>>8)&255)+","+std::to_string((g>>16)&255)+")';}requestAnimationFrame(ack)})()";
        web->ExecuteScript(reff::widen(code).c_str(),nullptr);
    }

    // CDP 仅由原生可信输入调用；网页不获得调试协议能力。用于真实 DOM 编辑和组合文本。
    void cdp(const wchar_t* method,const json& args,std::function<void()> done={}) {
        protocol_calls.push_back({method,args.dump(),std::move(done)});
        pump_cdp();
    }

    // 串行执行调试协议调用；组合更新完成后才提交文本，避免快速输入时重排不同步。
    void pump_cdp() {
        if(protocol_busy || protocol_calls.empty())return;
        auto call=std::move(protocol_calls.front());protocol_calls.pop_front();protocol_busy=true;
        winrt::check_hresult(web->CallDevToolsProtocolMethod(call.method.c_str(),reff::widen(call.args).c_str(),
            Callback<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler>(
                [this,done=std::move(call.done)](HRESULT hr,LPCWSTR)->HRESULT {
                    guard([&]{
                        protocol_busy=false;
                        if(FAILED(hr)) event("input-error",{{"hresult",static_cast<long>(hr)}});
                        else if(done)done();
                        pump_cdp();
                    });
                    return S_OK;
                }).Get()));
    }

    // UI 线程消费现有 IPC；不直接执行 Lua，仅适配窗口控制和原生转来的用户输入。
    // 失焦/隐藏只撤销输入状态，保留画面与浏览器；丢弃尚未执行的文本，补齐已按下的键鼠释放。
    void suspend_input() {
        // 同一轮失焦可能同时收到 focus=false 与 visible=false，不能再次清空已排队的松键。
        if(input_suspending)return;
        input_suspending=true;
        protocol_calls.clear();active_input.clear();
        if(!web){input_suspending=false;return;}
        cdp(L"Input.imeSetComposition",{{"text",""},{"selectionStart",0},{"selectionEnd",0}});
        for(const int key:pressed_keys){json args={{"type","keyUp"},{"windowsVirtualKeyCode",key},{"modifiers",0}};
            reff::webview2_key_identity(args,key,0,0);cdp(L"Input.dispatchKeyEvent",args);}
        pressed_keys.clear();
        if(composition){for(const bool right:{false,true})if(pressed_buttons&(right?2:1))
            composition->SendMouseInput(right?COREWEBVIEW2_MOUSE_EVENT_KIND_RIGHT_BUTTON_UP:COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_UP,
                COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_NONE,0,last_pointer);
            composition->SendMouseInput(COREWEBVIEW2_MOUSE_EVENT_KIND_LEAVE,COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_NONE,0,{0,0});}
        pressed_buttons=0;
        // 先让原焦点文档收到松键，再移开 iframe 焦点；ExecuteScript 不参与 CDP 队列，直接调用会抢跑。
        cdp(L"Input.imeSetComposition",{{"text",""},{"selectionStart",0},{"selectionEnd",0}},[this]{
            input_suspending=false;
            if(runtime&&!focused)runtime->suspend_input();
        });
    }

    void dispatch(const json& message) {
        const auto type=message.value("type",std::string{});
        if(type=="ready")runtime_ready=message;
        if(runtime)runtime->dispatch(message);
        if(type=="shutdown") {result=0;report["exitReason"]="shutdown";DestroyWindow(window);return;}
        if(type=="host-selftest" && selftest && web) {
            const auto action=message.value("action",std::string{});
            if(action=="capture-reset") {create_capture();resize(width,height);event("capture-rebuilt",{});}
            else if(action=="renderer-crash")web->CallDevToolsProtocolMethod(L"Page.crash",L"{}",nullptr);
            else if(action=="browser-crash")web->CallDevToolsProtocolMethod(L"Browser.crash",L"{}",nullptr);
            return;
        }
        if(type=="viewport") {
            const int w=message.value("width",0),h=message.value("height",0);
            const auto generation=message.value("generation",std::uint64_t{});
            if(w<320 || h<240 || w>reff::frame_max_width || h>reff::frame_max_height ||
                !generation || generation<viewport_generation || generation<pending_generation) return;
            pending_width=w;pending_height=h;pending_generation=generation;
            return;
        }
        if(type=="visible") {
            visible=message.value("value",false);
            focused=visible;
            if(!visible)suspend_input();
            if(visible && viewport_in_flight)viewport_started=Clock::now();
            // Hidden Visual 无需隐藏 HWND；禁用浏览器捕获会停止帧输出，恢复时仍应重新取得最新画面。
            if(controller) controller->put_IsVisible(visible);
            if(!visible) active_input.clear();
            return;
        }
        if(type=="focus"){
            focused=message.value("value",false);
            if(!focused)suspend_input();
            return;
        }
        if(!web || !visible || !focused) return;
        if(type=="mouse") {
            const auto action=message.value("action",std::string{});
            auto kind=COREWEBVIEW2_MOUSE_EVENT_KIND_MOVE;
            const bool right=message.value("button",0)==1;
            if(action=="wheel")kind=COREWEBVIEW2_MOUSE_EVENT_KIND_WHEEL;
            else if(action=="down")kind=right?COREWEBVIEW2_MOUSE_EVENT_KIND_RIGHT_BUTTON_DOWN:COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_DOWN;
            else if(action=="up")kind=right?COREWEBVIEW2_MOUSE_EVENT_KIND_RIGHT_BUTTON_UP:COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_UP;
            else if(message.value("leave",false))kind=COREWEBVIEW2_MOUSE_EVENT_KIND_LEAVE;
            const int flags=message.value("modifiers",0);
            UINT keys{};if(flags&2)keys|=MK_SHIFT;if(flags&4)keys|=MK_CONTROL;if(flags&16)keys|=MK_LBUTTON;if(flags&64)keys|=MK_RBUTTON;
            if(kind==COREWEBVIEW2_MOUSE_EVENT_KIND_LEAVE)keys=0;
            if(action=="down"){
                pressed_buttons|=right?2:1;
                if(message.value("clickCount",1)==2)kind=right?COREWEBVIEW2_MOUSE_EVENT_KIND_RIGHT_BUTTON_DOUBLE_CLICK:COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_DOUBLE_CLICK;
            }else if(action=="up")pressed_buttons&=~(right?2:1);
            POINT point{message.value("x",0),message.value("y",0)};last_pointer=point;
            if(kind==COREWEBVIEW2_MOUSE_EVENT_KIND_LEAVE)point={0,0};
            winrt::check_hresult(composition->SendMouseInput(kind,static_cast<COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS>(keys),
                action=="wheel"?UINT(message.value("delta",0)):0,point));
        } else if(type=="key") {
            const auto action=message.value("action",std::string{});const int key=message.value("key",0),flags=message.value("modifiers",0);
            int modifiers=0;if(flags&2)modifiers|=8;if(flags&4)modifiers|=2;if(flags&8)modifiers|=1;if(flags&128)modifiers|=4;
            if(action=="char" && (key<32 || (flags&4) || (flags&8)))return;
            if(action=="down")pressed_keys.insert(key);else if(action=="up")pressed_keys.erase(key);
            json args={{"type",action=="up"?"keyUp":action=="char"?"char":"rawKeyDown"},{"modifiers",modifiers},{"windowsVirtualKeyCode",key}};
            if(action=="char") args["text"]=reff::narrow(std::wstring(1,wchar_t(key)));
            if(action!="char")reff::webview2_key_identity(args,key,message.value("native",0),flags);
            if(action=="down" && (flags&4)) {
                if(key=='A')args["commands"]={"selectAll"};if(key=='C')args["commands"]={"copy"};
                if(key=='V')args["commands"]={"paste"};if(key=='X')args["commands"]={"cut"};
            }
            cdp(L"Input.dispatchKeyEvent",args);
        } else if(type=="ime") {
            const auto action=message.value("action",std::string{}),text=message.value("text",std::string{});
            if(action=="commit") cdp(L"Input.insertText",{{"text",text}});
            else cdp(L"Input.imeSetComposition",{{"text",action=="cancel"?"":text},
                {"selectionStart",action=="cancel"?0:reff::widen(text).size()},{"selectionEnd",action=="cancel"?0:reff::widen(text).size()}});
        }
    }
    void fail(const std::string& message,HRESULT hr) noexcept {
        try { report["error"]=message; report["hresult"]=static_cast<long>(hr);
            std::ofstream(output/"summary.json")<<report.dump(2);
            event("error",{{"message",message},{"hresult",static_cast<long>(hr)}});
        } catch(...) {}
        result=2; DestroyWindow(window);
    }
    void event(const char* name,json details) {
        details["event"]=name;
        details["elapsedMs"]=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-launched).count();
        log<<details.dump()<<'\n'; log.flush();
    }
    int width=800,height=600;
    bool ready=false;
    json runtime_ready;
    bool hello=false,visible=true,focused=true;
    std::set<int> pressed_keys;
    int pressed_buttons{};
    POINT last_pointer{};
    bool protocol_busy=false,input_suspending=false;
    struct ProtocolCall {std::wstring method;std::string args;std::function<void()> done;};
    std::deque<ProtocolCall> protocol_calls;
    std::uint64_t viewport_generation=1;
    std::uint64_t pending_generation{};
    int pending_width{},pending_height{};
    bool viewport_in_flight=false;
    Clock::time_point viewport_started{};
    int game_capture_count{};
    std::uint64_t published_count{},published_bytes{};
    std::vector<double> map_samples,capture_samples;
    std::string active_input;
    reff::Channel channel;reff::SharedFrame shared;reff::Queue<reff::Json> incoming;
    std::vector<std::uint8_t> packed;
    Clock::time_point begun{},ready_at{};
    Clock::time_point launched=Clock::now();
    std::ofstream log;
    json report={{"backend","webview2"},{"sdk","1.0.4258.31"}};
    winrt::Windows::System::DispatcherQueueController dispatcher{nullptr};
    comp::Compositor compositor{nullptr}; comp::ContainerVisual root{nullptr};
    capture::GraphicsCaptureItem item{nullptr}; capture::Direct3D11CaptureFramePool pool{nullptr};
    capture::GraphicsCaptureSession session{nullptr};
    direct::Direct3D11::IDirect3DDevice capture_device{nullptr};
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context; ComPtr<ID3D11Texture2D> staging;
    ComPtr<ICoreWebView2Environment> environment;
    ComPtr<CoreWebView2EnvironmentOptions> options;
    ComPtr<ICoreWebView2CompositionController> composition;
    ComPtr<ICoreWebView2Controller> controller; ComPtr<ICoreWebView2> web;
};

// 隐藏辅助 HWND 只服务 WebView2 父窗口契约和消息循环；从不自动操作用户或游戏窗口。
LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    auto* host=reinterpret_cast<WebViewHost*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE) {
        host=static_cast<WebViewHost*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(host)); host->window=window;
    }
    if(host && message==WM_TIMER) {host->guard([&]{host->tick();});return 0;}
    if(message==WM_DESTROY) {PostQuitMessage(0);return 0;}
    return DefWindowProcW(window,message,wp,lp);
}

// 正式宿主入口只创建隐藏辅助窗口，所有浏览器资源归当前 STA；独立测试也走相同 IPC/资源路径。
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    int argc{}; auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    WebViewHost host;
    for(int i=1;i<argc;++i) {
        if(std::wstring(argv[i])==L"--selftest")host.selftest=true;
        else if(std::wstring(argv[i]).starts_with(L"--reff-session=")) host.game_session=std::wstring(argv[i]).substr(15);
        else if(std::wstring(argv[i]).starts_with(L"--reff-cache=")) host.cache_root=std::filesystem::path(std::wstring(argv[i]).substr(13))/L"webview2";
    }
    for(int i=1;i<argc;++i){const std::wstring arg=argv[i];
        if(arg.starts_with(L"--reff-assets="))host.assets=arg.substr(14);
        else if(arg.starts_with(L"--reff-manifests="))host.manifests=arg.substr(17);
        else if(arg.starts_with(L"--reff-game="))host.game=reff::narrow(arg.substr(12));
    }
    LocalFree(argv);
    if(host.assets.empty()||host.manifests.empty()||host.game_session.empty())return 1;
    if(!host.game_session.empty()) {
        if(host.cache_root.empty())return 1;
        host.output=host.cache_root.parent_path().parent_path()/L"log"/
            L"webview2";
    }
    if(host.output.empty()) return 1;
    std::filesystem::create_directories(host.output);
    WNDCLASSW cls{}; cls.hInstance=instance; cls.lpszClassName=L"REFFWebView2Host";cls.lpfnWndProc=procedure;
    if(!RegisterClassW(&cls)) return 1;
    auto window=CreateWindowExW(WS_EX_TOOLWINDOW,cls.lpszClassName,L"REFF WebView2 Host",WS_POPUP,
        0,0,800,600,nullptr,nullptr,instance,&host);
    if(!window) return 1;
    host.guard([&]{host.start();});
    MSG msg{}; while(GetMessageW(&msg,nullptr,0,0)>0) {TranslateMessage(&msg);DispatchMessageW(&msg);}
    host.close();
    return host.result;
}
