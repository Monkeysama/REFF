#include "renderer.hpp"
#include <lua.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
using namespace reff;
Queue<Json> incoming;
Channel* transport{};
bool backend_ready{};
int update_ref = LUA_NOREF;

// 测试驱动只模拟 REFramework 的回调注册，加载真实核心与可选示例 Lua 脚本。
int on_update(lua_State* state) { lua_pushvalue(state, 2); update_ref = luaL_ref(state, LUA_REGISTRYINDEX); return 0; }
int on_reset(lua_State*) { return 0; }
int log_line(lua_State* state) { std::cout << lua_tostring(state, 1) << '\n'; return 0; }
int set_ready(lua_State* state) { backend_ready = lua_toboolean(state, 1) != 0; return 0; }
int is_ready(lua_State* state) { lua_pushboolean(state, backend_ready); return 1; }
int emit_event(lua_State* state) { (void)state; lua_pushboolean(state, true); return 1; }
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

// 把一份真实 CEF 离屏 BGRA 帧保存为 BMP，供人工查看；不截取用户桌面。
void save_bitmap(const std::filesystem::path& path, const std::vector<std::uint8_t>& pixels) {
    BITMAPFILEHEADER header{}; header.bfType = 0x4D42;
    header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER); header.bfSize = DWORD(header.bfOffBits + pixels.size());
    BITMAPINFOHEADER info{}; info.biSize = sizeof(info); info.biWidth = panel_width; info.biHeight = -panel_height;
    info.biPlanes = 1; info.biBitCount = 32; info.biCompression = BI_RGB;
    std::ofstream output(path, std::ios::binary); output.write(reinterpret_cast<char*>(&header), sizeof(header));
    output.write(reinterpret_cast<char*>(&info), sizeof(info)); output.write(reinterpret_cast<const char*>(pixels.data()), pixels.size());
}
}

// 真实 CEF + IPC + Lua + 隐藏 DX12 交换链自检；结果不代表已经通过 Wilds 游戏内测试。
int wmain(int argc, wchar_t** argv) {
    using namespace reff;
    if (argc < 2 || argc > 3 || (argc == 3 && std::wstring(argv[2]) != L"--stress")) { std::cerr << "Usage: reff-smoke.exe <repository-root> [--stress]\nNo checks were executed.\n"; return 2; }
    const bool stress = argc == 3;
    try {
        std::filesystem::path root = std::filesystem::absolute(argv[1]);
        auto runtime = root / L"staging/reframework/reff/runtime";
        auto assets = root / L"staging/reframework/reff/ui";
        auto session = make_session();
        // 每次 smoke 使用独立缓存，避免 Crashpad/Chromium 锁让后续回归误判为资源故障。
        auto cache = root / (L"artifacts/cef-cache-" + std::wstring(session.begin(), session.end()));
        std::filesystem::create_directories(cache);
        SharedFrame frame; frame.open(session, true);
        Channel channel; transport = &channel; Json result;
        Queue<Json> reports;
        Queue<Json> visibility_acks;
        Json mock_settings = {{"schemaVersion", 1}, {"language", "zh-CN"},
            {"appearance", {{"preset", "emerald"}, {"accent", "#67D3B2"}, {"background", "#10151E"}, {"surface", "#151D28"},
                {"backgroundOpacity", 0.94}, {"cornerRadius", 6}, {"surfaceBlur", 8}, {"textScale", 1.0}}},
            {"input", {{"mousePassthrough", false}, {"keyboardPassthrough", false}}}, {"window", {{"rememberGeometry", true}}}};
        channel.start(session, true, [&](Json message) {
            auto type = message.value("type", std::string{});
            if (type == "hello") { channel.send({{"type", "ready"}, {"ready", true}, {"epoch", 1}}); channel.send({{"type", "visible"}, {"value", true}}); }
            else if (type == "request" && message.value("method", std::string{}).starts_with("reff.settings.")) {
                const auto method = message.value("method", std::string{});
                if (method == "reff.settings.set") mock_settings.merge_patch(message.value("params", Json::object()));
                channel.send({{"type", "response"}, {"id", message.value("id", std::string{})}, {"epoch", 1}, {"result", mock_settings}});
            }
            else if (type == "request") incoming.push(std::move(message));
            else if (type == "test_result") reports.push(std::move(message));
            else if (type == "visibility_ack") visibility_acks.push(std::move(message));
        });
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

        // 测试使用不可见窗口和真实 DX12 设备，只验证提交成功，不干扰用户当前窗口。
        WNDCLASSW cls{}; cls.lpfnWndProc = DefWindowProcW; cls.hInstance = GetModuleHandleW(nullptr); cls.lpszClassName = L"REFFSmokeWindow";
        RegisterClassW(&cls); HWND window = CreateWindowW(cls.lpszClassName, L"REFF validation", WS_OVERLAPPEDWINDOW,
            0, 0, panel_width, panel_height, nullptr, nullptr, cls.hInstance, nullptr);
        Com<ID3D12Device> device;
        HRESULT dx_result = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
        Com<IDXGIFactory4> factory; Com<ID3D12CommandQueue> queue; Com<IDXGISwapChain1> swapchain;
        Renderer renderer; bool dx_ready = false;
        if (SUCCEEDED(dx_result)) {
            CreateDXGIFactory1(IID_PPV_ARGS(&factory)); D3D12_COMMAND_QUEUE_DESC queue_desc{};
            device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue));
            DXGI_SWAP_CHAIN_DESC1 desc{}; desc.Width = panel_width; desc.Height = panel_height; desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count = 1; desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.BufferCount = 2; desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            if (factory && queue && SUCCEEDED(factory->CreateSwapChainForHwnd(queue.Get(), window, &desc, nullptr, nullptr, &swapchain)))
                dx_ready = renderer.initialize(device.Get(), swapchain.Get(), queue.Get());
            if (!dx_ready) std::cerr << "DX12 initialization failed: " << renderer.last_error() << '\n';
        }
        auto executable = runtime / L"reff-host.exe";
        std::wstring command = L"\"" + executable.wstring() + L"\" --reff-session=" + session + L" --reff-assets=\"" + assets.wstring() +
            L"\" --reff-manifests=\"" + (root / L"staging/reframework/reff/plugins").wstring() +
            L"\" --reff-cache=\"" + cache.wstring() + L"\" --reff-game=MHWILDS --reff-self-test";
        if (stress) command += L" --reff-stress-test";
        Handle job(CreateJobObjectW(nullptr, nullptr)); JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits))) throw std::runtime_error("job setup failed");
        STARTUPINFOW startup{sizeof(startup)}; startup.dwFlags = STARTF_USESHOWWINDOW; startup.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION process_info{};
        if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr,
            runtime.c_str(), &startup, &process_info)) throw std::runtime_error("CEF host launch failed");
        Handle process(process_info.hProcess), thread(process_info.hThread);
        if (!AssignProcessToJobObject(job.get(), process.get())) { TerminateProcess(process.get(), 1); throw std::runtime_error("job assignment failed"); }
        ResumeThread(thread.get());
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(stress ? 90 : 40);
        std::vector<std::uint8_t> pixels; std::uint64_t sequence = 0; int rendered = 0;
        std::uint64_t report_tick = 0;
        while (std::chrono::steady_clock::now() < deadline) {
            MSG msg{}; while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
            lua_rawgeti(state, LUA_REGISTRYINDEX, update_ref);
            if (lua_pcall(state, 0, 0, 0)) throw std::runtime_error(lua_tostring(state, -1));
            frame.read(pixels, sequence);
            if (dx_ready && sequence) { if (!renderer.draw(frame, {panel_width / 2, panel_height / 2}, true, {panel_width, panel_height})) throw std::runtime_error("DX12 draw failed: " + renderer.last_error()); ++rendered; swapchain->Present(0, 0); }
            if (auto report = reports.pop()) { result = *report; report_tick = GetTickCount64(); }
            // DOM 就绪消息早于 OSR 合成；短暂继续泵消息，保存实际插件内容而不是 iframe 初始空白帧。
            if (!result.is_null() && sequence > 2 && GetTickCount64() - report_tick >= 300) break;
            if (WaitForSingleObject(process.get(), 0) == WAIT_OBJECT_0) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }
        int corner_alpha = -1, surface_alpha = -1;
        if (pixels.size() >= std::size_t(panel_width * panel_height * 4)) {
            corner_alpha = pixels[3];
            // 左侧导航始终由 Shell 绘制；右侧隔离插件可以合法使用不透明背景，不能用于验证 Shell 表面透明度。
            surface_alpha = pixels[(500 * panel_width + 100) * 4 + 3];
            if (corner_alpha > 32 || surface_alpha < 128 || surface_alpha >= 255)
                throw std::runtime_error("transparent rounded Shell pixels are invalid");
            save_bitmap(root / L"artifacts/cef-frame.bmp", pixels);
        }
        // 同一真实 CEF 会话完成 100 次关闭/打开；每条命令等待 UI 线程回执。
        // 这不向游戏注入输入，也不替代 F8、IME 和 Raw Input 的游戏验收。
        Json visibility_samples = Json::array();
        if (stress) {
            if (result.is_null() || !result["result"].value("passed", false) || result["result"].value("documentGeneration", 0) < 21)
                throw std::runtime_error("20 real document reloads did not complete");
            // 此固定示例具有 Shell 两个订阅和状态页一个订阅；重载探针不能残留。
            if (result["result"].value("subscriptions", 0) != 3 || result["result"].value("pendingRequests", -1) != 0 ||
                result["result"].value("subscriptionCleanup", -1) != 0)
                throw std::runtime_error("document reloads left stale subscription state");
            for (int step = 1; step <= 200; ++step) {
                channel.send({{"type", "visible"}, {"value", step % 2 == 0}, {"testSequence", step}});
                const auto ack_deadline = GetTickCount64() + 3000;
                bool acknowledged = false;
                while (GetTickCount64() < ack_deadline) {
                    lua_rawgeti(state, LUA_REGISTRYINDEX, update_ref);
                    if (lua_pcall(state, 0, 0, 0)) throw std::runtime_error(lua_tostring(state, -1));
                    if (auto ack = visibility_acks.pop()) {
                        if (ack->value("testSequence", 0) != step || ack->value("value", false) != (step % 2 == 0))
                            throw std::runtime_error("visibility acknowledgement mismatch");
                        visibility_samples.push_back(*ack); acknowledged = true; break;
                    }
                    Sleep(5);
                }
                if (!acknowledged) throw std::runtime_error("visibility acknowledgement timed out");
            }
            const auto& first = visibility_samples.front(); const auto& last = visibility_samples.back();
            if (last.at("subscriptions") != first.at("subscriptions") || last.at("pendingRequests") != 0 || last.at("subscriptionCleanup") != 0)
                throw std::runtime_error("visibility cycles accumulated subscriptions or requests");
        }
        channel.send({{"type", "shutdown"}});
        bool graceful_exit = WaitForSingleObject(process.get(), 8000) == WAIT_OBJECT_0;
        channel.stop(); renderer.reset(); lua_close(state); DestroyWindow(window);
        Json report{{"browserLuaRoundTrip", !result.is_null() && result["result"].value("passed", false)},
            {"frameSequence", sequence}, {"dx12Initialized", dx_ready}, {"dx12FramesSubmitted", rendered},
            {"cornerAlpha", corner_alpha}, {"surfaceAlpha", surface_alpha},
            {"gracefulHostExit", graceful_exit}, {"result", result}, {"gameTested", false}};
        report["visibilityCycles"] = visibility_samples.size() / 2;
        if (stress) report["visibilitySamples"] = visibility_samples;
        std::ofstream(root / (stress ? L"artifacts/stress-report.json" : L"artifacts/smoke-report.json")) << report.dump(2);
        report.erase("visibilitySamples");
        std::cout << report.dump(2) << '\n';
        return report["browserLuaRoundTrip"].get<bool>() && sequence && dx_ready && rendered && graceful_exit ? 0 : 1;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
