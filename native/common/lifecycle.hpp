#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>

namespace reff {

// 会话生命周期闸门：由宿主 UI 线程读取、IPC 线程推进关闭状态，禁止关闭后的迟到消息污染新会话。
// 资源所有权不属于闸门；调用者必须先调用 begin_close，再释放浏览器、管道和共享帧。
class SessionGate {
public:
    // 创建新会话并递增 epoch；旧会话的请求即使 ID 相同也会被拒绝。
    std::uint64_t open() {
        std::lock_guard lock(mutex_);
        closing_ = false;
        connected_ = false;
        return ++epoch_;
    }

    // 标记关闭屏障并撤销连接；返回值表示本次是否第一次进入关闭流程。
    bool begin_close() {
        std::lock_guard lock(mutex_);
        if (closing_) return false;
        closing_ = true;
        connected_ = false;
        ++epoch_;
        return true;
    }

    // 仅允许当前 epoch 的消息通过，调用者仍需进行消息类型和权限检查。
    bool accepts(std::uint64_t message_epoch) const {
        std::lock_guard lock(mutex_);
        return !closing_ && message_epoch == epoch_;
    }

    // 连接状态只由握手完成/断开边界修改，避免页面伪造 ready。
    void set_connected(bool connected) {
        std::lock_guard lock(mutex_);
        if (!closing_) connected_ = connected;
    }

    bool closing() const { std::lock_guard lock(mutex_); return closing_; }
    bool connected() const { std::lock_guard lock(mutex_); return connected_; }
    std::uint64_t epoch() const { std::lock_guard lock(mutex_); return epoch_; }

private:
    mutable std::mutex mutex_;
    std::uint64_t epoch_{};
    bool closing_{};
    bool connected_{};
};

// 线程安全的故障重启预算：一轮会话最多自动尝试有限次数，避免崩溃时忙循环拉起宿主。
class RestartBudget {
public:
    explicit RestartBudget(unsigned limit = 2) : limit_(limit) {}

    // 仅在用户仍保持面板可见时消费预算；隐藏后 reset 允许下一次显式打开重新尝试。
    bool consume() {
        auto current = used_.load(std::memory_order_relaxed);
        while (current < limit_ && !used_.compare_exchange_weak(current, current + 1,
                                                                  std::memory_order_acq_rel)) {}
        return current < limit_;
    }

    void reset() { used_.store(0, std::memory_order_release); }
    unsigned used() const { return used_.load(std::memory_order_acquire); }
    unsigned limit() const { return limit_; }

private:
    const unsigned limit_;
    std::atomic<unsigned> used_{};
};

} // namespace reff
