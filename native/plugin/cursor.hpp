#pragma once

#include <algorithm>
#include <utility>
#include <cstdint>
#include <cmath>

namespace reff {
// 光标显示计数租约：调用者必须始终位于游戏窗口线程，只归还 REFF 自己增加的计数。
// ShowCursor 是增减计数 API，不能把它当成无副作用的布尔开关反复调用。
class CursorVisibility {
public:
    // 打开时补足负计数；已可见时立即归还探测增量，反复同步不会累计计数。
    template<class ShowCursorFn>
    void synchronize(bool visible, ShowCursorFn show_cursor) {
        if (!visible) {
            while (increments_ > 0) { show_cursor(false); --increments_; }
            active_ = false;
            return;
        }
        active_ = true;
        if (increments_ >= max_increments) return;
        int count = show_cursor(true);
        if (count > 0) {
            show_cursor(false);
            return;
        }
        ++increments_;
        while (count < 0 && increments_ < max_increments) {
            count = show_cursor(true);
            ++increments_;
        }
    }

    bool active() const { return active_; }
    int owned_increments() const { return increments_; }

    // 虚拟光标模式下隐藏系统指针；只撤销本对象创建的负计数，不影响游戏自身的计数。
    template<class ShowCursorFn>
    void synchronize_hidden(bool hidden, ShowCursorFn show_cursor) {
        if (!hidden) {
            while (decrements_ > 0) { show_cursor(true); --decrements_; }
            hidden_ = false;
            return;
        }
        hidden_ = true;
        if (decrements_ >= max_increments) return;
        int count = show_cursor(false);
        if (count < 0) {
            // 返回 -1 表示调用前为 0：本次确实刚把可见指针隐藏，必须记为自己的租约。
            // 小于 -1 才表示原本已经隐藏，此时归还探测调用，避免关面板时错误显示游戏光标。
            if (count == -1) { ++decrements_; return; }
            show_cursor(true);
            return;
        }
        ++decrements_;
        while (count >= 0 && decrements_ < max_increments) {
            count = show_cursor(false);
            ++decrements_;
        }
    }

    bool hidden() const { return hidden_; }

private:
    // 防止其他组件持续错误递减时无限补偿；常见隐藏状态只需补一次。
    static constexpr int max_increments = 32;
    int increments_{};
    int decrements_{};
    bool active_{};
    bool hidden_{};
};

// 游戏客户区虚拟光标：只保存游戏客户区坐标，由游戏窗口线程通过 Raw Input 更新。
// 它不调用 SetCursorPos，因此游玩状态的物理指针锁定不会改变逻辑位置；CEF 坐标在使用时再换算。
class VirtualCursor {
public:
    // 分辨率、窗口模式切换后调用。保留已有位置并裁剪，避免光标跳回固定的网页中心。
    void set_bounds(int width, int height) {
        width_ = std::max(1, width); height_ = std::max(1, height);
        x_ = std::clamp(x_, 0, width_ - 1); y_ = std::clamp(y_, 0, height_ - 1);
    }
    // 仅在首次打开且无法取得物理光标时使用的回退位置。
    void reset() { x_ = width_ / 2; y_ = height_ / 2; }
    // 面板打开时以真实光标的游戏客户区位置建立初始位置，使菜单状态下两个光标重合。
    bool set_position(int x, int y) { return set(x, y); }
    bool move_relative(int delta_x, int delta_y) { return set(x_ + delta_x, y_ + delta_y); }
    // Raw Input 的绝对坐标范围为 [0, 65535]；右下边界需映射到最后一个有效逻辑像素。
    bool move_absolute(unsigned int raw_x, unsigned int raw_y) {
        return set(static_cast<int>((static_cast<unsigned long long>(raw_x) * width_) / 65536ULL),
                   static_cast<int>((static_cast<unsigned long long>(raw_y) * height_) / 65536ULL));
    }
    std::pair<int, int> position() const { return {x_, y_}; }
    std::pair<int, int> bounds() const { return {width_, height_}; }

private:
    bool set(int x, int y) {
        const int next_x = std::clamp(x, 0, width_ - 1);
        const int next_y = std::clamp(y, 0, height_ - 1);
        const bool changed = next_x != x_ || next_y != y_;
        x_ = next_x; y_ = next_y;
        return changed;
    }

    int width_{1440};
    int height_{900};
    int x_{720};
    int y_{450};
};

// 窗口线程独占的输入选择器：自由指针以系统位置为准，避免原始设备增量绕过 Windows 加速度后累计漂移。
// 连续回中心证据才启用相对模式；持续离开中心 150ms 后回到系统位置。算法不移动或限制系统指针。
class CursorInput {
public:
    VirtualCursor cursor;
    void begin(int width, int height, int x, int y) {
        cursor.set_bounds(width, height); cursor.set_position(x, y);
        relative_ = false; centered_ = 0; distance_ = 0; away_since_ = 0;
    }
    bool relative() const { return relative_; }
    // 系统事件只在自由模式更新位置；回中心引发的 WM_MOUSEMOVE 不得覆盖相对模式坐标。
    void system_position(int x, int y) { if (!relative_) cursor.set_position(x, y); }
    void raw(int dx, int dy, int system_x, int system_y, std::uint64_t now) {
        if (!dx && !dy) return;
        const auto [width, height] = cursor.bounds();
        const bool center = std::abs(system_x - width / 2) <= 2 && std::abs(system_y - height / 2) <= 2;
        if (!relative_) {
            if (center) {
                if (!centered_) { candidate_x_ = system_x; candidate_y_ = system_y; }
                candidate_x_ += dx; candidate_y_ += dy;
                distance_ += std::abs(double(dx)) + std::abs(double(dy));
                if (++centered_ >= 3 && distance_ >= 12) {
                    relative_ = true; cursor.set_position(candidate_x_, candidate_y_); return;
                }
            } else { centered_ = 0; distance_ = 0; }
            cursor.set_position(system_x, system_y);
        } else {
            cursor.move_relative(dx, dy);
            const bool away = std::abs(system_x - width / 2) > 32 || std::abs(system_y - height / 2) > 32;
            if (!away) away_since_ = 0;
            else if (!away_since_) away_since_ = now;
            else if (now - away_since_ >= 150) {
                relative_ = false; centered_ = 0; distance_ = 0; away_since_ = 0;
                cursor.set_position(system_x, system_y);
            }
        }
    }
private:
    bool relative_{};
    int centered_{}, candidate_x_{}, candidate_y_{};
    double distance_{};
    std::uint64_t away_since_{};
};

// 客户区与交换链像素可不相同；输入命中、箭头位置及大小必须使用同一比例，不能直接混用两种像素。
inline std::pair<float, float> client_to_render(int x, int y, int client_width, int client_height, int render_width, int render_height) {
    return {float(x) * render_width / std::max(1, client_width), float(y) * render_height / std::max(1, client_height)};
}
}
