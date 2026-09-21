#pragma once

#include "protocol.hpp"
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace reff {

// M3 插件清单的受限表示：只保存 UI 和能力声明，不把任意 JSON 暴露给网页。
struct PluginManifest {
    std::string id;
    std::string name;
    // 本地化名称只保存经过校验的语言映射；Shell 根据全局语言选择并回退到默认名称。
    Json localized_name{Json::object()};
    std::string author;
    std::string version;
    std::string reff_api;
    std::vector<std::string> games;
    std::vector<std::string> methods;
    std::vector<std::string> events;
    std::string entry;
    std::string kind;
    // UI 承载模式：component 由 Shell 右侧渲染，isolated-page 使用独立页面入口。
    std::string ui_mode;
    std::string fallback;
    // schema 模式的受限 UI 声明；只保存已校验的对象，不执行其中任何代码。
    Json schema{Json::object()};
    std::filesystem::path root;
};

// 解析并校验单个 manifest；路径、版本和能力字段不合法时返回错误文本。
std::optional<PluginManifest> load_manifest(const std::filesystem::path& file, std::string& error);

// 扫描插件目录并按 ID 去重；失败清单被跳过并由调用者记录诊断，不阻断核心 UI。
std::vector<PluginManifest> scan_manifests(const std::filesystem::path& directory,
                                           std::vector<std::string>& errors,
                                           std::string_view game);

// 将经过校验的清单转换成前端可见的最小摘要，避免页面取得本地绝对路径。
Json manifest_summary(const PluginManifest& manifest);

} // namespace reff
