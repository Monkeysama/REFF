# REFramework Frontend 0.1.0 开发者预览

## 支持范围

| 项目 | 状态 |
| --- | --- |
| Monster Hunter Wilds | 支持 |
| Windows x64 | 支持 |
| DirectX 12 | 支持 |
| 键盘与鼠标 | 支持 |
| HDR、其他游戏和图形 API | 未验证 |

## 已包含功能

- 游戏内 CEF Web UI Runtime。
- 可移动、可缩放并实时重排的公共窗口。
- Vue 3、Element Plus 和共享 UI 资源。
- REFramework Lua/TypeScript 插件桥接。
- 多插件注册、事件订阅和隔离页面。
- 设置页：主题、透明度、圆角、磨砂、文字大小、输入穿透、窗口记忆。
- `vue-i18n` 中英文切换及插件名称语言回退。
- 配置持久化与脚本重置恢复。

## 安装

1. 退出游戏。
2. 将压缩包内的 `reframework` 目录合并到游戏目录。
3. 保留已有 `reframework/data/REFF/settings.json`。
4. 启动游戏后按 F8 打开 REFF。

开发示例另行提供于 `REFF-0.1.0-preview6-examples.zip`。这是正式 Runtime 的增量包，包含顶层 `reframework/autorun/REFF.examples.lua`、`reframework/reff/plugins/example.*` 的可运行构建产物，以及 `reframework/reff/examples-dev` 下的源码和构建依赖说明；不包含 DLL、CEF 或 Shell。请先安装同版本正式 Runtime，再将此包合并到同一游戏目录；仅用于开发和回归测试。

## 文件与日志

- 用户设置：`reframework/data/REFF/settings.json`
- CEF 临时缓存：`reframework/data/REFF/cache/`
- REFF 诊断日志：`reframework/data/REFF/log/`

## 已知限制

- 首版仅保证 Wilds/DX12。
- 分发包与 GameTest 构建均包含已验收的透明 IME 代理；GameTest 额外启用性能诊断，供开发回归使用。
- 第三方插件必须自行提供翻译资源，缺少语言时按英文和默认名称回退。
- 当前版本为开发者预览，不承诺跨版本 ABI 兼容。


## 候选包

最新候选包为 `REFF-0.1.0-preview6.zip`，包含顶层 `reframework/` 目录。REFF 原生插件与 CEF 宿主的诊断日志统一写入 `reframework/data/REFF/log/`；旧版 `reframework/logs/` 中的文件不会自动删除。
