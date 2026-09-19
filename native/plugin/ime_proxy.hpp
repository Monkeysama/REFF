#pragma once

#include "../common/protocol.hpp"
#include <Windows.h>
#include <atomic>
#include <deque>
#include <functional>
#include <thread>
#include <mutex>

namespace reff {

// 游戏进程内的透明 EDIT 代理：只负责取得 Windows IME 原生焦点，不绘制可见 UI。
// 控件由专用线程拥有，消息结果通过无阻塞回调发送给 CEF 宿主；调用者负责会话生命周期。
class ImeProxy {
public:
    using Sender = std::function<void(Json)>;
    using Hotkey = std::function<bool(int)>;
    using Diagnostic = std::function<void(const std::string&)>;

    ~ImeProxy();

    // 启动一次代理线程并绑定游戏窗口；绑定后窗口不变，禁止在窗口线程等待退出。
    void start(HWND owner, Sender sender, Hotkey hotkey, Diagnostic diagnostic = {});
    // 请求代理线程停止，等待窗口和 AttachThreadInput 正常释放。
    void stop();
    // 激活或关闭输入会话；请求可来自 IPC 线程，实际控件操作在代理线程执行。
    void activate(Json request);
    void update_bounds(Json bounds);
    void deactivate();
    // 游戏窗口仍收到键盘消息时，将其转交给代理线程；代理线程先恢复焦点再交给系统 IME。
    void redirect_input(UINT message, WPARAM key, LPARAM native);
    bool owns(HWND window) const { return window && window == edit_.load(); }
    // 判断游戏窗口失焦是否只是转移到 REFF 的透明 EDIT；此时不能隐藏浏览器面板。
    bool is_focus_target(HWND window) const { return owns(window); }
    // WM_ACTIVATEAPP 的 lParam 是将要激活的线程 ID；代理线程属于内部焦点切换。
    bool owns_thread(DWORD thread) const { return thread != 0 && thread == thread_id_.load(); }
    // 输入框会话已激活时，候选窗口/线程焦点变化不应触发面板隐藏。
    bool active() const { return active_.load(std::memory_order_acquire); }
    // 代理是顶层透明窗口，某些窗口模式下会短暂成为前台窗口；这仍属于 REFF 面板焦点。
    bool owns_foreground() const { return edit_.load() && GetForegroundWindow() == edit_.load(); }

private:
    void run(std::stop_token stop, HWND owner);
    void apply_pending();
    void apply_bounds();
    void handle_ime(LPARAM flags);
    void handle_ime_end();
    void send_key(UINT message, WPARAM key, LPARAM native);
    void dispatch_redirected_input();
    bool ensure_input_focus();
    static LRESULT CALLBACK edit_proc(HWND, UINT, WPARAM, LPARAM);

    struct RedirectedInput {
        UINT message{};
        WPARAM wparam{};
        LPARAM lparam{};
    };

    std::jthread thread_;
    std::atomic<DWORD> thread_id_{};
    std::atomic<HWND> edit_{};
    std::atomic_bool active_{};
    HWND owner_{};
    WNDPROC original_{};
    // 字体由代理线程创建和销毁，控件销毁前不得释放仍被 EDIT 使用的 GDI 字体。
    HFONT font_{};
    int font_pixels_{};
    std::mutex mutex_;
    // 代理线程启动时会立即读取这两个请求；必须从 object 开始，不能使用 JSON null。
    Json pending_{Json::object()};
    Json bounds_{Json::object()};
    // 游戏窗口回调和代理窗口线程通过此有界队列交接按键，避免跨线程直接操作焦点窗口。
    std::deque<RedirectedInput> redirected_input_;
    // 当前逻辑输入框 ID 只由代理线程读写；同一会话重复上报只更新坐标，不能重置文本或抢焦点。
    std::string active_input_id_;
    Sender sender_{};
    Hotkey hotkey_{};
    Diagnostic diagnostic_{};
    bool attached_{};
    // 只在新输入会话建立时读取游戏窗口的起始布局；输入过程中由 Windows/IME 自行切换。
    DWORD owner_thread_id_{};
    bool redirected_input_reported_{};
    // 组合生命周期只在线程窗口过程中访问；用于在取消/结束消息时清理 CEF 状态。
    bool composition_active_{};
    // 每次 ImeCommitText 后记录仍可能由 TranslateMessage 产生的 WM_CHAR 数。
    // WM_IME_CHAR 始终属于 IME 提交路径，统一丢弃，不能占用这个计数。
    int suppress_chars_{};
    static inline ImeProxy* instance_{};
    static constexpr UINT apply_message_ = WM_APP + 0x52;
    static constexpr UINT redirect_message_ = WM_APP + 0x53;
    static constexpr size_t max_redirected_input_ = 64;
};

} // namespace reff
