# REFramework Frontend 0.1.0-preview6

这是 REFF 的开发者预览版本，供 Wilds / Windows x64 / DX12 / 键鼠环境验证。

本版本完成了游戏内 Web UI、公共窗口、插件桥接、Vue 3 + Element Plus Shell、官方设置页、中英文 i18n、配置持久化和诊断目录规范化。预览包采用 `reframework/` 顶层目录，可直接合并到游戏目录。

主要文件位置：

- 设置：`reframework/data/REFF/settings.json`
- CEF 缓存：`reframework/data/REFF/cache/`
- REFF 日志：`reframework/data/REFF/log/`

已知限制：仅保证 Monster Hunter Wilds、Windows x64、DX12 和键鼠；CEF 磨砂只处理网页表面；不承诺跨版本 ABI 兼容。
