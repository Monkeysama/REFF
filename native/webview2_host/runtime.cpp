#include "runtime.hpp"
#include <shlwapi.h>
#include <fstream>
#include <iterator>
#include <winrt/base.h>
using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
namespace reff {
namespace {
// 每个文档的桥只传 method/params；票据保存在闭包，由原生 NavigationCompleted 分配。
// JS 消息不能选择插件、会话或响应目标；卸载和超时会失败 Promise，不重放写操作。
constexpr wchar_t bridge[]=LR"JS((()=>{
 let ticket='',sequence=0,closed=false;const pending=new Map;
 const fail=(code,message)=>Object.assign(new Error(message),{code});
 const transmit=(id,p)=>chrome.webview.postMessage({kind:'request',ticket,id,request:{method:p.method,params:p.params}});
 chrome.webview.addEventListener('message',e=>{
  const m=e.data;if(!m||closed)return;
  if(m.kind==='bind'&&!ticket){ticket=m.ticket;for(const [id,p] of pending)transmit(id,p);return;}
  if(m.ticket!==ticket)return;
  if(m.kind==='event'){window.dispatchEvent(new CustomEvent('reff:'+m.subscriptionId,{detail:m.payload}));return;}
  if(m.kind!=='response')return;const p=pending.get(m.id);if(!p)return;
  clearTimeout(p.timer);pending.delete(m.id);
  m.ok?p.resolve(m.result):p.reject(fail(m.code,m.message));
 });
 Object.defineProperty(window,'reffNativeCall',{value:(method,params={})=>new Promise((resolve,reject)=>{
  if(closed||pending.size>=256){reject(fail('HOST_DISCONNECTED','桥接已关闭或队列已满'));return;}
  const id=String(++sequence),p={method,params,resolve,reject,timer:setTimeout(()=>{
   pending.delete(id);reject(fail('TIMEOUT','宿主响应超时'));},6500)};
  pending.set(id,p);if(ticket)transmit(id,p);
 }),writable:false,configurable:false});
 addEventListener('pagehide',()=>{closed=true;for(const p of pending.values()){
  clearTimeout(p.timer);p.reject(fail('PAGE_UNLOADED','页面已卸载'));}pending.clear();},{once:true});
})())JS";
std::string take(LPWSTR text){std::wstring copy=text?text:L"";CoTaskMemFree(text);return narrow(copy);}
std::string mime(const std::filesystem::path& path){
    const auto ext=path.extension().string();
    if(ext==".html")return "text/html; charset=utf-8";
    if(ext==".js"||ext==".mjs")return "text/javascript; charset=utf-8";
    if(ext==".css")return "text/css; charset=utf-8";
    if(ext==".json")return "application/json";
    if(ext==".svg")return "image/svg+xml";
    if(ext==".png")return "image/png";
    if(ext==".jpg"||ext==".jpeg")return "image/jpeg";
    if(ext==".woff2")return "font/woff2";
    if(ext==".woff")return "font/woff";
    return "application/octet-stream";
}
}
// 正式 custom scheme 具有 authority，因此每个插件 host 是独立安全来源；不映射整个磁盘目录。
void WebViewRuntime::configure_options(ICoreWebView2EnvironmentOptions* options){
    auto scheme=Microsoft::WRL::Make<CoreWebView2CustomSchemeRegistration>(L"reff");
    winrt::check_hresult(scheme->put_TreatAsSecure(TRUE));
    winrt::check_hresult(scheme->put_HasAuthorityComponent(TRUE));
    const wchar_t* origins[]={L"reff://shell",L"reff://*.plugin"};
    winrt::check_hresult(scheme->SetAllowedOrigins(2,origins));
    ComPtr<ICoreWebView2EnvironmentOptions4> options4;winrt::check_hresult(options->QueryInterface(IID_PPV_ARGS(&options4)));
    ICoreWebView2CustomSchemeRegistration* list[]={scheme.Get()};
    winrt::check_hresult(options4->SetCustomSchemeRegistrations(1,list));
}
// COM 回调不能向浏览器边界抛异常；HRESULT 与普通异常都交由宿主关闭会话并释放输入。
void WebViewRuntime::guard(const std::function<void()>& action)noexcept{
    if(closed_)return;try{action();}
    catch(const winrt::hresult_error& error){if(failure)failure(winrt::to_string(error.message()));}
    catch(const std::exception& error){if(failure)failure(error.what());}
    catch(...){if(failure)failure("unknown browser callback failure");}
}
// 原生持有实际 frame，并为每次导航分配不可复用的文档身份；IPC ID 包含 frame 前缀。
void WebViewRuntime::initialize(const std::shared_ptr<Document>& doc){
    doc->services.configure(session_,assets_,manifests_,game_);
    doc->services.self_test_=self_test;doc->services.dev_enabled=dev_enabled;
    doc->services.send=[this](Json value){return !closed_&&send&&send(std::move(value));};
    const std::weak_ptr<Document> weak=doc;
    doc->services.emit=[this,weak](const std::string& id,const Json& payload){if(const auto d=weak.lock())
        post(d,{{"kind","event"},{"subscriptionId",id},{"payload",payload}});};
    doc->services.reload=[this,weak]{if(const auto d=weak.lock()){
        if(d->main)web_->Reload();else if(d->frame)d->frame->ExecuteScript(L"location.reload()",nullptr);}};
    doc->services.dispatch(ready_);
}
// 仅注册本地资源、当前主文档及直接 iframe；不开放 host object、DevTools、下载或外部窗口。
void WebViewRuntime::attach(ICoreWebView2* web,ICoreWebView2Environment* environment,std::string session,
    std::filesystem::path assets,std::filesystem::path manifests,std::string game){
    web_=web;environment_=environment;session_=std::move(session);assets_=std::move(assets);
    manifests_=std::move(manifests);game_=std::move(game);
    auto main=std::make_shared<Document>();main->main=true;main->id="main";initialize(main);documents_.push_back(main);
    EventRegistrationToken token{};
    winrt::check_hresult(web_->AddScriptToExecuteOnDocumentCreated(bridge,
        Callback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>([this](HRESULT result,LPCWSTR)->HRESULT{
            guard([&]{winrt::check_hresult(result);winrt::check_hresult(web_->Navigate(L"reff://shell/index.html"));});return S_OK;
        }).Get()));
    winrt::check_hresult(web_->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>(
        [this,main](ICoreWebView2*,auto* args)->HRESULT{guard([&]{starting(main,args);});return S_OK;}).Get(),&token));
    winrt::check_hresult(web_->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>(
        [this,main](ICoreWebView2*,auto* args)->HRESULT{guard([&]{completed(main,args);});return S_OK;}).Get(),&token));
    winrt::check_hresult(web_->add_WebMessageReceived(Callback<ICoreWebView2WebMessageReceivedEventHandler>(
        [this,main](ICoreWebView2*,auto* args)->HRESULT{guard([&]{message(main,args);});return S_OK;}).Get(),&token));
    ComPtr<ICoreWebView2_4> web4;winrt::check_hresult(web_.As(&web4));
    winrt::check_hresult(web4->add_FrameCreated(Callback<ICoreWebView2FrameCreatedEventHandler>(
        [this](ICoreWebView2*,auto* args)->HRESULT{guard([&]{ComPtr<ICoreWebView2Frame> f;
            winrt::check_hresult(args->get_Frame(&f));frame_created(f.Get());});return S_OK;}).Get(),&token));
    winrt::check_hresult(web4->add_DownloadStarting(Callback<ICoreWebView2DownloadStartingEventHandler>(
        [](ICoreWebView2*,auto* args)->HRESULT{return args->put_Cancel(TRUE);}).Get(),&token));
    winrt::check_hresult(web_->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>(
        [](ICoreWebView2*,auto* args)->HRESULT{return args->put_Handled(TRUE);}).Get(),&token));
    winrt::check_hresult(web_->add_PermissionRequested(Callback<ICoreWebView2PermissionRequestedEventHandler>(
        [](ICoreWebView2*,auto* args)->HRESULT{return args->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY);}).Get(),&token));
    ComPtr<ICoreWebView2_22> web22;winrt::check_hresult(web_.As(&web22));
    winrt::check_hresult(web22->AddWebResourceRequestedFilterWithRequestSourceKinds(L"*",
        COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL,COREWEBVIEW2_WEB_RESOURCE_REQUEST_SOURCE_KINDS_ALL));
    winrt::check_hresult(web_->add_WebResourceRequested(Callback<ICoreWebView2WebResourceRequestedEventHandler>(
        [this](ICoreWebView2*,auto* args)->HRESULT{guard([&]{resource(args);});return S_OK;}).Get(),&token));
    // 仅独立夹具启用浏览器错误诊断，不在正常运行时记录控制台或用户输入。
    if(self_test){ComPtr<ICoreWebView2DevToolsProtocolEventReceiver> receiver;
        winrt::check_hresult(web_->GetDevToolsProtocolEventReceiver(L"Log.entryAdded",&receiver));
        winrt::check_hresult(receiver->add_DevToolsProtocolEventReceived(Callback<ICoreWebView2DevToolsProtocolEventReceivedEventHandler>(
            [this](ICoreWebView2*,auto* args)->HRESULT{guard([&]{LPWSTR raw{};winrt::check_hresult(args->get_ParameterObjectAsJson(&raw));
                if(send)send({{"type","test_diagnostic"},{"console",Json::parse(take(raw))}});});return S_OK;}).Get(),&token));
        winrt::check_hresult(web_->CallDevToolsProtocolMethod(L"Log.enable",L"{}",nullptr));}
}
// iframe 事件由 Runtime 分配实际对象，不接受网页传入 frame/plugin ID；文档销毁后撤销全部权限。
void WebViewRuntime::frame_created(ICoreWebView2Frame* frame){
    if(documents_.size()>=65)throw std::runtime_error("too many browser frames");
    auto doc=std::make_shared<Document>();doc->id="f"+std::to_string(++frame_sequence_);
    winrt::check_hresult(frame->QueryInterface(IID_PPV_ARGS(&doc->frame)));initialize(doc);documents_.push_back(doc);
    const std::weak_ptr<Document> weak=doc;EventRegistrationToken token{};
    winrt::check_hresult(doc->frame->add_NavigationStarting(Callback<ICoreWebView2FrameNavigationStartingEventHandler>(
        [this,weak](ICoreWebView2Frame*,auto* args)->HRESULT{guard([&]{if(auto d=weak.lock())starting(d,args);});return S_OK;}).Get(),&token));
    winrt::check_hresult(doc->frame->add_NavigationCompleted(Callback<ICoreWebView2FrameNavigationCompletedEventHandler>(
        [this,weak](ICoreWebView2Frame*,auto* args)->HRESULT{guard([&]{if(auto d=weak.lock())completed(d,args);});return S_OK;}).Get(),&token));
    winrt::check_hresult(doc->frame->add_WebMessageReceived(Callback<ICoreWebView2FrameWebMessageReceivedEventHandler>(
        [this,weak](ICoreWebView2Frame*,auto* args)->HRESULT{guard([&]{if(auto d=weak.lock())message(d,args);});return S_OK;}).Get(),&token));
    winrt::check_hresult(frame->add_Destroyed(Callback<ICoreWebView2FrameDestroyedEventHandler>(
        [this,weak](ICoreWebView2Frame*,IUnknown*)->HRESULT{guard([&]{if(auto d=weak.lock()){
            d->ticket.clear();d->dead=true;d->services.release_page();}});return S_OK;}).Get(),&token));
    ComPtr<ICoreWebView2Frame7> frame4;winrt::check_hresult(doc->frame.As(&frame4));
    winrt::check_hresult(frame4->add_FrameCreated(Callback<ICoreWebView2FrameChildFrameCreatedEventHandler>(
        [this](ICoreWebView2Frame*,auto*)->HRESULT{guard([&]{throw std::runtime_error("nested plugin frame denied");});return S_OK;}).Get(),&token));
}
void WebViewRuntime::starting(const std::shared_ptr<Document>& doc,ICoreWebView2NavigationStartingEventArgs* args){
    LPWSTR raw{};winrt::check_hresult(args->get_Uri(&raw));const auto url=take(raw);
    const auto route=doc->services.route_for_url(url);
    const bool allowed=route&&(doc->main?(route->shell&&route->relative_path=="index.html"):
        (!route->shell&&route->manifest->ui_mode=="isolated-page"&&route->relative_path==route->manifest->entry));
    if(self_test&&send)send({{"type","test_diagnostic"},{"navigation",url},{"frame",doc->id},{"allowed",allowed}});
    if(!allowed){winrt::check_hresult(args->put_Cancel(TRUE));return;}
    doc->ticket.clear();doc->url=url;winrt::check_hresult(args->get_NavigationId(&doc->navigation));
    doc->services.navigate(url,doc->id);
    if(doc->main)for(auto& other:documents_)if(!other->main){other->ticket.clear();other->services.release_page();}
}
void WebViewRuntime::completed(const std::shared_ptr<Document>& doc,ICoreWebView2NavigationCompletedEventArgs* args){
    BOOL ok{};UINT64 navigation{};winrt::check_hresult(args->get_IsSuccess(&ok));
    winrt::check_hresult(args->get_NavigationId(&navigation));
    if(self_test&&send)send({{"type","test_diagnostic"},{"completed",doc->url},{"frame",doc->id},{"ok",bool(ok)}});
    if(!ok||navigation!=doc->navigation||doc->dead)return;
    GUID id{};winrt::check_hresult(CoCreateGuid(&id));wchar_t text[40]{};StringFromGUID2(id,text,40);doc->ticket=narrow(text);
    post(doc,{{"kind","bind"}});
}
void WebViewRuntime::post(const std::shared_ptr<Document>& doc,Json value){
    if(closed_||doc->dead||doc->ticket.empty())return;
    value["ticket"]=doc->ticket;const auto raw=widen(value.dump());
    if(doc->main)winrt::check_hresult(web_->PostWebMessageAsJson(raw.c_str()));
    else winrt::check_hresult(doc->frame->PostWebMessageAsJson(raw.c_str()));
}
// 浏览器提供 Source，票据绑定当前导航；页面业务字段仅允许 method/params，不能伪造宿主路由。
void WebViewRuntime::message(const std::shared_ptr<Document>& doc,ICoreWebView2WebMessageReceivedEventArgs* args){
    LPWSTR source{};winrt::check_hresult(args->get_Source(&source));const auto origin=take(source);
    if(doc->dead||doc->ticket.empty()||origin!=doc->url)return;
    LPWSTR raw{};winrt::check_hresult(args->get_WebMessageAsJson(&raw));const auto body=take(raw);
    if(body.size()>max_message)return;const auto value=Json::parse(body,nullptr,false);
    if(!value.is_object()||value.size()!=4||!value.contains("kind")||!value["kind"].is_string()||value["kind"]!="request"||
        !value.contains("ticket")||!value["ticket"].is_string()||value["ticket"]!=doc->ticket||
        !value.contains("id")||!value["id"].is_string()||!value.contains("request")||!value["request"].is_object())return;
    const auto id=value["id"].get<std::string>();if(id.empty()||id.size()>64)return;
    if(!valid_page_request(value["request"])){
        post(doc,{{"kind","response"},{"id",id},{"ok",false},{"code","INVALID_PAGE_REQUEST"},{"message","method/params required"}});return;}
    if(!doc->main&&value["request"].value("method",std::string{})=="ui.input.focus"){
        post(doc,{{"kind","response"},{"id",id},{"ok",false},{"code","FORBIDDEN"},{"message","focus requires Shell coordinates"}});return;}
    auto reply=std::make_shared<HostServices::Reply>();const auto ticket=doc->ticket;
    const std::weak_ptr<Document> weak=doc;
    reply->complete=[this,weak,ticket,id](int status,const std::string& result){if(auto d=weak.lock()){
        if(d->ticket!=ticket)return;Json response={{"kind","response"},{"id",id},{"ok",status==0}};
        if(!status)response["result"]=Json::parse(result);else{
            const auto error=Json::parse(result,nullptr,false);response["code"]=error.is_object()?error.value("code","TRANSPORT_ERROR"):result;
            response["message"]=error.is_object()?error.value("message",result):result;}
        post(d,std::move(response));}};
    doc->services.query(value["request"].dump(),std::move(reply));
}
// 同步读取有界本地文件；先检查清单根/重解析点，再返回 MIME/CSP。外部 URL 不送往网络。
void WebViewRuntime::resource(ICoreWebView2WebResourceRequestedEventArgs* args){
    ComPtr<ICoreWebView2WebResourceRequest> request;winrt::check_hresult(args->get_Request(&request));
    LPWSTR raw{};winrt::check_hresult(request->get_Uri(&raw));const auto url=take(raw);
    LPWSTR method_raw{};winrt::check_hresult(request->get_Method(&method_raw));const auto method=take(method_raw);
    const auto& routes=documents_.front()->services;const auto route=routes.route_for_url(url);
    const auto path=method=="GET"?routes.resource_path(url):std::nullopt;
    int status=403;std::string content,headers="Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n";
    if(path){std::error_code error;const auto size=std::filesystem::file_size(*path,error);
        if(!error&&size<=16*1024*1024){std::ifstream file(*path,std::ios::binary);content.assign(std::istreambuf_iterator<char>(file),{});
            if(file.good()||file.eof()){status=200;headers+="Content-Type: "+mime(*path)+"\r\n";}}}
    if(status==200&&route){
        const bool shell=route->shell;
        headers+="Content-Security-Policy: default-src 'none'; script-src 'self' 'unsafe-inline' reff://shell/shared/; "
            "style-src 'self' 'unsafe-inline' reff://shell/shared/; img-src 'self' data: blob:; font-src 'self' reff://shell/shared/; "
            "connect-src 'none'; object-src 'none'; base-uri 'none'; form-action 'none'; frame-src "+std::string(shell?"reff:":"'none'")+"; frame-ancestors "+(shell?"'none'":"reff://shell")+"\r\n";
        if(shell&&route->relative_path.starts_with("shared/"))headers+="Access-Control-Allow-Origin: *\r\n";
    }
    if(status!=200)content="Resource denied";
    ComPtr<IStream> stream;stream.Attach(SHCreateMemStream(reinterpret_cast<const BYTE*>(content.data()),static_cast<UINT>(content.size())));
    if(!stream)throw std::runtime_error("resource stream allocation failed");
    ComPtr<ICoreWebView2WebResourceResponse> response;
    winrt::check_hresult(environment_->CreateWebResourceResponse(stream.Get(),status,status==200?L"OK":L"Forbidden",widen(headers).c_str(),&response));
    winrt::check_hresult(args->put_Response(response.Get()));
    if(self_test&&send)send({{"type","test_diagnostic"},{"url",url},{"status",status}});
}
void WebViewRuntime::dispatch(const Json& message){if(message.value("type",std::string{})=="ready")ready_=message;
    for(const auto& doc:documents_)doc->services.dispatch(message);}
// STA 逐文档清除逻辑焦点；不调用 MoveFocus 或激活隐藏 HWND，避免抢走游戏/IME 代理的系统焦点。
void WebViewRuntime::suspend_input(){
    constexpr auto code=L"document.activeElement?.blur();window.dispatchEvent(new Event('reff:input-suspend'))";
    for(const auto& doc:documents_)if(!doc->dead&&!doc->ticket.empty()){
        if(doc->main)web_->ExecuteScript(code,nullptr);else doc->frame->ExecuteScript(code,nullptr);
    }
}
void WebViewRuntime::tick(){for(const auto& doc:documents_)doc->services.tick();
    std::erase_if(documents_,[](const auto& doc){return doc->dead&&doc->services.counts()["subscriptionCleanup"]==0;});}
void WebViewRuntime::close(){if(closed_)return;for(auto& doc:documents_){doc->ticket.clear();doc->services.release_page();}closed_=true;
    documents_.clear();web_.Reset();environment_.Reset();}
}
