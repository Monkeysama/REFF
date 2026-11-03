# REFF 长期决策

- 官方 Shell、公共 Web UI、版本和依赖元数据内嵌到 `reff-webview2-host.exe`，不依赖安装目录中的 `reff/ui/`；公开 `reff://shell/` URL 保持不变。
- 第三方插件继续使用原有磁盘目录、manifest、Lua/SDK 接口和开发热刷新。不得将官方资源内嵌变成插件迁移要求。
- 保留游戏 DLL、独立宿主 EXE 和 Lua 入口的进程/模块边界；核心包外置一份合并许可；正式 Release 只发布核心 ZIP 及其 SHA-256，安装与诊断工具保留在源码仓库，不另打包。
- 游戏操作和原生部署约束以 AGENTS.md 为准；独立自检不能替代用户游戏验收。
- `reff/` 核心文件平铺：`reff-webview2-host.exe`、`THIRD-PARTY-NOTICES.txt` 和安装清单直接位于根目录，仅插件使用 `plugins/` 子目录；升级清理和恢复仍兼容旧 `runtime/`、`licenses/` 路径。
- Shell 以插件正文为主，侧栏和标题栏默认自动隐藏，靠近边缘时浮出，离开只留提示，可固定显示；容器留白默认 24 px（0–48），已有显式值保持。Core 按插件存储覆盖；隔离页 `ui.layout.get/set` 只作用于原生身份绑定的当前插件，null 恢复继承。留白和默认开启的上次插件记忆持久化到 Core 设置，不使用浏览器缓存。缺失插件回退 REFF 设置；Reset Scripts 的临时空选择不覆盖导航记录。
- 窗口拖动区由原生绑定的 Shell 上报实际 DOM 矩形，必须排除交互控件；不得重新依赖固定标题高度。
- Release 说明仅提取当前版本章节；CHANGELOG.md 保留完整历史。维护既有发布时不移动标签或替换已验证的核心附件。
