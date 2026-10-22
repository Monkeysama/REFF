#include "frame.hpp"
#include "settings.hpp"
#include <lua.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>

namespace {
using namespace reff;
Queue<Json> incoming;
Channel* transport{};
bool backend_ready{};
int update_ref = LUA_NOREF;
int reset_ref = LUA_NOREF;
std::uint64_t epoch=1;

// 测试驱动只模拟 REFramework 的回调注册，加载真实核心与可选示例 Lua 脚本。
int on_update(lua_State* state) { lua_pushvalue(state, 2); update_ref = luaL_ref(state, LUA_REGISTRYINDEX); return 0; }
int on_reset(lua_State* state) { lua_pushvalue(state,1);reset_ref=luaL_ref(state,LUA_REGISTRYINDEX);return 0; }
int log_line(lua_State* state) { std::cout << lua_tostring(state, 1) << '\n'; return 0; }
int set_ready(lua_State* state) { backend_ready = lua_toboolean(state, 1) != 0; return 0; }
int is_ready(lua_State* state) { lua_pushboolean(state, backend_ready); return 1; }
int emit_event(lua_State* state) {
    try {transport->send({{"type","event"},{"epoch",epoch},{"eventName",lua_tostring(state,2)},
        {"payload",Json::parse(lua_tostring(state,3))}});lua_pushboolean(state,true);return 1;}
    catch(...){return luaL_error(state,"event encoding failed");}
}
// 自检只提供示例所需的只读 REFramework 身份，不模拟或访问任何真实游戏对象。
int game_name(lua_State* state) { lua_pushstring(state, "mhwilds"); return 1; }
int framework_version(lua_State* state) { lua_pushstring(state, "smoke-test"); return 1; }
int poll(lua_State* state) {
    auto value = incoming.pop(); if (!value) return 0;
    auto raw = value->dump(); lua_pushlstring(state, raw.data(), raw.size()); return 1;
}
int respond(lua_State* state) {
    try { transport->send(Json::parse(lua_tostring(state, 1))); } catch (...) { return luaL_error(state, "response encoding failed"); }
    return 0;
}

// 测试 JSON codec 实现本原型使用的对象/数组/标量；完整 SDK 的 null 语义留在 M3。
void push_json(lua_State* state, const Json& value) {
    if (value.is_null()) lua_pushnil(state);
    else if (value.is_boolean()) lua_pushboolean(state, value.get<bool>());
    else if (value.is_number_integer()) lua_pushinteger(state, value.get<lua_Integer>());
    else if (value.is_number()) lua_pushnumber(state, value.get<double>());
    else if (value.is_string()) { auto text = value.get<std::string>(); lua_pushlstring(state, text.data(), text.size()); }
    else {
        lua_newtable(state);
        if (value.is_array()) { int i = 1; for (auto& item : value) { push_json(state, item); lua_rawseti(state, -2, i++); } }
        else for (auto it = value.begin(); it != value.end(); ++it) { push_json(state, it.value()); lua_setfield(state, -2, it.key().c_str()); }
    }
}
Json from_lua(lua_State* state, int index) {
    index = lua_absindex(state, index);
    switch (lua_type(state, index)) {
    case LUA_TNIL: return nullptr;
    case LUA_TBOOLEAN: return lua_toboolean(state, index) != 0;
    case LUA_TNUMBER: return lua_isinteger(state, index) ? Json(lua_tointeger(state, index)) : Json(lua_tonumber(state, index));
    case LUA_TSTRING: return std::string(lua_tostring(state, index));
    case LUA_TTABLE: {
        Json result = Json::object(); lua_pushnil(state);
        while (lua_next(state, index)) { if (lua_type(state, -2) != LUA_TSTRING) throw std::runtime_error("object key required");
            result[lua_tostring(state, -2)] = from_lua(state, -1); lua_pop(state, 1); }
        return result;
    }
    default: throw std::runtime_error("unsupported Lua value");
    }
}
int decode_json(lua_State* state) { try { push_json(state, Json::parse(lua_tostring(state, 1))); return 1; } catch (...) { return luaL_error(state, "invalid JSON"); } }
int encode_json(lua_State* state) { try { auto raw = from_lua(state, 1).dump(); lua_pushlstring(state, raw.data(), raw.size()); return 1; } catch (...) { return luaL_error(state, "invalid Lua value"); } }


}
// 真实 WebView2/Shell/三个前端示例 → IPC → 同源 Lua 调度器；测试进程只拥有自己的隐藏宿主。
int wmain(int argc,wchar_t** argv){
    if(argc!=4&&argc!=5)return 2;
    const bool behavior=argc==5;
    try{
        const auto root=std::filesystem::absolute(argv[1]),fixture=std::filesystem::absolute(argv[2]),host=std::filesystem::absolute(argv[3]);
        const auto session=make_session();SharedFrame frame;frame.open(session,true);
        Queue<Json> wire;Channel channel;transport=&channel;
        channel.start(session,true,[&](Json message){wire.push(std::move(message));});
        lua_State* state = luaL_newstate(); luaL_openlibs(state);
        lua_newtable(state);
        for (auto [name, fn] : {std::pair{"poll", poll}, {"respond", respond}, {"set_ready", set_ready}, {"is_ready", is_ready}, {"emit", emit_event}}) { lua_pushcfunction(state, fn); lua_setfield(state, -2, name); }
        lua_setglobal(state, "reff_native");
        lua_newtable(state); lua_pushcfunction(state, on_update); lua_setfield(state, -2, "on_application_entry");
        lua_pushcfunction(state, on_reset); lua_setfield(state, -2, "on_script_reset"); lua_setglobal(state, "re");
        lua_newtable(state); lua_pushcfunction(state, log_line); lua_setfield(state, -2, "info");
        lua_pushcfunction(state, log_line); lua_setfield(state, -2, "warn"); lua_setglobal(state, "log");
        lua_newtable(state); lua_pushcfunction(state, game_name); lua_setfield(state, -2, "get_game_name");
        lua_pushcfunction(state, framework_version); lua_setfield(state, -2, "get_version_string"); lua_setglobal(state, "reframework");
        lua_newtable(state); lua_pushcfunction(state, decode_json); lua_setfield(state, -2, "load_string");
        lua_pushcfunction(state, encode_json); lua_setfield(state, -2, "dump_string"); lua_setglobal(state, "json");
        lua_getglobal(state, "package"); auto lua_path = (root / "lua/?.lua").generic_string();
        lua_pushstring(state, lua_path.c_str()); lua_setfield(state, -2, "path"); lua_pop(state, 1);
        if (luaL_dofile(state, (root / "lua/REFF.lua").string().c_str())) throw std::runtime_error(lua_tostring(state, -1));
        if (luaL_dofile(state, (root / "examples/lua/REFF.examples.lua").string().c_str())) throw std::runtime_error(lua_tostring(state, -1));


        const auto cache=fixture/L"data/REFF/cache";std::filesystem::create_directories(cache);
        auto command=L"\""+host.wstring()+L"\" --selftest --reff-session="+session+
            L" --reff-assets=\""+(fixture/L"ui").wstring()+L"\" --reff-manifests=\""+(fixture/L"plugins").wstring()+
            L"\" --reff-cache=\""+cache.wstring()+L"\" --reff-game=MHWILDS";
        Handle job(CreateJobObjectW(nullptr,nullptr));JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if(!SetInformationJobObject(job.get(),JobObjectExtendedLimitInformation,&limits,sizeof(limits)))throw std::runtime_error("job creation failed");
        STARTUPINFOW startup{sizeof(startup)};startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
        PROCESS_INFORMATION pi{};
        if(!CreateProcessW(host.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED|CREATE_NO_WINDOW,nullptr,nullptr,&startup,&pi))throw std::runtime_error("host launch failed");
        Handle process(pi.hProcess),thread(pi.hThread);
        if(!AssignProcessToJobObject(job.get(),process.get()))throw std::runtime_error("job assignment failed");ResumeThread(thread.get());
        SettingsStore settings;std::string settings_error;
        const auto settings_path=fixture/L"data/REFF/settings.json";
        settings.open(settings_path,settings_error);
        if(!settings_error.empty())throw std::runtime_error(settings_error);
        const auto begun=GetTickCount64();Json result;std::set<std::string> handles;std::vector<Json> requests;
        bool reset_armed=false,reset_executed=false,prewarmed=false;std::string invalidated_request;
        std::uint64_t sequence{},generation{};std::vector<std::uint8_t> pixels;int width{},height{},frames{};
        std::ofstream diagnostics(fixture/"diagnostics.jsonl");
        while(GetTickCount64()-begun<60000){
            while(auto message=wire.pop()){
                const auto type=message->value("type",std::string{});
                if(type=="hello"){channel.send({{"type","ready"},{"ready",true},{"epoch",1}});
                    channel.send({{"type","visible"},{"value",!behavior}});channel.send({{"type","viewport"},{"width",960},{"height",640},{"generation",1}});}
                else if(type=="request"){
                    requests.push_back(*message);const auto method=message->value("method",std::string{});
                    if(reset_armed&&method=="example.vue.refresh"){
                        // 只重置本驱动的 Lua 状态：旧写请求不执行，原生通知新 epoch 后重新注册真实脚本。
                        reset_armed=false;reset_executed=true;invalidated_request=(*message)["id"];
                        channel.send({{"type","ready"},{"ready",false},{"epoch",++epoch}});
                        lua_rawgeti(state,LUA_REGISTRYINDEX,reset_ref);if(lua_pcall(state,0,0,0))throw std::runtime_error(lua_tostring(state,-1));
                        luaL_unref(state,LUA_REGISTRYINDEX,update_ref);luaL_unref(state,LUA_REGISTRYINDEX,reset_ref);
                        if(luaL_dofile(state,(root/"lua/REFF.lua").string().c_str())||luaL_dofile(state,(root/"examples/lua/REFF.examples.lua").string().c_str()))
                            throw std::runtime_error(lua_tostring(state,-1));
                        channel.send({{"type","response"},{"id",invalidated_request},{"epoch",1},{"result",{{"stale",true}}}});
                        channel.send({{"type","ready"},{"ready",true},{"epoch",epoch}});
                    }else if(method.starts_with("reff.settings.")){
                        Json response={{"type","response"},{"id",(*message)["id"]},{"epoch",epoch}};
                        bool ok=true;
                        if(method=="reff.settings.set")ok=settings.update((*message)["params"],settings_error);
                        else if(method=="reff.settings.reset")settings.reset();
                        if(ok&&method!="reff.settings.get"&&method!="reff.settings.hotkey.capture")ok=settings.save(settings_error);
                        if(ok)response["result"]=settings.snapshot();else response["error"]={{"code","INVALID_ARGUMENT"},{"message",settings_error}};
                        channel.send(std::move(response));
                    }
                    else {if(method=="ui.subscribe")handles.insert((*message)["params"]["subscriptionId"].get<std::string>());incoming.push(std::move(*message));}
                }else if(type=="test_result"){
                    const auto checkpoint=(*message)["result"].value("checkpoint",std::string{});
                    if(behavior&&checkpoint=="input"){
                        // 只向本驱动 Job 内的隐藏宿主发消息；测试批次不接触系统/游戏输入或剪贴板。
                        for(const auto& input:(*message)["result"]["messages"])channel.send(input);
                    }else if(behavior&&checkpoint=="marker"){
                        std::ofstream(fixture/L"plugins/example.vue/.reff-dev-version")<<GetTickCount64();
                    }else if(behavior&&checkpoint=="settings-reopen"){
                        settings.set_geometry({2560,1440,180,120,960,640});
                        if(!settings.save(settings_error))throw std::runtime_error(settings_error);
                        const auto expected=settings.snapshot();settings.open(settings_path,settings_error);
                        if(!settings_error.empty()||settings.snapshot()!=expected||!settings.restored_geometry(1920,1080))
                            throw std::runtime_error("settings/geometry reopen failed");
                    }else if(checkpoint=="reset")reset_armed=true;
                    else result=(*message)["result"];
                }
                else if(type=="test_diagnostic"){
                    diagnostics<<message->dump()<<'\n';
                    if(behavior&&!prewarmed&&message->value("frame",std::string{})=="main"&&message->value("ok",false)&&message->contains("completed")){
                        // 隐藏状态完成真实页面初始化后才显示，模拟延迟预热的浏览器半段；不伪造游戏侧计时通过。
                        // 首次打开才恢复保存尺寸，覆盖预热默认视口后立即改变布局的真实顺序。
                        prewarmed=true;channel.send({{"type","visible"},{"value",true}});
                        channel.send({{"type","viewport"},{"width",1458},{"height",818},{"generation",2}});
                    }
                }
            }
            lua_rawgeti(state,LUA_REGISTRYINDEX,update_ref);if(lua_pcall(state,0,0,0))throw std::runtime_error(lua_tostring(state,-1));
            if(frame.read(pixels,sequence,width,height,generation))++frames;
            if(!result.is_null()||WaitForSingleObject(process.get(),0)==WAIT_OBJECT_0)break;Sleep(8);
        }
        // 每个历史内部句柄应已被文档销毁撤销；直接询问真实 Lua 注册表，不能只看宿主计数。
        bool clean=true;
        for(const auto& handle:handles){const auto probe="return require('reff.sdk')._unsubscribe('"+handle+"')";
            if(luaL_dostring(state,probe.c_str()))throw std::runtime_error(lua_tostring(state,-1));
            if(lua_toboolean(state,-1))clean=false;lua_pop(state,1);}
        channel.send({{"type","shutdown"}});const bool graceful=WaitForSingleObject(process.get(),8000)==WAIT_OBJECT_0;
        channel.stop();lua_close(state);
        Json report={{"browser",result},{"frames",frames},{"width",width},{"height",height},{"generation",generation},
            {"luaSubscriptionCleanup",clean},{"gracefulExit",graceful},{"requests",requests},{"gameValidated",false}};
        const bool no_replay=behavior||(reset_executed&&std::count_if(requests.begin(),requests.end(),[&](const auto& value){return value["id"]==invalidated_request;})==1);
        const bool no_proxy=behavior||std::none_of(requests.begin(),requests.end(),[](const auto& value){return value["method"]=="reff.settings.reset";});
        report["resetExecuted"]=reset_executed;report["noWriteReplay"]=no_replay;report["noParentPrivilegeBypass"]=no_proxy;
        report["passed"]=!result.is_null()&&result.value("passed",false)&&frames>=5&&clean&&graceful&&no_replay&&no_proxy;
        report["behaviorMode"]=behavior;
        report["hiddenInitializationPassed"]=prewarmed;
        if(behavior)report["passed"]=report["passed"].get<bool>()&&prewarmed;
        std::ofstream(fixture/"integration-summary.json")<<report.dump(2);
        std::cout<<report.dump(2)<<'\n';return report["passed"].get<bool>()?0:1;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
