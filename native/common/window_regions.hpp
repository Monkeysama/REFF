#pragma once
#include "protocol.hpp"
#include <optional>
#include <vector>

namespace reff {
// Shell 提供视口内的纯拖动矩形；IPC 与窗口线程之间只复制值，不传 DOM 或浏览器对象。
struct WindowRegions {
    struct Rect { int x, y, width, height; };
    int width{}, height{};
    std::vector<Rect> rects;

    // 只接收有限个、完全位于视口内的整数矩形；空数组用于主动取消全部拖动区。
    static std::optional<WindowRegions> parse(const Json& value) {
        if (!value.is_object() || value.size() != 3 || !value.contains("rects") ||
            !value["rects"].is_array() || value["rects"].size() > 4) return {};
        const auto number = [](const Json& object, const char* key, int minimum, int maximum) {
            return object.contains(key) && object[key].is_number_integer() &&
                   object[key].get<double>() >= minimum && object[key].get<double>() <= maximum;
        };
        if (!number(value, "viewportWidth", 1, 2560) || !number(value, "viewportHeight", 1, 1440)) return {};
        WindowRegions result{value["viewportWidth"].get<int>(), value["viewportHeight"].get<int>()};
        for (const auto& item : value["rects"]) {
            if (!item.is_object() || item.size() != 4 || !number(item, "x", 0, result.width) ||
                !number(item, "y", 0, result.height) || !number(item, "width", 0, result.width) ||
                !number(item, "height", 0, result.height)) return {};
            Rect rect{item["x"], item["y"], item["width"], item["height"]};
            if (rect.x + rect.width > result.width || rect.y + rect.height > result.height) return {};
            result.rects.push_back(rect);
        }
        return result;
    }

    // 原生显示矩形可能正在缩放；按报告视口换算命中点，排除窗口外、按钮及正文区域。
    bool contains(int x, int y, int display_width, int display_height) const {
        if (width <= 0 || height <= 0 || display_width <= 0 || display_height <= 0 ||
            x < 0 || y < 0 || x >= display_width || y >= display_height) return false;
        const double px = double(x) * width / display_width;
        const double py = double(y) * height / display_height;
        for (const auto& rect : rects)
            if (px >= rect.x && px < rect.x + rect.width && py >= rect.y && py < rect.y + rect.height) return true;
        return false;
    }
};
}
