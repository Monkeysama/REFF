#include <reframework/API.h>
#include <lua.hpp>
#include <Windows.h>
#include <filesystem>
#include <iostream>
#include <cstdarg>
#include <atomic>
#include <cstring>

namespace {
lua_State* state{};
REFLuaStateCreatedCb created{};
REFLuaStateDestroyedCb destroyed{};
REFOnPresentCb present{};
REFOnDeviceResetCb reset{};
REFOnMessageCb message{};
int lock_depth{};
UINT cursor_message{};
int cursor_messages_forwarded{};
std::atomic_int startup_failures{};

// 独立 message-only 测试窗口，不显示界面或向游戏注入输入。
LRESULT CALLBACK test_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == cursor_message) ++cursor_messages_forwarded;
    if (message == WM_APP + 1) return 42;
    return DefWindowProcW(window, message, wparam, lparam);
}

// 模拟已存在 Lua 主状态的 REFramework 注册行为，验证插件能补装桥接。
bool register_created(REFLuaStateCreatedCb callback) { created = callback; callback(state); return true; }
bool register_destroyed(REFLuaStateDestroyedCb callback) { destroyed = callback; return true; }
bool register_present(REFOnPresentCb callback) { present = callback; return true; }
bool register_reset(REFOnDeviceResetCb callback) { reset = callback; return true; }
bool register_message(REFOnMessageCb callback) { message = callback; return true; }
void lock_lua() { ++lock_depth; }
void unlock_lua() { --lock_depth; }
// 后台启动失败通过原插件日志回调同步到测试线程，不访问游戏窗口。
void log_message(const char* format, ...) {
    if (std::strcmp(format, "REFF: %s") != 0) return;
    va_list args; va_start(args, format);
    const char* detail = va_arg(args, const char*);
    if (detail && std::strcmp(detail, "host not installed") == 0) ++startup_failures;
    va_end(args);
}
bool is_drawing_ui() { return false; }

// 检查真实 DLL 装入 Lua 的函数，不把重复的测试实现当成被测对象。
void check_script(const char* code) {
    if (luaL_dostring(state, code)) throw std::runtime_error(lua_tostring(state, -1));
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    try {
        state = luaL_newstate(); luaL_openlibs(state);
        HMODULE plugin = LoadLibraryW(argv[1]); if (!plugin) throw std::runtime_error("DLL load failed");
        auto version = reinterpret_cast<REFPluginRequiredVersionFn>(GetProcAddress(plugin, "reframework_plugin_required_version"));
        auto initialize = reinterpret_cast<REFPluginInitializeFn>(GetProcAddress(plugin, "reframework_plugin_initialize"));
        if (!version || !initialize) throw std::runtime_error("plugin exports missing");
        REFrameworkPluginVersion required{}; version(&required);
        if (required.major != 1 || required.minor != 15 || std::string(required.game_name) != "MHWILDS") throw std::runtime_error("API requirement mismatch");
        REFrameworkPluginFunctions functions{};
        functions.on_lua_state_created = register_created; functions.on_lua_state_destroyed = register_destroyed;
        functions.on_present = register_present; functions.on_device_reset = register_reset; functions.on_message = register_message;
        functions.lock_lua = lock_lua; functions.unlock_lua = unlock_lua;
        functions.log_info = functions.log_warn = functions.log_error = log_message; functions.is_drawing_ui = is_drawing_ui;
        REFrameworkRendererData renderer{}; renderer.renderer_type = REFRAMEWORK_RENDERER_D3D12;
        REFrameworkPluginInitializeParam param{}; param.functions = &functions; param.renderer_data = &renderer; param.version = &required;
        if (!initialize(&param) || !created || !destroyed || !present || !reset || !message || lock_depth) throw std::runtime_error("callback initialization failed");
        check_script("assert(type(reff_native.poll)=='function'); reff_native.set_ready(true); assert(reff_native.poll()==nil); assert(not reff_native.is_visible())");
        // 验证自有消息与普通消息都沿 REFramework 的窗口过程继续转发。
        cursor_message = RegisterWindowMessageW(L"REFF.CursorSync.v1");
        WNDCLASSW window_class{}; window_class.lpfnWndProc = test_window_proc;
        window_class.hInstance = GetModuleHandleW(nullptr); window_class.lpszClassName = L"REFFCursorContractTest";
        if (!RegisterClassW(&window_class)) throw std::runtime_error("test window registration failed");
        HWND window = CreateWindowExW(0, window_class.lpszClassName, L"", 0, 0, 0, 0, 0,
            HWND_MESSAGE, nullptr, window_class.hInstance, nullptr);
        if (!window || !message(window, WM_NULL, 0, 0)) throw std::runtime_error("cursor window setup failed");
        SendMessageW(window, cursor_message, 0, 0);
        if (cursor_messages_forwarded != 1 || SendMessageW(window, WM_APP + 1, 0, 0) != 42)
            throw std::runtime_error("cursor message dispatch broke window chain");
        // 在无宿主安装的测试目录触发真实 DLL 启动失败，验证释放与下一次 F8 重试。
        const auto missing_host = std::filesystem::absolute(argv[1]).parent_path().parent_path() / L"reff/runtime/reff-host.exe";
        if (std::filesystem::exists(missing_host)) throw std::runtime_error("failure fixture unexpectedly contains a host");
        for (int attempt = 1; attempt <= 2; ++attempt) {
            message(window, WM_KEYDOWN, VK_F8, 0);
            const auto deadline = GetTickCount64() + 3000;
            while (startup_failures.load() < attempt && GetTickCount64() < deadline) Sleep(10);
            if (startup_failures.load() != attempt) throw std::runtime_error("F8 retry did not complete failure cleanup");
            check_script("assert(not reff_native.is_visible()); assert(not reff_native.is_ready()); assert(reff_native.poll()==nil)");
            if (!message(window, WM_KEYDOWN, 'A', 0) || !message(window, WM_LBUTTONDOWN, 0, 0))
                throw std::runtime_error("failed host still captures input");
        }
        DestroyWindow(window);
        destroyed(state); lua_close(state);
        // 连续重建真实 Lua 状态，覆盖桥接安装/销毁 20 次；不冒充游戏 Reset Scripts 验收。
        for (int iteration = 0; iteration < 20; ++iteration) {
            state = luaL_newstate(); luaL_openlibs(state); created(state);
            check_script("reff_native.set_ready(true); assert(reff_native.poll()==nil); reff_native.set_ready(false)");
            destroyed(state); lua_close(state);
        }
        if (lock_depth) throw std::runtime_error("unbalanced Lua lock");
        // 插件按 REFramework 回调生命周期固定到进程结束，测试不强制卸载它。
        std::cout << "Actual REFF.dll exports, Lua binding and reset checks passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
