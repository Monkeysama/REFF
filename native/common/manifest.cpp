#include "manifest.hpp"
#include <fstream>
#include <regex>
#include <unordered_set>

namespace reff {
namespace {
bool text(const Json& object, const char* key, std::string& value, std::size_t max) {
    if (!object.contains(key) || !object[key].is_string()) return false;
    value = object[key].get<std::string>();
    return !value.empty() && value.size() <= max;
}

bool string_array(const Json& object, const char* key, std::vector<std::string>& result) {
    if (!object.contains(key) || !object[key].is_array() || object[key].size() > 128) return false;
    for (const auto& item : object[key]) {
        if (!item.is_string() || item.get<std::string>().empty() || item.get<std::string>().size() > 128) return false;
        result.push_back(item.get<std::string>());
    }
    return true;
}

bool safe_relative(const std::string& value) {
    std::filesystem::path path = std::filesystem::u8path(value);
    return !value.empty() && !path.is_absolute() && value.find('\\') == std::string::npos &&
        value.find(':') == std::string::npos && value.find("..") == std::string::npos;
}

// 校验结构化表单白名单；schema 只描述控件，不允许脚本、HTML、外部资源或任意递归对象。
bool valid_schema(const Json& schema) {
    if (!schema.is_object() || schema.value("version", 0) != 1 ||
        !schema.value("title", std::string{}).size() || schema.value("title", std::string{}).size() > 128 ||
        !schema.contains("fields") || !schema["fields"].is_array() || schema["fields"].size() > 64) return false;
    for (const auto* key : {"load", "changeEvent"})
        if (schema.contains(key) && (!schema[key].is_string() || schema[key].get<std::string>().empty() || schema[key].get<std::string>().size() > 128)) return false;
    std::unordered_set<std::string> ids;
    for (const auto& field : schema["fields"]) {
        if (!field.is_object() || !field.contains("id") || !field["id"].is_string() ||
            !std::regex_match(field["id"].get<std::string>(), std::regex("[a-zA-Z][a-zA-Z0-9_.-]{0,63}")) ||
            !ids.insert(field["id"].get<std::string>()).second ||
            !field.contains("type") || !field["type"].is_string()) return false;
        const auto type = field["type"].get<std::string>();
        if (type != "switch" && type != "slider" && type != "number" && type != "select" && type != "text" && type != "button") return false;
        if (field.contains("label") && (!field["label"].is_string() || field["label"].get<std::string>().size() > 128)) return false;
        if (field.contains("readonly") && !field["readonly"].is_boolean()) return false;
        if (field.contains("action") && (!field["action"].is_string() || field["action"].get<std::string>().empty() || field["action"].get<std::string>().size() > 128)) return false;
        if ((type == "slider" || type == "number") &&
            ((!field.contains("min") || !field["min"].is_number()) || (!field.contains("max") || !field["max"].is_number()) ||
             field["min"].get<double>() > field["max"].get<double>())) return false;
        if (type == "select" && (!field.contains("options") || !field["options"].is_array() || field["options"].empty() || field["options"].size() > 64)) return false;
        if (type == "select") for (const auto& option : field["options"]) {
            if (!option.is_object() || !option.value("value", Json()).is_string() || !option.value("label", Json()).is_string() ||
                option.value("value", std::string{}).size() > 128 || option.value("label", std::string{}).size() > 128) return false;
        }
    }
    return true;
}
} // namespace

// 读取 UTF-8 manifest 并执行字段白名单校验；不执行插件代码，也不解析外部资源。
std::optional<PluginManifest> load_manifest(const std::filesystem::path& file, std::string& error) {
    try {
        std::ifstream stream(file, std::ios::binary);
        if (!stream) { error = "无法打开 manifest"; return std::nullopt; }
        Json object; stream >> object;
        if (!object.is_object() || object.value("manifestVersion", 0) != 1) { error = "manifestVersion 不支持"; return std::nullopt; }
        PluginManifest result; result.root = file.parent_path();
        if (!text(object, "id", result.id, 96) || !std::regex_match(result.id, std::regex("[a-z0-9]+([.-][a-z0-9]+)*"))) { error = "id 不合法"; return std::nullopt; }
        // reff.* 由内置系统插件保留，第三方清单不能伪装成设置页或取得 Core 排序与权限。
        if (result.id.starts_with("reff.")) { error = "reff.* 为系统插件保留命名空间"; return std::nullopt; }
        if (!text(object, "name", result.name, 128) || !text(object, "version", result.version, 32) ||
            !text(object, "reffApi", result.reff_api, 64)) { error = "基本字段缺失"; return std::nullopt; }
        if (!object.contains("games") || !string_array(object, "games", result.games) || result.games.empty()) { error = "games 不合法"; return std::nullopt; }
        if (!object.contains("methods") || !string_array(object, "methods", result.methods)) { error = "methods 不合法"; return std::nullopt; }
        if (object.contains("events") && !string_array(object, "events", result.events)) { error = "events 不合法"; return std::nullopt; }
        const auto ui = object.value("ui", Json::object());
        if (!ui.is_object() || !text(ui, "entry", result.entry, 256) || !safe_relative(result.entry)) { error = "ui.entry 不合法"; return std::nullopt; }
        result.kind = ui.value("kind", "page");
        if (result.kind != "page" && result.kind != "schema") { error = "ui.kind 不支持"; return std::nullopt; }
        result.ui_mode = ui.value("mode", result.kind == "schema" ? "component" : "isolated-page");
        if (result.ui_mode != "component" && result.ui_mode != "isolated-page") { error = "ui.mode 不支持"; return std::nullopt; }
        if (ui.contains("schema")) {
            if (!valid_schema(ui["schema"])) { error = "ui.schema 不合法"; return std::nullopt; }
            result.schema = ui["schema"];
        } else if (result.kind == "schema") {
            error = "schema 插件缺少 ui.schema"; return std::nullopt;
        }
        if (!result.schema.empty()) {
            const auto declared_method = [&](const std::string& name) {
                return std::find(result.methods.begin(), result.methods.end(), name) != result.methods.end();
            };
            const auto declared_event = [&](const std::string& name) {
                return std::find(result.events.begin(), result.events.end(), name) != result.events.end();
            };
            if (result.schema.contains("load") && !declared_method(result.schema["load"].get<std::string>())) {
                error = "ui.schema.load 未在 methods 声明"; return std::nullopt;
            }
            if (result.schema.contains("changeEvent") && !declared_event(result.schema["changeEvent"].get<std::string>())) {
                error = "ui.schema.changeEvent 未在 events 声明"; return std::nullopt;
            }
            for (const auto& field : result.schema["fields"]) {
                if (field.contains("action") && !declared_method(field["action"].get<std::string>())) {
                    error = "ui.schema.action 未在 methods 声明"; return std::nullopt;
                }
            }
        }
        result.fallback = object.value("fallback", "none");
        if (result.fallback != "none" && result.fallback != "plugin-managed") { error = "fallback 不支持"; return std::nullopt; }
        return result;
    } catch (const std::exception& exception) { error = exception.what(); return std::nullopt; }
}

// 扫描目录时只接受 manifest.json，重复 ID 和非当前游戏清单均不会注册。
std::vector<PluginManifest> scan_manifests(const std::filesystem::path& directory,
                                           std::vector<std::string>& errors, std::string_view game) {
    std::vector<PluginManifest> result; std::unordered_set<std::string> ids;
    std::error_code code;
    if (!std::filesystem::exists(directory, code)) return result;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory, code)) {
        if (code || !entry.is_regular_file() || entry.path().filename() != "manifest.json") continue;
        std::string error; auto manifest = load_manifest(entry.path(), error);
        if (!manifest) { errors.push_back(entry.path().string() + ": " + error); continue; }
        const bool all_games = std::find(manifest->games.begin(), manifest->games.end(), "*") != manifest->games.end();
        if (!all_games && std::find(manifest->games.begin(), manifest->games.end(), game) == manifest->games.end()) continue;
        if (!ids.insert(manifest->id).second) { errors.push_back(entry.path().string() + ": 重复插件 ID"); continue; }
        result.push_back(std::move(*manifest));
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    return result;
}

// 前端只获得经过清洗的公开字段，entry 仍需由宿主按 root 重新解析。
Json manifest_summary(const PluginManifest& manifest) {
    return {{"id", manifest.id}, {"name", manifest.name}, {"version", manifest.version},
            {"kind", manifest.kind}, {"mode", manifest.ui_mode}, {"entry", manifest.entry}, {"methods", manifest.methods},
            {"events", manifest.events}, {"fallback", manifest.fallback},
            {"schema", manifest.schema},
            {"url", "reff://plugin/" + manifest.id + "/" + manifest.entry}};
}
} // namespace reff
