#include <Windows.h>
#include "reff/native_preview.h"
#include <stdexcept>
#include <iostream>
// 加载本地构建的真实 DLL 校验 C ABI；不初始化 REFramework、不连接游戏。
int main(int argc,char** argv){try{
    if(argc!=2)return 2;const auto dll=LoadLibraryA(argv[1]);if(!dll)throw std::runtime_error("load preview API DLL");
    const auto get=reinterpret_cast<REFFGetNativePreviewAPI>(GetProcAddress(dll,"reff_get_native_preview_api"));
    if(!get||get(0)||get(2))throw std::runtime_error("preview ABI negotiation");
    const auto api=get(1);if(!api||api->size!=sizeof(*api)||api->abi_version!=1||api->max_slots!=4||api->capabilities!=15)throw std::runtime_error("preview API capabilities");
    if(api->register_provider(nullptr)||api->update_provider(999,nullptr)||api->unregister_provider(999))throw std::runtime_error("preview API invalid handle");
    std::cout<<"Native preview ABI v1/capabilities/invalid handles passed\n";FreeLibrary(dll);return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
