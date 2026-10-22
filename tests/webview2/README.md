# WebView2 独立验证工具

这里的程序只创建并控制自己的隐藏宿主、IPC、缓存与图形资源，不启动或操作游戏。它们通过正式 `reff-webview2-host.exe` 的受控资源路由运行，不依赖原型宿主。

- `smoke.cpp`：真实网页捕获、共享帧和 WARP DX12 合成像素，7 个视口、交换链重建及连续缩放。
- `fault.cpp`：捕获资源重建、隐藏后缩放恢复、渲染/浏览器进程故障、断管道、自有宿主退出和正常关闭。故障注入仅在宿主显式使用 `--selftest` 时开放。
- `benchmark.cpp`、`benchmark-assets/`：固定动态页面依次显示、隐藏、再显示，每阶段 12 秒，记录帧与阶段时间。它不测游戏 FPS、系统 IME、剪贴板或输入延迟。

根工程或 `native/webview2_host/` 独立工程在 `BUILD_TESTING=ON` 时构建 `reff-wv2-dx12`、`reff-wv2-fault`、`reff-wv2-benchmark`。正式包不包含测试程序和负载页面。

仓库根目录运行：

```powershell
.\tools\fetch-webview2-sdk.ps1
.\tools\build.ps1
.\tools\test-webview2-p1.ps1 -BuildRoot .\build-webview2-release
.\tools\test-webview2-p1.ps1 -BuildRoot .\build-webview2-release -Behavior
.\tools\benchmark-webview2.ps1 -BuildRoot .\build-webview2-release
```

SDK 版本和归档 SHA-256 只读取根目录的 `dependencies.lock.json`；准备 SDK 不安装系统 Runtime。需要完整源码依赖时使用 `tools/fetch-dependencies.ps1`。

旧性能数据保留在历史 artifacts 中。当前负载改走正式本地资源服务，不能与旧原型内联页面样本直接作性能优劣结论。独立测试通过也不能代替用户的游戏内验收。
