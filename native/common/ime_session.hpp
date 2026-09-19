#pragma once

#include <string_view>

namespace reff {

// IME 会话状态判断：仅新输入框首次激活需要预置 CEF 逻辑焦点；同一输入框的重复坐标上报不能抢走透明代理焦点。
// 本函数无资源所有权，可在 CEF UI 线程使用并由独立测试覆盖。
inline bool starts_new_ime_session(std::string_view current_input, std::string_view requested_input, bool active) {
    return active && !requested_input.empty() && current_input != requested_input;
}

} // namespace reff
