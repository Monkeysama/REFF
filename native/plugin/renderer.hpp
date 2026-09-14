#pragma once
#include "frame.hpp"
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <array>

namespace reff {
template<class T> using Com = Microsoft::WRL::ComPtr<T>;

// 游戏 Present 线程独占的 DX12 合成器；通过 fence 判断可重用资源，不逐帧等待 GPU。
class Renderer {
public:
    // Present 线程采集的轻量渲染计数；调用 take_stats 后清零，不持有跨线程资源。
    struct Stats {
        std::uint64_t draw_calls{};
        std::uint64_t texture_uploads{};
        std::uint64_t uploaded_bytes{};
        std::uint64_t cursor_draws{};
    };
    bool initialize(ID3D12Device* device, IDXGISwapChain* swapchain, ID3D12CommandQueue* queue);
    // 在同一条 Present 命令列表中先合成 CEF 面板，再在游戏客户区绘制虚拟光标。
    // cursor_position 使用游戏客户区物理像素，调用方负责在该客户区内裁剪。
    bool draw(SharedFrame& frame, std::pair<int, int> cursor_position, bool cursor_visible,
              std::pair<int, int> client_size, std::pair<int, int> panel_origin = {-1, -1}, RECT display_rect = {}, int cursor_mode = 0,
              std::uint64_t expected_viewport_generation = 0, bool panel_resizing = false);
    void reset();
    // 仅由持有 renderer_mutex 的 Present 调用者读取并清零统计窗口。
    Stats take_stats() { auto result = stats_; stats_ = {}; return result; }
    // 只在 Present 线程调用：宿主换代后保留 GPU 资源，但不显示旧会话内容。
    void invalidate_frame() { sequence_ = 0; uploaded_ = false; }
    RECT panel_rect() const;
    // Present 线程转换面板矩形，供窗口线程的 CEF 命中与 IME 候选位置共享客户区坐标。
    RECT panel_client_rect(std::pair<int, int> client_size, std::pair<int, int> origin = {-1, -1}) const;
    HWND window() const { return window_; }
    // 仅由持有 renderer_mutex 的 Present 调用者读取；失败后保留诊断直到下次渲染尝试。
    const std::string& last_error() const { return last_error_; }
    std::uint64_t displayed_frame_generation() const { return frame_viewport_generation_; }
private:
    struct Slot {
        Com<ID3D12Resource> target;
    };
    struct CommandFrame {
        Com<ID3D12CommandAllocator> allocator;
        Com<ID3D12Resource> upload;
        Com<ID3D12Resource> cursor_upload;
        std::uint64_t fence{};
    };
    Com<ID3D12Device> device_;
    Com<IDXGISwapChain3> swapchain_;
    Com<ID3D12CommandQueue> queue_;
    Com<ID3D12GraphicsCommandList> list_;
    Com<ID3D12DescriptorHeap> rtv_, srv_;
    Com<ID3D12RootSignature> root_;
    Com<ID3D12PipelineState> pipeline_;
    Com<ID3D12Resource> texture_;
    Com<ID3D12Resource> cursor_texture_;
    Com<ID3D12Fence> fence_;
    Handle wait_event_;
    std::vector<Slot> slots_;
    std::vector<CommandFrame> command_frames_;
    std::size_t command_frame_index_{};
    std::vector<std::uint8_t> pixels_;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint_{};
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT cursor_footprint_{};
    UINT width_{}, height_{}, rtv_step_{};
    UINT64 fence_value_{};
    std::uint64_t sequence_{};
    bool uploaded_{};
    UINT frame_width_{panel_width}, frame_height_{panel_height};
    std::uint64_t frame_viewport_generation_{1};
    float displayed_content_aspect_{float(panel_width) / panel_height};
    float target_content_aspect_{float(panel_width) / panel_height};
    bool cursor_uploaded_{};
    std::vector<std::uint8_t> cursor_pixels_;
    UINT cursor_texture_width_{96}, cursor_texture_height_{24};
    std::array<std::pair<int, int>, 4> cursor_hotspots_{};
    Stats stats_{};
    HWND window_{};
    std::string last_error_;
};
}
