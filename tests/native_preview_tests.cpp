#include "webview2/preview_gpu.hpp"
#include <iostream>

using namespace reff;
int main(){try{
    // 注册失败不接管 user；替换/注销禁止新回调，但在途 GPU 租约继续保持旧实例。
    { NativePreviews registry;int released=0,called=0;
      struct State {int* releases;int* calls;} first{&released,&called},second{&released,&called};
      REFFPreviewProvider desc{sizeof(desc),"example.vue","portrait",&first,
        [](void* u,const REFFPreviewDrawContext*){++*static_cast<State*>(u)->calls;},
        [](void* u){++*static_cast<State*>(u)->releases;},0};
      const auto handle=registry.add(&desc);if(!handle||registry.add(&desc))throw std::runtime_error("duplicate provider accepted");
      PreviewSlot slot{};std::strcpy(slot.plugin,"example.vue");std::strcpy(slot.id,"portrait");REFFPreviewDrawContext context{};context.target_format=DXGI_FORMAT_R8G8B8A8_UNORM;
      context.target_format=DXGI_FORMAT_R10G10B10A2_UNORM;if(registry.record(slot,context))throw std::runtime_error("unconfirmed SDR10 accepted");
      context.target_format=DXGI_FORMAT_R8G8B8A8_UNORM;auto lease=registry.record(slot,context);
      if(registry.update(handle,&desc))throw std::runtime_error("same user transferred twice");desc.user=&second;desc.flags=REFF_PREVIEW_SDR10_CONFIRMED;
      if(!registry.update(handle,&desc)||released||!registry.remove(handle)||released!=1||registry.record(slot,context)||called!=1)throw std::runtime_error("provider update/unregister contract");
      lease.reset();if(released!=2)throw std::runtime_error("in-flight lease released early or leaked");
    }
    test::PreviewGpu gpu;SharedFrame frame;frame.open(make_session(),true);
    PreviewFrame meta{};meta.revision=3;meta.count=1;auto& slot=meta.slots[0];std::strcpy(slot.plugin,"example.vue");std::strcpy(slot.id,"portrait");slot.destination={20,30,240,240};slot.clip={30,40,180,180};
    std::vector<unsigned char> bgra(960*640*4);
    // 正常网页控件、半透明蓝色遮罩和最终虚拟光标，均应在预览之上。
    for(int y=70;y<100;++y)for(int x=70;x<100;++x){auto p=&bgra[(y*960+x)*4];p[2]=255;p[3]=255;}
    for(int y=70;y<100;++y)for(int x=120;x<150;++x){auto p=&bgra[(y*960+x)*4];p[0]=128;p[3]=128;}
    frame.write(bgra.data(),960,640,1,meta);
    auto verify=[&](bool shown,std::uint64_t generation,std::uint64_t revision,bool cursor=false){auto pixels=gpu.render(frame,960,640,generation,revision,cursor);
        auto rgb=[&](int x,int y,int r,int g,int b){auto p=&pixels[(y*test::PreviewGpu::width+x)*4];if(std::abs(int(p[0])-r)>2||std::abs(int(p[1])-g)>2||std::abs(int(p[2])-b)>2)throw std::runtime_error("preview pixel mismatch "+std::to_string(x)+","+std::to_string(y)+" = "+std::to_string(p[0])+","+std::to_string(p[1])+","+std::to_string(p[2]));};
        const bool fill=meta.count||(meta.flags&preview_surface_active);const int r=shown?0:fill?17:0,g=shown?255:fill?24:0,b=shown?0:fill?25:0;
        rgb(200,200,r,g,b);rgb(25,45,(!meta.count&&fill)||generation!=1||revision!=meta.revision?17:0,(!meta.count&&fill)||generation!=1||revision!=meta.revision?24:0,(!meta.count&&fill)||generation!=1||revision!=meta.revision?25:0);rgb(80,80,255,0,0);rgb(130,80,r*127/255,g*127/255,128+b*127/255);
        if(cursor){auto p=&pixels[(60*test::PreviewGpu::width+47)*4];if(p[0]==0&&p[1]==255&&p[2]==0)throw std::runtime_error("cursor hidden by preview");}
    };
    verify(true,1,3,true);verify(false,2,3);verify(false,1,4);gpu.rebuild();verify(true,1,3);
    const auto old_draws=gpu.draws;gpu.disable();verify(false,1,3);if(gpu.draws!=old_draws||gpu.releases!=1)throw std::runtime_error("unregister/GPU retirement failed");
    meta.count=0;meta.flags=preview_surface_active;frame.write(bgra.data(),960,640,1,meta);gpu.enable();verify(false,1,3);meta.flags=0;frame.write(bgra.data(),960,640,1,meta);verify(false,1,3);gpu.disable();gpu.renderer.collect_previews();
    // 新帧尚无安全孔位时仍须补底；SDR8/BGRA8/已确认 SDR10 都关闭透明穿透，旧代次不能进入回调。
    for(auto format:{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_B8G8R8A8_UNORM,DXGI_FORMAT_R10G10B10A2_UNORM}){
        test::PreviewGpu surface(format,true);SharedFrame snapshot;snapshot.open(make_session(),true);PreviewFrame pending;pending.flags=preview_surface_active;pending.revision=1;
        std::vector<unsigned char> empty(320*240*4);snapshot.write(empty.data(),320,240,1,pending);
        const auto output=surface.render(snapshot,320,240,2,2,false,true);const auto* c=output.data()+(120*test::PreviewGpu::width+160)*4;
        if(std::abs(int(c[0])-17)>1||std::abs(int(c[1])-24)>1||std::abs(int(c[2])-25)>1||surface.draws)throw std::runtime_error("transition fallback/format/stale callback failed");
        pending.count=1;pending.slots[0]=slot;snapshot.write(empty.data(),320,240,1,pending);
        surface.render(snapshot,320,240,1,1);if(surface.draws!=1)throw std::runtime_error("native preview did not recover from fallback");
        for(float alpha:{0.f,.5f,1.f}){
            surface.premultiplied(0,alpha,0,alpha);const auto image=surface.render(snapshot,320,240,1,1,false,true);const auto* p=image.data()+(120*test::PreviewGpu::width+160)*4;
            if(std::abs(int(p[0])-int(std::lround((1-alpha)*255)))>1||std::abs(int(p[1])-int(std::lround(alpha*255)))>1||std::abs(int(p[2])-int(std::lround((1-alpha)*255)))>1)
                throw std::runtime_error("valid provider alpha changed by backdrop");
        }
    }
    std::cout<<"Preview GPU: clipping/occlusion/alpha/cursor/stale viewport/revision/rebuild/unregister passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
