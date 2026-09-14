#include "browser.hpp"
#include "include/cef_command_line.h"
#include "include/cef_sandbox_win.h"
#include <fstream>

// 官方 bootstrap 在初始化沙箱后调用此导出；宿主不关闭沙箱、不创建桌面浏览器窗口。
extern "C" __declspec(dllexport) int RunWinMain(HINSTANCE instance, LPWSTR, int,
                                                void* sandbox_info, cef_version_info_t*) {
    CefMainArgs args(instance);
    CefRefPtr<reff::BrowserApp> app = new reff::BrowserApp;
    int exit_code = CefExecuteProcess(args, app, sandbox_info);
    if (exit_code >= 0) return exit_code;
    auto command = CefCommandLine::CreateCommandLine(); command->InitFromString(GetCommandLineW());
    if (!command->HasSwitch("reff-session") || !command->HasSwitch("reff-assets") || !command->HasSwitch("reff-cache")) return 2;
    CefSettings settings;
    settings.windowless_rendering_enabled = true;
    settings.no_sandbox = false;
    settings.log_severity = LOGSEVERITY_WARNING;
    CefString(&settings.root_cache_path) = command->GetSwitchValue("reff-cache");
    CefString(&settings.log_file) = command->GetSwitchValue("reff-cache").ToWString() + L"\\cef.log";
    if (!CefInitialize(args, settings, app, sandbox_info)) return CefGetExitCode();
    CefRunMessageLoop();
    CefShutdown();
    return 0;
}
