#include "ime_proxy.hpp"
#include <imm.h>
#include <algorithm>
#include <string>

namespace reff {
namespace {
std::wstring to_wide(const std::string& value) {
    if (value.empty()) return {};
    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (!length) return {};
    std::wstring result(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), length);
    return result;
}
std::string to_utf8(const std::wstring& value) {
    if (value.empty()) return {};
    int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (!length) return {};
    std::string result(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), length, nullptr, nullptr);
    return result;
}

std::string composition(HWND edit, bool result) {
    HIMC context = ImmGetContext(edit);
    if (!context) return {};
    LONG bytes = ImmGetCompositionStringW(context, result ? GCS_RESULTSTR : GCS_COMPSTR, nullptr, 0);
    std::wstring text;
    if (bytes > 0 && bytes <= 4096) {
        text.resize(static_cast<size_t>(bytes / sizeof(wchar_t)));
        ImmGetCompositionStringW(context, result ? GCS_RESULTSTR : GCS_COMPSTR, text.data(), bytes);
    }
    ImmReleaseContext(edit, context);
    return to_utf8(text);
}
} // namespace

ImeProxy::~ImeProxy() { stop(); }

// 创建代理线程；Windows EDIT 和 IME 上下文始终由该线程拥有，避免跨线程 SendMessage。
void ImeProxy::start(HWND owner, Sender sender, Hotkey hotkey) {
    if (thread_.joinable() || !owner || !IsWindow(owner)) return;
    owner_ = owner; sender_ = std::move(sender); hotkey_ = std::move(hotkey);
    thread_ = std::jthread([this, owner](std::stop_token stop) { run(stop, owner); });
}

// 关闭代理并撤销线程输入附加，确保 IME 不继续占用游戏焦点。
void ImeProxy::stop() {
    if (thread_.joinable()) { thread_.request_stop(); if (auto id = thread_id_.load()) PostThreadMessageW(id, WM_QUIT, 0, 0); thread_.join(); }
}

// 只更新队列；控件显示、文本和焦点都在代理线程处理。
void ImeProxy::activate(Json request) {
    {
        std::lock_guard lock(mutex_);
        // 焦点请求本身包含已经映射到游戏客户区的输入框矩形；它是隔离 iframe 没有
        // OnImeCompositionRangeChanged 时的权威后备坐标，不能继续沿用上一个主文档输入框的位置。
        if (request.is_object() && request.value("active", false)) bounds_ = request;
        pending_ = std::move(request);
    }
    if (HWND edit = edit_.load()) PostMessageW(edit, apply_message_, 0, 0);
}
void ImeProxy::update_bounds(Json bounds) {
    { std::lock_guard lock(mutex_); bounds_ = std::move(bounds); }
    if (HWND edit = edit_.load()) PostMessageW(edit, apply_message_, 0, 0);
}
void ImeProxy::deactivate() { activate(Json{{"active", false}}); }

// 代理线程创建透明标准 EDIT，并以 AttachThreadInput 取得与游戏一致的键盘焦点队列。
void ImeProxy::run(std::stop_token stop, HWND owner) {
    // 先显式创建消息队列，再发布线程 ID；stop() 才能可靠地投递 WM_QUIT，避免启动竞态导致 join 永久等待。
    MSG queue_probe{}; PeekMessageW(&queue_probe, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    owner_ = owner; thread_id_ = GetCurrentThreadId(); const DWORD game_thread = GetWindowThreadProcessId(owner, nullptr); const DWORD proxy_thread = thread_id_.load();
    if (!game_thread) { thread_id_ = 0; return; }
    attached_ = game_thread && game_thread != proxy_thread && AttachThreadInput(proxy_thread, game_thread, TRUE) != FALSE;
    // 键盘布局按线程保存；代理若沿用默认英文布局，焦点迁移会令系统中/英指示器反复跳变。
    if (HKL game_layout = GetKeyboardLayout(game_thread)) ActivateKeyboardLayout(game_layout, 0);
    instance_ = this;
    HWND edit = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_LAYERED, L"EDIT", L"",
        WS_POPUP | ES_LEFT | ES_AUTOHSCROLL, 0, 0, 1, 1, owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!edit) { if (attached_) AttachThreadInput(proxy_thread, game_thread, FALSE); attached_ = false; instance_ = nullptr; thread_id_ = 0; return; }
    SetPropW(edit, L"REFF.ImeProxy", this);
    original_ = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&edit_proc)));
    if (!original_) {
        RemovePropW(edit, L"REFF.ImeProxy"); DestroyWindow(edit);
        if (attached_) AttachThreadInput(proxy_thread, game_thread, FALSE);
        attached_ = false; instance_ = nullptr; thread_id_ = 0; return;
    }
    SetLayeredWindowAttributes(edit, 0, 0, LWA_ALPHA);
    // 无论消息循环、JSON 转换还是通道回调是否抛出异常，都必须在 DLL 线程退出前恢复窗口过程和输入附加。
    auto cleanup = [&] {
        try { if (composition_active_ && sender_) sender_({{"type", "ime"}, {"action", "cancel"}}); } catch (...) {}
        composition_active_ = false;
        ImmAssociateContext(edit, nullptr); RemovePropW(edit, L"REFF.ImeProxy");
        if (original_) SetWindowLongPtrW(edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(original_));
        DestroyWindow(edit); edit_ = nullptr; original_ = nullptr; active_ = false;
        if (attached_) AttachThreadInput(proxy_thread, game_thread, FALSE);
        attached_ = false; instance_ = nullptr; thread_id_ = 0;
    };
    edit_ = edit; composition_active_ = false; suppress_chars_ = 0;
    try {
        apply_pending();
        MSG message{};
        while (!stop.stop_requested() && GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    } catch (...) {
        // 代理线程不得把异常传播到 std::jthread；cleanup 仍会释放所有窗口和 IME 资源。
    }
    cleanup();
}

// 将输入请求映射到控件文本和焦点；隐藏请求会取消当前组合并把焦点还给游戏窗口。
void ImeProxy::apply_pending() {
    HWND edit = edit_.load(); if (!edit) return;
    Json request; { std::lock_guard lock(mutex_); request = pending_; }
    // 失效或尚未收到首个请求时按未激活处理，禁止 malformed JSON 穿透到线程入口。
    if (!request.is_object() || !request.value("active", false)) {
        const bool was_active = active_.exchange(false, std::memory_order_acq_rel);
        if (HIMC context = ImmGetContext(edit)) { ImmNotifyIME(context, NI_COMPOSITIONSTR, CPS_CANCEL, 0); ImmReleaseContext(edit, context); }
        if (was_active && sender_) sender_({{"type", "ime"}, {"action", "cancel"}});
        ShowWindow(edit, SW_HIDE);
        active_input_id_.clear();
        // 代理尚未激活时不能抢回游戏焦点，否则首次页面加载会产生一次额外焦点振荡。
        if (was_active && owner_) SetFocus(owner_);
        return;
    }
    const auto input_id = request.value("inputId", std::string{});
    const bool new_session = !active_.load(std::memory_order_acquire) || input_id != active_input_id_;
    active_ = true;
    apply_bounds(); ShowWindow(edit, SW_SHOW);
    // 同一 iframe 输入框可能在原生代理接管焦点后再次上报 focusin；重复初始化会中断输入法切换。
    // 只有输入框身份变化时同步文本、选区并取得系统焦点，后续上报仅用于更新候选框位置。
    if (new_session) {
        active_input_id_ = input_id;
        auto text = to_wide(request.value("text", std::string{}));
        const int length = static_cast<int>(text.size());
        const int selection_start = std::clamp(request.value("selectionStart", length), 0, length);
        const int selection_end = std::clamp(request.value("selectionEnd", length), selection_start, length);
        SetWindowTextW(edit, text.c_str());
        SendMessageW(edit, EM_SETSEL, static_cast<WPARAM>(selection_start), static_cast<LPARAM>(selection_end));
        SetFocus(edit);
        if (GetFocus() != edit) { SetActiveWindow(edit); SetFocus(edit); }
    }
}

// 使用游戏客户区坐标定位透明 EDIT，使输入法候选框靠近 CEF 当前插入点。
void ImeProxy::apply_bounds() {
    HWND edit = edit_.load(); if (!edit || !owner_) return;
    Json bounds; { std::lock_guard lock(mutex_); bounds = bounds_; }
    POINT point{bounds.value("x", 0), bounds.value("y", 0)}; ClientToScreen(owner_, &point);
    SetWindowPos(edit, HWND_TOP, point.x, point.y, std::max(1, bounds.value("width", 1)), std::max(1, bounds.value("height", 24)), SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

// 将输入法组合状态作为 CEF 消息发送；提交文本与组合文本分别处理，避免伪造 KEYEVENT_CHAR。
void ImeProxy::handle_ime(LPARAM flags) {
    HWND edit = edit_.load(); if (!edit || !sender_) return;
    if (flags & GCS_COMPSTR) {
        composition_active_ = true;
        sender_({{"type", "ime"}, {"action", "composition"}, {"text", composition(edit, false)}});
    }
    if (flags & GCS_RESULTSTR) {
        const auto committed = composition(edit, true);
        suppress_chars_ = std::min(64, static_cast<int>(to_wide(committed).size()));
        sender_({{"type", "ime"}, {"action", "commit"}, {"text", committed}});
        composition_active_ = false;
    }
}

// 输入法可能只发送 WM_IME_ENDCOMPOSITION 而没有结果文本；此时必须通知 CEF 清除残留组合状态。
void ImeProxy::handle_ime_end() {
    if (!composition_active_) return;
    composition_active_ = false;
    if (sender_) sender_({{"type", "ime"}, {"action", "cancel"}});
}

// 普通英文输入仍通过 CEF 键盘事件处理；IME 确认文本只能经 ImeCommitText 进入 CEF 一次。
void ImeProxy::send_key(UINT message, WPARAM key, LPARAM native) {
    if ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN) && (key == VK_F8 || key == VK_ESCAPE)) { if (hotkey_) hotkey_(static_cast<int>(key)); return; }
    // IMM 在 GCS_RESULTSTR 后通常投递 WM_IME_CHAR，部分布局还会再由 TranslateMessage 产生 WM_CHAR。
    // 前者若参与数量抵消，会令后者穿透到 CEF，与 ImeCommitText 形成重复提交；因此永不转发 WM_IME_CHAR。
    if (message == WM_IME_CHAR) return;
    // 仅屏蔽本次 IME 确认尾随的 WM_CHAR；英文输入没有先前确认文本，仍按普通键盘事件发送。
    if (message == WM_CHAR && suppress_chars_ > 0) { --suppress_chars_; return; }
    if (sender_) sender_({{"type", "key"}, {"key", static_cast<int>(key)}, {"native", static_cast<int>(native)},
        {"action", message == WM_KEYUP || message == WM_SYSKEYUP ? "up" : (message == WM_CHAR || message == WM_IME_CHAR) ? "char" : "down"}});
}

// EDIT 子类只处理 IME/键盘消息，其余行为保持标准控件原实现。
LRESULT CALLBACK ImeProxy::edit_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* self = instance_; if (!self || !self->original_) return DefWindowProcW(window, message, wparam, lparam);
    if (message == apply_message_) { self->apply_pending(); return 0; }
    LRESULT result = CallWindowProcW(self->original_, window, message, wparam, lparam);
    if (message == WM_IME_STARTCOMPOSITION) { self->composition_active_ = true; self->apply_bounds(); }
    else if (message == WM_IME_COMPOSITION) { self->handle_ime(lparam); self->apply_bounds(); }
    else if (message == WM_IME_ENDCOMPOSITION) self->handle_ime_end();
    else if (message == WM_IME_NOTIFY && (wparam == IMN_OPENCANDIDATE || wparam == IMN_CHANGECANDIDATE)) self->apply_bounds();
    else if (message == WM_KEYDOWN || message == WM_KEYUP || message == WM_SYSKEYDOWN || message == WM_SYSKEYUP || message == WM_CHAR || message == WM_IME_CHAR)
        self->send_key(message, wparam, lparam);
    return result;
}
} // namespace reff
