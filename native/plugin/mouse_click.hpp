#pragma once
#include <Windows.h>
#include <optional>

namespace reff {

// 将 Win32 单击/双击消息转换为 CEF 点击事件；对象只由游戏窗口线程持有和修改。
// Windows 会把快速第二次按下改成 WM_*BUTTONDBLCLK，因此必须把它作为按下事件转发，
// 并让随后抬起沿用相同 click_count，避免网页漏掉每组快速点击中的第二次操作。
class MouseClickTracker {
public:
    struct Event {
        int button{};
        bool mouse_up{};
        int click_count{1};
    };

    std::optional<Event> translate(UINT message) {
        switch (message) {
        case WM_LBUTTONDOWN: left_click_count_ = 1; return Event{0, false, 1};
        case WM_LBUTTONDBLCLK: left_click_count_ = 2; return Event{0, false, 2};
        case WM_LBUTTONUP: {
            const int count = left_click_count_;
            left_click_count_ = 1;
            return Event{0, true, count};
        }
        case WM_RBUTTONDOWN: right_click_count_ = 1; return Event{1, false, 1};
        case WM_RBUTTONDBLCLK: right_click_count_ = 2; return Event{1, false, 2};
        case WM_RBUTTONUP: {
            const int count = right_click_count_;
            right_click_count_ = 1;
            return Event{1, true, count};
        }
        default: return std::nullopt;
        }
    }

private:
    int left_click_count_{1};
    int right_click_count_{1};
};

}
