#pragma once

#include <atomic>
#include <windows.h>

struct IDirectInputDevice8W;

namespace reff {

// 已认证游戏的 DirectInput 键盘捕获层：只替换键盘设备共享 vtable 的 GetDeviceState 槽位，
// 原调用仍经过 REFramework 已安装的 Hook；返回后按状态清零 256 键快照，不接触鼠标设备或 CEF 进程。
class KeyboardCapture {
public:
    bool install();
    void set_blocked(bool value) { blocked_.store(value, std::memory_order_release); }
    bool installed() const { return slot_ != nullptr; }

private:
    using GetDeviceState = HRESULT(WINAPI*)(IDirectInputDevice8W*, DWORD, LPVOID);
    static HRESULT WINAPI get_device_state(IDirectInputDevice8W* device, DWORD size, LPVOID data);

    inline static KeyboardCapture* instance_{};
    std::atomic_bool blocked_{};
    GetDeviceState original_{};
    void** slot_{};
};

// 输入策略保持为可单测纯函数；文本焦点优先于用户的键盘穿透开关。
inline bool should_block_keyboard(bool visible, bool keyboard_passthrough, bool web_input_active) {
    return visible && (!keyboard_passthrough || web_input_active);
}

} // namespace reff
