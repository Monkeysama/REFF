#pragma once

namespace reff {

// 将 Win32 按键状态编码为 CEF event_flags_t；游戏窗口与 IME 代理必须使用同一位布局。
// 调用线程负责读取当前键盘状态，本函数不访问窗口、剪贴板或其他进程资源。
inline int cef_keyboard_modifiers(bool shift, bool control, bool alt, bool left_button = false, bool right_button = false) {
    int modifiers = 0;
    if (shift) modifiers |= 1 << 1;
    if (control) modifiers |= 1 << 2;
    if (alt) modifiers |= 1 << 3;
    if (left_button) modifiers |= 1 << 4;
    if (right_button) modifiers |= 1 << 6;
    return modifiers;
}

} // namespace reff
