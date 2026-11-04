#include "native_preview.hpp"
#include <d3d12.h>
#include <wrl/client.h>

namespace reff {
NativePreviews::Provider::~Provider(){if(accepted&&release)release(user);if(module)FreeLibrary(module);}
// 先校验并保留回调模块；注册表提交成功前不接管 user，分配失败不会误调用 release。
NativePreviews::Lease NativePreviews::make(const REFFPreviewProvider* v){
    if(!v||v->size!=sizeof(*v)||!v->plugin_id||!v->slot_id||!v->draw||!v->release||
        !preview_name(v->plugin_id,96)||!preview_name(v->slot_id,64))return {};
    HMODULE module{};
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(v->draw),&module))return {};
    HMODULE release_module{};
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(v->release),&release_module)||release_module!=module){FreeLibrary(module);return {};}
    auto p=std::make_shared<Provider>();p->plugin=v->plugin_id;p->id=v->slot_id;
    p->user=v->user;p->draw=v->draw;p->release=v->release;p->module=module;p->flags=v->flags;return p;
}
// 调用方线程注册；互斥区只覆盖注册表和短暂 draw，不等待 GPU。
REFFPreviewHandle NativePreviews::add(const REFFPreviewProvider* v){
    // 失败不接管 user；release 只对应成功注册/更新过的实例。
    std::lock_guard lock(mutex_);
    if(providers_.size()>=preview_slots)return 0;
    for(const auto& [_,p]:providers_)if(v&&v->plugin_id&&v->slot_id&&p->plugin==v->plugin_id&&p->id==v->slot_id)return 0;
    auto p=make(v);if(!p)return 0;const auto id=++sequence_;providers_.emplace(id,p);p->accepted=true;return id;
}
// 替换为独立资源实例；旧实例退出互斥区后由最后一份 GPU 租约负责释放。
bool NativePreviews::update(REFFPreviewHandle handle,const REFFPreviewProvider* v){
    Lease old;
    {std::lock_guard lock(mutex_);const auto it=providers_.find(handle);if(it==providers_.end()||!v||!v->plugin_id||!v->slot_id||it->second->plugin!=v->plugin_id||it->second->id!=v->slot_id)return false;
        if(v->user&&v->user==it->second->user)return false;
        auto p=make(v);if(!p)return false;old=std::move(it->second);p->accepted=true;it->second=std::move(p);}
    return true;
}
// 返回后不再开始该提供者的新回调；在途命令仍保留它的共享实例。
bool NativePreviews::remove(REFFPreviewHandle handle){
    Lease old;{std::lock_guard lock(mutex_);auto it=providers_.find(handle);if(it==providers_.end())return false;old=std::move(it->second);providers_.erase(it);}return true;
}
// Present 线程录制；调用者把返回租约绑定到本次命令的完成 fence，回调禁止重入 API。
NativePreviews::Lease NativePreviews::record(const PreviewSlot& slot,const REFFPreviewDrawContext& context){
    std::lock_guard lock(mutex_);
    for(const auto& [_,p]:providers_)if(p->plugin==slot.plugin&&p->id==slot.id){
        if(context.target_format==DXGI_FORMAT_R10G10B10A2_UNORM&&!(p->flags&REFF_PREVIEW_SDR10_CONFIRMED))return {};
        p->draw(p->user,&context);return p;}
    return {};
}
NativePreviews& native_previews(){static auto* registry=new NativePreviews;return *registry;}
namespace {
struct Retired {Microsoft::WRL::ComPtr<ID3D12Fence> fence;std::uint64_t value;std::vector<NativePreviews::Lease> leases;};
std::mutex retired_mutex;
auto& retired(){static auto* list=new std::vector<Retired>;return *list;}
}
// 设备重建等待超时时继续保留回调模块/资源，直到旧队列完成；设备移除的 fence 为 UINT64_MAX。
void defer_preview_release(ID3D12Fence* fence,std::uint64_t value,std::vector<NativePreviews::Lease> leases){
    if(leases.empty())return;std::lock_guard lock(retired_mutex);retired().push_back({fence,value,std::move(leases)});
}
void collect_retired_previews(){
    std::vector<Retired> completed;
    {std::lock_guard lock(retired_mutex);for(auto it=retired().begin();it!=retired().end();){
        if(it->fence->GetCompletedValue()>=it->value){completed.push_back(std::move(*it));it=retired().erase(it);}else ++it;}}
}
}
// 只导出不可变函数表；C ABI 按版本查询，不暴露 C++ 容器、异常或 Lua 状态。
extern "C" __declspec(dllexport) const REFFNativePreviewAPI* __cdecl reff_get_native_preview_api(uint32_t version){
    static const REFFNativePreviewAPI api{sizeof(REFFNativePreviewAPI),1,reff::preview_slots,15,
        [](const REFFPreviewProvider* p)->REFFPreviewHandle{try{return reff::native_previews().add(p);}catch(...){return 0;}},
        [](REFFPreviewHandle h,const REFFPreviewProvider* p)->int{try{return reff::native_previews().update(h,p);}catch(...){return 0;}},
        [](REFFPreviewHandle h)->int{try{return reff::native_previews().remove(h);}catch(...){return 0;}}};
    return version==1?&api:nullptr;
}
