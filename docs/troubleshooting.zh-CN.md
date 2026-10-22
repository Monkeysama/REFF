# REFF 故障排查

以下路径均相对于游戏的 `reframework` 目录；安装步骤见 [README](../README.md#安装)。

- 面板不显示：确认 REFramework 已加载，关闭原生菜单后按 F8；检查 `data/REFF/log/`。
- 页面纯灰或资源缺失：重新安装正式核心包；自建插件请核对资源路径及 API 版本。
- 运行时缺失或过旧：查看 `data/REFF/log/webview2/summary.json`。REFF 当前要求 WebView2 Evergreen 稳定版 ≥155.0.4283.62；仅在检测未通过时安装或更新。系统精简、手动移除或更新受限可能影响运行时。REFF 不自动下载或提权安装依赖。
- 中文输入异常：默认构建已启用透明 IME 代理；检查 `data/REFF/log/` 并说明发生在主页面还是插件输入框。
- 修改 Lua 后：执行 REFramework 的 Reset Scripts；修改网页资源后重新打开插件页面即可，必要时增加资源版本号避免缓存。
- 最小诊断：运行包内 `reff/tools/collect-installed-compatibility.ps1 -ReframeworkRoot "<游戏目录>\reframework"`。只提取版本、哈希、数值和初始化状态，不复制原始日志、设置、缓存或输入文本。
- 开发热重载默认关闭，启用方法见[插件开发](plugin-development.zh-CN.md)。
