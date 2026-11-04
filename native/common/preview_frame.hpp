#pragma once
#include <array>
#include <cstdint>
#include <cmath>
#include <string_view>
#include <cstring>

namespace reff {
inline constexpr unsigned preview_slots = 4;
inline constexpr std::uint32_t preview_surface_active = 1;
struct PreviewRect { float x{}, y{}, width{}, height{}; };
// 与 BGRA 同一共享帧事务发布；固定容量，不传进程指针，旧文档/旧视口只会停画。
struct PreviewSlot {
    char plugin[97]{}, id[65]{};
    PreviewRect destination, clip;
};
struct PreviewFrame {
    std::uint64_t revision{};
    std::uint32_t count{};
    std::uint32_t flags{};
    std::array<PreviewSlot, preview_slots> slots{};
};
inline bool preview_name(std::string_view name, std::size_t maximum) {
    return !name.empty() && name.size() <= maximum && name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789.-_") == std::string_view::npos;
}
inline PreviewRect intersect_preview(PreviewRect a, PreviewRect b) {
    const float x=std::max(a.x,b.x), y=std::max(a.y,b.y);
    return {x,y,std::max(0.f,std::min(a.x+a.width,b.x+b.width)-x),std::max(0.f,std::min(a.y+a.height,b.y+b.height)-y)};
}
}
