// 最小原生消费者：把单色 GPU 预览送入 example.vue/portrait；真实插件替换 Record 内部取景绘制。
// Start/Stop 在插件自己的生命周期线程调用，不从 Record/Release 重入 REFF API。
#include <Windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <cmath>
#include "reff/native_preview.h"

namespace {
struct Surface {
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv;
    // Present 录制；目标和命令列表均为借用，不保留它们，不改变 target 的资源状态。
    static void __cdecl Record(void* user,const REFFPreviewDrawContext* c){
        auto& s=*static_cast<Surface*>(user);
        if(s.device.Get()!=c->device){
            // 旧设备的在途资源应保留到它自己的 fence 完成；此示例仅持有 RTV 描述符堆。
            // 真实纹理/PSO 生产者按文档每帧 fence/value 保存租约，或通过 update_provider 替换实例。
            s.rtv.Reset();s.device=c->device;
        }
        if(!s.rtv){D3D12_DESCRIPTOR_HEAP_DESC desc{};desc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;desc.NumDescriptors=1;
            if(FAILED(c->device->CreateDescriptorHeap(&desc,IID_PPV_ARGS(&s.rtv))))return;}
        c->device->CreateRenderTargetView(c->target,nullptr,s.rtv->GetCPUDescriptorHandleForHeapStart());
        RECT clip{LONG(std::ceil(c->clip.x)),LONG(std::ceil(c->clip.y)),LONG(std::floor(c->clip.x+c->clip.width)),LONG(std::floor(c->clip.y+c->clip.height))};
        const float color[]{.04f,.25f,.45f,1};c->commands->ClearRenderTargetView(s.rtv->GetCPUDescriptorHandleForHeapStart(),color,1,&clip);
    }
    static void __cdecl Release(void* user){delete static_cast<Surface*>(user);}
};
const REFFNativePreviewAPI* api{};REFFPreviewHandle handle{};
}
extern "C" __declspec(dllexport) bool StartNativePreview(){
    if(handle)return true;const auto module=GetModuleHandleW(L"REFF.dll");if(!module)return false;
    const auto get=reinterpret_cast<REFFGetNativePreviewAPI>(GetProcAddress(module,"reff_get_native_preview_api"));
    api=get?get(REFF_PREVIEW_ABI_V1):nullptr;if(!api||api->size<sizeof(*api)||!(api->capabilities&REFF_PREVIEW_CAP_DX12_CALLBACK))return false;
    auto* surface=new Surface;REFFPreviewProvider provider{sizeof(provider),"example.vue","portrait",surface,&Surface::Record,&Surface::Release,0};
    handle=api->register_provider(&provider);if(!handle)delete surface;return handle!=0;
}
extern "C" __declspec(dllexport) void StopNativePreview(){if(handle){api->unregister_provider(handle);handle=0;}}
