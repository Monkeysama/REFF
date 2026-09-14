#include "renderer.hpp"
#include <fstream>
#include <iostream>

using namespace reff;
namespace {
// 独立 WARP 渲染回归：所有资源归本进程所有，隐藏窗口不接收用户输入；不启动游戏或 CEF。
void check(HRESULT value) { if (FAILED(value)) throw std::runtime_error("Test DX12 HRESULT=" + std::to_string(value)); }
void transition(ID3D12GraphicsCommandList* list, ID3D12Resource* target, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition = {target, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after}; list->ResourceBarrier(1, &b);
}

// 输出实际 GPU 回读的箭头局部放大图；最近邻放大用于人工检查像素边缘，不是另行模拟的示意图。
void save_cursor(const char* path, const std::vector<unsigned char>& rgba, int stride, int x, int y) {
    constexpr int zoom = 8, width = 24*zoom, height = 30*zoom;
    BITMAPFILEHEADER header{}; header.bfType = 0x4d42; header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
    header.bfSize = header.bfOffBits + width*height*4;
    BITMAPINFOHEADER info{}; info.biSize = sizeof(info); info.biWidth = width; info.biHeight = -height;
    info.biPlanes = 1; info.biBitCount = 32;
    std::ofstream out(path, std::ios::binary); out.write(reinterpret_cast<char*>(&header), sizeof(header)); out.write(reinterpret_cast<char*>(&info), sizeof(info));
    for (int row=0; row<height; ++row) for (int col=0; col<width; ++col) {
        auto p = &rgba[(y+row/zoom)*stride + (x+col/zoom)*4];
        unsigned char pixel[]{p[2],p[1],p[0],255};
        if (!p[3]) pixel[0]=pixel[1]=pixel[2]=100;
        out.write(reinterpret_cast<char*>(pixel), 4);
    }
}
}

// 通过正式 Renderer 绘制透明面板及光标，回读整个交换链并断言热点、尺寸、描边、面板外可见性和隐藏状态。
int main(int argc, char** argv) {
    constexpr int width=1280, height=960;
    HWND window{};
    try {
        WNDCLASSW cls{}; cls.lpfnWndProc=DefWindowProcW; cls.hInstance=GetModuleHandleW(nullptr); cls.lpszClassName=L"REFFRenderPixelsTest";
        if (!RegisterClassW(&cls)) throw std::runtime_error("Window class registration failed");
        window=CreateWindowW(cls.lpszClassName,L"",WS_OVERLAPPEDWINDOW,0,0,width,height,nullptr,nullptr,cls.hInstance,nullptr);
        if (!window) throw std::runtime_error("Hidden window creation failed");
        Com<IDXGIFactory4> factory; check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
        Com<IDXGIAdapter> warp; check(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)));
        Com<ID3D12Device> device; check(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
        Com<ID3D12CommandQueue> queue; D3D12_COMMAND_QUEUE_DESC qd{}; check(device->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)));
        DXGI_SWAP_CHAIN_DESC1 sd{}; sd.Width=width; sd.Height=height; sd.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.SampleDesc.Count=1; sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.BufferCount=2; sd.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
        Com<IDXGISwapChain1> swapchain; check(factory->CreateSwapChainForHwnd(queue.Get(),window,&sd,nullptr,nullptr,&swapchain));
        Renderer renderer;
        if (!renderer.initialize(device.Get(),swapchain.Get(),queue.Get())) throw std::runtime_error(renderer.last_error());
        // W1 位置回归：固定尺寸面板支持偏移，但始终被裁剪到客户区内。
        const auto centered = renderer.panel_client_rect({width, height});
        if (centered.left != 160 || centered.top != 160 || centered.right != 1120 || centered.bottom != 800)
            throw std::runtime_error("default panel position mismatch");
        const auto moved = renderer.panel_client_rect({width, height}, {24, 36});
        if (moved.left != 24 || moved.top != 36 || moved.right != 984 || moved.bottom != 676)
            throw std::runtime_error("moved panel position mismatch");
        const auto clamped = renderer.panel_client_rect({width, height}, {9999, -99});
        if (clamped.left != 320 || clamped.top != 0 || clamped.right != 1280 || clamped.bottom != 640)
            throw std::runtime_error("panel position clamp mismatch: " + std::to_string(clamped.left) + "," + std::to_string(clamped.top) + "," + std::to_string(clamped.right) + "," + std::to_string(clamped.bottom));
        Com<ID3D12Resource> target; check(swapchain->GetBuffer(0,IID_PPV_ARGS(&target)));
        Com<ID3D12DescriptorHeap> rtv; D3D12_DESCRIPTOR_HEAP_DESC hd{}; hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV; hd.NumDescriptors=1;
        check(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rtv))); device->CreateRenderTargetView(target.Get(),nullptr,rtv->GetCPUDescriptorHandleForHeapStart());
        Com<ID3D12CommandAllocator> allocator; check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
        Com<ID3D12GraphicsCommandList> list; check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list))); check(list->Close());
        Com<ID3D12Fence> fence; check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
        Handle event(CreateEventW(nullptr,FALSE,FALSE,nullptr)); UINT64 sequence{};
        auto submit = [&] {
            check(list->Close()); ID3D12CommandList* commands[]{list.Get()}; queue->ExecuteCommandLists(1,commands);
            check(queue->Signal(fence.Get(),++sequence)); check(fence->SetEventOnCompletion(sequence,event.get()));
            if (WaitForSingleObject(event.get(),5000)!=WAIT_OBJECT_0) throw std::runtime_error("GPU readback timed out");
        };
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{}; UINT64 bytes{}; auto td=target->GetDesc();
        device->GetCopyableFootprints(&td,0,1,0,&layout,nullptr,nullptr,&bytes);
        D3D12_HEAP_PROPERTIES hp{}; hp.Type=D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC bd{}; bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width=bytes; bd.Height=1; bd.DepthOrArraySize=1; bd.MipLevels=1; bd.SampleDesc.Count=1; bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        Com<ID3D12Resource> readback; check(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)));
        SharedFrame frame; frame.open(make_session(),true);
        std::vector<unsigned char> transparent(panel_width*panel_height*4);
        if (!frame.write(transparent.data(),panel_width,panel_height)) throw std::runtime_error("Frame write failed");
        for (int mode=0; mode<3; ++mode) {
            check(allocator->Reset()); check(list->Reset(allocator.Get(),nullptr));
            transition(list.Get(),target.Get(),D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_RENDER_TARGET);
            const float clear[]{0,0,0,0}; list->ClearRenderTargetView(rtv->GetCPUDescriptorHandleForHeapStart(),clear,0,nullptr);
            transition(list.Get(),target.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PRESENT); submit();
            const int scale = mode==1 ? 2 : 1;
            if (!renderer.draw(frame,{40,50},mode!=2,{width/scale,height/scale})) throw std::runtime_error(renderer.last_error());
            check(allocator->Reset()); check(list->Reset(allocator.Get(),nullptr));
            transition(list.Get(),target.Get(),D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_COPY_SOURCE);
            D3D12_TEXTURE_COPY_LOCATION src{}; src.pResource=target.Get(); src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            D3D12_TEXTURE_COPY_LOCATION dst{}; dst.pResource=readback.Get(); dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; dst.PlacedFootprint=layout;
            list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
            transition(list.Get(),target.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_PRESENT); submit();
            void* data{}; D3D12_RANGE range{0,SIZE_T(bytes)}; check(readback->Map(0,&range,&data));
            std::vector<unsigned char> pixels(static_cast<unsigned char*>(data),static_cast<unsigned char*>(data)+bytes); readback->Unmap(0,nullptr);
            int count=0, dark=0, bright=0, minx=width, miny=height, maxy=-1;
            for (int y=0;y<height;++y) for (int x=0;x<width;++x) {
                const auto* p=&pixels[layout.Offset+y*layout.Footprint.RowPitch+x*4];
                if (!p[3]) continue;
                if (mode==2 || x<40*scale || x>=(40+18)*scale || y<50*scale || y>=(50+24)*scale)
                    throw std::runtime_error("Cursor pixels escaped intended rectangle or remained visible while hidden");
                ++count; if (p[0]<50) ++dark; if(p[0]>240) ++bright;
                minx=std::min(minx,x); miny=std::min(miny,y); maxy=std::max(maxy,y);
            }
            // 断言前输出 GPU 实测值和图像，使失败也保留定位证据。
            std::cout << "mode=" << mode << " covered=" << count << " outline=" << dark << " white=" << bright
                      << " min=" << minx << ',' << miny << " maxy=" << maxy << '\n';
            if(mode==0 && argc==2) save_cursor(argv[1],pixels,layout.Footprint.RowPitch,38,48);
            if(mode!=2 && (count<100*scale*scale || !dark || !bright || minx!=40*scale || miny>50*scale+1 || maxy<72*scale))
                throw std::runtime_error("Cursor shape, hotspot or outline check failed");
        }
        renderer.reset(); DestroyWindow(window); return 0;
    } catch (const std::exception& error) { if(window) DestroyWindow(window); std::cerr<<error.what()<<'\n'; return 1; }
}
