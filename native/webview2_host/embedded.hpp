#pragma once
#include <optional>
#include <string_view>

namespace reff {
// 官方资源来自当前 EXE 的 RCDATA；返回视图不复制、不解压，寿命与宿主模块相同。
std::optional<std::string_view> embedded_resource(std::string_view name);
}
