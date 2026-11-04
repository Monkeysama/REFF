#pragma once
#include "renderer.hpp"
#include <stdexcept>
#include <vector>

namespace reff::test {
// 独立 WARP 合成夹具：只创建自有隐藏窗口、交换链及单色提供者，不读取游戏资源。
class PreviewGpu {
    template<class T> using C=Microsoft::WRL::ComPtr<T>;
    static void check(HRESULT h){if(FAILED(h))throw std::runtime_error("preview GPU HRESULT="+std::to_string(h));}
    HWND window_{};C<ID3D12Device> device_;C<ID3D12CommandQueue> queue_;C<IDXGISwapChain1> swapchain_;
    C<ID3D12Resource> target_,readback_;C<ID3D12DescriptorHeap> rtv_;C<ID3D12CommandAllocator> allocator_;
    C<ID3D12GraphicsCommandList> commands_;C<ID3D12Fence> fence_;Handle event_;UINT64 sequence_{},bytes_{};
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint_{};
    struct Color {float rgba[4]{0,1,0,1};C<ID3D12DescriptorHeap> rtv;int* releases{};int* draws{};} color_;
    REFFPreviewHandle handle_{};
    void transition(D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={target_.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};commands_->ResourceBarrier(1,&b);}
    void begin(){check(allocator_->Reset());check(commands_->Reset(allocator_.Get(),nullptr));}
    void submit(){check(commands_->Close());ID3D12CommandList* lists[]{commands_.Get()};queue_->ExecuteCommandLists(1,lists);check(queue_->Signal(fence_.Get(),++sequence_));check(fence_->SetEventOnCompletion(sequence_,event_.get()));if(WaitForSingleObject(event_.get(),5000)!=WAIT_OBJECT_0)throw std::runtime_error("preview readback timeout");}
public:
    NativePreviews registry;
    Renderer renderer;
    int releases{},draws{};
    static constexpr int width=1920,height=1440;
    PreviewGpu(){
        WNDCLASSW cls{};cls.lpfnWndProc=DefWindowProcW;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"REFFNativePreviewFixture";RegisterClassW(&cls);
        window_=CreateWindowW(cls.lpszClassName,L"",WS_OVERLAPPEDWINDOW,0,0,width,height,nullptr,nullptr,cls.hInstance,nullptr);if(!window_)throw std::runtime_error("preview fixture window");
        C<IDXGIFactory4> factory;check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));C<IDXGIAdapter> warp;check(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)));
        check(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device_)));D3D12_COMMAND_QUEUE_DESC q{};check(device_->CreateCommandQueue(&q,IID_PPV_ARGS(&queue_)));
        DXGI_SWAP_CHAIN_DESC1 sd{};sd.Width=width;sd.Height=height;sd.Format=DXGI_FORMAT_R8G8B8A8_UNORM;sd.SampleDesc.Count=1;sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;sd.BufferCount=2;sd.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
        check(factory->CreateSwapChainForHwnd(queue_.Get(),window_,&sd,nullptr,nullptr,&swapchain_));check(swapchain_->GetBuffer(0,IID_PPV_ARGS(&target_)));
        D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=1;check(device_->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rtv_)));device_->CreateRenderTargetView(target_.Get(),nullptr,rtv_->GetCPUDescriptorHandleForHeapStart());
        check(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator_)));check(device_->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator_.Get(),nullptr,IID_PPV_ARGS(&commands_)));check(commands_->Close());
        check(device_->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence_)));event_.reset(CreateEventW(nullptr,FALSE,FALSE,nullptr));
        auto desc=target_->GetDesc();device_->GetCopyableFootprints(&desc,0,1,0,&footprint_,nullptr,nullptr,&bytes_);
        D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_READBACK;D3D12_RESOURCE_DESC rb{};rb.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rb.Width=bytes_;rb.Height=1;rb.DepthOrArraySize=1;rb.MipLevels=1;rb.SampleDesc.Count=1;rb.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        check(device_->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rb,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback_)));
        if(!renderer.initialize(device_.Get(),swapchain_.Get(),queue_.Get()))throw std::runtime_error(renderer.last_error());
        color_.releases=&releases;color_.draws=&draws;enable();
    }
    ~PreviewGpu(){disable();renderer.reset();if(window_)DestroyWindow(window_);}
    void enable(){if(handle_)return;REFFPreviewProvider provider{sizeof(provider),"example.vue","portrait",&color_,
        [](void* user,const REFFPreviewDrawContext* c){auto& p=*static_cast<Color*>(user);++*p.draws;
            if(!p.rtv){D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=1;check(c->device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&p.rtv)));}
            c->device->CreateRenderTargetView(c->target,nullptr,p.rtv->GetCPUDescriptorHandleForHeapStart());
            RECT clip{LONG(std::ceil(c->clip.x)),LONG(std::ceil(c->clip.y)),LONG(std::floor(c->clip.x+c->clip.width)),LONG(std::floor(c->clip.y+c->clip.height))};
            c->commands->ClearRenderTargetView(p.rtv->GetCPUDescriptorHandleForHeapStart(),p.rgba,1,&clip);
        },[](void* user){++*static_cast<Color*>(user)->releases;},0};
        handle_=registry.add(&provider);if(!handle_)throw std::runtime_error("preview provider registration failed");}
    void disable(){if(handle_){registry.remove(handle_);handle_=0;}}
    void rebuild(){renderer.reset();if(!renderer.initialize(device_.Get(),swapchain_.Get(),queue_.Get()))throw std::runtime_error(renderer.last_error());}
    // 用正式 Renderer 合成真实浏览器帧，并读取实际 GPU 输出；网页图片不回传给提供者。
    std::vector<unsigned char> render(SharedFrame& frame,int fw,int fh,std::uint64_t generation,std::uint64_t revision,bool cursor=false){
        begin();transition(D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_RENDER_TARGET);const float black[]{0,0,0,1};commands_->ClearRenderTargetView(rtv_->GetCPUDescriptorHandleForHeapStart(),black,0,nullptr);transition(D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PRESENT);submit();
        if(!renderer.draw(frame,{45,55},cursor,{width,height},{0,0},{0,0,fw,fh},0,generation,false,&registry,revision))throw std::runtime_error(renderer.last_error());
        begin();transition(D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_COPY_SOURCE);D3D12_TEXTURE_COPY_LOCATION from{};from.pResource=target_.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;D3D12_TEXTURE_COPY_LOCATION to{};to.pResource=readback_.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=footprint_;commands_->CopyTextureRegion(&to,0,0,0,&from,nullptr);transition(D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_PRESENT);submit();
        void* data{};D3D12_RANGE range{0,SIZE_T(bytes_)};check(readback_->Map(0,&range,&data));std::vector<unsigned char> pixels(width*height*4);for(int y=0;y<height;++y)std::memcpy(pixels.data()+y*width*4,static_cast<unsigned char*>(data)+footprint_.Offset+y*footprint_.Footprint.RowPitch,width*4);readback_->Unmap(0,nullptr);renderer.collect_previews();return pixels;
    }
};
}
