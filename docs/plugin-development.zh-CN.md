# REFF 插件开发

本文档说明第三方插件如何声明使用 REFF、注册 Lua 服务、提供 Vue/React/原生页面，以及处理中文输入。

## 接入模型

插件由 `manifest.json` 和 Lua 注册代码组成，放置在：

```text
reframework/reff/plugins/<plugin-id>/manifest.json
reframework/reff/plugins/<plugin-id>/ui/...
reframework/autorun/<plugin-id>.lua
```

插件 ID 使用反向域名或小写点分格式，例如 `author.inventory`。`reff` 命名空间保留给官方插件。

## Manifest

```json
{
  "manifestVersion": 1,
  "id": "author.example",
  "name": "Example Plugin",
  "localizedName": {"zh-CN": "示例插件", "en-US": "Example Plugin"},
  "author": "Plugin Author",
  "version": "1.0.0",
  "reffApi": ">=0.1.0 <0.2.0",
  "games": ["*"],
  "ui": {"kind": "page", "mode": "isolated-page", "entry": "ui/index.html"},
  "methods": ["author.example.get"],
  "events": ["author.example.changed"]
}
```

`name` 是缺省名称，`localizedName` 可提供 `zh-CN` 与 `en-US` 等语言名称，`author` 填写插件作者。REFF 会在左侧导航、设置页插件清单和右侧标题栏中显示这些元数据。`games` 使用 REFramework 的 `target_name`，通用页面可使用 `*`。通配符只控制页面发现，不会绕过 REFF Core 的游戏兼容门控。

UI 支持两种模式：

- `component/schema`：页面显示在 REFF 公共工作台右侧，适合设置和状态面板。
- `isolated-page`：页面拥有独立内容区域，适合复杂应用。

## Lua 注册

```lua
local reff = require("reff.bridge")

reff.register_plugin({
    id = "author.example",
    methods = {
        ["author.example.get"] = function()
            return { value = 123 }
        end,
    },
    events = { "author.example.changed" },
})
```

Lua 方法运行在 REFramework 脚本环境。游戏对象、指针、类型和生命周期由插件自行检查，不能把某款游戏的对象结构当作通用 API。

## 页面与 SDK

页面通过 REFF SDK 调用 Lua 方法并订阅事件。项目示例提供三种构建方式：

- Vue 3 + TypeScript + Element Plus
- React + TypeScript
- 原生 HTML/JavaScript

源码仓库中的开发说明位于 `web/examples-dev`；发布示例包的独立开发目录为 `reframework/reff/examples-dev`。该目录包含 SDK、公共源码和三个插件各自的依赖清单、锁文件及配置。进入其中一个插件目录，例如 `examples-dev/web/plugins/example.vue`：

```powershell
pnpm install --frozen-lockfile
pnpm run typecheck
pnpm run build
```

回到 `examples-dev` 目录，明确指定同步目标：

```powershell
powershell.exe -ExecutionPolicy Bypass -File .\tools\build-and-sync.ps1 -PluginId example.vue -GameReframeworkRoot "<游戏目录>\reframework"
powershell.exe -ExecutionPolicy Bypass -File .\tools\watch-and-sync.ps1 -PluginId example.vue -GameReframeworkRoot "<游戏目录>\reframework"
```

源码修改后也可以运行 `watch-and-sync.ps1` 自动构建并同步 Web 资源；页面刷新通过受控的 `ui.reload` 开发接口完成。Lua 修改需要在游戏内执行 Reset Scripts。

使用 WebView2 测试后端时，开发刷新默认关闭。需要启用时，在游戏的 `reframework/data/REFF/development.json` 中写入 `{"enabled":true}`，然后重新启动 REFF 宿主。同步脚本生成的 `.reff-dev-version` 只表示资源有更新，本身不会开启开发权限；删除开关文件并重启宿主即可关闭。该文件属于本地开发配置，不应放进插件或框架分发包。

SDK 会自动选择 WebView2 原生传输。隔离插件的业务调用直接绑定当前 frame 与文档，输入法焦点仍由 Shell 换算坐标；使用 SDK 即可，无需自建父页面业务代理。WebView2 隔离页的刷新由 Shell 统一监视，避免同一次构建造成重复重载。


## 生命周期与安全边界

- 页面可能因 Reset Scripts、宿主重启、设备重建或游戏退出而销毁并重建。
- 方法调用必须接受重复注册、超时、断线和旧页面请求失效。
- 不要保存跨 Lua 状态重建的原生对象指针。
- 网络访问、系统浏览器窗口和外部网页不是 REFF 的运行能力。
- 插件前端应使用公共主题和 `vue-i18n` 约定；缺少当前语言时回退到英文，再回退到默认名称。

完整字段、消息信封、错误和权限边界以源码中的 manifest 校验和桥接实现为准。

## 宿主通信入口

Shell 或独立顶层页面使用 `createReffClient(createHostTransport())`；隔离插件页面继续使用 `createReffClient(createEmbeddedTransport())`。两者均通过 WebView2 注入的原生文档通道调用方法，身份和权限由宿主绑定的实际 frame 校验。隔离页仅将输入框焦点及坐标交给 Shell 换算，不通过父页面代理业务方法或事件。

旧宿主专用的 `createCefTransport` 和 `window.cefQuery` 已移除。曾直接引用该底层入口的页面需改用 `createHostTransport` 并重新构建；既有 `createEmbeddedTransport`、`call`、`subscribe` 和 Lua 注册接口保持不变。
