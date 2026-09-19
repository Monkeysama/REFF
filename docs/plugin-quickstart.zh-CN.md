# REFF 插件最小接入指南

本文给出 Lua 插件接入 REFF 的最小路径。示例只覆盖首版已实现能力。

## 目录与 manifest

插件目录至少包含一个 `manifest.json` 和 UI 入口。`entry` 必须是 manifest 所在目录下的相对文件，页面资源不能通过 CDN、绝对路径或网络加载。

```json
{
  "manifestVersion": 1,
  "id": "author.example",
  "name": "我的 REFF 插件",
  "version": "0.1.0",
  "reffApi": ">=0.1.0 <0.2.0",
  "games": ["MHWILDS", "MHRISE"],
  "ui": { "kind": "page", "mode": "isolated-page", "entry": "ui/index.html" },
  "methods": ["author.example.get"],
  "events": ["author.example.changed"],
  "fallback": "plugin-managed"
}
```

`methods` 和 `events` 只是声明，游戏侧还必须使用相同的插件 ID 注册实际 handler。方法名必须以 `author.example.` 开头，事件名和订阅 ID 使用小写字母、数字、点及连字符。

`games` 使用 REFramework 的大写 `target_name`。纯通用插件可以声明 `"games": ["*"]`；读取或修改游戏对象的插件必须列出实际支持目标，并在 Lua 后端按游戏校验类型、方法和对象状态。

## Lua 注册

```lua
local native = rawget(_G, "reff_native")
local SDK = require("reff.sdk")

local value = 0
local handle = SDK.register("author.example", {
    methods = {
        ["author.example.get"] = function()
            return { value = value }
        end,
    },
    events = { "author.example.changed" },
}, {
    is_ready = native.is_ready,
    emit = function(plugin_id, event_name, payload)
        return native.emit(plugin_id, event_name, json.dump_string(payload))
    end,
})
```

handler 运行在 REFramework 游戏线程。网页只传递可序列化对象，Lua 侧仍需检查类型、范围和游戏状态；不要把 userdata、裸指针或循环引用返回给页面。

## TypeScript/原生页面

```ts
import { createEmbeddedTransport, createReffClient, installInputFocusReporter } from '@reff-sdk/index';

const reff = createReffClient(createEmbeddedTransport());
const removeInputFocusReporter = installInputFocusReporter(reff);
await reff.ready();
const identity = await reff.call('ui.identity');
if (identity.pluginId !== 'author.example') throw new Error('插件身份不匹配');

const subscription = await reff.subscribe('author.example.changed', state => {
  render(state);
});
render(await reff.call('author.example.get'));

// 页面销毁时调用；写请求不会因为断线自动重放。
await subscription.unsubscribe();
removeInputFocusReporter();
reff.dispose();
```

订阅确认后先读取一次快照，再合并事件，可以覆盖建立订阅期间的变化窗口。脚本 Reset、页面重载或宿主断线后旧句柄失效，插件应重新执行 `ready`、身份检查、订阅和快照读取。

`installInputFocusReporter` 适用于 Shell 和 `isolated-page`。它监听文本输入框焦点，向宿主报告网页视口逻辑坐标，隔离页由 Shell 自动转换 iframe 偏移后再交给原生 IME 代理；页面卸载前应调用返回的清理函数。

## 旧 UI 回退

`fallback: "plugin-managed"` 表示插件自己保留 REFramework ImGui UI。只有 REFF 不存在、版本不兼容或页面未就绪时才绘制回退窗口；REFF 页面进入 Active 后停止旧 UI，避免两个界面同时接收输入。REFF 不会自动把 ImGui 调用转换成 HTML。

## 排查

- 左侧没有插件：检查 JSON 是否通过 manifest 校验、`games` 是否包含当前 REFramework 目标名（或 `*`），以及 ID 是否重复。
- `FORBIDDEN`：确认网页身份来自 `ui.identity`，方法/事件同时出现在 manifest 和 Lua 注册表，且使用自身命名空间。
- 页面白屏或 404：检查 `entry` 和资源根，移除网络、绝对路径、目录穿越和 URL 编码。
- 状态不更新：确认先等待 `subscribe` 返回，再读取快照；脚本 Reset 后重新注册和订阅。

详细消息、错误码和生命周期约束见 [插件与桥接契约](plugin-contract.zh-CN.md)。

开发时可在插件入口安装 `installDevReload(reff)`。配合 `watch-and-sync.ps1` 使用时，源码每次成功构建同步都会自动刷新当前插件页面；该辅助功能只用于开发包。

## 复用 REFF 公共 Vue / Element Plus（0.1.0 预览）

生产入口 HTML 先引用公共样式，再引用插件自己的模块；不用 CDN，也不复制组件库到插件包：

```html
<link rel="stylesheet" href="reff://shell/shared/reff-ui.css">
<script type="module" src="./assets/plugin.js"></script>
```

源码中通过 `vue` 使用 Vue，通过 `@reff/ui` 使用公共组件和安装器：

```ts
import { createApp } from 'vue';
import { installReffUi, ElMessage } from '@reff/ui';
import App from './App.vue';
installReffUi(createApp(App)).mount('#app');
```

Vite/Rollup 把 `vue` 和 `@reff/ui` 同时 external 到同一个 URL，确保每个文档内部只有一份 Vue：

```ts
rollupOptions: {
  external: ['vue', '@reff/ui'],
  output: { paths: {
    vue: 'reff://shell/shared/reff-ui.mjs',
    '@reff/ui': 'reff://shell/shared/reff-ui.mjs',
  } },
}
```

开发期 `@reff/ui` 类型/源码指向 `web/shared/src/index.ts`，参考 `example.vue` 的 Vite 配置与 `web/plugins/tsconfig.json`。当前公共组件导出清单以该文件为准，不支持随意导入 Element Plus 内部路径。React 与原生 HTML 示例使用无框架 SDK，不强制采用此公共模块。

在仓库根运行 `tools/build-web.ps1` 生成正式公共模块与 Shell；开发示例使用 `tools/build-web.ps1 -IncludeExamples`。该脚本同时检查类型、模块引用和公共 CSS 引用。`tools/stage.ps1` 默认不打包示例，开发回归需显式传入 `-IncludeExamples`。当前示例包提供三个功能相同的 `isolated-page` 页面：`example.vue` 使用 Vue 3，`example.react` 使用 React，`example.html` 使用原生 HTML/JavaScript。三者均显示当前游戏、REFramework 版本、REFF 运行时间和刷新次数；页面打开后每 500ms 自动读取最新快照。在 Monster Hunter Wilds 中额外显示本地玩家血量，并可调用各自命名空间的方法调整血量百分比。

公共文件可复用，但 iframe 中的状态、Vue 应用和组件实例各自独立。公共 CSS 包含 REFF 暗色变量与基础盒模型，插件可以在随后加载的 CSS 中覆盖自己的布局。公共资源 URL 需要与 Core 版本匹配；插件应在发布包中固定依赖版本并随包提供许可证信息。

### 公共缩放令牌

REFF 的文字大小设置通过 `--reff-text-scale` 驱动，并同步提供以下公共令牌：`--reff-font-size-xs`、`--reff-font-size-sm`、`--reff-font-size-md`、`--reff-font-size-lg`、`--reff-font-size-xl`，以及 `--reff-control-height-lg`、`--reff-control-height`、`--reff-control-height-sm`。插件自己的文字、按钮、输入框和卡片尺寸应基于这些令牌或 `calc(... * var(--reff-text-scale))` 编写，不要把用户可见文字和控件尺寸固定为不可缩放的 `px`。

公共主题已经覆盖 REFF 使用的 Element Plus 输入框、选择框、下拉项、复选框、单选框、开关、滑块和弹层。第三方插件如果引入自己的组件库，仍需在插件 CSS 中把该组件库的字体和控件尺寸映射到这些令牌；REFF 不会强制修改插件资源根目录内的任意固定样式。
