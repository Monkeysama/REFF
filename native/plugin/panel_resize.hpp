#pragma once

#include <Windows.h>
#include <algorithm>
#include <mutex>

namespace reff {

// W2 缩放状态机；只计算候选矩形，不接触 CEF、输入消息或渲染资源。
// 调用线程通常是游戏窗口线程；快照读取可来自 Present 线程，内部互斥保护状态。
class PanelResizeController {
public:
    enum class InputMode { absolute, relative };
    struct Snapshot { bool active{}; int edges{}; RECT rect{}; InputMode mode{InputMode::absolute}; };

    // 鼠标按下时建立一次不可变起点；已有会话或非法命中不会改变状态。
    bool begin(RECT start, int edges, int anchor_x, int anchor_y, InputMode mode) {
        if (edges == 0 || start.right <= start.left || start.bottom <= start.top) return false;
        std::lock_guard lock(mutex_);
        if (state_.active) return false;
        state_ = {true, edges, start, mode}; anchor_x_ = anchor_x; anchor_y_ = anchor_y; start_ = start;
        return true;
    }

    // 只接受建立会话时锁定的输入模式，拒绝另一输入源的坐标，避免相对/绝对坐标互相覆盖。
    bool update(int x, int y, InputMode mode, RECT& result) {
        std::lock_guard lock(mutex_);
        if (!state_.active || state_.mode != mode) return false;
        const int dx = x - anchor_x_, dy = y - anchor_y_;
        RECT next = start_;
        constexpr int min_width = 480, min_height = 320;
        if (state_.edges & 1) next.left = std::min(start_.left + dx, start_.right - min_width);
        if (state_.edges & 2) next.right = std::max(start_.right + dx, next.left + min_width);
        if (state_.edges & 4) next.top = std::min(start_.top + dy, start_.bottom - min_height);
        if (state_.edges & 8) next.bottom = std::max(start_.bottom + dy, next.top + min_height);
        state_.rect = next; result = next;
        return true;
    }

    // 鼠标释放、失焦或关闭时结束会话；重复结束是幂等操作。
    bool end() {
        std::lock_guard lock(mutex_);
        const bool was_active = state_.active; state_ = {}; anchor_x_ = anchor_y_ = 0; start_ = {};
        return was_active;
    }

    Snapshot snapshot() const { std::lock_guard lock(mutex_); return state_; }

private:
    mutable std::mutex mutex_;
    Snapshot state_{};
    RECT start_{};
    int anchor_x_{}, anchor_y_{};
};
}
