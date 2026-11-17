#include "frame.hpp"
#include "settings.hpp"
#include "window_regions.hpp"
#include "webview2/preview_gpu.hpp"
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

// 保存实际共享帧供人工检查，使用原始 BGRA，不另起浏览器或重绘测试页面。
void save_capture(const std::filesystem::path& path,const std::vector<std::uint8_t>& pixels,int width,int height){
    if(width<=0||height<=0||pixels.size()!=std::size_t(width)*height*4)throw std::runtime_error("capture dimensions invalid");
    BITMAPFILEHEADER file{};file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);
    file.bfSize=file.bfOffBits+static_cast<DWORD>(pixels.size());
    BITMAPINFOHEADER info{sizeof(info)};info.biWidth=width;info.biHeight=-height;info.biPlanes=1;info.biBitCount=32;info.biCompression=BI_RGB;
    std::ofstream output(path,std::ios::binary);output.write(reinterpret_cast<const char*>(&file),sizeof(file));
    output.write(reinterpret_cast<const char*>(&info),sizeof(info));output.write(reinterpret_cast<const char*>(pixels.data()),pixels.size());
    if(!output)throw std::runtime_error("capture write failed");
}

}
// 真实 WebView2/Shell/三个前端示例 → IPC → 同源 Lua 调度器；测试进程只拥有自己的隐藏宿主。
int wmain(int argc,wchar_t** argv){
    if(argc!=4&&argc!=5)return 2;
    const bool behavior=argc==5;
    const bool scaling=argc==5&&std::wstring(argv[4])==L"production-scale";
    const bool production=scaling||(argc==5&&std::wstring(argv[4])==L"production-preview");
    const bool preview=production||(argc==5&&std::wstring(argv[4])==L"preview");
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
            L" --reff-assets=\""+(fixture/L"outside").wstring()+L"\""+
            L" --reff-test-script=\""+(fixture/L"ui/shared/p1-test.js").wstring()+L"\" --reff-manifests=\""+(fixture/L"plugins").wstring()+
            L"\" --reff-cache=\""+cache.wstring()+L"\" --reff-game=MHWILDS";
        // 正式入口不启用 selftest 或注入脚本；隔离页从自己的模块注册槽位。
        if(production)command=L"\""+host.wstring()+L"\" --reff-session="+session+L" --reff-manifests=\""+
            (fixture/L"plugins").wstring()+L"\" --reff-cache=\""+cache.wstring()+L"\" --reff-game=MHWILDS";
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
        if((production||preview)&&!settings.update({{"appearance",{{"cornerRadius",6},{"surfaceBlur",8},{"textScale",production?1.2:1.4}}},
            {"layout",{{"sidebarCollapsed",true},{"headerCollapsed",true},{"pluginPadding",0}}},
            {"navigation",{{"rememberLastPlugin",true},{"lastPluginId","example.vue"}}}},settings_error))throw std::runtime_error(settings_error);
        const auto begun=GetTickCount64();Json result;std::set<std::string> handles;std::vector<Json> requests;
        bool reset_armed=false,reset_executed=false,prewarmed=false;std::string invalidated_request;
        std::uint64_t sequence{},generation{};std::vector<std::uint8_t> pixels;int width{},height{},frames{},layout_captures{};
        std::ofstream diagnostics(fixture/"diagnostics.jsonl");
        WindowRegions drag_regions;
        std::unique_ptr<reff::test::PreviewGpu> preview_gpu;if(preview)preview_gpu=std::make_unique<reff::test::PreviewGpu>();
        std::uint64_t preview_revision{};int preview_checks{};
        SharedFrame composition_frame;if(scaling)composition_frame.open(session+L"-composition",true);PreviewFrame production_frame;
        UINT64 scale_started{},last_resize{};int scale_composites{},scale_leaks{},scale_samples{},scale_steps{},scale_green{},scale_background{},css_composites{},css_leaks{},css_green{},settled_green{};std::uint64_t wanted_generation=1;int wanted_width=1458,wanted_height=818;
        while(GetTickCount64()-begun<(production?30000:60000)){
            while(auto message=wire.pop()){
                const auto type=message->value("type",std::string{});
                if(type=="preview_state"){preview_revision=message->value("revision",0ull);}
                else if(type=="hello"){channel.send({{"type","ready"},{"ready",true},{"epoch",1}});
                    channel.send({{"type","visible"},{"value",production||!behavior}});channel.send({{"type","viewport"},{"width",production?1458:960},{"height",production?818:640},{"generation",1}});}
                else if(type=="window_regions"){
                    if(const auto parsed=WindowRegions::parse((*message)["regions"]))drag_regions=*parsed;
                }
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
                    }else if(method=="ui.layout.get"||method=="ui.layout.set"){
                        Json response={{"type","response"},{"id",(*message)["id"]},{"epoch",epoch}};
                        const auto id=message->at("pluginId").get<std::string>();bool ok=true;
                        if(method=="ui.layout.set"){
                            ok=settings.set_plugin_padding(id,(*message)["params"]["padding"],settings_error)&&settings.save(settings_error);
                            if(ok)channel.send({{"type","settings_changed"}});
                        }
                        if(ok)response["result"]=settings.plugin_layout(id);
                        else response["error"]={{"code","INVALID_ARGUMENT"},{"message",settings_error}};
                        channel.send(std::move(response));
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
                    if(preview&&checkpoint=="preview-capture"){
                        const auto& expected=(*message)["result"];
                        std::uint64_t seq=0,vg{};int fw{},fh{};PreviewFrame metadata;std::vector<std::uint8_t> web;
                        if(!frame.read(web,seq,fw,fh,vg,0,&metadata)||metadata.revision!=preview_revision||metadata.count!=expected.value("count",0u))throw std::runtime_error("preview frame/revision/count not confirmed: "+std::to_string(metadata.count)+" revision="+std::to_string(metadata.revision)+"/"+std::to_string(preview_revision)+" expected="+expected.dump()+" clip="+Json::array({metadata.slots[0].clip.x,metadata.slots[0].clip.y,metadata.slots[0].clip.width,metadata.slots[0].clip.height}).dump());
                        if(expected.contains("provider")){if(expected["provider"].get<bool>())preview_gpu->enable();else preview_gpu->disable();}
                        if(expected.value("rebuild",false))preview_gpu->rebuild();
                        if(metadata.count){
                            const auto& desired=expected["rect"];
                            const auto& actual=metadata.slots[0].destination;
                            const float numbers[]{actual.x,actual.y,actual.width,actual.height};
                            for(int i=0;i<4;++i)if(std::abs(numbers[i]-desired[i].get<float>())>1.1f)throw std::runtime_error("native preview iframe projection mismatch");
                            const auto& clip=metadata.slots[0].clip;const float clips[]{clip.x,clip.y,clip.width,clip.height};
                            for(int i=0;i<4;++i)if(std::abs(clips[i]-expected["clip"][i].get<float>())>1.1f)throw std::runtime_error("native preview scroll clip mismatch");
                        }
                        auto output=preview_gpu->render(frame,fw,fh,vg,preview_revision);
                        for(const auto& sample:expected["samples"]){const int x=sample["x"],y=sample["y"];const auto* p=&output[(y*reff::test::PreviewGpu::width+x)*4];
                            for(int i=0;i<3;++i)if(std::abs(int(p[i])-sample["rgb"][i].get<int>())>4)throw std::runtime_error("browser/native preview composition mismatch "+std::to_string(x)+","+std::to_string(y)+" actual="+std::to_string(p[0])+","+std::to_string(p[1])+","+std::to_string(p[2]));
                        }
                        for(std::size_t i=0;i<output.size();i+=4)std::swap(output[i],output[i+2]);
                        save_capture(fixture/("preview-"+std::to_string(preview_checks)+".bmp"),output,reff::test::PreviewGpu::width,reff::test::PreviewGpu::height);
                        ++preview_checks;channel.send({{"type","test_ack"},{"id",expected.value("captureId",0)}});
                    }else if(behavior&&checkpoint=="layout-capture"){
                        const auto index=(*message)["result"].value("index",-1);
                        if(index<0||index>20)throw std::runtime_error("invalid test capture index");
                        if (frame.read(pixels,sequence,width,height,generation)) ++frames;
                        save_capture(fixture/("layout-"+std::to_string(index)+".bmp"),pixels,width,height);
                        ++layout_captures;
                        for(const auto& point:(*message)["result"]["points"]){
                            if(drag_regions.contains(point["x"],point["y"],width,height)!=point["drag"].get<bool>())
                                throw std::runtime_error("Shell/native drag hit region mismatch");
                        }
                    }else if(behavior&&checkpoint=="input"){
                        // 只向本驱动 Job 内的隐藏宿主发消息；测试批次不接触系统/游戏输入或剪贴板。
                        // 只记录独立夹具内置的输入批次，定位浏览器默认行为/暂停造成的挂起；不采集游戏输入。
                        diagnostics<<Json{{"inputBatch",(*message)["result"]["messages"]}}.dump()<<'\n';diagnostics.flush();
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
            if(frame.read(pixels,sequence,width,height,generation,0,&production_frame)){
                ++frames;
                if(production&&!scaling&&production_frame.count==1&&production_frame.revision==preview_revision){
                    auto output=preview_gpu->render(frame,width,height,generation,preview_revision);
                    const auto clip=production_frame.slots[0].clip;const int x=int(clip.x+clip.width/2),y=int(clip.y+clip.height/2);
                    const auto* p=output.data()+(y*reff::test::PreviewGpu::width+x)*4;
                    if(p[0]!=0||p[1]!=255||p[2]!=0||preview_gpu->draws<1)throw std::runtime_error("production preview GPU pixel mismatch");
                    for(std::size_t i=0;i<output.size();i+=4)std::swap(output[i],output[i+2]);
                    save_capture(fixture/"production-preview.bmp",output,reff::test::PreviewGpu::width,reff::test::PreviewGpu::height);
                    result={{"passed",true},{"productionHost",true},{"selfTestEnabled",false},{"cornerRadius",6},{"surfaceBlur",8},{"textScale",1.2},{"confirmedSlots",1}};
                }
            }
            // 正式入口连续改变视口；插件自身同时改变 CSS 字号/zoom，逐次 Present 检查透明孔下方不是游戏。
            if(scaling&&frames>0){
                const auto now=GetTickCount64();if(!scale_started&&production_frame.count==1)scale_started=now;
                if(scale_started){
                    if(now-scale_started<3500&&now-last_resize>65){last_resize=now;++scale_steps;++wanted_generation;wanted_width=1000+(scale_steps%8)*50;wanted_height=660+(scale_steps%5)*25;
                        channel.send({{"type","viewport"},{"width",wanted_width},{"height",wanted_height},{"generation",wanted_generation}});}
                    composition_frame.write(pixels.data(),width,height,generation,production_frame);
                    auto output=preview_gpu->render(composition_frame,width,height,wanted_generation,preview_revision,false,true,wanted_width,wanted_height);++scale_composites;
                    const float scale=std::min(float(wanted_width)/width,float(wanted_height)/height),left=(wanted_width-width*scale)/2,top=(wanted_height-height*scale)/2;
                    int leaks=0,samples=0,green=0,background=0;for(int y=120;y<height-100;y+=4)for(int x=120;x<width-100;x+=4){
                        // 只检查透明孔内部；边缘双线性采样/矩形裁剪保留原 alpha 语义，不用补色越界抹掉合法游戏混合。
                        bool interior=true;for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)if(pixels[((y+dy)*width+x+dx)*4+3])interior=false;if(!interior)continue;const int ox=int(left+(x+.5f)*scale),oy=int(top+(y+.5f)*scale);
                        const auto* color=output.data()+(oy*reff::test::PreviewGpu::width+ox)*4;++samples;if(color[0]<10&&color[1]>245&&color[2]<10)++green;if(color[0]==17&&color[1]==24&&color[2]==25)++background;if(color[0]>245&&color[1]<10&&color[2]>245)++leaks;}
                    if(leaks){diagnostics<<Json{{"leaks",leaks},{"phase",now-scale_started},{"frameGen",generation},{"wantedGen",wanted_generation},{"frameRev",production_frame.revision},{"wantedRev",preview_revision},{"count",production_frame.count},{"flags",production_frame.flags},{"size",{width,height}},{"clip",{production_frame.slots[0].clip.x,production_frame.slots[0].clip.y,production_frame.slots[0].clip.width,production_frame.slots[0].clip.height}}}.dump()<<'\n';}
                    // 保存真实 GPU 合成结果；浏览器单独截图不能证明底色/Native 与网页的层次。
                    const auto capture=[&](const char* file){auto image=output;for(std::size_t i=0;i<image.size();i+=4)std::swap(image[i],image[i+2]);save_capture(fixture/file,image,reff::test::PreviewGpu::width,reff::test::PreviewGpu::height);};
                    if(background>100&&!std::filesystem::exists(fixture/"scale-transition.bmp"))capture("scale-transition.bmp");
                    if(now-scale_started>9000)capture("scale-stable.bmp");
                    scale_samples+=samples;scale_leaks+=leaks;scale_green+=green;scale_background+=background;
                    if(now-scale_started>=3500&&now-scale_started<7000){++css_composites;css_leaks+=leaks;css_green+=green;}if(now-scale_started>=8000)settled_green+=green;
                    if(leaks&&!std::filesystem::exists(fixture/"scale-leak.bmp")){for(std::size_t i=0;i<output.size();i+=4)std::swap(output[i],output[i+2]);save_capture(fixture/"scale-leak.bmp",output,reff::test::PreviewGpu::width,reff::test::PreviewGpu::height);}
                    if(now-scale_started>9000)result={{"passed",scale_leaks==0&&scale_composites>30&&scale_steps>15&&scale_samples>1000&&css_composites>15&&css_green>100&&settled_green>100&&scale_background>100},{"productionHost",true},{"selfTestEnabled",false},{"continuousScale",true},{"composites",scale_composites},{"viewportSteps",scale_steps},{"transparentSamples",scale_samples},{"gameLeaks",scale_leaks},{"nativeGreenSamples",scale_green},{"fallbackSamples",scale_background},{"cssComposites",css_composites},{"cssLeaks",css_leaks},{"cssGreen",css_green},{"settledGreen",settled_green}};
                }
            }
            if(!result.is_null()||WaitForSingleObject(process.get(),0)==WAIT_OBJECT_0)break;Sleep(8);
        }
        // 每个历史内部句柄应已被文档销毁撤销；直接询问真实 Lua 注册表，不能只看宿主计数。
        bool clean=true;
        for(const auto& handle:handles){const auto probe="return require('reff.sdk')._unsubscribe('"+handle+"')";
            if(luaL_dostring(state,probe.c_str()))throw std::runtime_error(lua_tostring(state,-1));
            if(lua_toboolean(state,-1))clean=false;lua_pop(state,1);}
        save_capture(fixture/"shell.bmp",pixels,width,height);
        channel.send({{"type","shutdown"}});const bool graceful=WaitForSingleObject(process.get(),8000)==WAIT_OBJECT_0;
        channel.stop();lua_close(state);
        Json report={{"browser",result},{"frames",frames},{"width",width},{"height",height},{"generation",generation},
            {"luaSubscriptionCleanup",clean},{"gracefulExit",graceful},{"requests",requests},{"gameValidated",false}};
        const bool no_replay=behavior||(reset_executed&&std::count_if(requests.begin(),requests.end(),[&](const auto& value){return value["id"]==invalidated_request;})==1);
        const bool no_proxy=behavior||std::none_of(requests.begin(),requests.end(),[](const auto& value){return value["method"]=="reff.settings.reset";});
        report["resetExecuted"]=reset_executed;report["noWriteReplay"]=no_replay;report["noParentPrivilegeBypass"]=no_proxy;
        // 正式静态页面无动画时 WGC 不会重复交付；以已确认槽位及真实 GPU 像素为准。核心/输入仍要求连续五帧。
        const bool output_valid = production ? frames > 0 && preview_gpu->draws > 0 : result.contains("layoutPhase") ? layout_captures > 0 && frames > 0 : frames >= 5;
        report["layoutCaptures"] = layout_captures;
        report["previewChecks"]=preview_checks;if(preview)report["previewDraws"]=preview_gpu->draws;
        report["passed"]=!result.is_null()&&result.value("passed",false)&&output_valid&&clean&&graceful&&no_replay&&no_proxy;
        report["behaviorMode"]=behavior;
        report["embeddedShell"]=true;
        report["hiddenInitializationPassed"]=prewarmed;
        if(behavior&&!production)report["passed"]=report["passed"].get<bool>()&&prewarmed;
        if(production){report["mode"]=scaling?"production-scale":"production-preview";report["behaviorMode"]=false;for(const auto* key:{"hiddenInitializationPassed","noWriteReplay","noParentPrivilegeBypass","resetExecuted"})report.erase(key);}
        if(production&&result.is_null())report["browser"]={{"passed",false},{"error","production preview never reached confirmed slot"},{"productionHost",true},{"selfTestEnabled",false},{"cornerRadius",6}};
        std::ofstream(fixture/"integration-summary.json")<<report.dump(2);
        std::cout<<report.dump(2)<<'\n';return report["passed"].get<bool>()?0:1;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
