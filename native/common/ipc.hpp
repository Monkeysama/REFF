#pragma once
#include "protocol.hpp"
#include <Windows.h>
#include <atomic>
#include <functional>
#include <thread>

namespace reff {
// Windows 句柄只由拥有它的对象关闭，禁止复制所有权。
class Handle {
public:
    explicit Handle(HANDLE value = nullptr) : value_(value) {}
    ~Handle() { reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE get() const { return value_; }
    explicit operator bool() const { return value_ && value_ != INVALID_HANDLE_VALUE; }
    void reset(HANDLE value = nullptr) { if (*this) CloseHandle(value_); value_ = value; }
private:
    HANDLE value_{};
};

// 为本机当前用户建立对象 ACL，禁止使用 Everyone 可访问的 IPC 对象。
class UserSecurity {
public:
    UserSecurity();
    ~UserSecurity();
    SECURITY_ATTRIBUTES* get() { return &attributes_; }
private:
    SECURITY_ATTRIBUTES attributes_{sizeof(SECURITY_ATTRIBUTES), nullptr, FALSE};
};

std::wstring widen(const std::string& value);
std::string narrow(const std::wstring& value);
std::wstring make_session();

// 独立工作线程拥有管道，游戏/浏览器线程只向有界队列投递消息。
class Channel {
public:
    using Receiver = std::function<void(Json)>;
    Channel() = default;
    ~Channel();
    void start(const std::wstring& session, bool server, Receiver receiver);
    bool send(Json value);
    // 高优先级控制消息独立于普通输入队列，避免鼠标事件洪水阻塞 viewport/显隐状态。
    bool send_priority(Json value);
    // 仅保留最新控制消息，避免连续缩放时旧 viewport 请求排队。
    bool send_priority_latest(Json value);
    void stop();
    bool connected() const { return connected_.load(); }
private:
    void run(std::stop_token stop, std::wstring session, bool server, Receiver receiver);
    Queue<Json> outgoing_;
    Queue<Json> priority_outgoing_;
    std::jthread thread_;
    std::atomic_bool connected_{};
};
}
