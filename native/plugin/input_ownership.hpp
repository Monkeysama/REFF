#pragma once
#include <Windows.h>
#include <array>
#include <cstdint>
#include <cstddef>
#include <initializer_list>
#include <optional>
#include <span>
#include <unordered_map>

namespace reff {

// 纯消息所有权状态机：仅由游戏窗口线程访问，不读取系统状态、不改写/注入消息。
// legacy 与 raw 是独立 lane；raw 还按设备区分。Lua 只能读调用者发布的原子摘要。
class InputOwnership {
public:
    enum class Lane : std::uint8_t { legacy, raw };
    enum class Kind : std::uint8_t { keyboard, mouse };
    enum class Owner : std::uint8_t { game, reff };
    // Win32 RAWKEYBOARD 约定的溢出码；不引入仅 WDK 提供的 ntddkbd.h。
    static constexpr USHORT overrun_make_code = 0xff;
    struct Key {
        Lane lane{};
        Kind kind{};
        std::uintptr_t device{};
        std::uint16_t code{}, scan{}, flags{};
        bool operator==(const Key&) const = default;
    };
    struct Event { Key key; bool up{}; };
    struct Packet {
        std::array<Event, 10> events{};
        std::size_t size{};
        std::span<const Event> view() const { return {events.data(), size}; }
        void add(Key key, bool up) { events[size++] = {key, up}; }
    };

    // 只预览已有 owner 的硬约束；新 down 尚未提交，以便拖动/快捷键的实际返回值决定 owner。
    // 一个 RAWMOUSE 无法逐事件返回 bool：game owner 或 unknown release 优先整包放行。
    std::optional<bool> forced_route(std::span<const Event> events) const {
        bool reff = false;
        for (const auto& event : events) {
            const auto found = owners_.find(event.key);
            if (found == owners_.end()) {
                if (event.up) {
                    // 同包 down+up 没有可恢复的时序，仍按未知 release 保守放行。
                    return true;
                }
            } else if (found->second == Owner::game) return true;
            else reff = true;
        }
        if (reff) return false;
        return std::nullopt;
    }

    // 提交真实整包选择：新 down 采用最终 owner；repeat 不重写 owner，up 后撤销。
    // 混包冲突可能让已有 REFF 事件随包放行，这是 bool ABI 的限制，不伪称逐事件隔离。
    bool route(std::span<const Event> events, bool default_game) {
        const bool game = forced_route(events).value_or(default_game);
        for (const auto& event : events) {
            if (event.up) owners_.erase(event.key);
            else owners_.try_emplace(event.key, game ? Owner::game : Owner::reff);
        }
        return game;
    }
    bool has_reff_owned() const {
        for (const auto& [key, owner] : owners_) if (owner == Owner::reff) return true;
        return false;
    }

    // TranslateMessage 派生字符不另建 held 项；仅沿用仍按住的 legacy 扫描码/扩展位 owner。
    // 字符值不是 VKey，不能拿它配对；IME/零扫描码或 up 后迟到字符无可靠物理归属。
    std::optional<bool> character_route(UINT message, LPARAM lparam) const {
        if (message != WM_CHAR && message != WM_SYSCHAR) return std::nullopt;
        const auto bits = static_cast<std::uintptr_t>(lparam);
        const auto scan = static_cast<std::uint16_t>((bits >> 16) & 0xff);
        const auto flags = static_cast<std::uint16_t>((bits >> 24) & 1);
        if (!scan) return std::nullopt;
        bool reff = false;
        for (const auto& [key, owner] : owners_) {
            if (key.lane != Lane::legacy || key.kind != Kind::keyboard || key.scan != scan || key.flags != flags) continue;
            if (owner == Owner::game) return true; // 扫描码歧义同样保守让游戏优先。
            reff = true;
        }
        if (reff) return false;
        return std::nullopt;
    }

    // legacy 键盘保留扫描码与扩展位，区分左右修饰键及主键盘/小键盘；bit30 不是身份。
    static Packet legacy(UINT message, WPARAM wparam, LPARAM lparam) {
        Packet packet;
        if (message == WM_KEYDOWN || message == WM_KEYUP || message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) {
            packet.add({Lane::legacy, Kind::keyboard, 0, static_cast<std::uint16_t>(wparam),
                static_cast<std::uint16_t>((static_cast<std::uintptr_t>(lparam) >> 16) & 0xff),
                static_cast<std::uint16_t>((static_cast<std::uintptr_t>(lparam) >> 24) & 1)},
                message == WM_KEYUP || message == WM_SYSKEYUP);
            return packet;
        }
        unsigned button = 0; bool up = false;
        switch (message) {
        case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: button = 1; break;
        case WM_LBUTTONUP: button = 1; up = true; break;
        case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: button = 2; break;
        case WM_RBUTTONUP: button = 2; up = true; break;
        case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK: button = 3; break;
        case WM_MBUTTONUP: button = 3; up = true; break;
        case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK: case WM_XBUTTONUP:
            if (HIWORD(wparam) != XBUTTON1 && HIWORD(wparam) != XBUTTON2) return packet;
            button = HIWORD(wparam) == XBUTTON1 ? 4 : 5; up = message == WM_XBUTTONUP; break;
        default: return packet;
        }
        packet.add({Lane::legacy, Kind::mouse, 0, static_cast<std::uint16_t>(button)}, up);
        return packet;
    }

    // RAWKEYBOARD 的 BREAK 决定方向，E0/E1 是身份而非方向；无效 VKey 不创建永久 held 项。
    static Packet raw_keyboard(const RAWKEYBOARD& keyboard, std::uintptr_t device = 0) {
        Packet packet;
        if (!keyboard.VKey || keyboard.VKey == 255 || keyboard.MakeCode == overrun_make_code) return packet;
        packet.add({Lane::raw, Kind::keyboard, device, keyboard.VKey, keyboard.MakeCode,
            static_cast<std::uint16_t>(keyboard.Flags & (RI_KEY_E0 | RI_KEY_E1))}, (keyboard.Flags & RI_KEY_BREAK) != 0);
        return packet;
    }

    // 一包最多五组按钮，滚轮/运动没有 held owner；绝不修改系统 RAWINPUT 包。
    static Packet raw_mouse(const RAWMOUSE& mouse, std::uintptr_t device = 0) {
        Packet packet;
        for (unsigned button = 0; button < 5; ++button) {
            const unsigned down = 1u << (button * 2), up = down << 1;
            const Key key{Lane::raw, Kind::mouse, device, static_cast<std::uint16_t>(button + 1)};
            if (mouse.usButtonFlags & down) packet.add(key, false);
            if (mouse.usButtonFlags & up) packet.add(key, true);
        }
        return packet;
    }
private:
    struct Hash {
        std::size_t operator()(const Key& key) const {
            std::size_t hash = key.device;
            for (auto part : {static_cast<unsigned>(key.lane), static_cast<unsigned>(key.kind),
                static_cast<unsigned>(key.code), static_cast<unsigned>(key.scan), static_cast<unsigned>(key.flags)})
                hash = (hash * 131) ^ part;
            return hash;
        }
    };
    std::unordered_map<Key, Owner, Hash> owners_;
};
} // namespace reff
