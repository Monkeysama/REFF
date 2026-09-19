#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>

namespace reff {

enum class SupportStatus { recognized, experimental, verified, disabled };
enum class RendererBackend { d3d12 };
enum class CapabilityPolicy { disabled, automatic, required };
enum class ImePolicy { disabled, proxy };
enum class CompatibilityDecision {
    allowed,
    unknown_target,
    recognized_only,
    experimental_disabled,
    disabled,
    renderer_mismatch,
};

// 生成目录中的紧凑条目只持有静态字符串；运行时档案复制字符串，绝不依赖上游或 JSON 缓冲区寿命。
struct GameCatalogEntry {
    std::string_view target;
    std::string_view name;
    std::string_view executable;
    SupportStatus status;
    RendererBackend renderer;
    CapabilityPolicy direct_input_keyboard;
    CapabilityPolicy raw_input_keyboard;
    ImePolicy ime;
};

#include "game_catalog.generated.hpp"

// 游戏适配档案只描述原生能力和验证状态；网页、CEF、Lua 协议不按游戏分叉。
// 本结构在插件初始化线程创建并按值保存，后续线程只读。
struct GameProfile {
    std::string target;
    std::string name;
    std::string executable;
    bool known{};
    SupportStatus status{SupportStatus::disabled};
    RendererBackend renderer{RendererBackend::d3d12};
    CapabilityPolicy direct_input_keyboard{CapabilityPolicy::disabled};
    CapabilityPolicy raw_input_keyboard{CapabilityPolicy::disabled};
    ImePolicy ime{ImePolicy::disabled};
};

// REFramework target_name 使用大写稳定标识；这里做防御性归一化，避免大小写影响适配选择。
inline GameProfile game_profile_for(std::string_view target_name) {
    std::string target(target_name);
    std::transform(target.begin(), target.end(), target.begin(),
        [](unsigned char value) { return static_cast<char>(std::toupper(value)); });
    const auto entry = std::find_if(game_catalog.begin(), game_catalog.end(), [&](const auto& value) { return value.target == target; });
    if (entry == game_catalog.end()) return {target.empty() ? "UNKNOWN" : target};
    return {target, std::string(entry->name), std::string(entry->executable), true, entry->status, entry->renderer,
        entry->direct_input_keyboard, entry->raw_input_keyboard, entry->ime};
}

// 初始化决策是可单测纯函数；实验构建开关不能绕过未知、仅识别或禁用目标。
inline CompatibilityDecision compatibility_decision(const GameProfile& profile, bool actual_d3d12, bool allow_experimental) {
    if (!profile.known) return CompatibilityDecision::unknown_target;
    if (profile.status == SupportStatus::recognized) return CompatibilityDecision::recognized_only;
    if (profile.status == SupportStatus::disabled) return CompatibilityDecision::disabled;
    if (profile.status == SupportStatus::experimental && !allow_experimental) return CompatibilityDecision::experimental_disabled;
    if (profile.renderer == RendererBackend::d3d12 && !actual_d3d12) return CompatibilityDecision::renderer_mismatch;
    return CompatibilityDecision::allowed;
}

inline constexpr bool should_install_direct_input(const GameProfile& profile) {
    return profile.direct_input_keyboard != CapabilityPolicy::disabled;
}

inline constexpr std::string_view support_status_name(SupportStatus status) {
    switch (status) {
    case SupportStatus::recognized: return "recognized";
    case SupportStatus::experimental: return "experimental";
    case SupportStatus::verified: return "verified";
    case SupportStatus::disabled: return "disabled";
    }
    return "disabled";
}

inline constexpr std::string_view compatibility_decision_name(CompatibilityDecision decision) {
    switch (decision) {
    case CompatibilityDecision::allowed: return "allowed";
    case CompatibilityDecision::unknown_target: return "unknown_target";
    case CompatibilityDecision::recognized_only: return "recognized_only";
    case CompatibilityDecision::experimental_disabled: return "experimental_disabled";
    case CompatibilityDecision::disabled: return "disabled";
    case CompatibilityDecision::renderer_mismatch: return "renderer_mismatch";
    }
    return "disabled";
}

} // namespace reff
