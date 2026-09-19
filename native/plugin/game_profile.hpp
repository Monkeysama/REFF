#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace reff {

// 游戏适配档案：只描述经过 REFF 验证的原生能力；网页、CEF 和 Lua 协议不在这里按游戏分叉。
// 本结构在插件初始化线程创建并按值保存，后续线程只读，不持有 REFramework 字符串指针。
struct GameProfile {
    std::string target;
    std::string executable;
    bool supported{};
    bool d3d12_required{true};
    bool direct_input_keyboard{};
};

// REFramework target_name 使用大写稳定标识；这里做防御性归一化，避免大小写影响适配选择。
inline GameProfile game_profile_for(std::string_view target_name) {
    std::string target(target_name);
    std::transform(target.begin(), target.end(), target.begin(),
        [](unsigned char value) { return static_cast<char>(std::toupper(value)); });
    if (target == "MHWILDS") return {target, "MonsterHunterWilds.exe", true, true, true};
    if (target == "MHRISE") return {target, "MonsterHunterRise.exe", true, true, true};
    return {target.empty() ? "UNKNOWN" : target, {}, false, true, false};
}

} // namespace reff
