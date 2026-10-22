# REFF — REFramework Web UI Runtime

REFF 是面向 REFramework 插件的游戏内 Web UI 运行时，支持 Vue、React 和原生 HTML，并通过受控接口连接 Lua 与游戏功能。

REFF 是 REFramework 的扩展，不替换或修改 REFramework 原有菜单和功能。插件可以选择继续使用原生 UI，也可以声明使用 REFF 接管自己的界面。

## 功能

- 游戏内 WebView2 Web UI，使用系统共享的 Evergreen Runtime，不显示桌面浏览器窗口；页面只加载受控本地资源。
- 支持 Component/schema 和 Isolated Page 两种插件界面。
- Lua 插件注册、方法调用、事件发布与订阅。

## 支持范围

v1.0.0 正式版默认面向全部 RE Engine 游戏，统一使用一个核心包；游戏须已接入 REFramework，并以 **D3D12** 运行。未列出的 REFramework 目标使用通用档案。D3D11 绘制后端尚未实现。

| 游戏 | 状态 |
| --- | --- |
| Monster Hunter Wilds | 已经测试 |
| Monster Hunter Rise | 已经测试 |
| 其他 RE Engine 游戏 | 未测试 |

完整游戏列表见[游戏兼容性](docs/compatibility.zh-CN.md)。测试状态仅记录实际测试情况，不限制默认启用范围。

## 安装

先安装对应游戏的 [REFramework](https://github.com/praydog/REFramework)，使用 Windows 10 22H2 或更新系统，并以 **D3D12** 运行游戏。

1. 从 [Releases](https://github.com/Monkeysama/REFF/releases/latest) 下载 `REFF-1.0.0.zip`。
2. 退出游戏，把包内的 `reframework` 文件夹解压到游戏根目录，与已有文件夹合并，提示时覆盖同名 REFF 文件。
3. 启动游戏，关闭 REFramework 原生菜单，按 **F8** 打开 REFF；按 **F8** 或 **Esc** 关闭。

通常无需额外安装 WebView2。Windows 11 已预装，绝大多数 Windows 10 设备也已安装，REFF 会直接使用系统共享的运行时。只有日志提示运行时缺失或版本过旧时，才需要从 [Microsoft 官方页面](https://developer.microsoft.com/microsoft-edge/webview2/) 安装或更新 **WebView2 Evergreen Runtime**。

可选：带备份的安装、旧版升级与恢复

需要自动备份，或从旧 CEF 版升级时，可使用随包 PowerShell 安装器。退出游戏，将包解压到临时目录，再执行：

```powershell
& "<解压目录>\reframework\reff\tools\install-reff.ps1" -ReframeworkRoot "<游戏目录>\reframework"
```

安装器校验文件并备份覆盖内容，保留用户设置、第三方插件和独立示例。对于旧引擎文件，只按旧清单清理归属明确且未被修改的文件；没有旧清单或文件已被修改时会保留。

如需恢复，退出游戏后执行：

```powershell
& "<解压目录>\reframework\reff\tools\install-reff.ps1" -ReframeworkRoot "<游戏目录>\reframework" -RestoreBackup "<游戏目录>\reframework\data\REFF\backups\<备份名>"
```

备份含旧版本，升级后的游戏目录可能暂时仍很大；确认不再需要恢复后再自行删除对应备份。

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

## 文档

- [更新日志](CHANGELOG.md)
- [故障排查](docs/troubleshooting.zh-CN.md)
- [从源码构建](docs/build.zh-CN.md)
- [插件开发](docs/plugin-development.zh-CN.md)
- [运行时架构](docs/architecture.zh-CN.md)
- [游戏兼容性](docs/compatibility.zh-CN.md)

## 许可

REFF 使用 [MIT License](LICENSE)。REFramework、WebView2 SDK、Vue、Element Plus、字体和图标等遵循各自许可，分发包内的 `reframework/reff/licenses/` 提供许可与 NOTICE 文本。Evergreen Runtime 由 Microsoft 单独分发。
