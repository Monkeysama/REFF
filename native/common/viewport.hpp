#pragma once
#include "protocol.hpp"

namespace reff {
// 一代只对应一个完整尺寸；跨线程读取、发布必须通过 ViewportState，不拆分读取字段。
struct ViewportSnapshot {
    int width = panel_width, height = panel_height;
    std::uint64_t generation = 1;
    Json message() const {
        return {{"type", "viewport"}, {"width", width}, {"height", height}, {"generation", generation}};
    }
};

// 短临界区只覆盖内存快照及非阻塞入队；调用者不得在 send 内重入此状态或等待 IPC。
class ViewportState {
public:
    ViewportSnapshot snapshot() const {
        std::lock_guard lock(mutex_);
        return current_;
    }
    template<class Send> bool update(int width, int height, Send send) {
        std::lock_guard lock(mutex_);
        if (width == current_.width && height == current_.height) return false;
        return publish({width, height, current_.generation + 1}, send);
    }
    // 显示恢复时重新确认当前尺寸；不篡改宽高，以新代次要求浏览器交付本轮重排帧。
    template<class Send> bool refresh(Send send) {
        std::lock_guard lock(mutex_);
        return publish({current_.width, current_.height, current_.generation + 1}, send);
    }
private:
    template<class Send> bool publish(ViewportSnapshot next, Send send) {
        if (!send(next)) return false;
        current_ = next;
        return true;
    }
    mutable std::mutex mutex_;
    ViewportSnapshot current_;
};
}
