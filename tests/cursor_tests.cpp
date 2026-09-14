#include "cursor.hpp"
#include <iostream>
#include <stdexcept>

// 使用计数模型验证生命周期，不触碰游戏窗口、系统光标或用户输入。
int main() {
    int checks{};
    auto require = [&](bool result, const char* label) { ++checks; if (!result) throw std::runtime_error(label); };
    try {
        for (int initial : {-8, -1, 0, 3}) {
            int count = initial;
            auto show = [&](bool visible) { return count += visible ? 1 : -1; };
            reff::CursorVisibility cursor;
            for (int cycle = 0; cycle < 100; ++cycle) {
                cursor.synchronize(true, show);
                require(count >= 0, "opening must reveal hidden cursor");
                int visible_count = count;
                for (int i = 0; i < 20; ++i) cursor.synchronize(true, show);
                require(count == visible_count, "visible synchronization must not drift");
                cursor.synchronize(false, show);
                require(count == initial && !cursor.active(), "closing must restore own increments");
                cursor.synchronize(false, show);
                require(count == initial, "release is idempotent");
            }
        }
        int count = -1;
        auto show = [&](bool visible) { return count += visible ? 1 : -1; };
        reff::CursorVisibility cursor;
        cursor.synchronize(true, show);
        show(false); // 模拟游戏在面板打开期间再次隐藏光标。
        cursor.synchronize(true, show);
        require(count == 0, "game hide must be compensated");
        show(true); // 模拟另一组件合法增加显示计数，关闭时不能回滚该变化。
        cursor.synchronize(false, show);
        require(count == -1, "external counter changes must survive release");
        // 虚拟光标隐藏系统指针时同样只归还自己的计数。
        count = 2;
        cursor.synchronize_hidden(true, show);
        require(count < 0 && cursor.hidden(), "virtual cursor must hide the system cursor");
        cursor.synchronize_hidden(false, show);
        require(count == 2 && !cursor.hidden(), "virtual cursor close must restore the cursor count");
        reff::VirtualCursor virtual_cursor;
        require(virtual_cursor.position() == std::pair{720, 450}, "virtual cursor default position");
        virtual_cursor.move_relative(-5000, 5000);
        require(virtual_cursor.position() == std::pair{0, 899}, "relative cursor coordinates must clamp");
        virtual_cursor.move_absolute(65535, 65535);
        require(virtual_cursor.position() == std::pair{1439, 899}, "absolute cursor coordinates must map to bounds");
        // 游戏客户区不是固定 CEF 画布：尺寸变化后光标保持相对位置并继续按新的边界裁剪。
        virtual_cursor.set_bounds(1920, 1080);
        require(virtual_cursor.position() == std::pair{1439, 899}, "resizing must preserve a valid client position");
        virtual_cursor.move_relative(5000, 5000);
        require(virtual_cursor.position() == std::pair{1919, 1079}, "client-sized cursor must reach full game bounds");
        virtual_cursor.set_position(16, 24);
        require(virtual_cursor.position() == std::pair{16, 24}, "physical cursor position must seed virtual cursor");
        // Windows 加速度让设备增量与屏幕位移不同；自由模式不允许累计误差。
        reff::CursorInput input;
        input.begin(1920, 1080, 100, 100);
        for (int i = 1; i <= 80; ++i) {
            input.raw(2, 1, 100 + 6*i, 100 + 3*i, i);
            require(input.cursor.position() == std::pair{100 + 6*i, 100 + 3*i}, "accelerated system movement must not drift");
        }
        input.begin(1920, 1080, 960, 540);
        for (int i = 1; i <= 3; ++i) input.raw(5, 0, 960, 540, i);
        require(input.relative() && input.cursor.position() == std::pair{975, 540}, "recenter evidence must retain raw motion");
        input.system_position(960, 540);
        require(input.cursor.position() == std::pair{975, 540}, "warp WM_MOUSEMOVE must not reset relative position");
        input.raw(2, 0, 1100, 600, 100);
        input.raw(2, 0, 1150, 600, 260);
        require(!input.relative() && input.cursor.position() == std::pair{1150, 600}, "unlocked pointer must return to exact system coordinates");
        input.begin(1920, 1080, 1919, 500);
        for (int i = 1; i <= 10; ++i) input.raw(10, 0, 1919, 500, i);
        require(!input.relative(), "desktop edge clipping is not game recentering");
        require(reff::client_to_render(960, 540, 1920, 1080, 1280, 720) == std::pair{640.0f, 360.0f}, "client and backbuffer scaling must agree");
        count = -100;
        cursor.synchronize(true, show);
        require(cursor.owned_increments() == 32, "compensation has a finite bound");
        cursor.synchronize(false, show);
        require(count == -100, "bounded compensation remains balanced");
        std::cout << checks << " cursor lifecycle checks passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
