# REFF

REFF 是运行在 REFramework 之上的游戏内 Web UI 框架。它为 REFramework 插件提供现代化的Web UI界面。

REFF 是 REFramework 的扩展，不替换或修改 REFramework 原有菜单和功能。插件可以选择继续使用原生 UI，也可以声明使用 REFF 接管自己的界面。

## 功能

- 游戏内 CEF Web UI，不打开系统浏览器窗口，也不访问互联网。
- 支持 Component/schema 和 Isolated Page 两种插件界面。
- Lua 插件注册、方法调用、事件发布与订阅。
- Vue 3、React、原生 HTML/JavaScript 示例接入，以及 Vue/Element Plus 共享主题和公共 UI 资源。
- 设置页：启动后延迟预加载、主题、颜色、透明度、圆角、网页表面模糊、文字大小、输入穿透和窗口记忆。
- 中/英界面切换，插件名称和翻译资源可跟随 REFF 语言设置。
- 配置保存在游戏目录，脚本重载和重新启动游戏后仍然保留。

## 支持范围

当前兼容状态：

| 游戏 | REFramework 目标名 | 图形接口 | 状态 |
| --- | --- | --- | --- |
| Monster Hunter Wilds | `MHWILDS` | D3D12 | 已验证 |
| Monster Hunter Rise | `MHRISE` | D3D12 | 实验支持，等待完整游戏内验收 |

同一个 Runtime 包用于以上游戏。D3D11 后端尚未实现；未列出的游戏会在安装游戏专用 Hook 前停止初始化。详细范围见[游戏兼容性](docs/compatibility.zh-CN.md)。

## 安装

1. 退出游戏。
2. 下载 [REFF-0.1.0-preview6.zip](artifacts/REFF-0.1.0-preview6.zip)。
3. 将压缩包内的 `reframework` 目录合并到游戏安装目录。
4. 保留已有的 `reframework/data/REFF/settings.json`。
5. 启动游戏，关闭 REFramework 原生菜单后按 **F8** 打开 REFF，按 **F8** 或 **Esc** 关闭。

开发和回归测试还可以安装 Release 中的示例插件增量包。请先解压同版本正式 Runtime 包，再将示例包覆盖合并到同一个游戏目录；示例包包含可直接运行的构建产物，以及 `reff/examples-dev` 下的 Vue 3、React、原生 HTML 源码、SDK、公共 UI 源码、Vite 配置和锁定依赖，不重复包含 DLL、CEF 或 Shell。

## 文件位置

```text
reframework/data/REFF/settings.json  用户设置
reframework/data/REFF/cache/        CEF 临时缓存，可在退出游戏后清理
reframework/data/REFF/log/          REFF 专用诊断日志
```

旧版本在 `reframework/logs/` 生成的文件不会自动删除；新版本的 REFF 日志写入 `data/REFF/log/`。

## 插件接入

第三方插件需要同时提供 manifest 和 Lua 注册代码。插件可以选择 schema 组件页面或独立隔离页面，并声明所需的方法和事件。

最小接入示例和目录约定见：[插件最小接入指南](docs/plugin-quickstart.zh-CN.md)。完整的注册、权限、生命周期、错误和事件契约见：[插件与桥接契约](docs/plugin-contract.zh-CN.md)。

插件名称可以提供中英文版本：

```json
{
  "name": "Interaction Example",
  "localizedName": {
    "zh-CN": "Vue 3 接入示例",
    "en-US": "Vue 3 Integration Example"
  }
}
```

插件前端应使用 REFF 提供的公共 UI 资源和 i18n 约定。缺少当前语言时，REFF 会回退到英文名称，再回退到默认名称。

## 插件开发

项目内置的开发构建包含透明 IME 代理和示例插件，适合进行中文输入、事件订阅、页面重载和窗口交互回归。正式 Runtime 构建不包含示例业务。

在项目目录 `D:\Project\RE-FF` 中执行：

```powershell
.\tools\fetch-dependencies.ps1
.\tools\build-web.ps1 -IncludeExamples
.\tools\build.ps1 -Profile GameTest
.\tools\stage.ps1 -BuildRoot D:\Project\RE-FF\build-ime -IncludeExamples
```

### 发布

发布脚本会重新构建并生成正式 Runtime 包和包含示例插件的开发包，两个压缩包都带有 `reframework/` 顶层目录及 SHA-256 校验文件。默认只生成本地资产；确认版本和发布说明后，在已登录 GitHub CLI 的环境中执行：

```powershell
.\tools\release.ps1 -Tag v0.1.0-preview7 -Publish
```

推送形如 `v0.1.0-preview7` 的标签也会由 GitHub Actions 自动构建并发布 Release。标签的基础版本必须与根目录 `version.json` 一致；示例包仅用于开发和回归测试。

构建完成后，将 `staging/reframework` 合并到游戏目录。部署原生文件前必须退出目标游戏；项目提供的部署脚本会根据目标目录识别 Wilds 或 Rise 进程，并进行逐文件哈希校验。

更多内容见：[运行时架构](docs/architecture.zh-CN.md)、[版本规则](docs/versioning.zh-CN.md) 和 [内置设置与持久化](docs/settings.zh-CN.md)。

## 故障排查

- 面板不显示：确认 REFramework 已加载，关闭原生菜单后按 F8；检查 `data/REFF/log/`。
- 页面纯灰或资源缺失：确认 Runtime 包和示例包来自同一版本，并重新合并 `reframework` 目录。
- 中文输入异常：使用 `GameTest` 构建，避免用默认构建覆盖启用透明 IME 代理的 DLL。
- 修改 Lua 后：执行 REFramework 的 Reset Scripts；修改网页资源后重新打开插件页面即可，必要时增加资源版本号避免缓存。
- 日志只用于 REFF 诊断，不使用 `re2_framework_log.txt`。CEF 临时缓存可在游戏退出后删除，下一次启动会自动重建。

## 文档

- [插件最小接入指南](docs/plugin-quickstart.zh-CN.md)
- [插件与桥接契约](docs/plugin-contract.zh-CN.md)
- [运行时架构](docs/architecture.zh-CN.md)
- [内置设置与持久化](docs/settings.zh-CN.md)
- [游戏兼容性](docs/compatibility.zh-CN.md)
- [开发者预览说明](docs/developer-preview-0.1.0.zh-CN.md)
- [发布说明](docs/release-notes-0.1.0-preview6.zh-CN.md)

## 许可

REFF 使用 [MIT License](LICENSE)。CEF/Chromium、Vue、Element Plus、字体和图标等第三方资源遵循各自许可，分发包内的 `reframework/licenses/` 提供对应许可证文本。
