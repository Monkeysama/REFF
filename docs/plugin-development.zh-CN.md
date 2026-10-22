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
  "reffApi": ">=1.0.0 <2.0.0",
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

### 示例源码与构建同步

示例插件不随版本更新发布。源码位于 [Vue 3 示例](../web/plugins/example.vue)、[React 示例](../web/plugins/example.react)、[原生 HTML 示例](../web/plugins/example.html)，Lua 后端位于 [examples/lua/REFF.examples.lua](../examples/lua/REFF.examples.lua)，完整构建与同步说明见[示例开发](../web/examples-dev/README.zh-CN.md)。

克隆仓库后进入其中一个插件目录，例如 `web/plugins/example.vue`：

```powershell
pnpm install --frozen-lockfile
pnpm run typecheck
pnpm run build
```

进入仓库的 `web/examples-dev` 目录，明确指定同步目标：

```powershell
powershell.exe -ExecutionPolicy Bypass -File .\tools\build-and-sync.ps1 -PluginId example.vue -GameReframeworkRoot "<游戏目录>\reframework"
powershell.exe -ExecutionPolicy Bypass -File .\tools\watch-and-sync.ps1 -PluginId example.vue -GameReframeworkRoot "<游戏目录>\reframework"
```

源码修改后也可以运行 `watch-and-sync.ps1` 自动构建并同步 Web 资源；页面刷新通过受控的 `ui.reload` 开发接口完成。Lua 修改需要在游戏内执行 Reset Scripts。

开发刷新默认关闭。需要启用时，在游戏的 `reframework/data/REFF/development.json` 中写入 `{"enabled":true}`，然后重新启动 REFF 宿主。同步脚本生成的 `.reff-dev-version` 只表示资源有更新，本身不会开启开发权限；删除开关文件并重启宿主即可关闭。该文件属于本地开发配置，不应放进插件或框架分发包。

SDK 会自动选择 WebView2 原生传输。隔离插件的业务调用直接绑定当前 frame 与文档，输入法焦点仍由 Shell 换算坐标；使用 SDK 即可，无需自建父页面业务代理。WebView2 隔离页的刷新由 Shell 统一监视，避免同一次构建造成重复重载。


## 继承主题、字号与全局设置

设置由 REFF 统一保存。插件应跟随当前偏好，不要在自己的配置中另存一份 REFF 主题或修改全局设置文件。`component/schema` 页面由 Shell 渲染，直接使用 Shell 的公共样式；`isolated-page` 属于独立文档，CSS 变量不会跨 iframe 自动继承，必须在插件文档中加载公共样式并接入偏好桥接。

### 接入公共样式和偏好同步

所有隔离页面在 HTML 的 `<head>` 中加载同版本 REFF 的公共样式：

```html
<link rel="stylesheet" href="reff://shell/shared/reff-ui.css">
```

**Vue 3 页面**使用公共 UI 安装器，安装器会同时启用偏好桥接：

```ts
import { createApp } from 'vue';
import { installReffUi } from '@reff/ui';
import App from './App.vue';

// 在当前插件文档中注册公共组件，并接收 Shell 的主题和语言更新。
installReffUi(createApp(App)).mount('#app');
```

沿用 [Vue 示例的 Vite 配置](../web/plugins/example.vue/vite.config.ts)：`vue` 和 `@reff/ui` 在构建时保留为外部依赖，输出路径均映射到 `reff://shell/shared/reff-ui.mjs`。源码别名用于类型检查，不要把开发机文件路径写入游戏内页面。

**React、原生 HTML 或其他框架页面**可在 HTML 中添加以下原生模块脚本，无需创建 Vue 应用：

```html
<script type="module">
  import { installReffPreferencesBridge } from 'reff://shell/shared/reff-ui.mjs';

  // 每个文档安装一次；页面销毁时移除监听器。
  const disposePreferences = installReffPreferencesBridge();
  window.addEventListener('pagehide', disposePreferences, { once: true });
</script>
```

这段代码由浏览器直接加载。如果将它放入 Vite 管理的 TS/JS 入口，需要在构建配置中将运行时模块设为外部依赖；也可参考 [preferences.ts](../web/shared/src/preferences.ts) 将框架无关的偏好桥接源码打包进插件。不要同时启用两种桥接方式。

桥接安装后会向当前 Shell 请求偏好，收到后调用 `applyReffPreferences()`，将变量写入当前文档的 `document.documentElement`，更新 `lang` 并派发 `reff:preferences-changed` 事件。此后用户修改设置，当前插件会收到更新，无需轮询或重载。只引入 `reff-ui.css`、只创建 SDK 客户端，或只读取一次设置，都不能代替这一步。现有 React/HTML 示例入口只加载了公共 CSS；需要完整跟随用户外观时，应补上上述桥接。

### 颜色、透明度与圆角

自定义 CSS 使用 REFF 语义变量。用户选择预设或自定义颜色后，Shell 会传入实际颜色值；插件无需自行维护预设配色。

| 用途 | CSS 变量 |
| --- | --- |
| 页面背景、RGB 通道 | `--reff-bg`、`--reff-bg-rgb` |
| 面板底色、RGB 通道 | `--reff-surface`、`--reff-surface-rgb` |
| 卡片、浮层 | `--reff-card`、`--reff-raised` |
| 正文、次要文字 | `--reff-text`、`--reff-muted` |
| 边框 | `--reff-border` |
| 强调色、增强强调色 | `--reff-accent`、`--reff-accent-strong` |
| 背景不透明度 | `--reff-panel-opacity`，0.55–1 |
| 公共窗口圆角 | `--reff-corner-radius`，0–16px |
| 页面表面模糊 | `--reff-surface-blur`，0–32px |

例如，插件自己的面板可以这样写：

```css
html, body, #app, #root { margin: 0; min-height: 100%; background: transparent; }
.plugin-panel {
  color: var(--reff-text);
  background: rgb(var(--reff-surface-rgb) / var(--reff-panel-opacity));
  border: 1px solid var(--reff-border);
  border-radius: var(--reff-corner-radius);
  padding: 16px;
}
.plugin-title { color: var(--reff-accent); }
.plugin-hint { color: var(--reff-muted); }
```

窗口圆角由 Shell 控制；插件内部卡片只有引用圆角变量才会跟随。保持页面底层透明，可让 Shell 的背景效果显现。不要对整个页面设置 `opacity`，否则文字和控件也会一起变透明；多层面板叠加背景会降低透视效果。`backdrop-filter: blur(var(--reff-surface-blur))` 只能处理浏览器文档可见的背景，不代表可以模糊游戏画面或 iframe 外部内容。

### 字号与控件尺寸

字号设置对应 `appearance.textScale`，范围为 0.8–1.4。公共主题同时提供已经缩放的字号和控件尺寸：

| 变量 | 基准值（比例为 1 时） |
| --- | --- |
| `--reff-font-size-xs` / `sm` / `md` / `lg` / `xl` | 12 / 13 / 14 / 16 / 18px |
| `--reff-control-height-sm` / `--reff-control-height` / `--reff-control-height-lg` | 24 / 32 / 40px |
| `--reff-control-padding-x` / `--reff-control-padding-y` | 12 / 4px |
| `--reff-text-scale` | 当前缩放比例 |

```css
.plugin-panel { font-size: var(--reff-font-size-md); line-height: 1.5; }
.plugin-panel h2 { font-size: var(--reff-font-size-xl); }
.plugin-hint { font-size: var(--reff-font-size-sm); }
.plugin-panel input, .plugin-panel button, .plugin-panel select {
  font: inherit;
  min-height: var(--reff-control-height);
  padding: var(--reff-control-padding-y) var(--reff-control-padding-x);
}
/* 特殊字号可从设计基准值计算，20px 尚未包含缩放。 */
.plugin-value { font-size: calc(20px * var(--reff-text-scale)); }
```

`--reff-font-size-*` 和 `--reff-control-*` 已包含比例，不要再乘一次 `--reff-text-scale`。写死 `font-size: 14px` 的元素不会跟随；单独使用 `rem` 也不会自动跟随，因为 REFF 没有按设置修改根字号。公共 CSS 已处理白名单 Element Plus 控件的字号和常用尺寸；其他组件库需要自行映射其主题变量。布局应允许换行和滚动，避免固定高度在 1.4 倍字号时截断内容。不要用整页 `zoom` 或 `transform: scale()` 代替字号设置，这会改变布局及输入坐标。

### 语言与需要代码响应的偏好

CSS 变量会自动更新；Canvas、图表或翻译库等需要代码响应时，监听桥接派发的事件。以下 TypeScript 示例更新标记为 `<button data-i18n="apply">应用</button>` 的按钮，适用于已配置公共 UI 类型别名的页面；应在安装桥接前注册监听，避免漏掉首次偏好消息：

```ts
import type { ReffPreferences } from '@reff/ui';

// 在回调中更新插件自己的翻译状态、图表配色或 Canvas，并触发重绘。
function onPreferencesChanged(event: Event): void {
  const preferences = (event as CustomEvent<ReffPreferences>).detail;
  const applyButton = document.querySelector('[data-i18n="apply"]');
  if (applyButton) applyButton.textContent = preferences.language === 'zh-CN' ? '应用' : 'Apply';
}
window.addEventListener('reff:preferences-changed', onPreferencesChanged);
window.addEventListener('pagehide', () => {
  window.removeEventListener('reff:preferences-changed', onPreferencesChanged);
}, { once: true });
```

`language` 为 `zh-CN` 或 `en-US`。更新 `document.documentElement.lang` 不会自动翻译插件文字；插件应把事件中的语言同步给自己的 i18n 状态。Vue 可使用 `vue-i18n`；Element Plus 的内置文案还需由 `ElConfigProvider` 接收 `REFF_ELEMENT_LOCALES[language]`，其中 `language` 应为随事件更新的响应式状态。React 和 HTML 同样需要更新自己的文案。Manifest 的 `localizedName` 只影响 Shell 显示的插件名称。

### 其他全局设置的职责

| 设置 | 插件需要做什么 |
| --- | --- |
| 快捷键、键鼠穿透 | 由 REFF 原生输入层处理；插件不要重复注册全局快捷键或自行拦截游戏输入。 |
| 预加载、窗口位置与大小记忆 | 由宿主管理；插件使用容器尺寸布局，不另存全局窗口几何。 |
| 中文输入与输入框焦点 | 页面安装 SDK 的 `installInputFocusReporter(reff)`，销毁时调用返回的清理函数，参见 [SDK 源码](../web/sdk/index.ts)。偏好桥接不代替输入桥接。 |
| 插件自己的业务设置 | 使用插件自己的命名空间与 Lua 方法保存；全局主题、语言仍以 Shell 的偏好为准。 |

偏好桥接的公开类型只包含 `language` 和 `appearance`。插件不应依赖消息中恰好附带的其他内部字段，也不能直接调用仅 Shell 可用的 `reff.settings.get/set/reset` 来管理全局设置。

接入后，在游戏内手动切换主题和自定义颜色、将字号调到 0.8 与 1.4、切换语言并调整透明度和圆角，确认插件当前页立即变化、文本不截断，关闭重开后仍跟随 REFF 保存的偏好。桥接与令牌定义分别见 [preferences.ts](../web/shared/src/preferences.ts) 和 [theme.css](../web/shared/src/theme.css)。

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
