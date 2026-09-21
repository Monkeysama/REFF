# REFF

REFF 是运行在 REFramework 之上的游戏内 Web UI 框架。它为 REFramework 插件提供现代化的Web UI界面。

REFF 是 REFramework 的扩展，不替换或修改 REFramework 原有菜单和功能。插件可以选择继续使用原生 UI，也可以声明使用 REFF 接管自己的界面。

## 功能

- 游戏内 CEF Web UI，不打开系统浏览器窗口，也不访问互联网。
- 支持 Component/schema 和 Isolated Page 两种插件界面。
- Lua 插件注册、方法调用、事件发布与订阅。

## 支持范围

当前兼容状态：

| 游戏 | REFramework 目标名 | 图形接口 | 状态 |
| --- | --- | --- | --- |
| Monster Hunter Wilds | `MHWILDS` | D3D12 | 已验证 |
| Monster Hunter Rise | `MHRISE` | D3D12 | 已验证 |
| Resident Evil 4 | `RE4` | D3D12 | 已验证 |

正式 Runtime 只开放以上 3 款已验证游戏。统一实验包另提供 16 个现代 D3D12 候选目标；它们不代表已经完成兼容。D3D11 后端尚未实现，详细范围和实验包说明见[游戏兼容性](docs/compatibility.zh-CN.md)。

统一实验包用于没有本机验证条件的候选游戏，安装方式与正式包相同，但只应在对应游戏中使用。

实验包中的目标仍会经过运行时 D3D12 检查，未通过检查时不会安装游戏输入 Hook。

## 安装

1. 退出游戏。
2. 下载对应版本的正式 Runtime 包。
3. 将压缩包内的 `reframework` 目录合并到游戏安装目录。
4. 启动游戏，关闭 REFramework 原生菜单后按 **F8** 打开 REFF，按 **F8** 或 **Esc** 关闭。

开发和回归测试还可以安装 Release 中的示例插件增量包。请先解压同版本正式 Runtime 包，再将示例包覆盖合并到同一个游戏目录；示例包包含可直接运行的构建产物，以及 `reff/examples-dev` 下的 Vue 3、React、原生 HTML 源码、SDK、公共 UI 源码、Vite 配置和锁定依赖，不重复包含 DLL、CEF 或 Shell。

## 文件位置

```text
reframework/data/REFF/settings.json  用户设置
reframework/data/REFF/cache/        CEF 临时缓存，可在退出游戏后清理
reframework/data/REFF/log/          REFF 专用诊断日志
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
- 中文输入异常：使用 `GameTest` 构建，避免用默认构建覆盖启用透明 IME 代理的 DLL。
- 修改 Lua 后：执行 REFramework 的 Reset Scripts；修改网页资源后重新打开插件页面即可，必要时增加资源版本号避免缓存。
- 日志只用于 REFF 诊断，不使用 `re2_framework_log.txt`。CEF 临时缓存可在游戏退出后删除，下一次启动会自动重建。

## 文档

- [插件开发](docs/plugin-development.zh-CN.md)
- [运行时架构](docs/architecture.zh-CN.md)
- [游戏兼容性](docs/compatibility.zh-CN.md)

## 许可

REFF 使用 [MIT License](LICENSE)。CEF/Chromium、Vue、Element Plus、字体和图标等第三方资源遵循各自许可，分发包内的 `reframework/licenses/` 提供对应许可证文本。
