# 更新日志

## v1.0.0 — 2026-10-22

REFF — REFramework Web UI Runtime 首个正式版本。

### 运行时

- 浏览器后端迁移至 WebView2 Evergreen，共享系统 Runtime，移除 CEF 及旧原型运行路径。
- 保留游戏内窗口拖动、连续缩放、主题、语言、文字缩放、窗口位置记忆和设置持久化。
- 支持 Vue 3、React 与原生 HTML 插件，以及受控 Lua 方法调用、事件订阅和 Reset Scripts 恢复。
- 改善中文输入、焦点切换、剪贴板快捷键和键鼠穿透；修复录制快捷键时将 Ctrl 等修饰键单独保存的问题。
- 隔离页面的调用身份由原生宿主绑定，限制本地资源、插件权限和跨文档订阅；开发热重载默认关闭。

### 支持范围与分发

- 全部已接入 REFramework 的 RE Engine 游戏默认允许启用，未列入目录的游戏使用通用档案；当前要求 D3D12。
- 游戏状态仅记录“已经测试”或“未测试”，不作为启用限制。Monster Hunter Wilds、Monster Hunter Rise 已有人工测试记录，其余游戏未测试；这些记录不等于所有版本、设备和新二进制均已验证。
- 统一发布 `REFF-1.0.0.zip` 与 SHA-256 校验文件，停止提供实验包、预览版和示例插件发布包。
- 示例保留在 [`web/plugins`](web/plugins) 和 [`examples/lua`](examples/lua)，构建与同步说明见[示例源码文档](web/examples-dev/README.zh-CN.md)。
- 安装器支持完整性校验、事务备份、升级与恢复，保留用户设置、第三方插件和已有示例。
- 正式版本标签触发 GitHub Actions 构建、独立回归、归档校验和正式 Release 发布。

### 开发者迁移

- SDK 使用 `createHostTransport()`，替代已移除的 `createCefTransport()`；隔离页面使用 `createEmbeddedTransport()`。
- 框架、公共 UI 和示例版本统一为 `1.0.0`，示例 manifest 的 `reffApi` 更新为 `>=1.0.0 <2.0.0`。
- 需要 Windows 10 22H2（build 19045）或更新版本、WebView2 Evergreen 稳定版 Runtime ≥155.0.4283.62。系统 Runtime 不随包分发，REFF 不自动下载安装或提权。

### 验证

- 本地通过 28 项原生测试、真实 WebView2/Lua 桥接与行为回归、DX12 连续缩放及故障恢复测试。
- 本地通过安装与恢复、归档完整性与正式标签契约、三个示例源码独立构建和同步测试。
- 未将独立自检当作游戏验收；游戏帧率与输入延迟未测。D3D11 后端尚未实现。
