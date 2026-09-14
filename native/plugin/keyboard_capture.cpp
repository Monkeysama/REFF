#define DIRECTINPUT_VERSION 0x0800
#define INITGUID
#include <dinput.h>
#include "keyboard_capture.hpp"
#include <cstring>

namespace reff {

// 插件在 REFramework 完成自身 DirectInput Hook 后安装；保存的原入口会先进入其 trampoline 链，再返回本层过滤。
bool KeyboardCapture::install() {
    if (slot_) return true;
    const auto module = GetModuleHandleW(L"dinput8.dll");
    if (!module) return false;
    using DirectInput8CreateFunction = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
    const auto create = reinterpret_cast<DirectInput8CreateFunction>(GetProcAddress(module, "DirectInput8Create"));
    if (!create) return false;
    IDirectInput8W* input{};
    if (FAILED(create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W, reinterpret_cast<void**>(&input), nullptr)) || !input) return false;
    IDirectInputDevice8W* keyboard{};
    const auto created = input->CreateDevice(GUID_SysKeyboard, &keyboard, nullptr);
    input->Release();
    if (FAILED(created) || !keyboard) return false;
    auto** vtable = *reinterpret_cast<void***>(keyboard);
    void** slot = &vtable[9];
    auto original = reinterpret_cast<GetDeviceState>(*slot);
    DWORD old_protection{};
    if (!original || !VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old_protection)) { keyboard->Release(); return false; }
    instance_ = this;
    *slot = reinterpret_cast<void*>(&KeyboardCapture::get_device_state);
    DWORD ignored{};
    const bool restored = VirtualProtect(slot, sizeof(void*), old_protection, &ignored) != FALSE;
    keyboard->Release();
    if (!restored) {
        DWORD writable{};
        if (VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &writable)) {
            *slot = reinterpret_cast<void*>(original);
            VirtualProtect(slot, sizeof(void*), writable, &ignored);
        }
        instance_ = nullptr;
        return false;
    }
    original_ = original;
    slot_ = slot;
    return true;
}

// DirectInput 已完成设备读取后再清零游戏可见的键盘状态；F8/IME 仍由 Win32 与透明代理路径处理。
HRESULT WINAPI KeyboardCapture::get_device_state(IDirectInputDevice8W* device, DWORD size, LPVOID data) {
    auto* self = instance_;
    if (!self || !self->original_) return DIERR_NOTINITIALIZED;
    const auto result = self->original_(device, size, data);
    if (SUCCEEDED(result) && size == 256 && data && self->blocked_.load(std::memory_order_acquire)) std::memset(data, 0, size);
    return result;
}

} // namespace reff
