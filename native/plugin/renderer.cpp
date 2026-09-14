#include "renderer.hpp"
#include "renderer_shaders.hpp"
#include "cursor.hpp"
#include <cstring>
#include <source_location>
#include <windows.h>

namespace reff {
// 把 HRESULT 转成异常，仅在外层 Present 边界捕获，避免半初始化对象继续绘制。
static void check(HRESULT result, std::source_location location = std::source_location::current()) {
    if (FAILED(result)) throw std::runtime_error("DX12 HRESULT=" + std::to_string(result) +
        " at " + location.function_name() + ":" + std::to_string(location.line()));
}

static D3D12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE type) {
    D3D12_HEAP_PROPERTIES result{}; result.Type = type;
    result.CreationNodeMask = result.VisibleNodeMask = 1; return result;
}

// 显式资源状态转换；本合成器的上传与绘制均在 REFramework 提供的同一队列排序。
static void barrier(ID3D12GraphicsCommandList* list, ID3D12Resource* resource,
                    D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER value{};
    value.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    value.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
    list->ResourceBarrier(1, &value);
}

// 将 Windows 系统调整光标栅格化为 4 个 24x24 RGBA 图标，并保留系统 hotspot。
static std::vector<std::uint8_t> load_system_resize_cursors(std::array<std::pair<int, int>, 4>& hotspots) {
    const LPCWSTR ids[]{IDC_SIZEWE, IDC_SIZENS, IDC_SIZENWSE, IDC_SIZENESW};
    std::vector<std::uint8_t> atlas(96 * 24 * 4);
    for (int i=0; i<4; ++i) {
        HCURSOR cursor=LoadCursorW(nullptr, ids[i]); ICONINFO info{};
        if (!cursor || !GetIconInfo(cursor, &info)) throw std::runtime_error("GetIconInfo resize cursor failed");
        hotspots[i] = {std::clamp<int>(int(info.xHotspot * 24 / std::max(1, GetSystemMetrics(SM_CXCURSOR))), 0, 23),
                       std::clamp<int>(int(info.yHotspot * 24 / std::max(1, GetSystemMetrics(SM_CYCURSOR))), 0, 23)};
        BITMAPINFO bi{}; bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); bi.bmiHeader.biWidth=24; bi.bmiHeader.biHeight=-24;
        bi.bmiHeader.biPlanes=1; bi.bmiHeader.biBitCount=32; bi.bmiHeader.biCompression=BI_RGB;
        void* bits{}; HDC dc=GetDC(nullptr); HBITMAP bitmap=CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        HDC mem=CreateCompatibleDC(dc); HGDIOBJ old=SelectObject(mem, bitmap); std::memset(bits, 0, 24*24*4);
        DrawIconEx(mem, 0, 0, cursor, 24, 24, 0, nullptr, DI_NORMAL);
        for (int y=0; y<24; ++y) std::memcpy(atlas.data() + y*96*4 + i*24*4, static_cast<const std::uint8_t*>(bits) + y*24*4, 24*4);
        SelectObject(mem, old); DeleteDC(mem); DeleteObject(bitmap); ReleaseDC(nullptr, dc); DeleteObject(info.hbmMask); DeleteObject(info.hbmColor);
    }
    return atlas;
}

// 初始化一次 DX12 对象；缓冲数量来自交换链，不能假设游戏固定双缓冲。
bool Renderer::initialize(ID3D12Device* device, IDXGISwapChain* swapchain, ID3D12CommandQueue* queue) {
    if (device_) return true;
    last_error_.clear();
    if (!device || !swapchain || !queue) { last_error_ = "Missing DX12 device, swapchain or queue"; return false; }
    try {
        device_ = device; queue_ = queue; check(swapchain->QueryInterface(IID_PPV_ARGS(&swapchain_)));
        DXGI_SWAP_CHAIN_DESC desc{}; check(swapchain->GetDesc(&desc));
        if (!desc.BufferCount || desc.BufferCount > 8) throw std::runtime_error("unsupported buffer count");
        width_ = desc.BufferDesc.Width; height_ = desc.BufferDesc.Height; window_ = desc.OutputWindow;
        D3D12_DESCRIPTOR_HEAP_DESC descriptors{};
        descriptors.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; descriptors.NumDescriptors = desc.BufferCount;
        check(device->CreateDescriptorHeap(&descriptors, IID_PPV_ARGS(&rtv_)));
        descriptors.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; descriptors.NumDescriptors = 2;
        descriptors.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        check(device->CreateDescriptorHeap(&descriptors, IID_PPV_ARGS(&srv_)));
        rtv_step_ = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        slots_.resize(desc.BufferCount);
        for (UINT i = 0; i < desc.BufferCount; ++i) {
            check(swapchain->GetBuffer(i, IID_PPV_ARGS(&slots_[i].target)));
            auto rtv = rtv_->GetCPUDescriptorHandleForHeapStart(); rtv.ptr += i * rtv_step_;
            device->CreateRenderTargetView(slots_[i].target.Get(), nullptr, rtv);
        }
        auto resource_desc = slots_[0].target->GetDesc();
        width_ = UINT(resource_desc.Width); height_ = resource_desc.Height;
        // 命令资源独立于交换链缓冲区，避免某个 backbuffer 的 fence 未完成时整次面板合成被跳过。
        // 保留足够的在途命令，覆盖高刷新率、帧生成和 CEF 鼠标重绘同时发生的情况。
        const auto command_frame_count = std::max<std::size_t>(desc.BufferCount + 8, 12);
        command_frames_.resize(command_frame_count);
        for (auto& command_frame : command_frames_)
            check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&command_frame.allocator)));
        check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, command_frames_[0].allocator.Get(), nullptr, IID_PPV_ARGS(&list_)));
        check(list_->Close()); check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)));
        wait_event_.reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
        if (!wait_event_) throw std::runtime_error("DX12 fence event creation failed");

        D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0, 0};
        std::array<D3D12_ROOT_PARAMETER, 2> parameters{};
        parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[0].DescriptorTable = {1, &range}; parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        // 根常量只在 Present 线程写入：用于把光标的小四边形定位到游戏客户区，无额外 GPU 资源。
        parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[1].Constants = {0, 0, 12}; parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        D3D12_STATIC_SAMPLER_DESC sampler{}; sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS; sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_SIGNATURE_DESC root_desc{}; root_desc.NumParameters = UINT(parameters.size()); root_desc.pParameters = parameters.data();
        root_desc.NumStaticSamplers = 1; root_desc.pStaticSamplers = &sampler;
        Com<ID3DBlob> signature, errors;
        check(D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors));
        check(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&root_)));
        auto vertex = compile_overlay_shader("vs", "vs_5_0");
        auto pixel = compile_overlay_shader("ps", "ps_5_0");
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{}; pso.pRootSignature = root_.Get();
        pso.VS = {vertex->GetBufferPointer(), vertex->GetBufferSize()};
        pso.PS = {pixel->GetBufferPointer(), pixel->GetBufferSize()};
        auto& blend = pso.BlendState.RenderTarget[0]; blend.BlendEnable = TRUE;
        blend.SrcBlend = D3D12_BLEND_ONE; blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA; blend.BlendOp = D3D12_BLEND_OP_ADD;
        blend.SrcBlendAlpha = D3D12_BLEND_ONE; blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA; blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pso.SampleMask = UINT_MAX; pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE; pso.RasterizerState.DepthClipEnable = TRUE;
        pso.DepthStencilState.DepthEnable = FALSE; pso.DepthStencilState.StencilEnable = FALSE;
        pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        pso.DepthStencilState.FrontFace = pso.DepthStencilState.BackFace = {
            D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS};
        pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; pso.NumRenderTargets = 1;
        pso.RTVFormats[0] = resource_desc.Format; pso.SampleDesc.Count = 1;
        check(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&pipeline_)));

        D3D12_RESOURCE_DESC tex{}; tex.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        tex.Width = frame_max_width; tex.Height = frame_max_height; tex.DepthOrArraySize = 1; tex.MipLevels = 1;
        tex.Format = DXGI_FORMAT_B8G8R8A8_UNORM; tex.SampleDesc.Count = 1;
        auto default_heap = heap(D3D12_HEAP_TYPE_DEFAULT);
        check(device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &tex,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&texture_)));
        device->CreateShaderResourceView(texture_.Get(), nullptr, srv_->GetCPUDescriptorHandleForHeapStart());
        cursor_pixels_ = load_system_resize_cursors(cursor_hotspots_);
        D3D12_RESOURCE_DESC cursor_desc=tex; cursor_desc.Width=cursor_texture_width_; cursor_desc.Height=cursor_texture_height_; cursor_desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
        D3D12_RESOURCE_STATES cursor_initial=D3D12_RESOURCE_STATE_COPY_DEST;
        check(device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &cursor_desc, cursor_initial, nullptr, IID_PPV_ARGS(&cursor_texture_)));
        auto cursor_srv=srv_->GetCPUDescriptorHandleForHeapStart(); cursor_srv.ptr += device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        device->CreateShaderResourceView(cursor_texture_.Get(), nullptr, cursor_srv);
        UINT64 cursor_upload_size{}; device->GetCopyableFootprints(&cursor_desc, 0, 1, 0, &cursor_footprint_, nullptr, nullptr, &cursor_upload_size);
        UINT64 upload_size{}; device->GetCopyableFootprints(&tex, 0, 1, 0, &footprint_, nullptr, nullptr, &upload_size);
        D3D12_RESOURCE_DESC upload{}; upload.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; upload.Width = upload_size;
        upload.Height = 1; upload.DepthOrArraySize = 1; upload.MipLevels = 1; upload.SampleDesc.Count = 1;
        upload.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; auto upload_heap = heap(D3D12_HEAP_TYPE_UPLOAD);
        for (auto& command_frame : command_frames_) check(device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &upload,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&command_frame.upload)));
        upload.Width=cursor_upload_size;
        for (auto& command_frame : command_frames_) check(device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &upload,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&command_frame.cursor_upload)));
        return true;
    } catch (const std::exception& error) { last_error_ = error.what(); reset(); return false; }
      catch (...) { last_error_ = "Unknown DX12 initialization exception"; reset(); return false; }
}

// 面板居中，小窗口按比例缩小；此矩形也用于鼠标坐标转换。
RECT Renderer::panel_rect() const {
    float scale = std::min({1.0f, float(width_) / panel_width, float(height_) / panel_height});
    LONG w = LONG(panel_width * scale), h = LONG(panel_height * scale);
    LONG x = (LONG(width_) - w) / 2, y = (LONG(height_) - h) / 2;
    return {x, y, x + w, y + h};
}

// 每帧最多上传一份完整快照；GPU 忙时跳过本帧，不让 UI 拖住游戏渲染线程。
RECT Renderer::panel_client_rect(std::pair<int, int> client_size, std::pair<int, int> origin) const {
    const float scale = std::min({1.0f, float(client_size.first) / panel_width, float(client_size.second) / panel_height});
    const LONG display_width = std::max<LONG>(1, LONG(panel_width * scale));
    const LONG display_height = std::max<LONG>(1, LONG(panel_height * scale));
    const bool centered = origin.first == -1 && origin.second == -1;
    LONG left = centered ? (client_size.first - display_width) / 2 : origin.first;
    LONG top = centered ? (client_size.second - display_height) / 2 : origin.second;
    left = std::clamp<LONG>(left, 0, std::max<LONG>(0, client_size.first - display_width));
    top = std::clamp<LONG>(top, 0, std::max<LONG>(0, client_size.second - display_height));
    return {left, top, left + display_width, top + display_height};
}

// Present 线程提交面板与箭头；坐标和箭头尺寸一起按客户区/交换链比例换算。
bool Renderer::draw(SharedFrame& frame, std::pair<int, int> cursor_position, bool cursor_visible,
                    std::pair<int, int> client_size, std::pair<int, int> panel_origin, RECT display_rect, int cursor_mode,
                    std::uint64_t expected_viewport_generation, bool panel_resizing) {
    last_error_.clear();
    if (!device_) { last_error_ = "DX12 renderer is not initialized"; return false; }
    try {
        auto index = swapchain_->GetCurrentBackBufferIndex(); auto& slot = slots_.at(index);
        CommandFrame* command_frame = nullptr;
        const auto completed = fence_->GetCompletedValue();
        for (std::size_t attempt = 0; attempt < command_frames_.size(); ++attempt) {
            command_frame_index_ = (command_frame_index_ + 1) % command_frames_.size();
            auto& candidate = command_frames_[command_frame_index_];
            if (candidate.fence <= completed) { command_frame = &candidate; break; }
        }
        // 游戏队列通常会在一个刷新周期内完成；若所有槽都忙，等待最老的一份命令而不丢整块面板。
        if (!command_frame) {
            auto& candidate = command_frames_[command_frame_index_];
            if (fence_->SetEventOnCompletion(candidate.fence, wait_event_.get()) != S_OK ||
                WaitForSingleObject(wait_event_.get(), 50) != WAIT_OBJECT_0) {
                last_error_ = "DX12 command frame pool remained busy for 50ms";
                return true;
            }
            command_frame = &candidate;
        }
        int frame_width{}, frame_height{}; std::uint64_t viewport_generation{};
        // 实时缩放允许接受最近到达的完整帧；CEF 重排可能落后于输入，不能因追逐最新 generation 而停住画面。
        const bool changed = frame.read(pixels_, sequence_, frame_width, frame_height, viewport_generation);
        if (!changed && !uploaded_) return true;
        check(command_frame->allocator->Reset()); check(list_->Reset(command_frame->allocator.Get(), pipeline_.Get()));
        if (changed) {
            frame_width_ = UINT(frame_width); frame_height_ = UINT(frame_height);
            frame_viewport_generation_ = viewport_generation;
            target_content_aspect_ = float(frame_width_) / std::max(1u, frame_height_);
            void* data{}; D3D12_RANGE read_range{0, 0}; check(command_frame->upload->Map(0, &read_range, &data));
            for (UINT y = 0; y < frame_height_; ++y) std::memcpy(static_cast<char*>(data) + footprint_.Offset + y * footprint_.Footprint.RowPitch,
                pixels_.data() + y * frame_width_ * 4, frame_width_ * 4);
            command_frame->upload->Unmap(0, nullptr);
            barrier(list_.Get(), texture_.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
            D3D12_TEXTURE_COPY_LOCATION source{}; source.pResource = command_frame->upload.Get(); source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; source.PlacedFootprint = footprint_;
            D3D12_TEXTURE_COPY_LOCATION dest{}; dest.pResource = texture_.Get(); dest.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            list_->CopyTextureRegion(&dest, 0, 0, 0, &source, nullptr);
            barrier(list_.Get(), texture_.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            uploaded_ = true;
            ++stats_.texture_uploads;
            stats_.uploaded_bytes += pixels_.size();
        }
        if (!cursor_uploaded_) {
            void* data{}; D3D12_RANGE read_range{0,0}; check(command_frame->cursor_upload->Map(0, &read_range, &data));
            for (UINT y=0; y<cursor_texture_height_; ++y) std::memcpy(static_cast<char*>(data)+cursor_footprint_.Offset+y*cursor_footprint_.Footprint.RowPitch,
                cursor_pixels_.data()+y*cursor_texture_width_*4, cursor_texture_width_*4);
            command_frame->cursor_upload->Unmap(0,nullptr);
            D3D12_TEXTURE_COPY_LOCATION source{}; source.pResource=command_frame->cursor_upload.Get(); source.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; source.PlacedFootprint=cursor_footprint_;
            D3D12_TEXTURE_COPY_LOCATION dest{}; dest.pResource=cursor_texture_.Get(); dest.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            list_->CopyTextureRegion(&dest,0,0,0,&source,nullptr);
            barrier(list_.Get(), cursor_texture_.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            cursor_uploaded_=true;
        }
        barrier(list_.Get(), slot.target.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
        auto target = rtv_->GetCPUDescriptorHandleForHeapStart(); target.ptr += index * rtv_step_;
        list_->OMSetRenderTargets(1, &target, FALSE, nullptr);
        auto panel_client = (display_rect.right > display_rect.left && display_rect.bottom > display_rect.top) ? display_rect : panel_client_rect(client_size, panel_origin);
        // 缩放期间对新旧 CEF 帧的源宽高比做渐进过渡；面板矩形每帧立即跟随，避免 100ms 重排节拍造成跳动。
        const LONG target_width = panel_client.right - panel_client.left, target_height = panel_client.bottom - panel_client.top;
        if (!panel_resizing) displayed_content_aspect_ = target_content_aspect_;
        else displayed_content_aspect_ += (target_content_aspect_ - displayed_content_aspect_) * 0.22f;
        const float panel_aspect = float(target_width) / std::max<LONG>(1, target_height);
        LONG fitted_width{}, fitted_height{};
        if (panel_aspect > displayed_content_aspect_) {
            fitted_height = target_height; fitted_width = std::max<LONG>(1, LONG(target_height * displayed_content_aspect_));
        } else {
            fitted_width = target_width; fitted_height = std::max<LONG>(1, LONG(target_width / std::max(0.01f, displayed_content_aspect_)));
        }
        panel_client.left += (target_width - fitted_width) / 2; panel_client.top += (target_height - fitted_height) / 2;
        panel_client.right = panel_client.left + fitted_width; panel_client.bottom = panel_client.top + fitted_height;
        RECT rect{MulDiv(panel_client.left, width_, std::max(1, client_size.first)),
                  MulDiv(panel_client.top, height_, std::max(1, client_size.second)),
                  MulDiv(panel_client.right, width_, std::max(1, client_size.first)),
                  MulDiv(panel_client.bottom, height_, std::max(1, client_size.second))};
        D3D12_VIEWPORT viewport{float(rect.left), float(rect.top), float(rect.right-rect.left), float(rect.bottom-rect.top), 0, 1};
        list_->RSSetViewports(1, &viewport); list_->RSSetScissorRects(1, &rect);
        list_->SetGraphicsRootSignature(root_.Get()); ID3D12DescriptorHeap* heaps[]{srv_.Get()};
        list_->SetDescriptorHeaps(1, heaps); list_->SetGraphicsRootDescriptorTable(0, srv_->GetGPUDescriptorHandleForHeapStart());
        const float panel_parameters[]{0, 0, 0, 0, float(width_), float(height_), 0, 0,
            float(frame_width_) / frame_max_width, float(frame_height_) / frame_max_height, 0, 0};
        list_->SetGraphicsRoot32BitConstants(1, 12, panel_parameters, 0);
        list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST); list_->DrawInstanced(3, 1, 0, 0);
        // 光标叠加在面板之后，且使用完整游戏客户区 viewport，不受 CEF 固定大小画布裁剪。
        RECT client_rect{0, 0, LONG(width_), LONG(height_)};
        D3D12_VIEWPORT client_viewport{0, 0, float(width_), float(height_), 0, 1};
        list_->RSSetViewports(1, &client_viewport); list_->RSSetScissorRects(1, &client_rect);
        const auto [cursor_x, cursor_y] = client_to_render(cursor_position.first, cursor_position.second, client_size.first, client_size.second, width_, height_);
        const auto [cursor_width, cursor_height] = client_to_render(cursor_mode ? 24 : 18, 24, client_size.first, client_size.second, width_, height_);
        // 调整大小图标使用 Windows 光标真实 hotspot；普通箭头仍以左上尖端作为热点。
        const auto hotspot = cursor_mode ? client_to_render(cursor_hotspots_[std::clamp(cursor_mode,1,4)-1].first,
            cursor_hotspots_[std::clamp(cursor_mode,1,4)-1].second, client_size.first, client_size.second, width_, height_) : std::pair<float,float>{0,0};
        const float cursor_origin_x = cursor_mode ? cursor_x - hotspot.first : cursor_x;
        const float cursor_origin_y = cursor_mode ? cursor_y - hotspot.second : cursor_y;
        const float cursor_parameters[]{cursor_origin_x, cursor_origin_y, cursor_visible ? cursor_width : -1.0f, cursor_height,
                                        float(width_), float(height_), float(cursor_mode), 0, 1, 1, 0, 0};
        list_->SetGraphicsRoot32BitConstants(1, 12, cursor_parameters, 0);
        if (cursor_visible) { list_->DrawInstanced(6, 1, 0, 0); ++stats_.cursor_draws; }
        barrier(list_.Get(), slot.target.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        check(list_->Close()); ID3D12CommandList* lists[]{list_.Get()}; queue_->ExecuteCommandLists(1, lists);
        check(queue_->Signal(fence_.Get(), ++fence_value_)); command_frame->fence = fence_value_;
        ++stats_.draw_calls;
        return true;
    } catch (const std::exception& error) { last_error_ = error.what(); return false; }
      catch (...) { last_error_ = "Unknown DX12 draw exception"; return false; }
}

// 仅设备重建/测试退出时等待已提交工作，正常 Present 路径禁止调用此等待。
void Renderer::reset() {
    if (fence_ && queue_ && fence_value_ && fence_->GetCompletedValue() < fence_value_) {
        Handle event(CreateEventW(nullptr, FALSE, FALSE, nullptr));
        if (SUCCEEDED(fence_->SetEventOnCompletion(fence_value_, event.get()))) WaitForSingleObject(event.get(), 2000);
    }
    slots_.clear(); texture_.Reset(); pipeline_.Reset(); root_.Reset(); list_.Reset(); fence_.Reset(); wait_event_.reset();
    command_frames_.clear(); command_frame_index_ = 0;
    rtv_.Reset(); srv_.Reset(); swapchain_.Reset(); queue_.Reset(); device_.Reset();
    sequence_ = 0; fence_value_ = 0; uploaded_ = false; frame_viewport_generation_ = 1;
    displayed_content_aspect_ = target_content_aspect_ = float(panel_width) / panel_height; stats_ = {}; window_ = nullptr;
}
}

