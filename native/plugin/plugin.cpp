#include <reframework/API.h>
#include <lua.hpp>
#include "renderer.hpp"
#include "cursor.hpp"
#include "ime_proxy.hpp"
#include "panel_resize.hpp"
#include "host_health.hpp"
#include "mouse_click.hpp"
#include "settings.hpp"
#include "keyboard_capture.hpp"
#include "game_profile.hpp"
// 预览构建标识：确保发布候选包重新生成 Core DLL，便于核对构建时间与版本。
#include <windowsx.h>
#include <commctrl.h>
#include <imm.h>
#include <atomic>
#include <filesystem>
#include <memory>
#include <unordered_set>
#include <fstream>
#include <sstream>
#include <optional>
#include <tuple>
#include <vector>
#include <algorithm>
#include <cmath>

namespace {
using namespace reff;
const REFrameworkPluginInitializeParam* api{};
using SetCursorPosFunction = BOOL(WINAPI*)(int, int);
#ifndef REFF_ENABLE_CURSOR_IAT_HOOK
// 进程级 IAT 修改默认关闭；只有完成单独稳定性验证后才允许启用。
#define REFF_ENABLE_CURSOR_IAT_HOOK 0
#endif
#ifndef REFF_ENABLE_CURSOR_DIAGNOSTICS
#define REFF_ENABLE_CURSOR_DIAGNOSTICS 0
#endif
#ifndef REFF_ENABLE_IME_PROXY
#define REFF_ENABLE_IME_PROXY 0
#endif
#ifndef REFF_ENABLE_PERF_DIAGNOSTICS
#define REFF_ENABLE_PERF_DIAGNOSTICS 0
#endif
#ifndef REFF_ENABLE_RESIZE_DIAGNOSTICS
#define REFF_ENABLE_RESIZE_DIAGNOSTICS 0
#endif
#ifndef REFF_ENABLE_EXPERIMENTAL_GAMES
#define REFF_ENABLE_EXPERIMENTAL_GAMES 0
#endif
#ifndef REFF_PROJECT_VERSION
#define REFF_PROJECT_VERSION "unknown"
#endif
SetCursorPosFunction original_set_cursor_pos{};
void** set_cursor_pos_iat{};
std::atomic_bool iat_installed{};

// 常驻运行时由 REFramework 持有回调，DLL 固定到进程结束，避免卸载后执行悬空回调。
struct Runtime {
    Channel channel;
    Queue<Json> requests;
    // Present 线程只投递低频性能摘要，后台服务线程独占文件写入。
    Queue<std::string> perf_logs;
    std::atomic<std::shared_ptr<SharedFrame>> frame;
    std::atomic_bool visible{}, ready{}, connected{}, start_requested{};
    // requested_visible 保留用户的打开意图；focus_suspended 只表示 Alt-Tab/失焦期间暂停输入，面板仍继续绘制。
    std::atomic_bool requested_visible{}, focus_suspended{};
    std::atomic_uint64_t epoch{1};
    std::atomic<lua_State*> owner{};
    std::jthread service;
    Renderer renderer;
    std::shared_ptr<SharedFrame> rendered_frame;
    std::mutex renderer_mutex;
    std::filesystem::path root;
    // 当前游戏由 REFramework 已识别的 target_name 决定；复制后跨线程只读，不依赖上游指针寿命。
    GameProfile game;
    std::wstring session;
    std::unordered_set<std::string> seen;
    std::mutex request_mutex;
    std::atomic_int left{}, top{}, right{}, bottom{};
    std::atomic_int panel_display_width{panel_width}, panel_display_height{panel_height};
    std::atomic_int viewport_width{panel_width}, viewport_height{panel_height};
    // viewport_generation 与 CEF 宿主和共享帧同步；窗口线程递增，Present 线程只读取。
    std::atomic_uint64_t viewport_generation{1};
    std::uint64_t last_viewport_commit_tick{};
    // 面板位置由窗口线程拖动、Present 线程裁剪；只保存客户区坐标，关闭后在本次进程内保留。
    std::atomic_int panel_origin_left{-1}, panel_origin_top{-1};
    std::atomic_bool panel_position_initialized{};
    std::atomic<HWND> game_window{};
    std::atomic_bool cursor_sync_pending{}, cursor_owned{};
    // 虚拟光标以游戏客户区为坐标空间，只在游戏窗口线程更新；网页命中坐标按当前面板矩形换算。
    CursorInput cursor_input;
    // 窗口线程修改输入状态，Present 只尝试读取快照；不能让 GPU 线程改写非原子的光标数据。
    std::mutex pointer_mutex;
    // Present 线程在输入锁竞争时沿用最近一次快照，不能因鼠标消息占锁而跳过整次面板合成。
    std::pair<int, int> rendered_cursor_position{720, 450};
    // 仅在指针位于 CEF 面板矩形中时向网页发送鼠标事件；离开时单次通知 CEF 清除 hover 状态。
    bool pointer_in_panel{}, pending_virtual_cursor_dispatch{};
    int last_panel_x{-1}, last_panel_y{-1};
    std::uint64_t last_cef_move_tick{};
    // 标题栏拖动状态只由游戏窗口线程访问；Raw Input 与 WM_MOUSEMOVE 共用同一套锚点。
    std::atomic_bool panel_dragging{};
    // 面板几何的写入来自窗口线程，读取来自 Present 线程；用同一把锁提交完整矩形，避免单轴调整读到中间状态。
    std::mutex panel_geometry_mutex;
    // W2 尺寸调整状态；输入线程写入请求，Present 线程负责裁剪并提交显示矩形。
    std::atomic_int panel_resize_edges{};
    std::atomic_int resize_cursor_mode{};
    // 缩放控制器独占一次缩放会话的起点和输入模式；旧原子字段仅作为跨线程兼容快照。
    PanelResizeController resize_controller;
    int panel_drag_anchor_x{}, panel_drag_anchor_y{};
    int panel_drag_start_left{}, panel_drag_start_top{};
    int panel_drag_start_right{}, panel_drag_start_bottom{};
    // 窗口消息处理可能因系统重入；禁止同一线程再次进入 REFF 输入分发。
    std::atomic_bool message_dispatching{};
    // Win32 会把快速第二次按下编码为 DBLCLK；跟踪器保留到对应抬起，仅由窗口线程访问。
    MouseClickTracker mouse_click_tracker;
    UINT cursor_sync_message{};
    CursorVisibility cursor;
    HCURSOR previous_cursor{}, arrow_cursor{};
    std::atomic_bool cursor_blocked{};
    // 输入穿透开关由设置 IPC 更新，窗口线程只读取原子快照；文本输入期间键盘始终由 REFF 独占。
    std::atomic_bool mouse_passthrough{}, keyboard_passthrough{}, web_input_active{};
    // 部分游戏使用 RIDEV_NOLEGACY，透明 EDIT 因而收不到 WM_KEY*；运行时按能力检测后转译 Raw Keyboard。
    std::atomic_bool raw_keyboard_no_legacy{};
    ULONGLONG raw_keyboard_probe_tick{};
    // 快捷键绑定由设置线程更新，窗口消息线程只读取原子快照；capture_active 用于等待用户按键时屏蔽旧绑定。
    std::atomic_int hotkey_key{VK_F8}, hotkey_modifiers{};
    std::atomic_bool hotkey_capture{};
    // 延迟预热只控制每局游戏的一次后台宿主启动；预热期间不改变面板显隐与输入状态。
    std::atomic_bool preload_enabled{true};
    // 配置文件归 Core 所有；几何更新在窗口线程写内存，后台服务线程负责实际磁盘 I/O。
    SettingsStore settings;
    std::atomic_bool settings_save_requested{};
    KeyboardCapture keyboard_capture;
    std::atomic_bool viewport_reset_requested{};
    // 兼容诊断节点可能来自窗口、IPC 和 Present 线程；原子标志保证每类环境摘要只记录一次。
    std::atomic_bool compatibility_window_logged{}, compatibility_renderer_logged{};
    // 性能日志窗口只由 Present 线程访问；避免每帧写日志，仅按固定间隔输出聚合计数。
    std::uint64_t perf_log_tick{};
    std::uint64_t perf_draw_attempts{};
    std::uint64_t perf_draw_failures{};
    std::uint64_t perf_draw_cpu_us{};
    std::uint64_t perf_draw_cpu_max_us{};
    std::uint64_t perf_present_last_us{};
    std::uint64_t perf_present_window_started_us{};
    std::vector<std::uint32_t> perf_present_intervals_us;
    // 原生 EDIT 代理仅在网页输入框获得焦点时激活；窗口和 IME 上下文归代理线程所有。
#if REFF_ENABLE_IME_PROXY
    ImeProxy ime;
#endif
};
Runtime* runtime{};
std::mutex resize_log_mutex;
std::uint64_t resize_log_tick{};
std::mutex compatibility_log_mutex;
#if defined(REFF_ENABLE_IME_DIAGNOSTICS)
std::mutex ime_log_mutex;
#endif

// REFF 原生诊断统一写入 data/REFF/log，避免在 reframework 根目录创建自有日志目录。
std::filesystem::path reff_log_dir(const std::filesystem::path& root) {
    return root / L"data" / L"REFF" / L"log";
}

// 兼容日志记录低频能力决策，不包含用户按键、文本或游戏对象数据，可由外部测试者直接提供。
void compatibility_log(const std::string& message) {
    std::lock_guard lock(compatibility_log_mutex);
    if (!runtime || runtime->root.empty()) return;
    try {
        const auto dir = reff_log_dir(runtime->root);
        std::filesystem::create_directories(dir);
        std::ofstream file(dir / L"compatibility.log", std::ios::app);
        file << GetTickCount64() << " " << message << "\n";
    } catch (...) { /* 诊断失败不能改变游戏初始化或输入行为。 */ }
}

// 每次游戏进程初始化都清空上一轮兼容日志，确保目标、构建和生命周期证据属于同一次运行。
void reset_compatibility_log() {
    if (!runtime || runtime->root.empty()) return;
    try {
        const auto dir = reff_log_dir(runtime->root);
        std::filesystem::create_directories(dir);
        std::ofstream file(dir / L"compatibility.log", std::ios::trunc);
        file << "# REFF compatibility diagnostics v1\n";
    } catch (...) {}
}

// 游戏侧 IME 诊断单独写入 reff_ime.log，避免污染 REFramework 主日志和缩放日志。
void ime_log(const std::string& message) {
#if defined(REFF_ENABLE_IME_DIAGNOSTICS)
    std::lock_guard lock(ime_log_mutex);
    if (!runtime || runtime->root.empty()) return;
    try {
        const auto dir = reff_log_dir(runtime->root);
        std::filesystem::create_directories(dir);
        std::ofstream file(dir / L"reff_ime.log", std::ios::app);
        file << GetTickCount64() << " " << message << "\n";
    } catch (...) {}
#else
    (void)message;
#endif
}

// 每局游戏从空白 IME 诊断开始，避免跨进程焦点序列混在一起；只在显式诊断构建写文件。
void reset_ime_log() {
#if defined(REFF_ENABLE_IME_DIAGNOSTICS)
    if (!runtime || runtime->root.empty()) return;
    try {
        const auto dir = reff_log_dir(runtime->root);
        std::filesystem::create_directories(dir);
        std::ofstream file(dir / L"reff_ime.log", std::ios::trunc);
        file << "# REFF game-side IME diagnostics\n";
    } catch (...) {}
#endif
}

// REFF 专用缩放诊断日志；独立写入 data/REFF/log，不占用 REFramework 主日志。
void resize_log(const std::string& message, bool sampled = false) {
#if REFF_ENABLE_RESIZE_DIAGNOSTICS
    if (!runtime || runtime->root.empty()) return;
    const auto now = GetTickCount64();
    if (sampled && resize_log_tick && now - resize_log_tick < 500) return;
    if (sampled) resize_log_tick = now;
    std::lock_guard lock(resize_log_mutex);
    try {
        const auto dir = reff_log_dir(runtime->root);
        std::filesystem::create_directories(dir);
        std::ofstream file(dir / L"reff_resize.log", std::ios::app);
        file << now << " " << message << "\n";
    } catch (...) { /* 诊断日志失败不能影响游戏线程。 */ }
#else
    (void)message; (void)sampled;
#endif
}

// Present 线程只把性能摘要压入有界队列，不能在游戏渲染回调里打开文件。
void perf_log(const std::string& message) {
#if REFF_ENABLE_PERF_DIAGNOSTICS
    if (runtime) runtime->perf_logs.push(message);
#else
    (void)message;
#endif
}

// 后台服务线程批量刷新性能日志；资源所有权不进入 Present 或窗口消息线程。
void flush_perf_logs() {
#if REFF_ENABLE_PERF_DIAGNOSTICS
    if (!runtime || runtime->root.empty()) return;
    std::vector<std::string> messages;
    for (int i = 0; i < 16; ++i) { auto message = runtime->perf_logs.pop(); if (!message) break; messages.push_back(std::move(*message)); }
    if (messages.empty()) return;
    try {
        const auto dir = reff_log_dir(runtime->root);
        std::filesystem::create_directories(dir);
        std::ofstream file(dir / L"reff_perf.log", std::ios::app);
        const auto tick = GetTickCount64();
        for (const auto& message : messages) file << tick << " " << message << "\n";
    } catch (...) { /* 诊断失败不能改变宿主或游戏生命周期。 */ }
#endif
}

// 每次 REFF 初始化都清空本次游戏会话的性能日志，避免把旧构建数据混入当前报告。
void reset_perf_log() {
#if REFF_ENABLE_PERF_DIAGNOSTICS
    if (!runtime || runtime->root.empty()) return;
    runtime->perf_logs.clear();
    try {
        const auto dir = reff_log_dir(runtime->root);
        std::filesystem::create_directories(dir);
        std::ofstream file(dir / L"reff_perf.log", std::ios::trunc);
        file << "# REFF performance diagnostics; 10s aggregate windows\n";
    } catch (...) {}
#endif
}
constexpr UINT_PTR cursor_subclass_id = 0x52454646;

// 面板可见期间拦截游戏 EXE 通过 IAT 调用的 SetCursorPos，保持返回成功但不改变光标位置。
// 这针对游玩状态的鼠标回中心逻辑；动态 GetProcAddress 调用不经过 IAT，仍需游戏内验证。
BOOL WINAPI blocked_set_cursor_pos(int x, int y) {
    (void)x; (void)y;
#if REFF_ENABLE_CURSOR_IAT_HOOK
    if (runtime && runtime->cursor_blocked.load(std::memory_order_acquire)) return TRUE;
#endif
    return original_set_cursor_pos ? original_set_cursor_pos(x, y) : TRUE;
}

// 修改 IAT 槽位时只短暂改变页面保护，并刷新指令缓存，不触碰 USER32.dll 代码页。
bool write_iat_value(void** slot, void* value) {
    if (!slot) return false;
    DWORD old_protection{};
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old_protection)) return false;
    std::memcpy(slot, &value, sizeof(value)); FlushInstructionCache(GetCurrentProcess(), slot, sizeof(value));
    DWORD ignored{}; VirtualProtect(slot, sizeof(void*), old_protection, &ignored); return true;
}

// 在游戏主 EXE 的 USER32 导入表中找到 SetCursorPos，覆盖游戏自身导入地址。
// 在游戏主 EXE 的 USER32 导入表按函数名定位 IAT 槽位；仅供显式实验和诊断构建使用。
void** find_user32_iat(const char* target) {
    auto module = GetModuleHandleW(nullptr); if (!module) return nullptr;
    auto base = reinterpret_cast<BYTE*>(module);
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;
    auto directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!directory.VirtualAddress) return nullptr;
    auto descriptor = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress);
    for (; descriptor->Name; ++descriptor) {
        const char* module_name = reinterpret_cast<const char*>(base + descriptor->Name);
        if (_stricmp(module_name, "USER32.dll") != 0 || !descriptor->FirstThunk) continue;
        auto* first = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + descriptor->FirstThunk);
        auto* original = descriptor->OriginalFirstThunk ? reinterpret_cast<IMAGE_THUNK_DATA64*>(base + descriptor->OriginalFirstThunk) : first;
        for (; original->u1.AddressOfData; ++original, ++first) {
            if (IMAGE_SNAP_BY_ORDINAL64(original->u1.Ordinal)) continue;
            auto* name = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + original->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char*>(name->Name), target) == 0)
                return reinterpret_cast<void**>(&first->u1.Function);
        }
    }
    return nullptr;
}

// 初始化一次 IAT 钩子；插件固定到进程结束，因此槽位在 REFramework 生命周期内保持有效。
bool install_set_cursor_pos_hook() {
    if (iat_installed.load(std::memory_order_acquire)) return true;
    set_cursor_pos_iat = find_user32_iat("SetCursorPos"); if (!set_cursor_pos_iat) return false;
    void* current = *set_cursor_pos_iat;
    if (current == reinterpret_cast<void*>(blocked_set_cursor_pos)) { iat_installed = true; return true; }
    if (!current) return false;
    original_set_cursor_pos = reinterpret_cast<SetCursorPosFunction>(current);
    if (!write_iat_value(set_cursor_pos_iat, reinterpret_cast<void*>(blocked_set_cursor_pos))) {
        set_cursor_pos_iat = nullptr; original_set_cursor_pos = nullptr; return false;
    }
    iat_installed = true; return true;
}


// 跨线程只投递自有窗口消息；ShowCursor 的计数修改必须留在窗口所属线程。
void request_cursor_sync() {
    HWND window = runtime->game_window.load();
    if (!window || !runtime->cursor_sync_message || runtime->cursor_sync_pending.exchange(true)) return;
    if (!PostMessageW(window, runtime->cursor_sync_message, 0, 0)) runtime->cursor_sync_pending = false;
}

// 窗口线程切换系统光标租约；REFF 打开时隐藏实体光标，DX12 合成器绘制虚拟光标。
void sync_cursor(HWND window) {
    if (GetCurrentThreadId() != GetWindowThreadProcessId(window, nullptr)) return;
    bool foreground = GetForegroundWindow() == window;
#if REFF_ENABLE_IME_PROXY
    foreground = foreground || runtime->ime.owns_foreground();
    // 中文候选窗口可能临时成为前台窗口；IME 会话激活期间保持 REFF 面板渲染。
    foreground = foreground || runtime->ime.active();
#endif
    // REFramework 菜单状态不再参与系统光标租约判断；菜单短暂绘制时不能因一次鼠标移动把 REFF 会话隐藏。
    bool visible = runtime->visible && runtime->connected && runtime->channel.connected() && foreground;
    // 归还旧的显示租约，再按当前状态建立隐藏租约，避免 ShowCursor 计数方向交叉。
    runtime->cursor.synchronize(false, [](bool show) { return ShowCursor(show ? TRUE : FALSE); });
    runtime->cursor.synchronize_hidden(visible, [](bool show) { return ShowCursor(show ? TRUE : FALSE); });
    runtime->cursor_owned = runtime->cursor.hidden();
}

// 用游戏客户区大小更新虚拟光标边界。窗口化、分辨率或显示模式切换时保留现有位置并裁剪。
void synchronize_virtual_cursor_bounds(HWND window) {
    RECT client{};
    if (window && GetClientRect(window, &client))
        runtime->cursor_input.cursor.set_bounds(client.right - client.left, client.bottom - client.top);
}

// F8 打开时读取实体鼠标位置作为虚拟光标起点；游玩状态由 Raw Input 后续维护，不依赖会被游戏重置的实体坐标。
void synchronize_virtual_cursor_position(HWND window) {
    synchronize_virtual_cursor_bounds(window);
    POINT cursor{};
    if (window && GetCursorPos(&cursor) && ScreenToClient(window, &cursor))
        runtime->cursor_input.begin(runtime->cursor_input.cursor.bounds().first, runtime->cursor_input.cursor.bounds().second, cursor.x, cursor.y);
    else
        runtime->cursor_input.begin(runtime->cursor_input.cursor.bounds().first, runtime->cursor_input.cursor.bounds().second,
            runtime->cursor_input.cursor.bounds().first / 2, runtime->cursor_input.cursor.bounds().second / 2);
    runtime->pointer_in_panel = false;
    runtime->last_panel_x = runtime->last_panel_y = -1;
    runtime->last_cef_move_tick = 0;
    runtime->pending_virtual_cursor_dispatch = true;
}

// 把游戏客户区位置换算为固定 CEF 逻辑坐标。面板外移动不交给网页命中测试，但仍绘制原生虚拟光标。
bool map_virtual_cursor_to_panel(int x, int y, int& panel_x, int& panel_y) {
    const int left = runtime->left.load();
    const int top = runtime->top.load();
    const int right = runtime->right.load();
    const int bottom = runtime->bottom.load();
    if (right <= left || bottom <= top || x < left || x >= right || y < top || y >= bottom) return false;
    const int content_width = std::max(1, runtime->viewport_width.load()), content_height = std::max(1, runtime->viewport_height.load());
    panel_x = std::clamp((x - left) * content_width / (right - left), 0, content_width - 1);
    panel_y = std::clamp((y - top) * content_height / (bottom - top), 0, content_height - 1);
    return true;
}

// 仅在进入、离开或面板内移动时发送 CEF 鼠标事件。CEF 不再绘制指针，原生渲染器负责全客户区视觉光标。
void dispatch_virtual_cursor_move(int modifiers) {
    if (!runtime->visible || !runtime->connected || !runtime->channel.connected()) return;
    const auto [x, y] = runtime->cursor_input.cursor.position();
    int panel_x{}, panel_y{};
    const bool inside = map_virtual_cursor_to_panel(x, y, panel_x, panel_y);
    if (inside) {
        const bool entered = !runtime->pointer_in_panel;
        const auto now = GetTickCount64();
        // CEF 页面每次移动都可能触发整页 OnPaint；8ms 节流保留交互响应，同时避免高频 Raw Input 造成提交拥塞。
        const bool due = now - runtime->last_cef_move_tick >= 8;
        if (entered || (due && (panel_x != runtime->last_panel_x || panel_y != runtime->last_panel_y))) {
            runtime->channel.send({{"type", "mouse"}, {"action", "move"}, {"x", panel_x}, {"y", panel_y}, {"button", 0}, {"delta", 0}, {"modifiers", modifiers}});
            runtime->last_panel_x = panel_x; runtime->last_panel_y = panel_y;
            runtime->last_cef_move_tick = now;
        }
        runtime->pointer_in_panel = true;
    } else if (runtime->pointer_in_panel) {
        runtime->channel.send({{"type", "mouse"}, {"action", "move"}, {"x", 0}, {"y", 0}, {"button", 0}, {"delta", 0}, {"modifiers", modifiers}, {"leave", true}});
        runtime->pointer_in_panel = false;
        runtime->last_panel_x = runtime->last_panel_y = -1;
        runtime->last_cef_move_tick = 0;
    }
}

// 标题栏按下后记录指针与面板左上角的相对锚点；拖动不修改 CEF 页面尺寸。
bool begin_panel_drag(int x, int y) {
    std::lock_guard geometry_lock(runtime->panel_geometry_mutex);
    const int left = runtime->left.load(), top = runtime->top.load();
    const int right = runtime->right.load(), bottom = runtime->bottom.load();
    const int titlebar_height = std::max(24, (bottom - top) * 56 / std::max(1, runtime->viewport_height.load()));
    if (right <= left || bottom <= top || x < left || x >= right || y < top || y >= top + titlebar_height) return false;
    runtime->panel_dragging = true;
    // 命中测试已使用当前已绘制矩形；从此刻起位置由拖动状态拥有，避免首帧初始化覆盖拖动坐标。
    runtime->panel_position_initialized.store(true, std::memory_order_release);
    runtime->panel_drag_anchor_x = x; runtime->panel_drag_anchor_y = y;
    runtime->panel_drag_start_left = left; runtime->panel_drag_start_top = top;
    return true;
}

// W2 边缘/角落命中：记录起始矩形，后续调整仍使用 Raw Input 虚拟光标坐标。
bool begin_panel_resize(int x, int y) {
    std::lock_guard geometry_lock(runtime->panel_geometry_mutex);
    const int l=runtime->left.load(), t=runtime->top.load(), r=runtime->right.load(), b=runtime->bottom.load();
    constexpr int hit=8;
    if (x<l-hit || x>r+hit || y<t-hit || y>b+hit) return false;
    const int e=(x<=l+hit?1:0)|(x>=r-hit?2:0)|(y<=t+hit?4:0)|(y>=b-hit?8:0);
    if (!e || ((e&3)==3) || ((e&12)==12)) return false;
    const auto mode = runtime->cursor_input.relative() ? PanelResizeController::InputMode::relative : PanelResizeController::InputMode::absolute;
    if (!runtime->resize_controller.begin({l, t, r, b}, e, x, y, mode)) return false;
    runtime->panel_resize_edges=e; runtime->panel_dragging=true;
    runtime->panel_drag_anchor_x=x; runtime->panel_drag_anchor_y=y;
    runtime->panel_drag_start_left=l; runtime->panel_drag_start_top=t;
    runtime->panel_drag_start_right=r; runtime->panel_drag_start_bottom=b;
    runtime->panel_position_initialized.store(true, std::memory_order_release);
    resize_log("begin edges=" + std::to_string(e) + " anchor=" + std::to_string(x) + "," + std::to_string(y) +
               " mode=" + std::to_string(static_cast<int>(mode)));
    return true;
}

// 根据光标与面板边缘的距离返回调整方向，仅用于光标视觉提示。
int resize_cursor_mode_at(int x, int y) {
    const int l=runtime->left.load(), t=runtime->top.load(), r=runtime->right.load(), b=runtime->bottom.load(); constexpr int hit=8;
    const bool nl=std::abs(x-l)<=hit, nr=std::abs(x-r)<=hit, nt=std::abs(y-t)<=hit, nb=std::abs(y-b)<=hit;
    if ((nl||nr)&&(nt||nb)) return ((nl&&nt)||(nr&&nb)) ? 3 : 4;
    if (nl||nr) return 1; if (nt||nb) return 2; return 0;
}

// 声明实时 viewport 提交函数；调整输入线程与释放消息都会调用。
void commit_viewport_resize(bool force = false);

// W2 根据命中边更新显示矩形，限制最小尺寸；每次调整都会触发受控的 CEF 实时重排。
void update_panel_resize(int x, int y, PanelResizeController::InputMode mode) {
    RECT next{};
    if (!runtime->resize_controller.update(x, y, mode, next)) return;
    {
        std::lock_guard geometry_lock(runtime->panel_geometry_mutex);
        runtime->panel_origin_left=next.left; runtime->panel_origin_top=next.top;
        runtime->panel_display_width=next.right-next.left; runtime->panel_display_height=next.bottom-next.top;
        runtime->right=next.right; runtime->bottom=next.bottom;
    }
    resize_log("input mode=" + std::to_string(static_cast<int>(mode)) + " x=" + std::to_string(x) + " y=" + std::to_string(y) +
               " rect=" + std::to_string(next.left) + "," + std::to_string(next.top) + "," + std::to_string(next.right) + "," + std::to_string(next.bottom), true);
    commit_viewport_resize(); request_cursor_sync();
}

// 缩放指针的唯一归一化入口；输入消息只提供坐标，几何状态只在此处更新。
void submit_resize_pointer(int x, int y, PanelResizeController::InputMode mode) {
    update_panel_resize(x, y, mode);
}

// 根据当前虚拟光标更新面板位置；Present 下一帧会再次裁剪到游戏客户区边界。
void update_panel_drag(int x, int y) {
    if (!runtime->panel_dragging) return;
    std::lock_guard geometry_lock(runtime->panel_geometry_mutex);
    runtime->panel_origin_left = runtime->panel_drag_start_left + x - runtime->panel_drag_anchor_x;
    runtime->panel_origin_top = runtime->panel_drag_start_top + y - runtime->panel_drag_anchor_y;
    request_cursor_sync();
}

// 将最新面板尺寸发送给 CEF；拖动时按 16ms 合并，释放时 force 确保最后一个尺寸立即提交。
void commit_viewport_resize(bool force) {
    const int width = std::clamp(runtime->panel_display_width.load(), 320, frame_max_width);
    const int height = std::clamp(runtime->panel_display_height.load(), 240, frame_max_height);
    if (width == runtime->viewport_width.load() && height == runtime->viewport_height.load()) return;
    const auto now = GetTickCount64();
    if (!force && runtime->last_viewport_commit_tick && now - runtime->last_viewport_commit_tick < 16) return;
    const auto generation = runtime->viewport_generation.load(std::memory_order_relaxed) + 1;
    if (!runtime->channel.send_priority_latest({{"type", "viewport"}, {"width", width}, {"height", height}, {"generation", generation}})) return;
    resize_log("viewport_send generation=" + std::to_string(generation) + " size=" + std::to_string(width) + "x" + std::to_string(height));
    runtime->viewport_width = width; runtime->viewport_height = height; runtime->viewport_generation = generation;
    runtime->last_viewport_commit_tick = now;
}

void end_panel_drag() {
    const bool was_dragging = runtime->panel_dragging.load();
    const bool was_resize = runtime->panel_resize_edges.load() != 0;
    runtime->resize_controller.end();
    runtime->panel_resize_edges=0; runtime->panel_dragging = false; runtime->resize_cursor_mode=0;
    if (was_resize) resize_log("end");
    if (was_resize && runtime->connected.load()) commit_viewport_resize(true);
    if (was_dragging && runtime->settings.remember_geometry()) {
        RECT client{};
        const auto window = runtime->game_window.load();
        if (window && GetClientRect(window, &client) && client.right > 0 && client.bottom > 0) {
            std::lock_guard geometry_lock(runtime->panel_geometry_mutex);
            runtime->settings.set_geometry({client.right, client.bottom, runtime->panel_origin_left.load(), runtime->panel_origin_top.load(),
                runtime->panel_display_width.load(), runtime->panel_display_height.load()});
            runtime->settings_save_requested = true;
        }
    }
}

// 同时发布给 DirectInput 过滤层；隐藏、显式穿透时放行，文本输入始终保护游戏状态。
void refresh_keyboard_capture() {
    runtime->keyboard_capture.set_blocked(should_block_keyboard(runtime->visible.load(),
        runtime->keyboard_passthrough.load(), runtime->web_input_active.load()));
}

// 显隐操作只修改原子状态，启动宿主和磁盘工作由后台服务执行。
void set_visible(bool visible) {
    runtime->requested_visible = visible;
    runtime->visible = visible;
    runtime->focus_suspended = false;
    runtime->cursor_blocked = visible;
    if (!visible) end_panel_drag();
#if REFF_ENABLE_IME_PROXY
    if (!visible) runtime->ime.deactivate();
#endif
    if (!visible) runtime->web_input_active = false;
    refresh_keyboard_capture();
    if (visible) runtime->start_requested = true;
    if (runtime->connected) {
        runtime->channel.send({{"type", "visible"}, {"value", visible}});
    }
    request_cursor_sync();
}

// 游戏失焦时只释放输入与系统光标；DX12 面板继续合成，避免 Alt-Tab 后从游戏窗口消失。
void suspend_for_focus() {
    if (!runtime->visible || runtime->focus_suspended.exchange(true)) return;
    ime_log("suspend_for_focus");
    end_panel_drag();
    runtime->cursor_blocked = false;
#if REFF_ENABLE_IME_PROXY
    runtime->ime.deactivate();
#endif
    runtime->web_input_active = false;
    refresh_keyboard_capture();
    if (runtime->connected) runtime->channel.send({{"type", "focus"}, {"value", false}});
    request_cursor_sync();
}

// 游戏重新获得焦点时恢复输入捕获；页面一直可见，因此不触发 CEF 隐藏或重新布局。
void restore_after_focus(HWND window) {
    if (!runtime->focus_suspended || !runtime->requested_visible || !runtime->connected || !runtime->ready) return;
    if (GetForegroundWindow() != window) return;
    runtime->focus_suspended = false;
    runtime->cursor_blocked = true;
    refresh_keyboard_capture();
    runtime->channel.send({{"type", "focus"}, {"value", true}});
    request_cursor_sync();
}

// 自有光标消息在窗口子类中处理，避免被 REFramework 菜单的输入拦截提前吞掉。
// 普通输入继续走既有窗口过程；此处只负责光标和失焦/销毁时的归还。
LRESULT CALLBACK cursor_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
                                    UINT_PTR subclass_id, DWORD_PTR) {
    if (message == runtime->cursor_sync_message) {
        runtime->cursor_sync_pending = false;
        sync_cursor(window);
        return 0;
    }
    if (message == WM_KILLFOCUS || message == WM_NCDESTROY) {
        end_panel_drag();
        if (message == WM_KILLFOCUS) suspend_for_focus();
        else set_visible(false);
        sync_cursor(window);
        if (message == WM_NCDESTROY) {
            RemoveWindowSubclass(window, cursor_window_proc, subclass_id);
            runtime->game_window = nullptr;
            runtime->cursor_sync_pending = false;
        }
    }
    if (message == WM_SETCURSOR && runtime->visible && runtime->connected) {
        sync_cursor(window);
        if (runtime->cursor.active()) return TRUE;
    }
    return DefSubclassProc(window, message, wparam, lparam);
}

int current_hotkey_modifiers();

// 首次窗口消息抵达时在所属线程安装子类，不从后台线程跨线程修改窗口过程。
bool ensure_cursor_window(HWND window) {
    if (runtime->game_window == window) return true;
    if (!window || GetCurrentThreadId() != GetWindowThreadProcessId(window, nullptr)) return false;
    if (runtime->game_window) return false;
    // 不再安装 SetWindowSubclass：REFramework 自身已经拥有窗口过程钩子，
    // 叠加子类会改变消息调用链并增加重入风险。光标同步统一经 on_message 处理。
    runtime->game_window = window;
    if (!runtime->compatibility_window_logged.exchange(true)) {
        RECT client{}; GetClientRect(window, &client);
        compatibility_log("window ready thread=" + std::to_string(GetWindowThreadProcessId(window, nullptr)) +
            " client=" + std::to_string(std::max(0L, client.right - client.left)) + "x" +
            std::to_string(std::max(0L, client.bottom - client.top)));
    }
#if REFF_ENABLE_IME_PROXY
    if (runtime->game.ime == ImePolicy::proxy) runtime->ime.start(window, [](Json value) { runtime->channel.send(std::move(value)); }, [](int key) {
        const bool matches = runtime->hotkey_key.load(std::memory_order_acquire) == key &&
            runtime->hotkey_modifiers.load(std::memory_order_acquire) == current_hotkey_modifiers();
        if (!runtime->hotkey_capture.load(std::memory_order_acquire) && (matches || key == VK_ESCAPE)) { set_visible(false); return true; }
        return false;
    }, [](const std::string& message) { ime_log(message); });
#endif
    return true;
}

// 查询进程当前注册的 Raw Input 键盘能力；只识别通用 HID 键盘，不依赖游戏名称。
bool detect_raw_keyboard_no_legacy() {
    UINT count = 0;
    if (GetRegisteredRawInputDevices(nullptr, &count, sizeof(RAWINPUTDEVICE)) != 0 || !count) return false;
    std::vector<RAWINPUTDEVICE> devices(count);
    if (GetRegisteredRawInputDevices(devices.data(), &count, sizeof(RAWINPUTDEVICE)) == static_cast<UINT>(-1)) return false;
    return std::any_of(devices.begin(), devices.begin() + count, [](const RAWINPUTDEVICE& device) {
        return device.usUsagePage == 0x01 && device.usUsage == 0x06 && (device.dwFlags & RIDEV_NOLEGACY) == RIDEV_NOLEGACY;
    });
}

// 将 RAWKEYBOARD 还原为标准键盘消息参数，交给代理线程的 TranslateMessage/IMM32 处理。
std::optional<std::tuple<UINT, WPARAM, LPARAM>> translate_raw_keyboard(const RAWKEYBOARD& keyboard) {
    if (keyboard.VKey == 0 || keyboard.VKey == 255) return std::nullopt;
    UINT message = keyboard.Message;
    if (message != WM_KEYDOWN && message != WM_KEYUP && message != WM_SYSKEYDOWN && message != WM_SYSKEYUP)
        message = (keyboard.Flags & RI_KEY_BREAK) ? WM_KEYUP : WM_KEYDOWN;
    LPARAM native = 1 | (static_cast<LPARAM>(keyboard.MakeCode) << 16);
    if (keyboard.Flags & (RI_KEY_E0 | RI_KEY_E1)) native |= 1LL << 24;
    if (keyboard.Flags & RI_KEY_BREAK) native |= (1LL << 30) | (1LL << 31);
    return std::tuple{message, static_cast<WPARAM>(keyboard.VKey), native};
}

// 把网页面板逻辑像素换算成游戏客户区像素；所有坐标在游戏窗口线程使用前再次裁剪。
void map_ime_bounds(Json& value) {
    const int panel_left = runtime->left.load();
    const int panel_top = runtime->top.load();
    const int panel_width_pixels = std::max(1, runtime->right.load() - panel_left);
    const int panel_height_pixels = std::max(1, runtime->bottom.load() - panel_top);
    const int content_width = std::max(1, runtime->viewport_width.load()), content_height = std::max(1, runtime->viewport_height.load());
    const int x = std::clamp(value.value("x", 0), 0, content_width - 1);
    const int y = std::clamp(value.value("y", 0), 0, content_height - 1);
    const int width = std::clamp(value.value("width", 1), 1, content_width);
    const int height = std::clamp(value.value("height", 24), 1, content_height);
    value["x"] = panel_left + x * panel_width_pixels / content_width;
    value["y"] = panel_top + y * panel_height_pixels / content_height;
    value["width"] = std::max(1, width * panel_width_pixels / content_width);
    value["height"] = std::max(1, height * panel_height_pixels / content_height);
    if (value.contains("fontSize")) value["fontSize"] = std::clamp(value.value("fontSize", 16), 1, 256) * panel_height_pixels / content_height;
}

// 将已校验配置发布为输入线程可直接读取的原子状态；视觉配置由 Shell 自身应用。
void apply_runtime_settings(const Json& settings) {
    const auto input = settings.value("input", Json::object());
    runtime->mouse_passthrough = input.value("mousePassthrough", false);
    runtime->keyboard_passthrough = input.value("keyboardPassthrough", false);
    const auto hotkey = settings.value("hotkey", Json::object());
    runtime->hotkey_key = hotkey.value("key", VK_F8);
    runtime->hotkey_modifiers = hotkey.value("modifiers", 0);
    runtime->preload_enabled = settings.value("startup", Json::object()).value("preload", true);
    refresh_keyboard_capture();
}

// 将 Windows 当前修饰键转换为设置文件使用的掩码；左右 Shift/Ctrl/Alt 统一为同一逻辑键。
int current_hotkey_modifiers() {
    int modifiers = 0;
    if (GetKeyState(VK_SHIFT) & 0x8000) modifiers |= 1;
    if (GetKeyState(VK_CONTROL) & 0x8000) modifiers |= 2;
    if (GetKeyState(VK_MENU) & 0x8000) modifiers |= 4;
    if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) modifiers |= 8;
    return modifiers;
}

// 只有完整的按键和修饰键组合才触发面板开关，避免 Ctrl/F8 等组合误触发普通 F8 绑定。
bool is_configured_hotkey(UINT message, WPARAM key, LPARAM lparam) {
    if (message != WM_KEYDOWN && message != WM_SYSKEYDOWN) return false;
    if (lparam & (1LL << 30)) return false;
    return runtime->hotkey_key.load(std::memory_order_acquire) == static_cast<int>(key) &&
        runtime->hotkey_modifiers.load(std::memory_order_acquire) == current_hotkey_modifiers();
}

// Core 设置请求在 IPC 线程完成，不经过 Lua，因此脚本重载和单个插件故障不会阻断设置页。
bool handle_settings_request(const Json& request) {
    const auto method = request.value("method", std::string{});
    if (method != "reff.settings.get" && method != "reff.settings.set" && method != "reff.settings.reset" && method != "reff.settings.hotkey.capture") return false;
    auto response = Json{{"type", "response"}, {"id", request.value("id", std::string{})}, {"epoch", runtime->epoch.load()}};
    try {
        if (request.contains("pluginId")) throw std::runtime_error("系统设置只允许 Shell 调用");
        if (method == "reff.settings.hotkey.capture") {
            const auto params = request.value("params", Json::object());
            if (!params.is_object() || !params.value("active", Json()).is_boolean()) throw std::runtime_error("快捷键捕获参数不合法");
            runtime->hotkey_capture = params.value("active", false);
        } else if (method == "reff.settings.set") {
            std::string error;
            if (!runtime->settings.update(request.value("params", Json::object()), error)) throw std::runtime_error(error);
        } else if (method == "reff.settings.reset") {
            runtime->settings.reset();
            // 恢复默认值时让下一帧重新建立默认居中矩形。
            std::lock_guard geometry_lock(runtime->panel_geometry_mutex);
            runtime->panel_origin_left = runtime->panel_origin_top = -1;
            runtime->panel_display_width = panel_width; runtime->panel_display_height = panel_height;
            runtime->panel_position_initialized = false;
            runtime->viewport_reset_requested = true;
            request_cursor_sync();
        }
        auto settings = runtime->settings.snapshot();
        apply_runtime_settings(settings);
        if (method != "reff.settings.get" && method != "reff.settings.hotkey.capture") {
            // 开启记录时把当前窗口作为新的持久化起点；关闭记录会由 SettingsStore 清除旧几何。
            if (method == "reff.settings.set" && settings["window"].value("rememberGeometry", true)) {
                RECT client{}; const auto window = runtime->game_window.load();
                if (window && GetClientRect(window, &client) && client.right > 0 && client.bottom > 0) {
                    std::lock_guard geometry_lock(runtime->panel_geometry_mutex);
                    runtime->settings.set_geometry({client.right, client.bottom, runtime->panel_origin_left.load(), runtime->panel_origin_top.load(),
                        runtime->panel_display_width.load(), runtime->panel_display_height.load()});
                    settings = runtime->settings.snapshot();
                }
            }
            std::string error;
            if (!runtime->settings.save(error)) throw std::runtime_error(error);
        }
        response["result"] = std::move(settings);
    } catch (const std::exception& exception) {
        response["error"] = {{"code", "INVALID_ARGUMENT"}, {"message", exception.what()}};
    }
    runtime->channel.send(std::move(response));
    return true;
}

// IPC 线程校验会话、epoch 与白名单；Core 设置在本层处理，插件业务只投递给 Lua 安全执行阶段。
void receive(Json value) {
    try {
        auto type = value.value("type", std::string{});
        if (type == "hello") {
            if (value.value("protocol", 0u) != protocol_version || value.value("sessionId", std::string{}) != narrow(runtime->session)) return;
            runtime->connected = true;
            compatibility_log("host handshake=ok protocol=" + std::to_string(protocol_version));
            // 新宿主从默认视口启动，必须重新发送保留的窗口尺寸与代次。
            runtime->channel.send({{"type", "viewport"}, {"width", runtime->viewport_width.load()},
                {"height", runtime->viewport_height.load()}, {"generation", runtime->viewport_generation.load()}});
            request_cursor_sync();
            runtime->channel.send({{"type", "ready"}, {"ready", runtime->ready.load()}, {"epoch", runtime->epoch.load()}});
            runtime->channel.send({{"type", "visible"}, {"value", runtime->visible.load()}}); return;
        }
        if (!runtime->connected) return;
        if (type == "close") { set_visible(false); return; }
        if (type == "ime_focus") {
            if (value.value("sessionId", std::string{}) != narrow(runtime->session)) return;
            runtime->web_input_active = value.value("active", false);
            refresh_keyboard_capture();
            ime_log("focus_receive active=" + std::string(value.value("active", false) ? "1" : "0") +
                    " input=" + value.value("inputId", std::string{}) +
                    " x=" + std::to_string(value.value("x", 0)) + " y=" + std::to_string(value.value("y", 0)));
            if (value.value("active", false)) map_ime_bounds(value);
#if REFF_ENABLE_IME_PROXY
            if (runtime->game.ime == ImePolicy::proxy) runtime->ime.activate(std::move(value));
#endif
            return;
        }
        if (type == "ime_bounds") {
            if (value.value("sessionId", std::string{}) != narrow(runtime->session)) return;
            ime_log("bounds_receive x=" + std::to_string(value.value("x", 0)) +
                    " y=" + std::to_string(value.value("y", 0)));
            map_ime_bounds(value);
#if REFF_ENABLE_IME_PROXY
            if (runtime->game.ime == ImePolicy::proxy) runtime->ime.update_bounds(std::move(value));
#endif
            return;
        }
        if (type != "request" || !valid_request(value) || value.value("sessionId", std::string{}) != narrow(runtime->session)) return;
        auto epoch = runtime->epoch.load();
        if (value.value("epoch", std::uint64_t{}) != epoch) {
            auto error = failure(value, "SCRIPT_RESET", "请求会话已经失效"); error["epoch"] = epoch;
            runtime->channel.send(error); return;
        }
        {
            std::lock_guard lock(runtime->request_mutex);
            auto id = value["id"].get<std::string>();
            if (runtime->seen.contains(id) || runtime->seen.size() >= 100000) {
                auto error = failure(value, "INVALID_ARGUMENT", "重复请求或会话请求上限"); error["epoch"] = epoch;
                runtime->channel.send(error); return;
            }
            runtime->seen.insert(id);
        }
        if (handle_settings_request(value)) return;
        if (!runtime->ready || value.value("epoch", std::uint64_t{}) != epoch) {
            auto error = failure(value, "SCRIPT_RESET", "脚本尚未就绪或已重置"); error["epoch"] = epoch;
            runtime->channel.send(error); return;
        }
        value["deadline"] = GetTickCount64() + 5000;
        if (!runtime->requests.push(value)) {
            auto error = failure(value, "QUEUE_FULL", "请求队列已满"); error["epoch"] = epoch;
            runtime->channel.send(error);
        }
    } catch (...) { api->functions->log_warn("REFF: rejected malformed IPC message"); }
}

// 后台创建浏览器进程与共享区；失败后恢复游戏输入，不自动重放写请求。
void service_loop(std::stop_token stop) {
    Handle process, job;
    HostHealth health;
    bool shutdown_sent = false;
    ULONGLONG shutdown_tick = 0;
    constexpr auto preload_delay = std::chrono::seconds(8);
    bool preload_attempted = false;
    std::optional<std::chrono::steady_clock::time_point> preload_ready_since;
    // 先停止 IPC 回调，再清理业务队列；指针状态在同一把锁下结束。
    // Job 只包含本次创建的 REFF 宿主及其后代，关闭它不会终止游戏或其他 Mod。
    auto release_host = [&] {
        runtime->connected = false;
        runtime->channel.stop();
        runtime->connected = false;
        {
            std::lock_guard lock(runtime->pointer_mutex);
            set_visible(false);
            runtime->focus_suspended = false;
            runtime->start_requested = false;
            runtime->pending_virtual_cursor_dispatch = false;
            runtime->pointer_in_panel = false;
        }
        { std::lock_guard lock(runtime->request_mutex); runtime->requests.clear(); runtime->seen.clear(); }
        runtime->frame.store(nullptr);
        job.reset(); process.reset();
        shutdown_sent = false;
    };
    auto hidden_since = std::chrono::steady_clock::now();
    while (!stop.stop_requested()) {
        const auto loop_now = std::chrono::steady_clock::now();
        const bool preload_eligible = runtime->ready.load(std::memory_order_acquire) &&
            runtime->preload_enabled.load(std::memory_order_acquire);
        if (!preload_attempted && !process && preload_eligible) {
            if (!preload_ready_since) preload_ready_since = loop_now;
            if (loop_now - *preload_ready_since >= preload_delay) {
                preload_attempted = true;
                runtime->start_requested = true;
                api->functions->log_info("REFF: delayed browser prewarm requested");
            }
        } else if (!preload_eligible) {
            preload_ready_since.reset();
        }
        // 用户在延迟期内主动打开时，同一宿主已经承担预热职责；回收后不自动循环拉起。
        if (runtime->visible.load(std::memory_order_acquire)) preload_attempted = true;
        if (process) {
            const char* reason = health.failure(GetTickCount64(), WaitForSingleObject(process.get(), 0) == WAIT_OBJECT_0,
                runtime->connected.load(), runtime->channel.connected());
            if (!reason && shutdown_sent && GetTickCount64() - shutdown_tick >= 3000) reason = "shutdown timed out";
            if (reason) {
                release_host();
                api->functions->log_info("REFF: host stopped (%s); input released; F8 starts a new session", reason);
            }
        }
        if (!process && runtime->start_requested.exchange(false)) {
            try {
                runtime->session = make_session();
                auto frame = std::make_shared<SharedFrame>(); frame->open(runtime->session, true);
                runtime->frame.store(frame);
                { std::lock_guard lock(runtime->request_mutex); runtime->seen.clear(); runtime->requests.clear(); }
                runtime->channel.start(runtime->session, true, receive);
                auto directory = runtime->root / L"reff" / L"runtime";
                auto executable = directory / L"reff-host.exe";
                auto assets = runtime->root / L"reff" / L"ui";
                auto manifests = runtime->root / L"reff" / L"plugins";
                auto cache = runtime->root / L"data" / L"REFF" / L"cache";
                std::filesystem::create_directories(cache);
                if (!std::filesystem::exists(executable)) throw std::runtime_error("host not installed");
                std::wstring command = L"\"" + executable.wstring() + L"\" --reff-session=" + runtime->session +
                    L" --reff-assets=\"" + assets.wstring() + L"\" --reff-manifests=\"" + manifests.wstring() +
                    L"\" --reff-cache=\"" + cache.wstring() + L"\" --reff-game=" +
                    std::wstring(runtime->game.target.begin(), runtime->game.target.end());
                job.reset(CreateJobObjectW(nullptr, nullptr));
                JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
                limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
                if (!job || !SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits))) throw std::runtime_error("job setup failed");
                STARTUPINFOW startup{sizeof(startup)}; startup.dwFlags = STARTF_USESHOWWINDOW; startup.wShowWindow = SW_HIDE;
                PROCESS_INFORMATION info{};
                if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED | CREATE_NO_WINDOW,
                    nullptr, directory.c_str(), &startup, &info)) throw std::runtime_error("host process creation failed");
                process.reset(info.hProcess); Handle thread(info.hThread);
                if (!AssignProcessToJobObject(job.get(), process.get())) {
                    TerminateProcess(process.get(), 1); process.reset(); throw std::runtime_error("job assignment failed");
                }
                if (ResumeThread(thread.get()) == DWORD(-1)) throw std::runtime_error("host resume failed");
                health.started(GetTickCount64());
                hidden_since = std::chrono::steady_clock::now();
                api->functions->log_info("REFF: browser started");
                compatibility_log("host process=started");
            } catch (const std::exception& error) {
                release_host();
                api->functions->log_error("REFF: %s", error.what());
            }
        }
        if (runtime->visible || runtime->focus_suspended) hidden_since = std::chrono::steady_clock::now();
        else if (process && !shutdown_sent && std::chrono::steady_clock::now() - hidden_since > std::chrono::minutes(5)) {
            runtime->channel.send({{"type", "shutdown"}});
            shutdown_sent = true; shutdown_tick = GetTickCount64();
        }
        flush_perf_logs();
        if (runtime->settings_save_requested.exchange(false)) {
            std::string error;
            if (!runtime->settings.save(error)) api->functions->log_warn("REFF: settings save failed: %s", error.c_str());
        }
        // 光标同步只在显隐、焦点和 WM_SETCURSOR 事件发生时触发，避免后台高频投递窗口消息。
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    flush_perf_logs();
    release_host();
    // release_host 可能结束尚未松开的拖动会话；在其后再做最后一次保存，避免退出瞬间丢失窗口几何。
    if (runtime->settings_save_requested.exchange(false)) {
        std::string error;
        if (!runtime->settings.save(error)) api->functions->log_warn("REFF: final settings save failed: %s", error.c_str());
    }
}

// Lua 在既有游戏回调中拉取请求；C++ 不从 IPC/渲染线程调用 Lua。
int lua_poll(lua_State* state) {
    if (runtime->owner != state) return 0;
    std::lock_guard lock(runtime->request_mutex);
    // 已断开的宿主不再领取尚未执行的写请求；已交给 Lua 的调用不承诺回滚。
    if (!runtime->connected || !runtime->channel.connected()) { runtime->requests.clear(); return 0; }
    for (int i = 0; i < 32; ++i) {
        auto value = runtime->requests.pop(); if (!value) return 0;
        if (value->value("epoch", std::uint64_t{}) != runtime->epoch.load()) continue;
        if (value->value("deadline", ULONGLONG{}) < GetTickCount64()) {
            auto error = failure(*value, "TIMEOUT", "请求在执行前过期"); error["epoch"] = runtime->epoch.load();
            runtime->channel.send(error); continue;
        }
        auto raw = value->dump(); lua_pushlstring(state, raw.data(), raw.size()); return 1;
    }
    return 0;
}

// Lua 返回可序列化的响应，拒绝跨 epoch 或过大的消息。
int lua_respond(lua_State* state) {
    if (runtime->owner != state) return 0;
    size_t length{}; const char* raw = lua_tolstring(state, 1, &length);
    if (!raw || length > max_message) return 0;
    try {
        auto value = Json::parse(raw, raw + length);
        if (value.value("epoch", std::uint64_t{}) == runtime->epoch.load()) runtime->channel.send(std::move(value));
    } catch (...) { api->functions->log_warn("REFF: invalid Lua response"); }
    return 0;
}

// Lua 事件出口只接受注册表已验证后的插件、事件和 JSON payload；事件广播仍由宿主按订阅句柄过滤。
int lua_emit(lua_State* state) {
    if (runtime->owner != state) { lua_pushboolean(state, false); return 1; }
    size_t plugin_length{}, event_length{}, payload_length{};
    const char* plugin_id = luaL_checklstring(state, 1, &plugin_length);
    const char* event_name = luaL_checklstring(state, 2, &event_length);
    const char* payload = luaL_checklstring(state, 3, &payload_length);
    if (plugin_length > 96 || event_length > 96 || payload_length == 0 || payload_length > max_message) {
        lua_pushboolean(state, false); return 1;
    }
    try {
        auto value = Json::parse(payload, payload + payload_length);
        if (!value.is_object() && !value.is_array()) { lua_pushboolean(state, false); return 1; }
        runtime->channel.send({{"type", "event"}, {"epoch", runtime->epoch.load()},
            {"pluginId", std::string(plugin_id, plugin_length)}, {"eventName", std::string(event_name, event_length)}, {"payload", std::move(value)}});
        lua_pushboolean(state, true); return 1;
    } catch (...) { lua_pushboolean(state, false); return 1; }
}

// bootstrap 主动认领运行状态，避免其他插件的独立 Lua 状态误消费队列。
int lua_ready(lua_State* state) {
    runtime->owner = state; runtime->ready = lua_toboolean(state, 1) != 0;
    if (runtime->connected) runtime->channel.send({{"type", "ready"}, {"ready", runtime->ready.load()}, {"epoch", runtime->epoch.load()}});
    return 0;
}
int lua_visible(lua_State* state) { lua_pushboolean(state, runtime->visible && runtime->connected); return 1; }
// Lua 插件查询当前桥接是否已完成就绪握手；仅返回布尔值，不暴露会话或指针。
int lua_ready_state(lua_State* state) { lua_pushboolean(state, runtime->ready && runtime->connected); return 1; }

// Lua 状态创建时只绑定本模块函数，不改动原有 re/sdk/imgui 命名空间。
void on_lua_created(lua_State* state) {
    api->functions->lock_lua();
    lua_newtable(state);
    lua_pushcfunction(state, lua_poll); lua_setfield(state, -2, "poll");
    lua_pushcfunction(state, lua_respond); lua_setfield(state, -2, "respond");
    lua_pushcfunction(state, lua_emit); lua_setfield(state, -2, "emit");
    lua_pushcfunction(state, lua_ready); lua_setfield(state, -2, "set_ready");
    lua_pushcfunction(state, lua_visible); lua_setfield(state, -2, "is_visible");
    lua_pushcfunction(state, lua_ready_state); lua_setfield(state, -2, "is_ready");
    lua_setglobal(state, "reff_native");
    api->functions->unlock_lua();
    api->functions->log_info("REFF: Lua bridge installed (matching Lua 5.4.3 source)");
}

// 脚本销毁撤销所有旧请求，通知网页重新等待后端，禁止继承旧状态指针。
void on_lua_destroyed(lua_State* state) {
    if (runtime->owner != state) return;
    std::lock_guard lock(runtime->request_mutex);
    runtime->owner = nullptr; runtime->ready = false; ++runtime->epoch;
    runtime->requests.clear(); runtime->seen.clear(); set_visible(false);
    if (runtime->connected) runtime->channel.send({{"type", "ready"}, {"ready", false}, {"epoch", runtime->epoch.load()}});
}

// Present 回调仅做图形工作；失焦或 REFramework 菜单打开时把输入归还游戏/原菜单。
void on_present() {
#if REFF_ENABLE_PERF_DIAGNOSTICS
    // Present 间隔覆盖宿主未启动、隐藏与显示状态；超过 1 秒的暂停不计入分位数。
    const auto present_now_us = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    if (!runtime->perf_present_window_started_us) runtime->perf_present_window_started_us = present_now_us;
    if (runtime->perf_present_last_us) {
        const auto interval = present_now_us - runtime->perf_present_last_us;
        if (interval > 0 && interval <= 1000000) runtime->perf_present_intervals_us.push_back(static_cast<std::uint32_t>(interval));
    }
    runtime->perf_present_last_us = present_now_us;
    if (present_now_us - runtime->perf_present_window_started_us >= 10000000 && !runtime->perf_present_intervals_us.empty()) {
        auto sorted = runtime->perf_present_intervals_us;
        std::sort(sorted.begin(), sorted.end());
        const auto quantile = [&](double value) {
            const auto index = std::min(sorted.size() - 1, static_cast<std::size_t>(std::ceil(sorted.size() * value) - 1));
            return sorted[index];
        };
        std::uint64_t total{}; for (const auto interval : sorted) total += interval;
        const double average_fps = total ? 1000000.0 * sorted.size() / total : 0.0;
        perf_log("present visible=" + std::to_string(runtime->visible.load() ? 1 : 0) +
            " connected=" + std::to_string(runtime->connected.load() ? 1 : 0) +
            " intervals=" + std::to_string(sorted.size()) +
            " average_fps=" + std::to_string(average_fps) +
            " p50_us=" + std::to_string(quantile(0.50)) +
            " p95_us=" + std::to_string(quantile(0.95)) +
            " p99_us=" + std::to_string(quantile(0.99)));
        runtime->perf_present_intervals_us.clear();
        runtime->perf_present_window_started_us = present_now_us;
    }
#endif
    if (!runtime->visible || !runtime->connected) return;
    std::lock_guard lock(runtime->renderer_mutex);
    const auto* renderer = api->renderer_data;
    if (renderer->renderer_type != REFRAMEWORK_RENDERER_D3D12) { set_visible(false); return; }
    if (!runtime->renderer.initialize(static_cast<ID3D12Device*>(renderer->device),
        static_cast<IDXGISwapChain*>(renderer->swapchain), static_cast<ID3D12CommandQueue*>(renderer->command_queue))) {
        set_visible(false); api->functions->log_error("REFF: DX12 initialization failed: %s", runtime->renderer.last_error().c_str()); return;
    }
    if (!runtime->compatibility_renderer_logged.exchange(true)) {
        compatibility_log("renderer initialized=d3d12 window=" + std::to_string(reinterpret_cast<std::uintptr_t>(runtime->renderer.window())));
    }
    bool foreground = GetForegroundWindow() == runtime->renderer.window();
#if REFF_ENABLE_IME_PROXY
    foreground = foreground || runtime->ime.owns_foreground();
    foreground = foreground || runtime->ime.active();
#endif
    if (!foreground) suspend_for_focus();
    else if (runtime->focus_suspended) restore_after_focus(runtime->renderer.window());
    RECT client{};
    if (!GetClientRect(runtime->renderer.window(), &client) || client.right <= 0 || client.bottom <= 0) return;
    const std::pair client_size{int(client.right), int(client.bottom)};
    RECT rect{};
    bool geometry_initialized = false;
    {
    std::lock_guard geometry_lock(runtime->panel_geometry_mutex);
    // 拖动可能与首个 Present 并发发生；此时禁止初始化分支把输入线程刚写入的位置覆盖为居中位置。
    if (!runtime->panel_position_initialized.load(std::memory_order_acquire) && !runtime->panel_dragging.load(std::memory_order_acquire)) {
        const auto restored = runtime->settings.restored_geometry(client_size.first, client_size.second);
        if (restored) rect = {restored->left, restored->top, restored->left + restored->width, restored->top + restored->height};
        else rect = runtime->renderer.panel_client_rect(client_size);
        runtime->panel_origin_left = rect.left; runtime->panel_origin_top = rect.top;
        runtime->panel_display_width = rect.right - rect.left; runtime->panel_display_height = rect.bottom - rect.top;
        runtime->panel_position_initialized.store(true, std::memory_order_release);
        geometry_initialized = true;
    } else {
        const auto requested_origin = std::pair{runtime->panel_origin_left.load(), runtime->panel_origin_top.load()};
        // 拖动边缘时尺寸可能瞬间超过客户区；先限制到客户区，避免 DX12 viewport/scissor 越界导致整块面板消失。
        const int pw=std::clamp(runtime->panel_display_width.load(), 1, std::max(1, client_size.first));
        const int ph=std::clamp(runtime->panel_display_height.load(), 1, std::max(1, client_size.second));
        rect={requested_origin.first, requested_origin.second, requested_origin.first+pw, requested_origin.second+ph};
        rect.left=std::clamp<LONG>(rect.left,0,std::max<LONG>(0,client_size.first-pw)); rect.top=std::clamp<LONG>(rect.top,0,std::max<LONG>(0,client_size.second-ph)); rect.right=rect.left+pw; rect.bottom=rect.top+ph;
    }
    // 拖动过程中保留输入线程写入的请求位置；松开后下一帧再把越界位置归一化。
    if (!runtime->panel_dragging) {
        runtime->panel_origin_left = rect.left; runtime->panel_origin_top = rect.top;
    }
    const bool geometry_changed = runtime->left != rect.left || runtime->top != rect.top ||
        runtime->right != rect.right || runtime->bottom != rect.bottom;
    runtime->left = rect.left; runtime->top = rect.top; runtime->right = rect.right; runtime->bottom = rect.bottom;
    if (geometry_changed) request_cursor_sync();
    }
    // 首次恢复保存几何后，显示矩形已经变化但 CEF 仍可能处于默认 viewport；
    // 通过窗口线程消息提交一次强制同步，确保页面首帧按实际尺寸布局。
    if (geometry_initialized) {
        runtime->viewport_reset_requested.store(true, std::memory_order_release);
        request_cursor_sync();
    }
    std::pair<int, int> cursor_position;
    {
        std::unique_lock pointer_lock(runtime->pointer_mutex, std::try_to_lock);
        if (pointer_lock.owns_lock())
            runtime->rendered_cursor_position = runtime->cursor_input.cursor.position();
        cursor_position = runtime->rendered_cursor_position;
    }
    auto frame = runtime->frame.load();
    // 宿主重启后从序号 1 生产画面，必须丢弃上一会话的读取序号。
    if (frame && frame != runtime->rendered_frame) {
        runtime->renderer.invalidate_frame(); runtime->rendered_frame = frame;
    }
    if (frame) {
#if REFF_ENABLE_PERF_DIAGNOSTICS
        const auto draw_started = std::chrono::steady_clock::now();
#endif
        // 系统光标租约经窗口消息异步归还；渲染侧同时检查前台状态，确保 Alt-Tab 首帧不残留虚拟光标。
        const bool draw_cursor = foreground && !runtime->focus_suspended.load(std::memory_order_acquire) &&
            runtime->cursor_owned.load(std::memory_order_acquire);
        const bool drawn = runtime->renderer.draw(*frame, cursor_position, draw_cursor, client_size,
                                                   {rect.left, rect.top}, rect, runtime->resize_cursor_mode.load(),
                                                   runtime->viewport_generation.load(std::memory_order_acquire),
                                                   runtime->panel_resize_edges.load(std::memory_order_acquire) != 0);
#if REFF_ENABLE_PERF_DIAGNOSTICS
        const auto draw_cpu_us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - draw_started).count();
        ++runtime->perf_draw_attempts;
        runtime->perf_draw_cpu_us += static_cast<std::uint64_t>(std::max<std::int64_t>(0, draw_cpu_us));
        runtime->perf_draw_cpu_max_us = std::max(runtime->perf_draw_cpu_max_us, static_cast<std::uint64_t>(std::max<std::int64_t>(0, draw_cpu_us)));
        if (!drawn) ++runtime->perf_draw_failures;
#endif
        resize_log("renderer_accept generation=" + std::to_string(runtime->renderer.displayed_frame_generation()), true);
        if (!drawn) {
            set_visible(false); api->functions->log_error("REFF: DX12 draw failed: %s", runtime->renderer.last_error().c_str());
        } else if (!runtime->renderer.last_error().empty()) {
            // draw 返回 true 但命令池超时表示本帧没有新命令；保留日志用于判断闪烁是否来自 GPU 压力。
            api->functions->log_warn("REFF: DX12 draw skipped: %s", runtime->renderer.last_error().c_str());
        }
        const auto now = GetTickCount64();
        if (!runtime->perf_log_tick) runtime->perf_log_tick = now;
        else if (now - runtime->perf_log_tick >= 10000) {
            const auto stats = runtime->renderer.take_stats();
#if REFF_ENABLE_PERF_DIAGNOSTICS
            const auto non_submits = runtime->perf_draw_attempts >= stats.draw_calls ? runtime->perf_draw_attempts - stats.draw_calls : 0;
            perf_log("draw_attempts=" + std::to_string(runtime->perf_draw_attempts) +
                " draw_submits=" + std::to_string(stats.draw_calls) +
                " draw_non_submits=" + std::to_string(non_submits) +
                " draw_failures=" + std::to_string(runtime->perf_draw_failures) +
                " draw_cpu_us=" + std::to_string(runtime->perf_draw_cpu_us) +
                " draw_cpu_max_us=" + std::to_string(runtime->perf_draw_cpu_max_us) +
                " uploads=" + std::to_string(stats.texture_uploads) +
                " uploaded_bytes=" + std::to_string(stats.uploaded_bytes) +
                " cursor_draws=" + std::to_string(stats.cursor_draws));
            runtime->perf_draw_attempts = runtime->perf_draw_failures = runtime->perf_draw_cpu_us = runtime->perf_draw_cpu_max_us = 0;
#else
            (void)stats;
#endif
            runtime->perf_log_tick = now;
        }
    }
}

void on_reset() {
    compatibility_log("renderer reset");
    runtime->compatibility_renderer_logged = false;
    set_visible(false); std::lock_guard lock(runtime->renderer_mutex); runtime->renderer.reset(); runtime->rendered_frame.reset();
}

// Windows 消息转换为网页输入；Raw Input/游戏轮询的覆盖范围仍需游戏内验证。
bool on_message(void* window, unsigned int message, unsigned long long wparam, long long lparam) {
    HWND game_window = static_cast<HWND>(window);
    // REFramework 的窗口过程带递归锁；若系统在处理消息时再次回调插件，直接放行内层消息。
    bool expected = false;
    if (!runtime->message_dispatching.compare_exchange_strong(expected, true)) return true;
    struct DispatchGuard { std::atomic_bool& flag; ~DispatchGuard() { flag.store(false); } } guard{runtime->message_dispatching};
    if (!ensure_cursor_window(game_window)) return true;
    std::lock_guard pointer_lock(runtime->pointer_mutex);
    synchronize_virtual_cursor_bounds(game_window);
    if (runtime->cursor_sync_message && message == runtime->cursor_sync_message) {
        runtime->cursor_sync_pending = false;
        if (runtime->viewport_reset_requested.exchange(false)) commit_viewport_resize(true);
        sync_cursor(game_window);
        dispatch_virtual_cursor_move(0);
        return false;
    }
    if (is_configured_hotkey(message, wparam, lparam) && !runtime->hotkey_capture.load(std::memory_order_acquire)) {
        const bool opening = !runtime->visible.load();
        if (opening) synchronize_virtual_cursor_position(game_window);
        if (!opening) end_panel_drag();
        set_visible(opening); sync_cursor(game_window); return false;
    }
    if ((message == WM_ACTIVATEAPP && !wparam) || message == WM_KILLFOCUS) {
#if REFF_ENABLE_IME_PROXY
        // Windows 可在同一进程的不同 GUI 线程之间发送应用激活通知；转入代理线程
        // 不能被当作 Alt-Tab，否则 ShowWindow/SetFocus 尚未完成就会取消输入会话。
        if (message == WM_ACTIVATEAPP && runtime->ime.owns_thread(static_cast<DWORD>(lparam))) {
            ime_log("internal_proxy_activation");
            return true;
        }
        // 点击网页输入框会让游戏窗口把焦点交给透明 EDIT；这是 REFF 内部焦点迁移，不是 Alt-Tab。
        if (message == WM_KILLFOCUS &&
            (runtime->ime.is_focus_target(reinterpret_cast<HWND>(lparam)) || runtime->ime.active())) return true;
#endif
        ime_log("external_focus_message message=" + std::to_string(message) + " target=" + std::to_string(lparam));
        suspend_for_focus(); sync_cursor(game_window); return true;
    }
    if (message == WM_SETFOCUS || (message == WM_ACTIVATEAPP && wparam)) {
        restore_after_focus(game_window); sync_cursor(game_window); return true;
    }
    if (!runtime->visible || !runtime->connected || !runtime->channel.connected()) return true;
    // 注册了 INPUTSINK 的游戏在后台仍可能收到 Raw Input；暂停期间必须完整放行，不能更新网页指针或吞掉系统输入。
    if (runtime->focus_suspended) return true;
    if (runtime->pending_virtual_cursor_dispatch) {
        dispatch_virtual_cursor_move(0); runtime->pending_virtual_cursor_dispatch = false;
    }
    if (message == WM_KEYDOWN && wparam == VK_ESCAPE && !runtime->hotkey_capture.load(std::memory_order_acquire)) { set_visible(false); sync_cursor(game_window); return false; }
#if REFF_ENABLE_IME_PROXY
    // 少数游戏会在透明 EDIT 激活后把焦点抢回主窗口；此时把原始按键和输入语言消息转交代理线程，
    // 由代理恢复焦点并经过 TranslateMessage/IMM32 处理。派生字符仍丢弃，避免重复提交。
    if (runtime->ime.active() &&
        (message == WM_KEYDOWN || message == WM_KEYUP || message == WM_SYSKEYDOWN || message == WM_SYSKEYUP ||
         message == WM_INPUTLANGCHANGEREQUEST || message == WM_INPUTLANGCHANGE)) {
        runtime->ime.redirect_input(message, static_cast<WPARAM>(wparam), static_cast<LPARAM>(lparam));
        return false;
    }
    if (runtime->ime.active() &&
        (message == WM_CHAR || message == WM_SYSCHAR || message == WM_IME_CHAR || message == WM_IME_COMPOSITION)) return false;
#endif
    if (message == WM_SETCURSOR) {
        sync_cursor(game_window);
        // 游戏可能在面板存续期间重设当前系统箭头；置空只影响当前线程的光标形状，
        // 不改写导入表、不移动物理指针，关闭面板后由游戏下一次 WM_SETCURSOR 自行恢复样式。
        if (runtime->cursor_owned) SetCursor(nullptr);
        return false;
    }
    try {
        int modifiers = 0;
        if (GetKeyState(VK_SHIFT) & 0x8000) modifiers |= 1 << 1;
        if (GetKeyState(VK_CONTROL) & 0x8000) modifiers |= 1 << 2;
        if (GetKeyState(VK_MENU) & 0x8000) modifiers |= 1 << 3;
        if (GetKeyState(VK_LBUTTON) & 0x8000) modifiers |= 1 << 4;
        if (GetKeyState(VK_RBUTTON) & 0x8000) modifiers |= 1 << 6;
        if (message == WM_INPUT) {
            // WM_INPUT 的 lParam 是临时 HRAWINPUT，只能在本次窗口消息内读取；不跨线程保存。
            bool input_passthrough = runtime->mouse_passthrough.load();
            if (GET_RAWINPUT_CODE_WPARAM(wparam) == RIM_INPUT) {
                UINT size = sizeof(RAWINPUT); RAWINPUT raw{};
                const UINT read = GetRawInputData(reinterpret_cast<HRAWINPUT>(lparam), RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER));
                if (read != static_cast<UINT>(-1) && raw.header.dwType == RIM_TYPEKEYBOARD) {
                    input_passthrough = runtime->keyboard_passthrough.load() && !runtime->web_input_active.load();
                    // RIDEV_NOLEGACY 会抑制透明 EDIT 所需的 WM_KEY*。仅在该能力确实启用时转译，
                    // 避免同时存在传统消息的游戏产生重复按键或重复提交。
#if REFF_ENABLE_IME_PROXY
                    const auto now = GetTickCount64();
                    if (!runtime->raw_keyboard_no_legacy.load(std::memory_order_acquire) &&
                        (!runtime->raw_keyboard_probe_tick || now - runtime->raw_keyboard_probe_tick >= 1000)) {
                        runtime->raw_keyboard_probe_tick = now;
                        if (detect_raw_keyboard_no_legacy()) {
                            runtime->raw_keyboard_no_legacy.store(true, std::memory_order_release);
                            ime_log("raw_keyboard_no_legacy=1");
                            compatibility_log("raw_input_keyboard no_legacy=1 translation=enabled");
                        }
                    }
                    if (runtime->game.raw_input_keyboard != CapabilityPolicy::disabled &&
                        runtime->ime.active() && runtime->raw_keyboard_no_legacy.load(std::memory_order_acquire)) {
                        if (const auto translated = translate_raw_keyboard(raw.data.keyboard)) {
                            const auto [key_message, key, native] = *translated;
                            runtime->ime.redirect_input(key_message, key, native);
                        }
                        return false;
                    }
#endif
                } else if (read != static_cast<UINT>(-1) && raw.header.dwType == RIM_TYPEMOUSE) {
                    const bool was_relative = runtime->cursor_input.relative();
                    POINT physical{};
                    if (GetCursorPos(&physical) && ScreenToClient(game_window, &physical)) {
                        if (raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) {
                            // 绝对设备坐标属于桌面而非客户区；系统已经完成虚拟桌面、DPI 和窗口原点换算。
                            runtime->cursor_input.begin(runtime->cursor_input.cursor.bounds().first, runtime->cursor_input.cursor.bounds().second,
                                physical.x, physical.y);
                        } else runtime->cursor_input.raw(raw.data.mouse.lLastX, raw.data.mouse.lLastY, physical.x, physical.y, GetTickCount64());
                        if (was_relative != runtime->cursor_input.relative())
                            api->functions->log_info("REFF: cursor input mode = %s", runtime->cursor_input.relative() ? "relative (recenter detected)" : "system position");
                        // W1 标题栏拖动使用与光标绘制相同的 Raw Input 虚拟坐标源。
                        if (runtime->panel_resize_edges.load(std::memory_order_acquire)) {
                            const auto mode = runtime->resize_controller.snapshot().mode;
                            if (mode == PanelResizeController::InputMode::relative)
                                submit_resize_pointer(runtime->cursor_input.cursor.position().first, runtime->cursor_input.cursor.position().second, mode);
                        }
                        else update_panel_drag(runtime->cursor_input.cursor.position().first, runtime->cursor_input.cursor.position().second);
                        if (!runtime->panel_dragging) runtime->resize_cursor_mode = resize_cursor_mode_at(runtime->cursor_input.cursor.position().first, runtime->cursor_input.cursor.position().second);
                        // 缩放期间 CEF 不需要鼠标命中事件；暂停高频 move，避免占满 IPC 队列阻塞 viewport 重排。
                        if (!runtime->panel_resize_edges.load(std::memory_order_acquire)) dispatch_virtual_cursor_move(modifiers);
                    }
                }
            }
            // REFramework 的消息回调返回 false 会阻止游戏窗口过程处理该原始鼠标输入。
            return input_passthrough;
        }
        if (message == WM_MOUSEMOVE) {
            POINT physical{};
            if (GetCursorPos(&physical) && ScreenToClient(game_window, &physical)) {
                if (!runtime->cursor_input.relative()) {
                    runtime->cursor_input.system_position(physical.x, physical.y);
                    if (runtime->panel_resize_edges.load(std::memory_order_acquire)) {
                        const auto mode = runtime->resize_controller.snapshot().mode;
                        if (mode == PanelResizeController::InputMode::absolute) submit_resize_pointer(physical.x, physical.y, mode);
                    }
                    else update_panel_drag(physical.x, physical.y);
                } else {
                    const auto [virtual_x, virtual_y] = runtime->cursor_input.cursor.position();
                    // Raw Input 缺帧时 Windows 仍会发送 WM_MOUSEMOVE；缩放使用当次客户区坐标兜底，避免尺寸停在按下位置。
                    if (runtime->panel_resize_edges.load(std::memory_order_acquire)) {
                        const auto mode = runtime->resize_controller.snapshot().mode;
                        if (mode == PanelResizeController::InputMode::absolute) submit_resize_pointer(physical.x, physical.y, mode);
                    }
                    else update_panel_drag(virtual_x, virtual_y);
                    if (!runtime->panel_dragging) runtime->resize_cursor_mode = resize_cursor_mode_at(virtual_x, virtual_y);
                }
                // 缩放期间只传递几何 viewport，禁止鼠标 move 洪水把重排消息推迟到释放之后。
                if (!runtime->panel_resize_edges.load(std::memory_order_acquire)) dispatch_virtual_cursor_move(modifiers);
            }
            return runtime->mouse_passthrough.load();
        }
        if (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP || message == WM_LBUTTONDBLCLK ||
            message == WM_RBUTTONDOWN || message == WM_RBUTTONUP || message == WM_RBUTTONDBLCLK ||
            message == WM_MOUSEWHEEL) {
            // 点击与绘制使用同一输入源；自由模式刷新最新系统位置，回中心模式保留相对坐标。
            POINT physical{};
            if (GetCursorPos(&physical) && ScreenToClient(game_window, &physical) && !runtime->cursor_input.relative())
                runtime->cursor_input.system_position(physical.x, physical.y);
            const auto [x, y] = runtime->cursor_input.cursor.position();
            const auto click = runtime->mouse_click_tracker.translate(message);
            if (click && click->button == 0 && !click->mouse_up &&
                (begin_panel_resize(x, y) || begin_panel_drag(x, y))) return false;
            if (click && click->button == 0 && click->mouse_up && runtime->panel_dragging) { end_panel_drag(); return false; }
            int panel_x{}, panel_y{};
            if (!map_virtual_cursor_to_panel(x, y, panel_x, panel_y)) return runtime->mouse_passthrough.load();
            const std::string action = message == WM_MOUSEWHEEL ? "wheel" : click->mouse_up ? "up" : "down";
            runtime->channel.send({{"type", "mouse"}, {"action", action}, {"x", panel_x}, {"y", panel_y},
                {"button", click ? click->button : 0}, {"clickCount", click ? click->click_count : 1},
                {"delta", GET_WHEEL_DELTA_WPARAM(wparam)}, {"modifiers", modifiers}});
            return runtime->mouse_passthrough.load();
        }
#if !REFF_ENABLE_IME_PROXY
        if (message == WM_IME_COMPOSITION && (lparam & GCS_RESULTSTR)) {
            // 只转发已经确认的组合文本，避免把候选窗口内部状态暴露给 CEF。
            HIMC context = ImmGetContext(game_window);
            if (context) {
                LONG bytes = ImmGetCompositionStringW(context, GCS_RESULTSTR, nullptr, 0);
                if (bytes > 0 && bytes <= 512) {
                    std::wstring result(std::size_t(bytes / sizeof(wchar_t)), L'\0');
                    ImmGetCompositionStringW(context, GCS_RESULTSTR, result.data(), bytes);
                    for (wchar_t character : result)
                        runtime->channel.send({{"type", "key"}, {"key", int(character)}, {"native", 0}, {"modifiers", modifiers}, {"action", "char"}});
                }
                ImmReleaseContext(game_window, context);
            }
            return false;
        }
#endif
        if (message == WM_KEYDOWN || message == WM_KEYUP || message == WM_SYSKEYDOWN || message == WM_SYSKEYUP ||
            message == WM_CHAR || message == WM_SYSCHAR || message == WM_IME_CHAR) {
            runtime->channel.send({{"type", "key"}, {"key", int(wparam)}, {"native", int(lparam)}, {"modifiers", modifiers},
                {"action", (message == WM_KEYUP || message == WM_SYSKEYUP) ? "up" :
                    (message == WM_CHAR || message == WM_SYSCHAR || message == WM_IME_CHAR) ? "char" : "down"}});
            return runtime->keyboard_passthrough.load() && !runtime->web_input_active.load();
        }
        // 当前 CEF 桥接不使用中键、侧键和水平滚轮；面板打开时仍须按鼠标穿透设置统一阻断或放行。
        if (message == WM_MBUTTONDOWN || message == WM_MBUTTONUP || message == WM_MBUTTONDBLCLK ||
            message == WM_XBUTTONDOWN || message == WM_XBUTTONUP || message == WM_XBUTTONDBLCLK || message == WM_MOUSEHWHEEL)
            return runtime->mouse_passthrough.load();
    } catch (...) { set_visible(false); }
    return true;
}
}

// DLL 使用通用 REFramework ABI，不在加载阶段绑定单一游戏；初始化阶段再按能力档案拒绝未验证目标。
extern "C" __declspec(dllexport) void reframework_plugin_required_version(REFrameworkPluginVersion* version) {
    version->major = REFRAMEWORK_PLUGIN_VERSION_MAJOR; version->minor = REFRAMEWORK_PLUGIN_VERSION_MINOR;
    version->patch = REFRAMEWORK_PLUGIN_VERSION_PATCH; version->game_name = nullptr;
}

// 初始化选择当前游戏适配档案并注册扩展；未认证游戏或非 D3D12 渲染器在安装 Hook 前安全退出。
extern "C" __declspec(dllexport) bool reframework_plugin_initialize(const REFrameworkPluginInitializeParam* param) {
    if (!param || !param->functions || !param->renderer_data || !param->version) return false;
    api = param; runtime = new Runtime;
    // DLL 路径由当前模块地址解析；在任何兼容拒绝前建立日志根，失败目标同样能留下可诊断原因。
    HMODULE module{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&reframework_plugin_initialize), &module);
    wchar_t path[32768]{}; GetModuleFileNameW(module, path, 32768);
    runtime->root = std::filesystem::path(path).parent_path().parent_path();
    runtime->game = game_profile_for(param->version->game_name ? param->version->game_name : "");
    reset_compatibility_log();
    constexpr bool experimental_build = REFF_ENABLE_EXPERIMENTAL_GAMES != 0;
    const auto decision = compatibility_decision(runtime->game,
        param->renderer_data->renderer_type == REFRAMEWORK_RENDERER_D3D12, experimental_build);
    compatibility_log("reff_version=" REFF_PROJECT_VERSION " reframework_api=" + std::to_string(param->version->major) + "." +
        std::to_string(param->version->minor) + "." + std::to_string(param->version->patch));
    compatibility_log("target=" + runtime->game.target + " status=" + std::string(support_status_name(runtime->game.status)) +
        " renderer=" + (param->renderer_data->renderer_type == REFRAMEWORK_RENDERER_D3D12 ? "d3d12" : "other") +
        " experimental_build=" + (experimental_build ? "1" : "0") +
        " ime_proxy_compiled=" + (REFF_ENABLE_IME_PROXY ? "1" : "0") +
        " decision=" + std::string(compatibility_decision_name(decision)));
    if (decision != CompatibilityDecision::allowed) {
        param->functions->log_error("REFF: game target %s rejected: %s", runtime->game.target.c_str(),
            compatibility_decision_name(decision).data());
        delete runtime; runtime = nullptr; api = nullptr; return false;
    }
    runtime->cursor_sync_message = RegisterWindowMessageW(L"REFF.CursorSync.v1");
    runtime->arrow_cursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!runtime->cursor_sync_message || !runtime->arrow_cursor) {
        param->functions->log_error("REFF: cursor initialization failed"); return false;
    }
#if REFF_ENABLE_CURSOR_IAT_HOOK
    if (install_set_cursor_pos_hook())
        param->functions->log_info("REFF: SetCursorPos IAT hook installed");
    else
        param->functions->log_warn("REFF: SetCursorPos IAT not found; game cursor lock may remain active");
#else
    param->functions->log_warn("REFF: SetCursorPos IAT hook disabled pending stability validation");
#endif
#if REFF_ENABLE_CURSOR_DIAGNOSTICS
    // 诊断开关已保留为兼容配置，但不再修改任何 IAT；进程级观察包装曾触发安全快速失败。
    param->functions->log_warn("REFF: cursor diagnostics disabled for safety; no process-wide IAT is modified");
#endif
    reset_ime_log();
    std::string settings_warning;
    runtime->settings.open(runtime->root / L"data" / L"REFF" / L"settings.json", settings_warning);
    apply_runtime_settings(runtime->settings.snapshot());
    if (!settings_warning.empty()) param->functions->log_warn("REFF: settings fallback to defaults: %s", settings_warning.c_str());
    if (should_install_direct_input(runtime->game)) {
        if (runtime->keyboard_capture.install()) {
            param->functions->log_info("REFF: DirectInput keyboard capture installed");
            compatibility_log("direct_input_keyboard=installed");
        } else {
            param->functions->log_warn("REFF: DirectInput keyboard capture unavailable; Win32 keyboard filtering remains active");
            compatibility_log("direct_input_keyboard=unavailable fallback=win32");
        }
        refresh_keyboard_capture();
    } else compatibility_log("direct_input_keyboard=disabled");
#if REFF_ENABLE_PERF_DIAGNOSTICS
    runtime->perf_present_intervals_us.reserve(4096);
#endif
    reset_perf_log();
    param->functions->on_lua_state_created(on_lua_created);
    param->functions->on_lua_state_destroyed(on_lua_destroyed);
    param->functions->on_present(on_present);
    param->functions->on_device_reset(on_reset);
    param->functions->on_message(on_message);
    runtime->service = std::jthread(service_loop);
    compatibility_log("initialization=complete");
    param->functions->log_info("REFF initialized for %s. The configured hotkey opens/closes the local web panel.", runtime->game.target.c_str());
    return true;
}

