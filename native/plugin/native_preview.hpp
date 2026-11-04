#pragma once
#include "../include/reff/native_preview.h"
#include "preview_frame.hpp"
#include <functional>
#include <memory>
#include <mutex>
#include <map>
#include <vector>
#include <string>
#include <Windows.h>

namespace reff {
// 注册表只管显示提供者，不触碰游戏相机/Lua；回调期间互斥以让注销完成后不再进入回调。
class NativePreviews {
public:
    struct Provider {
        std::string plugin,id;
        void* user{}; REFFPreviewDraw draw{}; REFFPreviewRelease release{}; HMODULE module{}; uint32_t flags{}; bool accepted{};
        ~Provider();
    };
    using Lease=std::shared_ptr<Provider>;
    REFFPreviewHandle add(const REFFPreviewProvider* value);
    bool update(REFFPreviewHandle handle,const REFFPreviewProvider* value);
    bool remove(REFFPreviewHandle handle);
    Lease record(const PreviewSlot& slot,const REFFPreviewDrawContext& context);
private:
    Lease make(const REFFPreviewProvider* value);
    std::mutex mutex_;
    std::map<REFFPreviewHandle,Lease> providers_;
    REFFPreviewHandle sequence_{};
};
NativePreviews& native_previews();
void defer_preview_release(ID3D12Fence* fence,std::uint64_t value,std::vector<NativePreviews::Lease> leases);
void collect_retired_previews();
}
