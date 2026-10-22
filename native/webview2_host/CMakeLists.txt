cmake_minimum_required(VERSION 3.24)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(REFFWebView2 LANGUAGES C CXX)
  include(CTest)
endif()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# WebView2 宿主可独立或随根项目构建；共享捕获和输入代码，所有浏览器对象归 STA 所有。
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
get_filename_component(REFF_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(WEBVIEW2_ROOT "${REFF_ROOT}/.deps/webview2" CACHE PATH "锁定 WebView2 SDK")
add_executable(reff-webview2-host WIN32 main.cpp runtime.cpp services.cpp
  ../common/ipc.cpp ../common/frame.cpp ../common/manifest.cpp)
target_include_directories(reff-webview2-host PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
  "${WEBVIEW2_ROOT}/build/native/include" "${REFF_ROOT}/.deps/reframework/dependencies/nlohmann" "${REFF_ROOT}/native/common")
target_compile_definitions(reff-webview2-host PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(reff-webview2-host PRIVATE /utf-8 /EHsc /W4)
target_link_libraries(reff-webview2-host PRIVATE "${WEBVIEW2_ROOT}/build/native/x64/WebView2LoaderStatic.lib"
  windowsapp d3d11 dxgi user32 ole32 shlwapi version shell32 CoreMessaging advapi32)

if(BUILD_TESTING)
# 独立自检使用与游戏插件相同提交的 Lua，不读取游戏进程；浏览器用隐藏 CompositionController。
file(GLOB LUA_SOURCES "${REFF_ROOT}/.deps/reframework/dependencies/lua/src/*.c")
list(FILTER LUA_SOURCES EXCLUDE REGEX "/(lua|luac)\\.c$")
add_library(reff_p1_lua STATIC ${LUA_SOURCES})
target_include_directories(reff_p1_lua PUBLIC "${REFF_ROOT}/.deps/reframework/dependencies/lua/src")
foreach(test IN ITEMS services integration)
  add_executable(reff-wv2-${test} "${REFF_ROOT}/tests/webview2_${test}.cpp" services.cpp ../common/ipc.cpp ../common/frame.cpp ../common/manifest.cpp)
  target_include_directories(reff-wv2-${test} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${REFF_ROOT}/native/common" "${REFF_ROOT}/.deps/reframework/dependencies/nlohmann")
  target_compile_definitions(reff-wv2-${test} PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_compile_options(reff-wv2-${test} PRIVATE /utf-8 /EHsc /W4)
  target_link_libraries(reff-wv2-${test} PRIVATE advapi32 ole32 reff_p1_lua)
endforeach()

# 浏览器联调用同一份配置仓库验证磁盘持久化，不复制另一套默认值或校验规则。
target_sources(reff-wv2-integration PRIVATE ../plugin/settings.cpp)
target_include_directories(reff-wv2-integration PRIVATE "${REFF_ROOT}/native/plugin")

# 正式资源路由下独立验证 DX12 像素、持续缩放、宿主故障及性能负载，不操作游戏。
add_executable(reff-wv2-dx12 "${REFF_ROOT}/tests/webview2/smoke.cpp" ../plugin/renderer.cpp ../common/ipc.cpp ../common/frame.cpp)
add_executable(reff-wv2-fault "${REFF_ROOT}/tests/webview2/fault.cpp" ../common/ipc.cpp ../common/frame.cpp)
add_executable(reff-wv2-benchmark "${REFF_ROOT}/tests/webview2/benchmark.cpp" ../common/ipc.cpp ../common/frame.cpp)
foreach(test IN ITEMS dx12 fault benchmark)
  target_include_directories(reff-wv2-${test} PRIVATE "${REFF_ROOT}/native/common" "${REFF_ROOT}/native/plugin" "${REFF_ROOT}/.deps/reframework/dependencies/nlohmann")
  target_compile_definitions(reff-wv2-${test} PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_compile_options(reff-wv2-${test} PRIVATE /utf-8 /EHsc)
  target_link_libraries(reff-wv2-${test} PRIVATE d3d12 dxgi d3dcompiler user32 ole32 advapi32)
endforeach()

endif()
