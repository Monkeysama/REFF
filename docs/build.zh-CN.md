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

先构建 Web，再构建原生宿主：官方网页、公共模块、版本及依赖元数据会写入 EXE。修改官方网页后需重新构建宿主；插件自己的网页构建同步流程不变。默认输出到 `build-webview2-release`。

浏览器自检需要提供宿主所需接口的 WebView2 Evergreen 稳定版 Runtime 和可用的 Windows 图形会话；自检使用实际内嵌 Shell 和三个磁盘插件，只操作独立隐藏宿主，不操作游戏。

`tools/release.ps1 -Tag v1.0.3` 只生成核心 `REFF-1.0.3.zip` 及其 SHA-256，不生成工具包、实验包、示例包或预览版。标签必须与 `version.json` 完全一致。显式 `-Publish` 上传正式 Release；GitHub Actions 推送正式版本标签时自动构建、回归并发布，手动运行可选择只生成资产。发布说明仅使用当前版本的 CHANGELOG 章节。已发布版本的说明更正可通过 `tools/update-release-metadata.ps1 -Tag v1.0.3` 完成，需要已登录的 GitHub CLI；它不改变版本标签或核心附件。

推送 `main` 并修改 CHANGELOG 或发布说明维护脚本时，Actions 自动同步当前已发布版本的说明并移除该版本的旧工具附件；不会重建核心包或移动标签。
