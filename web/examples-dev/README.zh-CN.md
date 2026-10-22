# REFF 示例插件源码

三个示例源码位于仓库的 `web/plugins/example.vue`、`web/plugins/example.react` 和 `web/plugins/example.html`。本目录提供构建、同步和监视工具，示例不再随 Release 发布。它不包含 `node_modules`，请使用 Node.js 24.x 和 pnpm 11.x 安装锁定版本的依赖。

在仓库根目录执行以下命令安装锁定的 Web 依赖并检查类型：

```powershell
pnpm --dir web/shell install --frozen-lockfile
.\tools\build-web.ps1 -IncludeExamples
```

以下 pnpm 命令在 `web/examples-dev` 执行；三个插件也可各自在源码目录安装依赖并构建。

## 构建并同步到游戏

开发时可以使用根目录脚本构建并同步一个示例插件，不需要重新编译或部署 REFF DLL：

```powershell
pnpm run build:vue
pnpm run sync:vue -GameReframeworkRoot "<游戏目录>\reframework"
```

可用的构建命令为 `build:vue`、`build:react`、`build:html`，同步命令为 `sync:vue`、`sync:react`、`sync:html`。同步必须显式传入 `-GameReframeworkRoot`，也可直接调用 `tools/build-and-sync.ps1 -PluginId example.vue -GameReframeworkRoot "<游戏目录>\reframework"`。脚本执行 `Vite build`，再将当前插件的 `manifest.json` 和 `ui/dist` 同步到目标目录，不操作游戏。修改 Lua 后端仍需用户在 REFramework 中执行 Reset Scripts。

需要连续开发时可启动自动监视和同步：

```powershell
pnpm run watch:vue -GameReframeworkRoot "<游戏目录>\reframework"
```

也可使用 `watch:react` 或 `watch:html`，同样显式传入目标。监视器先构建同步一次，再检测源码、公共 SDK/UI、入口文件、Vite 配置及 manifest 的保存并再次构建同步。构建成功更新开发版本标记；启用下述开发开关后，WebView2 Shell 统一刷新当前 iframe。构建失败保留上一版资源。按 `Ctrl+C` 停止监视。该模式只覆盖当前插件目录，不更新 REFF DLL 或宿主。

构建结果位于各插件的 `ui/dist`。将对应插件目录复制到游戏的 `reframework/reff/plugins/` 后，重新打开 REFF 页面即可加载；修改 `manifest.json` 时需要重新启动 REFF 宿主或游戏。Lua 后端源码位于 `examples/lua/REFF.examples.lua`，可通过 REFramework 的 Reset Scripts 重载。

示例页面使用 `reff://shell/shared/` 公共资源，必须和同版本 REFF Runtime 一起使用。

## 单独开发一个示例

每个 `web/plugins/example.vue`、`example.react`、`example.html` 目录均带有自己的 `package.json`、`pnpm-lock.yaml`、`pnpm-workspace.yaml`、`tsconfig.json` 和 Vite 配置。可只在选定目录运行 `pnpm install --frozen-lockfile`、`pnpm run typecheck`、`pnpm run build`，无需先安装 Shell 的依赖。保留源码仓库中的 `web/sdk` 与 `web/shared/src` 相对位置，SDK 和公共组件源码供类型检查与构建使用。三个示例仍需相同版本的 REFF Runtime 提供游戏内服务。

同步和监视命令需要显式目标，例如 `pnpm run sync -GameReframeworkRoot 'C:\Steam\steamapps\common\MonsterHunterWilds\reframework'`；命令只同步当前插件，不启动或操作游戏。WebView2 自动刷新默认关闭，先手动建立 `reframework/data/REFF/development.json`，内容为 `{"enabled":true}`，并重新打开 REFF 宿主；Shell 统一检测开发标记，每次构建只刷新当前插件一次。不启用开发模式时可手动重新打开页面。
