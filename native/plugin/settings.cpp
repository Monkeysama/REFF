#include "settings.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <regex>
#include <windows.h>

namespace reff {
namespace {

constexpr int min_panel_width = 320;
constexpr int min_panel_height = 240;

bool color(const Json& value) {
    return value.is_string() && std::regex_match(value.get<std::string>(), std::regex("#[0-9a-fA-F]{6}"));
}

bool finite_number(const Json& value, double minimum, double maximum) {
    return value.is_number() && std::isfinite(value.get<double>()) && value.get<double>() >= minimum && value.get<double>() <= maximum;
}

Json preset(std::string_view name) {
    if (name == "ocean") return {{"preset", "ocean"}, {"accent", "#5DADE2"}, {"background", "#101820"}, {"surface", "#17232D"}};
    if (name == "violet") return {{"preset", "violet"}, {"accent", "#A78BFA"}, {"background", "#15141B"}, {"surface", "#211F2A"}};
    if (name == "amber") return {{"preset", "amber"}, {"accent", "#F2B84B"}, {"background", "#181613"}, {"surface", "#252119"}};
    return {{"preset", "emerald"}, {"accent", "#67D3B2"}, {"background", "#10151E"}, {"surface", "#151D28"}};
}

bool known_keys(const Json& object, std::initializer_list<std::string_view> keys) {
    if (!object.is_object()) return false;
    for (const auto& [key, value] : object.items()) {
        (void)value;
        if (std::find(keys.begin(), keys.end(), key) == keys.end()) return false;
    }
    return true;
}

} // namespace

// 默认配置偏向清晰可读和不影响游戏操作；透明度仍保留足够对比度。
Json SettingsStore::defaults() {
    Json appearance = preset("emerald");
    appearance["backgroundOpacity"] = 0.94;
    appearance["cornerRadius"] = 6;
    appearance["surfaceBlur"] = 8;
    appearance["textScale"] = 1.0;
    return {{"schemaVersion", 1}, {"language", "zh-CN"}, {"hotkey", {{"key", VK_F8}, {"modifiers", 0}}}, {"appearance", appearance},
            {"startup", {{"preload", true}}},
            {"input", {{"mousePassthrough", false}, {"keyboardPassthrough", false}}},
            {"window", {{"rememberGeometry", true}}}};
}

// 完整文档校验用于读取磁盘配置；字段缺失按默认值补齐，未知或越界字段拒绝加载。
bool SettingsStore::validate_document(Json& value, std::string& error) {
    if (!known_keys(value, {"schemaVersion", "language", "hotkey", "appearance", "startup", "input", "window"}) || value.value("schemaVersion", 0) != 1) {
        error = "配置版本或顶层字段不受支持"; return false;
    }
    const auto language = value.value("language", std::string{});
    if (language != "zh-CN" && language != "en-US") { error = "language 不受支持"; return false; }
    // 旧配置没有 hotkey 时补回 F8；绑定只保存 Windows 虚拟键码和四种修饰键掩码。
    if (!value.contains("hotkey")) value["hotkey"] = defaults()["hotkey"];
    if (!known_keys(value["hotkey"], {"key", "modifiers"}) ||
        !value["hotkey"].value("key", Json()).is_number_integer() ||
        !value["hotkey"].value("modifiers", Json()).is_number_integer() ||
        value["hotkey"].value("key", 0) < 1 || value["hotkey"].value("key", 0) > 255 ||
        value["hotkey"].value("modifiers", -1) < 0 || value["hotkey"].value("modifiers", 16) > 15) {
        error = "快捷键绑定不合法"; return false;
    }
    // 早期配置没有启动分组；升级时默认开启延迟预热，不改变 schemaVersion。
    if (!value.contains("startup")) value["startup"] = defaults()["startup"];
    if (!known_keys(value["startup"], {"preload"}) || !value["startup"].value("preload", Json()).is_boolean()) {
        error = "startup 字段不合法"; return false;
    }
    if (!value.contains("appearance") || !known_keys(value["appearance"], {"preset", "accent", "background", "surface", "backgroundOpacity", "cornerRadius", "surfaceBlur", "textScale"})) {
        error = "appearance 字段不合法"; return false;
    }
    auto& appearance = value["appearance"];
    // 向后兼容 D1.5 首轮配置；新字段缺失时补入默认文字比例再继续校验。
    if (!appearance.contains("textScale")) appearance["textScale"] = 1.0;
    const auto preset_name = appearance.value("preset", std::string{});
    if (preset_name != "emerald" && preset_name != "ocean" && preset_name != "violet" && preset_name != "amber" && preset_name != "custom") {
        error = "主题预设不受支持"; return false;
    }
    if (!color(appearance.value("accent", Json())) || !color(appearance.value("background", Json())) || !color(appearance.value("surface", Json())) ||
        !finite_number(appearance.value("backgroundOpacity", Json()), 0.55, 1.0) ||
        !finite_number(appearance.value("cornerRadius", Json()), 0, 16) || !finite_number(appearance.value("surfaceBlur", Json()), 0, 32) ||
        !finite_number(appearance.value("textScale", Json()), 0.8, 1.4)) {
        error = "外观参数超出允许范围"; return false;
    }
    if (!value.contains("input") || !known_keys(value["input"], {"mousePassthrough", "keyboardPassthrough"}) ||
        !value["input"].value("mousePassthrough", Json()).is_boolean() || !value["input"].value("keyboardPassthrough", Json()).is_boolean()) {
        error = "input 字段不合法"; return false;
    }
    if (!value.contains("window") || !known_keys(value["window"], {"rememberGeometry", "geometry"}) ||
        !value["window"].value("rememberGeometry", Json()).is_boolean()) {
        error = "window 字段不合法"; return false;
    }
    if (value["window"].contains("geometry")) {
        const auto& geometry = value["window"]["geometry"];
        if (!known_keys(geometry, {"clientWidth", "clientHeight", "left", "top", "width", "height"})) { error = "窗口几何字段不合法"; return false; }
        for (const auto* key : {"clientWidth", "clientHeight", "left", "top", "width", "height"})
            if (!geometry.contains(key) || !geometry[key].is_number_integer()) { error = "窗口几何值不合法"; return false; }
        if (geometry["clientWidth"].get<int>() < 1 || geometry["clientHeight"].get<int>() < 1 ||
            geometry["width"].get<int>() < min_panel_width || geometry["height"].get<int>() < min_panel_height) {
            error = "窗口几何尺寸不合法"; return false;
        }
    }
    return true;
}

// 初始化时只读取一次配置；损坏文件回退默认值并保留原文件，等待用户下次保存时替换。
void SettingsStore::open(std::filesystem::path path, std::string& warning) {
    std::lock_guard lock(mutex_);
    path_ = std::move(path);
    value_ = defaults();
    try {
        if (!std::filesystem::exists(path_)) return;
        std::ifstream stream(path_, std::ios::binary);
        Json candidate; stream >> candidate;
        std::string error;
        if (!stream || !validate_document(candidate, error)) { warning = error.empty() ? "无法读取配置" : error; return; }
        value_ = std::move(candidate);
    } catch (const std::exception& exception) { warning = exception.what(); }
}

Json SettingsStore::snapshot() const {
    std::lock_guard lock(mutex_);
    return value_;
}

// 设置页只提交差量对象；每个分组独立合并，禁止通过配置接口改写内部窗口几何。
bool SettingsStore::update(const Json& patch, std::string& error) {
    if (!known_keys(patch, {"language", "hotkey", "appearance", "startup", "input", "window"})) { error = "设置补丁包含未知字段"; return false; }
    std::lock_guard lock(mutex_);
    Json candidate = value_;
    if (patch.contains("language")) candidate["language"] = patch["language"];
    if (patch.contains("hotkey")) {
        if (!known_keys(patch["hotkey"], {"key", "modifiers"})) { error = "快捷键补丁不合法"; return false; }
        for (const auto* key : {"key", "modifiers"}) if (!patch["hotkey"].contains(key) || !patch["hotkey"][key].is_number_integer()) {
            error = "快捷键补丁不合法"; return false;
        }
        candidate["hotkey"] = patch["hotkey"];
    }
    if (patch.contains("appearance")) {
        if (!known_keys(patch["appearance"], {"preset", "accent", "background", "surface", "backgroundOpacity", "cornerRadius", "surfaceBlur", "textScale"})) {
            error = "appearance 补丁不合法"; return false;
        }
        const auto& incoming = patch["appearance"];
        if (incoming.contains("preset")) {
            if (!incoming["preset"].is_string()) { error = "主题预设不合法"; return false; }
            const auto name = incoming["preset"].get<std::string>();
            if (name == "emerald" || name == "ocean" || name == "violet" || name == "amber") {
                auto visual = preset(name);
                for (const auto& [key, value] : visual.items()) candidate["appearance"][key] = value;
            } else if (name == "custom") candidate["appearance"]["preset"] = "custom";
            else { error = "主题预设不受支持"; return false; }
        }
        for (const auto* key : {"accent", "background", "surface", "backgroundOpacity", "cornerRadius", "surfaceBlur", "textScale"}) {
            if (!incoming.contains(key)) continue;
            candidate["appearance"][key] = incoming[key];
            if (std::string_view(key) == "accent" || std::string_view(key) == "background" || std::string_view(key) == "surface")
                candidate["appearance"]["preset"] = "custom";
        }
    }
    if (patch.contains("startup")) {
        if (!known_keys(patch["startup"], {"preload"}) ||
            (patch["startup"].contains("preload") && !patch["startup"]["preload"].is_boolean())) {
            error = "startup 补丁不合法"; return false;
        }
        if (patch["startup"].contains("preload")) candidate["startup"]["preload"] = patch["startup"]["preload"];
    }
    if (patch.contains("input")) {
        if (!known_keys(patch["input"], {"mousePassthrough", "keyboardPassthrough"})) { error = "input 补丁不合法"; return false; }
        for (const auto* key : {"mousePassthrough", "keyboardPassthrough"}) if (patch["input"].contains(key)) candidate["input"][key] = patch["input"][key];
    }
    if (patch.contains("window")) {
        if (!known_keys(patch["window"], {"rememberGeometry"})) { error = "window 补丁不合法"; return false; }
        if (patch["window"].contains("rememberGeometry")) {
            candidate["window"]["rememberGeometry"] = patch["window"]["rememberGeometry"];
            if (!patch["window"]["rememberGeometry"].is_boolean() || !patch["window"]["rememberGeometry"].get<bool>()) candidate["window"].erase("geometry");
        }
    }
    if (!validate_document(candidate, error)) return false;
    value_ = std::move(candidate);
    return true;
}

void SettingsStore::reset() {
    std::lock_guard lock(mutex_);
    value_ = defaults();
}

bool SettingsStore::remember_geometry() const {
    std::lock_guard lock(mutex_);
    return value_["window"].value("rememberGeometry", true);
}

// 拖动或缩放结束时只更新内存；调用方再通知后台服务线程落盘。
void SettingsStore::set_geometry(const SavedPanelGeometry& geometry) {
    std::lock_guard lock(mutex_);
    if (!value_["window"].value("rememberGeometry", true)) return;
    value_["window"]["geometry"] = {{"clientWidth", geometry.client_width}, {"clientHeight", geometry.client_height},
        {"left", geometry.left}, {"top", geometry.top}, {"width", geometry.width}, {"height", geometry.height}};
}

// 跨分辨率恢复时按较小轴比例缩放窗口，再确保标题栏和最小尺寸仍在当前客户区内。
std::optional<SavedPanelGeometry> SettingsStore::restored_geometry(int client_width, int client_height) const {
    std::lock_guard lock(mutex_);
    if (!value_["window"].value("rememberGeometry", true) || !value_["window"].contains("geometry") || client_width < 1 || client_height < 1) return {};
    const auto& saved = value_["window"]["geometry"];
    const int old_width = saved.value("clientWidth", 0), old_height = saved.value("clientHeight", 0);
    if (old_width < 1 || old_height < 1) return {};
    const double scale = std::min(double(client_width) / old_width, double(client_height) / old_height);
    const int minimum_width = std::min(min_panel_width, client_width), minimum_height = std::min(min_panel_height, client_height);
    const int width = std::clamp(int(std::lround(saved.value("width", panel_width) * scale)), minimum_width, client_width);
    const int height = std::clamp(int(std::lround(saved.value("height", panel_height) * scale)), minimum_height, client_height);
    const int left = std::clamp(int(std::lround(saved.value("left", 0) * double(client_width) / old_width)), 0, std::max(0, client_width - width));
    const int top = std::clamp(int(std::lround(saved.value("top", 0) * double(client_height) / old_height)), 0, std::max(0, client_height - height));
    return SavedPanelGeometry{client_width, client_height, left, top, width, height};
}

// 写入同目录临时文件后原子替换，避免游戏或系统中断留下半个 JSON 文档。
bool SettingsStore::save(std::string& error) const {
    std::filesystem::path path; Json snapshot_value;
    { std::lock_guard lock(mutex_); path = path_; snapshot_value = value_; }
    try {
        std::filesystem::create_directories(path.parent_path());
        auto temporary = path; temporary += L".tmp";
        { std::ofstream stream(temporary, std::ios::binary | std::ios::trunc); stream << snapshot_value.dump(2) << '\n'; if (!stream) throw std::runtime_error("写入临时配置失败"); }
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            std::error_code ignored; std::filesystem::remove(temporary, ignored);
            throw std::runtime_error("替换配置文件失败：" + std::to_string(GetLastError()));
        }
        return true;
    } catch (const std::exception& exception) { error = exception.what(); return false; }
}

} // namespace reff
