#include "reff/native_preview.h"
/* 用真正的 C 编译器验证公共头，不依赖 C++ 模板或 COM 头文件。 */
int reff_preview_c_header_contract(const REFFNativePreviewAPI* api) { return api && api->abi_version == REFF_PREVIEW_ABI_V1; }
