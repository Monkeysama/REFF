# REFF 插件与桥接契约

版本：0.6 · 日期：2026-09-15 · 状态：Lua 注册/事件、Shell 身份和独立插件页面资源/权限已实现；Component/Isolated Page 双模式由契约保留。本文含目标契约，不能把所有草案视为已实现 API。

实现边界：网页只提交 method/params，宿主按 `reff://plugin/<id>/<entry>` 绑定 pluginId/pageId；插件资源只能读取对应 manifest 根目录，方法/事件由 manifest 与 Lua 注册表共同授权。Shell 只负责插件发现、导航、通用 Schema 和隔离页承载，不持有第三方插件业务状态。`ready()/is_ui_ready()` 用于页面与宿主的就绪握手，SDK 对重置和断线提供明确错误语义。示例包提供 Vue 3、React、原生 HTML 三个功能相同的 Isolated Page，用于演示 IME、事件和公共资源用法，正式 Runtime 不包含示例业务。

## 1. 接入模型

插件接入由三部分组成：manifest 声明、游戏侧注册、网页就绪握手。单独放置 manifest 不会自动注册 Lua 函数，也不会把已有 ImGui 代码转换成 HTML。

协议允许在 0.x 版本中演进；不兼容变更必须同时升级宿主和插件，并记录迁移说明。

## 2. Manifest 草案

以下示例使用 `MHWILDS` 作为原生插件的 `target_name`；日志中的小写显示名为 `mhwilds`。

```json
{
  "manifestVersion": 1,
  "id": "example.vue",
  "name": "Vue 3 接入示例",
  "version": "0.1.0",
  "reffApi": ">=0.1.0 <0.2.0",
  "games": ["MHWILDS"],
  "ui": {
    "kind": "page",
    "mode": "isolated-page",
    "entry": "ui/dist/index.html"
  },
  "methods": [
    "example.vue.get",
    "example.vue.refresh"
  ],
  "events": ["example.vue.refreshed"],
  "fallback": "none"
}
```

- ID 使用小写字母、数字及内部的点/连字符，禁止路径分隔符、空段和保留名；唯一性检查在加载时完成。`reff.*` 是 Core 系统插件保留命名空间，第三方 manifest 会被拒绝。
- `entry` 相对 manifest 所在资源根目录，禁止绝对路径和越界资源。
- `reffApi` 是 SDK 能力兼容范围，与 manifestVersion、IPC protocolVersion 分开管理。
- `methods/events` 是插件声明的能力集合，实际可调用集合还必须经过游戏侧注册。
- 注册记录将 method 映射到所属插件，禁止覆盖其他插件方法。Lua 游戏侧注册表是最终权威，未知方法返回 `METHOD_NOT_FOUND`；前端只访问自己的注册能力，跨插件调用不在首版范围。
- `fallback` 只是声明作者提供回退逻辑，不能代替作者实现旧 UI。

结构化表单模式采用 `ui.kind = "schema"`。第一版支持开关、数字、滑条、选择框、文本和按钮；每个字段必须有稳定 ID，`readonly` 可把状态字段设为只读。可选的 `schema.load` 指定初始快照方法，`schema.changeEvent` 指定状态事件，按钮的 `action` 指定提交方法；这些名称必须同时出现在插件的 `methods`/`events` 权限声明中。数值范围和数据合法性仍由后端最终确认。

## 3. UI 双承载模式

REFF Shell 保持常驻，左侧显示插件列表，右侧显示当前页面。manifest 的 `ui.mode` 明确页面承载方式：

- `component`：当前由 Shell 的受限 Schema 渲染器承载；页面切换只更新右侧内容，不重载 CEF 主 frame。设置和计数器示例采用此模式。
- `isolated-page`：页面保留插件自己的 HTML/JS/CSS，在受控隔离容器中运行，适合第三方原生 HTML、Vue、React 等页面。Shell 负责外层窗口、导航和安全边界，iframe 内容区填满右侧可用视口；插件负责 iframe 内的全部页面排版，不应依赖固定的 720px 等窗口尺寸。插件桥接通过 Shell 容器转发，不能直接取得底层凭证。

隔离页的输入法由 SDK 的 `installInputFocusReporter` 统一接入。插件上报的是 iframe 内网页视口坐标，Shell 根据当前 iframe 客户区矩形换算为 Shell 坐标，再由宿主按当前 REFF 面板尺寸映射到游戏客户区；插件不应自行加面板或 iframe 偏移。

同一插件可以提供多个页面，但每个页面必须明确模式；Component 页面运行在 Shell 信任边界内，Isolated Page 页面不应依赖 Shell 的 Vue 实例。两种模式共享插件发现、方法/事件权限、页面销毁、订阅取消和脚本 epoch 语义。

使用 `installReffUi()` 的隔离 Vue 页面会接收 Shell 发送的 `reff:preferences`，公共模块据此同步主题令牌与 `html.lang`，并派发 `reff:preferences-changed` DOM 事件。该事件只提供偏好，不自动翻译插件业务文案。Core 设置方法只允许 Shell 调用，不属于普通插件公开能力。

## 4. Lua SDK 草案

| 拟议接口 | 行为 |
| --- | --- |
| `register(pluginId, handlers)` | 绑定 manifest、注册方法和事件并返回插件句柄；同一 epoch 内拒绝重复方法/事件 |
| `handle.emit(eventName, payload)` | 校验已声明事件并发送可序列化状态；宿主再按订阅句柄过滤 |
| `handle.is_ui_ready()` | 表示当前会话的页面及桥接可用 |
| `handle.unregister()` | 幂等清理本插件方法、事件和订阅 |

函数放在独立 `reff` 模块命名空间，不覆盖 `re`、`sdk`、`imgui`。Lua 模块装载、Lua ABI 与桥接绑定必须使用目标 REFramework 提供的兼容运行时。

框架自动处理脚本重置清理；插件可主动 unregister。作者的 UI 回调根据就绪状态决定显示旧 UI 或 REFF 入口，但隐藏 REFF 面板不等于失去就绪状态，不应导致两个 UI 反复抢占。

## 4. TypeScript SDK 草案

```ts
await reff.ready();

const state = await reff.call("example.vue.get", {});
await reff.call("example.vue.refresh", {});

const subscription = await reff.subscribe(
  "example.vue.refreshed",
  (state) => renderState(state)
);

await subscription.unsubscribe();
```

`ready()` 只表示当前页面对应的后端注册和桥接已就绪。`call()` 返回 Promise；`subscribe()` 等待服务端确认订阅，返回可重复安全解除的句柄。页面销毁、会话断开和脚本重置时，SDK 自动停止旧订阅。

API 中的 `renderState` 是插件自己的展示函数。正式包提供类型声明与稳定错误类型，SDK 不依赖 Vue。SDK 的 mock transport 用于网页开发预览，不能当成游戏内验收。

公共 Shell 使用 Vue 3 + Vite，并统一提供 Element Plus 组件层。插件 SDK 保持框架无关；插件可以使用 Shell 暴露的基础组件，也可以在自己的资源根目录使用原生 HTML/CSS 或其他前端框架。插件不得通过 CDN 或远程资源加载 Element Plus，也不应重复打包公共组件库。

状态同步使用“订阅确认后由页面主动取快照，再按 revision 合并”的方式，防止读取和订阅之间漏掉变化。Lua 侧只接受已声明事件，宿主在收到 `event` 消息后向当前页面的已确认订阅句柄派发 DOM CustomEvent。

## 5. 消息信封

控制消息使用 UTF-8 JSON；命名管道上采用有长度上限的长度前缀帧，例如 uint32 little-endian 长度加正文。共享内存承载画面，不通过 JSON/base64 传整张截图。

请求示例（字段由各层按职责填充，网页不能自行决定可信身份）：

```json
{
  "protocolVersion": 1,
  "type": "request",
  "sessionId": "opaque-session-id",
  "luaEpoch": 3,
  "pageId": "opaque-page-instance",
  "pluginId": "example.vue",
  "id": "42",
  "method": "example.vue.refresh",
  "params": { "enabled": true },
  "timeoutMs": 5000
}
```

成功响应携带相同会话、epoch、pageId、pluginId 和 id，以及 `type: "response"`、`result`。失败响应携带 `error: { code, message }`；`result` 与 `error` 互斥。事件携带 eventName、subscriptionId、revision 和 payload。

宿主验证来源和注册绑定，游戏侧再次校验会话、目标及授权，不把前端传入的 pluginId 当作授权证明。会话校验凭证不暴露给网页。

## 6. 数据与执行语义

- 可传对象、数组、字符串、布尔值、有限数字及 null。Lua 侧提供明确的 null 标记和数组/对象转换规则，不能让 nil 静默删除字段或空数组变空对象。
- 超出 JavaScript 安全整数范围的值使用约定的字符串或标记结构；不传裸指针、函数、userdata、循环引用、NaN 或 Infinity。
- 后端再次检查参数类型、范围和当前游戏状态；UI 组件校验只用于交互反馈。
- 每个会话内请求 ID 唯一，重复请求应被拒绝或返回已缓存结果，不能重复执行写操作。
- 游戏侧以入队后的单调时钟判断过期，不依赖跨进程墙钟同步。
- 超时/取消只能阻止尚未开始的操作或停止等待结果；已经执行的修改不自动撤销。重连和超时后不自动重试写请求。
- Hook 返回值、逐帧修改和实时对象操作留在 Lua/C++，网页只能配置它们或订阅摘要。

初始保护值是可调默认值，插件应根据自身数据规模设置合理上限：

| 项目 | 初始值 |
| --- | --- |
| 普通请求超时 | 5 秒 |
| 单条控制消息正文 | 最大 256 KiB |
| 单插件待处理请求 | 最大 128 条 |
| 全局待处理请求 | 最大 512 条 |
| 每次游戏调度 | 最多 32 条；约 0.25 ms 后不再取新请求 |
| 常规 UI 状态推送 | 默认合并到最高 10 次/秒；交互结果及时返回 |

调度耗时阈值不能抢占正在执行的 handler。必须记录慢 handler 并由插件拆分工作。队列满时立即返回可识别错误，不无限增长；状态事件可合并，已接受的写请求不能无声丢弃。

## 7. 错误与生命周期

| 错误码草案 | 含义与 UI 行为 |
| --- | --- |
| `NOT_READY` | 后端/页面尚未就绪，提示等待或重连 |
| `INCOMPATIBLE_VERSION` | Runtime 或协议不兼容，停用该接入 |
| `METHOD_NOT_FOUND` | 未注册方法，报告插件集成错误 |
| `FORBIDDEN` | 来源或能力不匹配，拒绝操作 |
| `INVALID_ARGUMENT` | 参数或数据编码不合法，显示可理解的校验错误 |
| `QUEUE_FULL` | 当前请求过多，提示稍后再试 |
| `TIMEOUT` | 等待超时，写操作结果可能未知，需要重取状态 |
| `SCRIPT_RESET` | 旧 Lua epoch 已失效，重新注册并获取快照 |
| `PLUGIN_UNLOADED` | 目标插件不可用，关闭相关页面或显示状态 |
| `HOST_DISCONNECTED` | 浏览器/游戏连接断开，释放输入并等待恢复 |
| `HANDLER_ERROR` | 插件处理失败，记录诊断，避免把敏感本地信息直接展示给页面 |

接管顺序：Discovered → Registered → PageReady → Active。只有 Active 才允许作者抑制旧 UI。页面不可用、注册失效、版本不兼容或连接断开时撤销就绪状态；页面正常隐藏时仍可保持 Active，但停止不必要订阅。

Reset Scripts 后所有旧句柄、请求和订阅失效，即使新插件使用相同 ID 也不能继承旧 epoch 的请求。关闭页面后，迟到响应不能交给同 ID 的新页面。

## 8. 必要验证

插件接入测试至少覆盖：重复 ID、版本不兼容、非法参数、null/数组与大整数、跨插件伪造请求、消息截断/超限、超时不重复写入、队列背压、页面关闭迟到响应、脚本重置、注册清理和状态订阅顺序。

这些是跨进程和生命周期边界的行为测试，不为每个 UI 样式或可逆文案改动添加重复测试。


公共 UI 资源复用首轮已实现，入口与构建契约见插件最小接入指南末节。隔离页只额外允许 shared 路径的脚本/样式；不共享父窗口 Vue 实例。游戏回归待用户验证。
