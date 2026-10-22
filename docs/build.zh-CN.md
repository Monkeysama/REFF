# REFF 从源码构建

## 构建环境

- Windows x64，Visual Studio 2022 或 2026，安装 C++ 工具链、CMake 和 Windows SDK。
- Node.js 和 pnpm；当前 GitHub Actions 使用 Node.js 24.19.0、pnpm 11.19.0。
- 浏览器自检需要 Windows 10 22H2 或更新系统，以及可用的 WebView2 Evergreen Runtime。

克隆仓库后，在仓库根目录执行以下命令。依赖脚本按锁文件下载并校验 REFramework 和 WebView2 SDK。

## 构建与自检

```powershell
.\tools\fetch-dependencies.ps1
pnpm --dir .\web\shell install --frozen-lockfile
.\tools\build-web.ps1 -IncludeExamples
.\tools\build.ps1
.\tools\test-webview2-p1.ps1 -BuildRoot .\build-webview2-release
.\tools\test-webview2-p1.ps1 -BuildRoot .\build-webview2-release -Behavior
.\tools\stage.ps1
.\tools\test-distribution.ps1
```

默认构建输出到 `build-webview2-release`，仅构建 WebView2 宿主。浏览器自检需要 WebView2 Evergreen 稳定版 Runtime ≥155.0.4283.62 和可用的 Windows 图形会话；自检仅操作独立隐藏宿主，不操作游戏。`tools/release.ps1 -Tag v1.0.0` 仅生成 `REFF-1.0.0.zip` 和 SHA-256，不再生成实验包、示例包或预览版。标签必须与 `version.json` 完全一致。显式 `-Publish` 上传正式 Release；GitHub Actions 推送正式版本标签时自动构建、回归并发布，手动运行可选择只生成资产。
