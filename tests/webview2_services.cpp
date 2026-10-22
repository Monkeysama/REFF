#include "services.hpp"
#include <iostream>
#include <fstream>
#include "keyboard.hpp"
using namespace reff;
namespace {
void require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
struct Result {int code=-1;std::string raw;};
// 同步入口只收集回调与 IPC 消息，不模拟浏览器身份；实际 frame 绑定另由浏览器联调覆盖。
std::shared_ptr<Result> call(HostServices& service,const std::string& method,Json params=Json::object()){
    auto result=std::make_shared<Result>();auto reply=std::make_shared<HostServices::Reply>();
    reply->complete=[result](int code,const std::string& raw){result->code=code;result->raw=raw;};
    service.query(Json{{"method",method},{"params",params}}.dump(),reply);return result;
}
}
// 独立验证资源边界、权限、跨文档订阅和 epoch；文件只读取测试夹具，不访问游戏目录。
int wmain(int argc,wchar_t** argv){try{
    if(argc!=2)return 2;const std::filesystem::path fixture=argv[1];
    const auto config=fixture/"dev-test";std::filesystem::create_directories(config);
    require(!development_enabled(config),"missing development config enabled");
    for(const auto raw:{"{bad",R"({"enabled":"true"})",R"({"enabled":false})",R"({"enabled":true,"extra":1})"}){
        std::ofstream(config/"development.json")<<raw;require(!development_enabled(config),"invalid development config enabled");}
    std::ofstream(config/"development.json")<<R"({"enabled":true})";
    require(development_enabled(config),"explicit development config rejected");
    Json keyboard;webview2_key_identity(keyboard,'K',0,4);require(keyboard["code"]=="KeyK"&&keyboard["key"]=="k","letter key mapping failed");
    webview2_key_identity(keyboard,VK_F24,0,0);require(keyboard["code"]=="F24","function key mapping failed");
    webview2_key_identity(keyboard,VK_OEM_1,0,0);require(keyboard["code"]=="Semicolon","punctuation mapping failed");
    HostServices a,b;std::vector<Json> sent;bool available=true;
    auto setup=[&](HostServices& s){s.configure("test",fixture/"ui",fixture/"plugins","MHWILDS");
        s.send=[&](Json value){if(!available)return false;sent.push_back(std::move(value));return true;};
        s.dispatch({{"type","ready"},{"ready",true},{"epoch",1}});};
    setup(a);setup(b);
    require(a.resource_path("reff://shell/index.html").has_value(),"valid Shell resource rejected");
    require(a.resource_path("reff://example.vue.plugin/ui/dist/index.html?reload=1").has_value(),"cache busting rejected");
    for(const auto* url:{"https://example.com/","file:///C:/Windows/win.ini","reff://shell/../version.json",
        "reff://shell/%2e%2e/version.json","reff://shell/C:/Windows/win.ini","reff://unknown.plugin/index.html",
        "reff://shell/escape/version.json","reff://shell/link-root/index.html"})
        require(!a.resource_path(url),"unsafe resource accepted");
    require(a.navigate("reff://example.vue.plugin/ui/dist/index.html","f1"),"plugin navigation rejected");
    require(b.navigate("reff://example.vue.plugin/ui/dist/index.html","f2"),"second frame rejected");
    require(call(a,"ui.plugins")->code==403,"plugin acquired Shell privileges");
    require(call(a,"reff.settings.get")->code==403,"plugin read core settings");
    require(call(a,"example.react.get")->code==403,"cross-plugin method accepted");
    require(call(a,"ui.dev.version")->raw=="DEV_DISABLED","development endpoint enabled by default");
    require(call(a,"ui.reload")->raw=="DEV_DISABLED","reload enabled by default");
    auto forged=std::make_shared<HostServices::Reply>();int status{};
    forged->complete=[&](int code,const auto&){status=code;};
    a.query(R"({"method":"ui.status","params":{},"pluginId":"shell"})",forged);
    require(status==400,"forged identity accepted");
    require(call(a,"ui.unsubscribe",{{"subscriptionId","unowned"}})->code==403,"unowned handle accepted");
    const Json params={{"eventName","example.vue.refreshed"},{"subscriptionId","same-local-handle"}};
    auto first=call(a,"ui.subscribe",params);const auto req1=sent.back();
    require(call(a,"ui.subscribe",params)->code==409,"pending duplicate accepted");
    auto second=call(b,"ui.subscribe",params);const auto req2=sent.back();
    require(req1["id"]!=req2["id"]&&req1["params"]!=req2["params"],"frames share native request/subscription IDs");
    auto response=[](const Json& req){return Json{{"type","response"},{"id",req["id"]},{"epoch",1},{"result",{{"subscriptionId",req["params"]["subscriptionId"]}}}};};
    a.dispatch(response(req2));require(first->code==-1,"response crossed frames");
    a.dispatch(response(req1));b.dispatch(response(req2));
    require(first->code==0&&second->code==0,"subscription response failed");
    require(Json::parse(first->raw)["subscriptionId"]=="same-local-handle","internal handle leaked");
    int events{};a.emit=[&](const std::string& id,const Json&){require(id=="same-local-handle","wrong local event ID");++events;};
    a.dispatch({{"type","event"},{"epoch",0},{"eventName","example.vue.refreshed"},{"payload",{}}});
    a.dispatch({{"type","event"},{"epoch",1},{"eventName","example.vue.refreshed"},{"payload",{{"ok",true}}}});
    require(events==1,"event epoch/ownership failed");
    available=false;a.release_page();require(a.counts()["subscriptionCleanup"]==1,"cleanup lost under backpressure");
    available=true;a.tick();require(a.counts()["subscriptionCleanup"]==0,"cleanup was not retried");
    require(sent.back()["params"]["subscriptionId"]==req1["params"]["subscriptionId"],"cleanup revoked another frame");
    require(a.navigate("reff://example.vue.plugin/ui/dist/index.html","f1"),"reload failed");
    auto old=call(a,"example.vue.get");const auto old_req=sent.back();
    a.dispatch({{"type","ready"},{"ready",false},{"epoch",2}});
    require(old->raw=="SCRIPT_RESET","pending request survived epoch reset");
    a.dispatch(response(old_req));require(old->raw=="SCRIPT_RESET","stale response reused");
    require(call(a,"example.vue.get")->raw=="NOT_READY","write accepted while not ready");
    a.dispatch({{"type","ready"},{"ready",true},{"epoch",2}});
    auto timeout=call(a,"ui.subscribe",params);const auto timeout_req=sent.back();
    Sleep(5100);a.tick();require(timeout->raw=="TIMEOUT","deadline failed");
    require(sent.back()["params"]["subscriptionId"]==timeout_req["params"]["subscriptionId"],"timed-out subscribe leaked");
    b.release_page();std::cout<<"WebView2 services: resource/permission/identity/subscription/reset/timeout passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
