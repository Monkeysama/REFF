#include "embedded.hpp"
#include <windows.h>
#include <stdexcept>

namespace reff {
// 仅查当前模块的固定资源索引；返回内存视图，不打开外部文件或复制到缓存。
std::optional<std::string_view> embedded_resource(std::string_view name){
    struct Entry { std::string_view name; int id; };
    static constexpr Entry entries[]={
#include "resources.inc"
    };
    for(const auto& entry:entries)if(entry.name==name){
        const auto module=GetModuleHandleW(nullptr);
        const auto resource=FindResourceW(module,MAKEINTRESOURCEW(entry.id),RT_RCDATA);
        if(!resource)throw std::runtime_error("embedded resource missing");
        const auto size=SizeofResource(module,resource);
        const auto data=LockResource(LoadResource(module,resource));
        if(!data)throw std::runtime_error("embedded resource unavailable");
        return std::string_view(static_cast<const char*>(data),size);
    }
    return {};
}
}
