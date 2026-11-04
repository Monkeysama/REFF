#pragma once
#include "ipc.hpp"
#include "preview_frame.hpp"
#include <span>

namespace reff {
inline constexpr std::uint32_t frame_layout_version = 3;
// W2 使用固定容量、动态实际宽高的 BGRA 帧；互斥量仅尝试获取，绝不阻塞渲染线程。
struct FrameData {
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t width;
    std::uint32_t height;
    std::uint64_t sequence;
    //浏览器生成该帧时对应的 viewport 代次；渲染器据此丢弃过期尺寸帧。
    std::uint64_t viewport_generation;
    PreviewFrame preview;
    std::uint8_t pixels[frame_max_width * frame_max_height * 4];
};

class SharedFrame {
public:
    ~SharedFrame();
    void open(const std::wstring& session, bool create);
    bool write(const void* bgra, int width, int height, std::uint64_t viewport_generation = 1, const PreviewFrame& preview = {});
    bool read(std::vector<std::uint8_t>& pixels, std::uint64_t& sequence, int& width, int& height,
              std::uint64_t& viewport_generation, std::uint64_t expected_viewport_generation = 0, PreviewFrame* preview = nullptr);
    bool read(std::vector<std::uint8_t>& pixels, std::uint64_t& sequence, int& width, int& height) {
        std::uint64_t ignored_generation{};
        return read(pixels, sequence, width, height, ignored_generation);
    }
    bool read(std::vector<std::uint8_t>& pixels, std::uint64_t& sequence) { int width{}, height{}; return read(pixels, sequence, width, height); }
private:
    Handle mapping_;
    Handle mutex_;
    FrameData* data_{};
};
}
