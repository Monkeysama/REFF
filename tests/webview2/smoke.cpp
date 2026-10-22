#include "renderer.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace reff;
namespace {
// 所有资源只属于独立测试进程；失败时回收测试宿主，不影响用户的游戏或其他浏览器。
void check(HRESULT hr) {if(FAILED(hr))throw std::runtime_error("DX12 HRESULT="+std::to_string(hr));}
}

// 真实 WebView2 → Visual 捕获 → IPC/BGRA → 现有 DX12 Renderer 的联调，不替代游戏验收。
int wmain(int argc,wchar_t** argv) {
    if(argc!=5)return 1;
    HWND window{};PROCESS_INFORMATION process{};Channel channel;
    try {
        const std::filesystem::path host=std::filesystem::absolute(argv[1]),output=std::filesystem::absolute(argv[2]);
        std::filesystem::create_directories(output);
        const auto session=make_session();SharedFrame frame;frame.open(session,true);
        Queue<Json> incoming;
        channel.start(session,true,[&](Json message){incoming.push(std::move(message));});
        const auto cache=output/L"data"/L"REFF"/L"cache";
        std::filesystem::create_directories(cache);
        auto command=L"\""+host.wstring()+L"\" --reff-session="+session+L" --reff-cache=\""+cache.wstring()+L"\"";
        // 显式传入独立资源根，不接触游戏资源。
        command+=L" --reff-assets=\""+std::filesystem::absolute(argv[3]).wstring()+
            L"\" --reff-manifests=\""+std::filesystem::absolute(argv[4]).wstring()+L"\" --reff-game=MHWILDS";
        STARTUPINFOW startup{sizeof(startup)};startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
        if(!CreateProcessW(host.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process))
            throw std::runtime_error("host launch failed");
        CloseHandle(process.hThread);process.hThread=nullptr;
        WNDCLASSW cls{};cls.lpfnWndProc=DefWindowProcW;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"REFFWebView2Smoke";
        RegisterClassW(&cls);
        window=CreateWindowW(cls.lpszClassName,L"",WS_OVERLAPPEDWINDOW,0,0,1280,960,nullptr,nullptr,cls.hInstance,nullptr);
        if(!window)throw std::runtime_error("hidden DX12 window creation failed");
        Com<IDXGIFactory4> factory;check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
        Com<IDXGIAdapter> warp;check(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)));
        Com<ID3D12Device> device;check(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
        Com<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC qd{};check(device->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)));
        DXGI_SWAP_CHAIN_DESC1 sd{};sd.Width=1280;sd.Height=960;sd.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.SampleDesc.Count=1;sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;sd.BufferCount=2;sd.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
        Com<IDXGISwapChain1> swapchain;check(factory->CreateSwapChainForHwnd(queue.Get(),window,&sd,nullptr,nullptr,&swapchain));
        Renderer renderer;if(!renderer.initialize(device.Get(),swapchain.Get(),queue.Get()))throw std::runtime_error(renderer.last_error());
        const auto begun=GetTickCount64();bool hello=false;int phase=0,phase_frames=0;std::uint64_t sequence{},generation{};
        std::vector<std::uint8_t> pixels;int width{},height{};
        Json report={{"phases",Json::array()},{"gameValidated",false}};
        // 覆盖常用视口和共享缓冲上限；仅改变自有测试宿主，不切换游戏分辨率或系统 DPI。
        const std::pair<int,int> sizes[]={{960,640},{1280,720},{1920,1080},{2560,1440},{640,480},{1000,700},{960,640}};
        constexpr int phase_count=static_cast<int>(std::size(sizes));
        while(phase<phase_count && GetTickCount64()-begun<45000) {
            MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
            while(auto message=incoming.pop())if(message->value("type",std::string{})=="hello") {
                if(message->value("sessionId",std::string{})!=narrow(session))throw std::runtime_error("session mismatch");
                hello=true;channel.send({{"type","visible"},{"value",true}});
                channel.send({{"type","viewport"},{"width",sizes[phase].first},{"height",sizes[phase].second},{"generation",phase+1}});
            }
            if(frame.read(pixels,sequence,width,height,generation,phase+1)) {
                if(width!=sizes[phase].first || height!=sizes[phase].second)throw std::runtime_error("viewport generation/size mismatch");
                const auto* pixel=pixels.data()+(32*width+32)*4;
                if(pixel[0]!=224 || pixel[1]!=160 || pixel[2]!=64 || pixel[3]!=255)throw std::runtime_error("web pixels mismatch");
                if(!renderer.draw(frame,{30,30},false,{1280,960},{20,20},{20,20,20+width,20+height},0,phase+1,false))
                    throw std::runtime_error(renderer.last_error());
                ++phase_frames;
                if(phase_frames>=12) {
                    report["phases"].push_back({{"generation",phase+1},{"width",width},{"height",height},{"frames",phase_frames}});
                    ++phase;phase_frames=0;
                    if(phase<phase_count)channel.send({{"type","viewport"},{"width",sizes[phase].first},{"height",sizes[phase].second},{"generation",phase+1}});
                }
            }
            if(WaitForSingleObject(process.hProcess,0)==WAIT_OBJECT_0)throw std::runtime_error("host exited early");
            Sleep(8);
        }
        if(!hello || phase!=phase_count)throw std::runtime_error("IPC or frame output timed out");
        // 释放合成器后重建自有交换链缓冲；验证原浏览器会话可重新上传已有共享帧。
        // 这覆盖正常 ResizeBuffers，不等同于物理 GPU 移除或游戏设备重建验收。
        report["swapchainRebuilds"]=Json::array();
        for(const auto target_size : {std::pair{1920,1080},std::pair{1280,960}}) {
            renderer.reset();
            check(swapchain->ResizeBuffers(2,target_size.first,target_size.second,DXGI_FORMAT_R8G8B8A8_UNORM,0));
            if(!renderer.initialize(device.Get(),swapchain.Get(),queue.Get()))throw std::runtime_error(renderer.last_error());
            if(!renderer.draw(frame,{30,30},false,target_size,{20,20},{20,20,20+width,20+height},0,phase_count,false))
                throw std::runtime_error(renderer.last_error());
            if(renderer.displayed_frame_generation()!=phase_count)throw std::runtime_error("swapchain rebuild lost shared frame generation");
            report["swapchainRebuilds"].push_back({{"width",target_size.first},{"height",target_size.second},{"generation",phase_count}});
        }
        // 模拟持续拖动的尺寸流，而不是逐项等待；画面必须在请求持续变化时继续交付，不能只在松手后恢复。
        std::map<std::uint64_t,std::pair<int,int>> requested;
        requested[phase_count]=sizes[phase_count-1];
        auto stream_start=GetTickCount64(),last_request=stream_start,last_frame=stream_start;
        std::uint64_t request_generation=phase_count,max_gap=0;
        int during_drag=0,total_stream_frames=0;bool settled=false,final_sent=false;
        while(GetTickCount64()-stream_start<7000) {
            const auto now=GetTickCount64(),elapsed=now-stream_start;
            if(elapsed<4000 && now-last_request>=16) {
                ++request_generation;
                const int w=740+int(request_generation%51)*5,h=480+int(request_generation%31)*4;
                requested[request_generation]={w,h};
                channel.send_priority_latest({{"type","viewport"},{"width",w},{"height",h},{"generation",request_generation}});
                last_request=now;
            } else if(elapsed>=4000 && !final_sent) {
                ++request_generation;requested[request_generation]=sizes[phase_count-1];
                channel.send_priority_latest({{"type","viewport"},{"width",sizes[phase_count-1].first},{"height",sizes[phase_count-1].second},{"generation",request_generation}});
                final_sent=true;
            }
            if(frame.read(pixels,sequence,width,height,generation)) {
                auto it=requested.find(generation);
                if(it==requested.end() || it->second!=std::pair{width,height})throw std::runtime_error("continuous resize mixed viewport generations");
                const auto* sample=pixels.data()+(32*width+32)*4;
                if(sample[0]!=224 || sample[1]!=160 || sample[2]!=64 || sample[3]!=255)throw std::runtime_error("continuous resize invalid web pixels");
                ++total_stream_frames;
                if(elapsed<4000 && generation>phase_count) {++during_drag;max_gap=std::max(max_gap,now-last_frame);last_frame=now;}
                if(final_sent && generation==request_generation)settled=true;
                if(!renderer.draw(frame,{30,30},false,{1280,960},{20,20},{20,20,20+width,20+height},0,generation,false))
                    throw std::runtime_error(renderer.last_error());
            }
            if(settled)break;
            if(WaitForSingleObject(process.hProcess,0)==WAIT_OBJECT_0)throw std::runtime_error("host exited during continuous resize");
            Sleep(8);
        }
        report["continuousResize"]={{"requests",request_generation-phase_count},{"framesDuringDrag",during_drag},
            {"totalFrames",total_stream_frames},{"maxGapMs",max_gap},{"settled",settled},
            {"passed",during_drag>=12 && max_gap<1000 && settled}};
        std::ofstream(output/"resize-summary.json")<<report["continuousResize"].dump(2);
        if(!report["continuousResize"]["passed"].get<bool>()) {
            std::cerr<<report["continuousResize"].dump(2)<<'\n';
            throw std::runtime_error("no continuous layout delivery while viewport requests remain in flight");
        }
        // 从实际 DX12 后备缓冲读回蓝色测试块，证明网页像素确实合成到了交换链而非只增加绘制计数。
        Com<ID3D12Resource> target;check(swapchain->GetBuffer(0,IID_PPV_ARGS(&target)));
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=256;
        buffer.Height=1;buffer.DepthOrArraySize=1;buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        Com<ID3D12Resource> readback;check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)));
        Com<ID3D12CommandAllocator> allocator;check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
        Com<ID3D12GraphicsCommandList> list;check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition={target.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_COPY_SOURCE};
        list->ResourceBarrier(1,&barrier);
        D3D12_TEXTURE_COPY_LOCATION source{};source.pResource=target.Get();source.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION dest{};dest.pResource=readback.Get();dest.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dest.PlacedFootprint.Footprint={DXGI_FORMAT_R8G8B8A8_UNORM,1,1,1,256};
        D3D12_BOX box{52,52,0,53,53,1};list->CopyTextureRegion(&dest,0,0,0,&source,&box);
        std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);list->ResourceBarrier(1,&barrier);check(list->Close());
        ID3D12CommandList* commands[]{list.Get()};queue->ExecuteCommandLists(1,commands);
        Com<ID3D12Fence> fence;check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
        Handle done(CreateEventW(nullptr,FALSE,FALSE,nullptr));check(queue->Signal(fence.Get(),1));check(fence->SetEventOnCompletion(1,done.get()));
        if(WaitForSingleObject(done.get(),3000)!=WAIT_OBJECT_0)throw std::runtime_error("DX12 pixel readback timed out");
        void* mapped{};D3D12_RANGE range{0,4};check(readback->Map(0,&range,&mapped));
        const auto* pixel=static_cast<const std::uint8_t*>(mapped);report["compositedRgba"]={pixel[0],pixel[1],pixel[2],pixel[3]};
        const bool composited=pixel[0]==64&&pixel[1]==160&&pixel[2]==224&&pixel[3]==255;readback->Unmap(0,nullptr);
        if(!composited)throw std::runtime_error("DX12 composited web pixel mismatch");
        const auto stats=renderer.take_stats();report["drawCalls"]=stats.draw_calls;report["textureUploads"]=stats.texture_uploads;
        report["passed"]=stats.draw_calls>=48 && stats.texture_uploads>0;
        channel.send({{"type","shutdown"}});
        if(WaitForSingleObject(process.hProcess,5000)!=WAIT_OBJECT_0)throw std::runtime_error("host shutdown timed out");
        renderer.reset();channel.stop();CloseHandle(process.hProcess);process.hProcess=nullptr;DestroyWindow(window);window=nullptr;
        std::ofstream(output/"smoke-summary.json")<<report.dump(2);std::cout<<report.dump(2)<<'\n';
        return report["passed"]==true?0:1;
    } catch(const std::exception& e) {
        channel.stop();
        if(process.hProcess){if(WaitForSingleObject(process.hProcess,0)!=WAIT_OBJECT_0)TerminateProcess(process.hProcess,1);CloseHandle(process.hProcess);}
        if(window)DestroyWindow(window);std::cerr<<e.what()<<'\n';return 1;
    }
}
