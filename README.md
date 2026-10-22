# REFF — REFramework Web UI Runtime

REFF 是面向 REFramework 插件的游戏内 Web UI 运行时，支持 Vue、React 和原生 HTML，并通过受控接口连接 Lua 与游戏功能。

REFF 是 REFramework 的扩展，不替换或修改 REFramework 原有菜单和功能。插件可以选择继续使用原生 UI，也可以声明使用 REFF 接管自己的界面。

## 功能

- 游戏内 WebView2 Web UI，使用系统共享的 Evergreen Runtime，不显示桌面浏览器窗口；页面只加载受控本地资源。
- 支持 Component/schema 和 Isolated Page 两种插件界面。
- Lua 插件注册、方法调用、事件发布与订阅。

## 支持范围

当前兼容状态：

| 游戏 | REFramework 目标名 | 图形接口 | 状态 |
| --- | --- | --- | --- |
| Monster Hunter Wilds | `MHWILDS` | D3D12 | WebView2 游戏人工验收通过（含设置、输入、三类前端、缩放及恢复） |
| Monster Hunter Rise | `MHRISE` | D3D12 | WebView2 游戏人工验收通过 |
| Resident Evil 4 | `RE4` | D3D12 | 保留能力档案；WebView2 未测，因本机无游戏暂缓验收 |

普通包保留以上 3 款游戏的原生能力档案；本轮按用户确认的 Wilds、Rise 范围验收；RE4 不作已验证声明。当前 WebView2 分发产物用于预发布验证。统一实验包另提供 16 个现代 D3D12 候选目标；它们不代表已完成兼容。D3D11 后端尚未实现，详细范围见[游戏兼容性](docs/compatibility.zh-CN.md)。

统一实验包用于没有本机验证条件的候选游戏，安装方式与正式包相同，但只应在对应游戏中使用。

实验包中的目标仍会经过运行时 D3D12 检查，未通过检查时不会安装游戏输入 Hook。

## 安装

需要已安装 REFramework、Windows 10 22H2（build 19045）或更新系统，以及 **Microsoft Edge WebView2 Evergreen 稳定版 Runtime ≥155.0.4283.62**。安装 Edge 浏览器不代表已安装 Runtime。REFF 检查依赖，缺失或过旧时关闭自身界面并记录原因，不自动下载安装器或提权。共享 Runtime 单独由 Microsoft 维护，不计入 REFF 包大小；这些最低门槛不等于已逐版本完成兼容测试。

1. 退出游戏，将对应包解压到临时目录，保留包内的 `reframework` 结构。
2. 运行随包安装器，指定游戏内的 `reframework` 目录：

   ```powershell
   & "<解压目录>\reframework\reff\tools\install-reff.ps1" -ReframeworkRoot "<游戏目录>\reframework"
   ```

3. 安装器校验源文件，备份覆盖内容，再逐文件安装并核对哈希。启动游戏，关闭 REFramework 原生菜单后按 **F8** 打开，按 **F8** 或 **Esc** 关闭。

系统 Runtime 可从 [Microsoft 官方 WebView2 页面](https://developer.microsoft.com/microsoft-edge/webview2/) 获取。直接合并解压可用于新安装；从 CEF 升级建议使用安装器，才能按旧清单清理已确认归属、且未被修改的旧引擎文件。没有旧清单或文件已被修改时会保留它们，不会猜测删除。

安装器输出备份目录，恢复时在退出游戏后运行：

```powershell
& "<解压目录>\reframework\reff\tools\install-reff.ps1" -ReframeworkRoot "<游戏目录>\reframework" -RestoreBackup "<游戏目录>\reframework\data\REFF\backups\<备份名>"
```

设置、第三方插件和独立示例会保留。备份含旧版本，升级后的游戏目录可能暂时仍很大；确认不再需要恢复后再自行删除对应备份。

示例插件为同版本独立增量包，安装核心包后合并到游戏目录即可。它包含 Vue 3、React、原生 HTML 的构建产物，以及 `reff/examples-dev` 下的源码、SDK、公共 UI 源码、独立 Vite 配置和锁定依赖，不重复包含 DLL、浏览器引擎或 Shell，也不包含 node_modules。普通包和实验包均不附带示例。

## 文件位置

```text
reframework/data/REFF/settings.json  用户设置
reframework/data/REFF/cache/webview2/  WebView2 临时缓存，可在退出游戏后清理
reframework/data/REFF/log/          REFF 专用诊断日志
reframework/data/REFF/backups/      安装事务备份；不属于 Runtime 分发体积
```

## 插件接入

第三方插件需要同时提供 manifest 和 Lua 注册代码。插件可以选择 schema 组件页面或独立隔离页面，并声明所需的方法和事件。

插件目录、Manifest、Lua 注册、Vue/React/原生接入和中文输入说明见：[插件开发](docs/plugin-development.zh-CN.md)。

插件名称可以提供中英文版本：

```json
{
  "name": "Interaction Example",
  "author": "Plugin Author",
  "localizedName": {
    "zh-CN": "Vue 3 接入示例",
    "en-US": "Vue 3 Integration Example"
  }
}
```

插件前端应使用 REFF 提供的公共 UI 资源和 i18n 约定。缺少当前语言时，REFF 会回退到英文名称，再回退到默认名称。

## 故障排查

- 面板不显示：确认 REFramework 已加载，关闭原生菜单后按 F8；检查 `data/REFF/log/`。
- 页面纯灰或资源缺失：确认 Runtime 包和示例包来自同一版本，并重新合并 `reframework` 目录。
- 依赖不可用：检查 `data/REFF/log/webview2/summary.json` 中的系统/Runtime 检测结果；安装或更新官方稳定版 Evergreen Runtime。
- 中文输入异常：默认构建已启用透明 IME 代理；检查 `data/REFF/log/` 并说明发生在主页面还是插件输入框。
- 修改 Lua 后：执行 REFramework 的 Reset Scripts；修改网页资源后重新打开插件页面即可，必要时增加资源版本号避免缓存。
- 最小诊断：运行包内 `reff/tools/collect-installed-compatibility.ps1 -ReframeworkRoot "<游戏目录>\reframework"`。只提取版本、哈希、数值和初始化状态，不复制原始日志、设置、缓存或输入文本。
- 开发热重载默认关闭，启用方法见[插件开发](docs/plugin-development.zh-CN.md)。

## 从源码构建

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

默认构建输出到 `build-webview2-release`，仅构建 WebView2 宿主。浏览器自检需要上述共享 Runtime 和可用的 Windows 图形会话；自检仅操作独立隐藏宿主，不操作游戏。`tools/release.ps1 -Tag <版本标签>` 生成普通包、实验包、示例增量包和 SHA-256；只有显式传入 `-Publish` 且当前原生产物的游戏验收记录齐全，才允许上传。

## 文档

- [插件开发](docs/plugin-development.zh-CN.md)
- [运行时架构](docs/architecture.zh-CN.md)
- [游戏兼容性](docs/compatibility.zh-CN.md)

## 许可

REFF 使用 [MIT License](LICENSE)。REFramework、WebView2 SDK、Vue、Element Plus、字体和图标等遵循各自许可，分发包内的 `reframework/reff/licenses/` 提供许可与 NOTICE 文本。Evergreen Runtime 由 Microsoft 单独分发。
