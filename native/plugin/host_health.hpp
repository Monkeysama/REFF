#pragma once
#include <cstdint>

namespace reff {
// 服务线程独占的宿主监控：握手最多等待 15 秒，已建立的连接断开立即结束会话。
// 不自动重试；用户下一次 F8 才创建新进程，避免故障循环和写请求重放。
class HostHealth {
public:
    void started(std::uint64_t now) { started_at_ = now; }
    const char* failure(std::uint64_t now, bool exited, bool handshake, bool pipe) const {
        if (exited) return "process exited";
        if (handshake && !pipe) return "IPC disconnected";
        if (!handshake && now - started_at_ >= 15000) return "handshake timed out";
        return nullptr;
    }
private:
    std::uint64_t started_at_{};
};
}
