# REFF 示例插件开发包

本目录包含 Vue 3、React 和原生 HTML/JavaScript 三个示例插件的源码与构建配置。它不包含 `node_modules`，请使用 Node.js 24.x 和 pnpm 11.x 安装锁定版本的依赖。

在本目录根部执行以下命令安装锁定的 Web 依赖并检查类型：

```powershell
pnpm run install:deps
pnpm run typecheck
```

根目录的 `package.json` 提供统一命令入口；具体版本依赖和 `pnpm-lock.yaml` 位于 `web/shell`，安装脚本会使用该锁文件并将依赖放在 `web/shell/node_modules`，与示例 Vite 配置的路径保持一致。

## 构建并同步到游戏

开发时可以使用根目录脚本构建并同步一个示例插件，不需要重新编译或部署 REFF DLL：

```powershell
pnpm run build:vue
pnpm run sync:vue
```

可用的构建命令为 `build:vue`、`build:react`、`build:html`，同步命令为 `sync:vue`、`sync:react`、`sync:html`。也可以直接运行 `tools/build-and-sync.ps1 -PluginId example.vue`，传入 `-GameReframeworkRoot` 覆盖默认游戏目录。脚本会执行 `Vite build`，再将该插件的 `manifest.json` 和 `ui/dist` 覆盖到游戏的 `reframework/reff/plugins/<plugin-id>`。它不会自动操作游戏；同步完成后请在游戏内切换页面或关闭并重新打开 REFF，使 CEF 加载新资源。修改 Lua 后端时仍使用 REFramework 的 `Reset Scripts`。

需要连续开发时可启动自动监视和同步：

```powershell
pnpm run watch:vue
```

也可使用 `watch:react` 或 `watch:html`。监视器会先构建同步一次，之后检测源码、公共 SDK/UI、入口文件、Vite 配置和 manifest 的保存并自动再次构建同步。构建成功会更新开发版本标记，示例页通过 SDK 的 `installDevReload` 自动刷新当前 iframe；构建失败会保留上一版资源。按 `Ctrl+C` 停止监视。该模式只覆盖当前插件目录，不会更新 REFF DLL 或 CEF。

构建结果位于各插件的 `ui/dist`。将对应插件目录复制到游戏的 `reframework/reff/plugins/` 后，重新打开 REFF 页面即可加载；修改 `manifest.json` 时需要重新启动 REFF 宿主或游戏。Lua 后端源码位于 `examples/lua/REFF.examples.lua`，可通过 REFramework 的 Reset Scripts 重载。

示例页面使用 `reff://shell/shared/` 公共资源，必须和同版本 REFF Runtime 一起使用。
