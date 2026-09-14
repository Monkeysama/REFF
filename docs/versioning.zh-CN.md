# REFF 版本规则

REFramework Frontend 使用语义化版本 `MAJOR.MINOR.PATCH`，当前产品版本为 `0.1.0`。根目录 `version.json` 是产品版本的唯一来源；CMake 配置和 Vite 构建均从该文件读取，发布清单同时携带该版本文件。

## 递增规则

- `MAJOR`：进入 1.0 后，对公开插件接口、安装布局或运行行为作不兼容修改。
- `MINOR`：加入向后兼容的功能；在 `0.x` 开发阶段，不兼容的公开接口调整也提升此位，并提供迁移说明。
- `PATCH`：向后兼容的缺陷修复、性能优化，以及不改变接口语义的 UI 和文案调整。
- 预发布候选可使用 `-alpha.N`、`-beta.N` 或 `-rc.N` 标签；构建工具支持该形式前，候选状态只写入发布说明，不写入产品版本源。

版本在形成可部署候选构建时递增。连续的修复和界面调整可以共享一个开发版本，在发布候选冻结后再决定补丁版本，避免每项内部修改单独产生版本。

## 发布标签与脚本

发布标签使用 `v` 前缀，例如 `v0.1.0` 或 `v0.1.0-preview7`；标签去掉前缀后的基础版本必须与 `version.json` 一致。项目提供 `tools/release.ps1`，会生成正式 Runtime 包、包含示例插件的开发包以及对应的 SHA-256 校验文件：

```powershell
.\tools\release.ps1 -Tag v0.1.0-preview7
```

确认本地产物和发布说明后，在已登录 GitHub CLI 的环境中追加 `-Publish` 上传 Release。推送匹配 `v*.*.*` 的标签会由 `.github/workflows/release.yml` 自动执行相同流程；带 `preview`、`alpha`、`beta` 或 `rc` 的标签会标记为预发布版本。

## 兼容版本

产品版本与以下版本分别管理，不能互相替代：

- 原生 `protocol_version`：游戏 DLL 与 CEF 宿主之间的协议版本。
- 插件 manifest 的 `reffApi`：第三方插件声明可兼容的 REFF API 范围。
- `REFF_UI_VERSION`：公共 Vue/Element Plus 模块的兼容版本。
- 插件 manifest 的 `version`：插件自身版本。

产品补丁版本不会自动改变这些兼容版本。只有相应契约发生变化时才单独递增，并在发布说明中列明迁移或兼容范围。
