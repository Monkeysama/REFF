# REFF 长期决策

- 官方 Shell、公共 Web UI、版本和依赖元数据内嵌到 `reff-webview2-host.exe`，不依赖安装目录中的 `reff/ui/`；公开 `reff://shell/` URL 保持不变。
- 第三方插件继续使用原有磁盘目录、manifest、Lua/SDK 接口和开发热刷新。不得将官方资源内嵌变成插件迁移要求。
- 保留游戏 DLL、独立宿主 EXE 和 Lua 入口的进程/模块边界；核心包外置一份合并许可，安装与诊断工具单独提供。
- 游戏操作和原生部署约束以 AGENTS.md 为准；独立自检不能替代用户游戏验收。
- `reff/` 核心文件平铺：`reff-webview2-host.exe`、`THIRD-PARTY-NOTICES.txt` 和安装清单直接位于根目录，仅插件使用 `plugins/` 子目录；升级清理和恢复仍兼容旧 `runtime/`、`licenses/` 路径。
