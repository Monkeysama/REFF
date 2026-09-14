#pragma once
#include "protocol.hpp"

namespace reff {
// 宿主 UI 线程持有页面身份；frame 标识来自 CEF，generation 区分同一 frame 的不同文档。
// Shell 是框架自有页面，不冒充插件；插件身份将在独立入口加载时由 manifest 分配。
struct PageIdentity {
    int browser_id{};
    std::string frame_id;
    std::uint64_t generation{};
    std::string page_id;
    // 独立插件页由宿主从 manifest 绑定身份；Shell 页保持空值。
    std::string plugin_id;

    bool matches(int browser, std::string_view frame, std::uint64_t document) const {
        return !page_id.empty() && browser_id == browser && frame_id == frame && generation == document;
    }
};

// 网页只能提交方法和参数；拒绝调用者提供的身份/路由字段，避免影响下游授权。
inline bool valid_page_request(const Json& value) {
    if (!value.is_object() || value.size() != 2 || !value.contains("method") ||
        !value["method"].is_string() || !value.contains("params") || !value["params"].is_object()) return false;
    return valid_name(value["method"].get<std::string>(), 128);
}
}
