#include "renderer_shaders.hpp"
#include <iostream>

// 不创建游戏窗口或 GPU：用实际 D3D 编译器检查发布渲染器的两个入口，编译失败必须使 CTest 失败。
int main() {
    try {
        auto vertex = reff::compile_overlay_shader("vs", "vs_5_0");
        auto pixel = reff::compile_overlay_shader("ps", "ps_5_0");
        if (!vertex || !pixel || !vertex->GetBufferSize() || !pixel->GetBufferSize()) return 1;
        std::cout << "Overlay vertex and pixel shaders compiled successfully\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
