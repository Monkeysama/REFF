#pragma once
#include <json.hpp>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace reff {
using Json = nlohmann::json;
inline constexpr std::uint32_t protocol_version = 1;
inline constexpr std::size_t max_message = 256 * 1024;
inline constexpr std::size_t queue_limit = 128;
inline constexpr int panel_width = 960;
inline constexpr int panel_height = 640;
// W2 动态视口的首版上限，覆盖当前 Wilds 2560x1440 测试环境，避免共享区随拖动反复重建。
inline constexpr int frame_max_width = 2560;
inline constexpr int frame_max_height = 1440;

// 解析任意分片的长度前缀消息；不合法长度立即中止连接，防止无限分配。
class Decoder {
public:
    std::vector<Json> feed(const char* data, std::size_t length) {
        std::vector<Json> result;
        for (std::size_t i = 0; i < length; ++i) {
            bytes_.push_back(data[i]);
            if (bytes_.size() == 4) {
                wanted_ = 0;
                for (int j = 0; j != 4; ++j)
                    wanted_ |= std::uint32_t(static_cast<unsigned char>(bytes_[j])) << (8 * j);
                if (wanted_ == 0 || wanted_ > max_message) throw std::runtime_error("invalid frame length");
            }
            if (wanted_ && bytes_.size() == wanted_ + 4) {
                result.push_back(Json::parse(bytes_.begin() + 4, bytes_.end()));
                bytes_.clear(); wanted_ = 0;
            }
        }
        return result;
    }
private:
    std::vector<char> bytes_;
    std::uint32_t wanted_{};
};

// 编码控制消息；画面数据只能走共享内存，不能通过此通道传输。
inline std::string encode(const Json& value) {
    auto body = value.dump();
    if (body.empty() || body.size() > max_message) throw std::runtime_error("message too large");
    std::string result(4, '\0');
    for (int i = 0; i != 4; ++i) result[i] = char((body.size() >> (8 * i)) & 255);
    return result + body;
}

// 有界跨线程队列：写入失败交由调用者报告，禁止无声丢弃已接受的业务请求。
template<class T> class Queue {
public:
    bool push(T item) {
        std::lock_guard lock(mutex_);
        if (items_.size() >= queue_limit) return false;
        items_.push_back(std::move(item)); return true;
    }
    std::optional<T> pop() {
        std::lock_guard lock(mutex_);
        if (items_.empty()) return {};
        T result = std::move(items_.front()); items_.pop_front(); return result;
    }
    // 替换尚未发送的旧状态消息；适用于 viewport 这类只关心最新值的控制状态。
    bool replace(T item) {
        std::lock_guard lock(mutex_);
        items_.clear(); items_.push_back(std::move(item)); return true;
    }
    void clear() { std::lock_guard lock(mutex_); items_.clear(); }
private:
    std::mutex mutex_;
    std::deque<T> items_;
};

// 校验方法名的结构，不在原生层维护插件白名单；Lua 注册表负责最终的方法归属和权限判断。
inline bool valid_name(std::string_view value, std::size_t max = 96) {
    if (value.empty() || value.size() > max || value.front() == '.' || value.front() == '-' ||
        value.back() == '.' || value.back() == '-' || value.find("..") != std::string_view::npos ||
        value.find("--") != std::string_view::npos) return false;
    for (const char character : value)
        if (!((character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '.' || character == '-')) return false;
    return true;
}

// 原生层只接受结构合法的请求；未知方法交给 Lua 返回 METHOD_NOT_FOUND，避免 C++ 白名单与 manifest 重复维护。
inline bool valid_request(const Json& value) {
    if (!value.is_object() || !value.contains("id") || !value["id"].is_string() ||
        !value.contains("method") || !value["method"].is_string() ||
        !value.contains("params") || !value["params"].is_object()) return false;
    auto id = value["id"].get<std::string>();
    auto method = value["method"].get<std::string>();
    if (id.empty() || id.size() > 64 || !valid_name(method, 128)) return false;
    if (value.contains("pluginId") && (!value["pluginId"].is_string() || !valid_name(value["pluginId"].get<std::string>()))) return false;
    return true;
}

// 保留请求身份的错误响应，网页可以匹配自己的 Promise。
inline Json failure(const Json& request, const std::string& code, const std::string& message) {
    return {{"type", "response"}, {"id", request.value("id", std::string{})},
            {"error", {{"code", code}, {"message", message}}}};
}
}
