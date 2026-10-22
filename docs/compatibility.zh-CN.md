# REFF 游戏兼容性

REFF 默认使用系统共享的 WebView2 Evergreen 稳定版 Runtime、独立原生宿主、Web Shell、Lua SDK 和插件协议。原生 Core 从 REFramework 初始化参数读取游戏 `target_name`，再选择图形与输入能力；不通过窗口标题或安装路径猜测游戏。要求 Windows build ≥19045、Runtime ≥155.0.4283.62，实际测试环境为 build 26300 / Runtime 155；最低门槛不代表已逐环境认证。

## 验证实验游戏

实验包中的目标均为候选状态，不能视为已兼容。安装前退出游戏，按 README 使用随包安装器，或在新安装时将包内 `reframework` 目录合并到游戏根目录。启动后按快捷键打开 REFF，先检查显示、移动、连续边角缩放、三类页面和主页面/iframe 输入框，再检查键鼠穿透、快捷键、Alt-Tab、设备/交换链重建、休眠恢复、Reset Scripts 和正常退出。所有游戏操作由测试者手动完成。

测试完成后，可运行包内 `reff/tools/collect-installed-compatibility.ps1`，生成版本、DLL/宿主哈希、Runtime/系统数值和初始化状态的最小 JSON 诊断包；普通包同样提供该工具。它不复制原始日志。不要把 `re2_framework_log.txt`、设置、缓存或输入内容作为诊断包发送。

## 当前矩阵

| 游戏 | target_name | 渲染器 | 状态 |
| --- | --- | --- | --- |
| Monster Hunter Wilds | `MHWILDS` | D3D12 | WebView2 游戏人工验收通过（含设置、输入、三类前端、缩放及恢复） |
| Monster Hunter Rise | `MHRISE` | D3D12 | WebView2 游戏人工验收通过 |
| Resident Evil 4 | `RE4` | D3D12 | 保留能力档案；WebView2 未测，因本机无游戏暂缓验收 |
| Resident Evil Village | `RE8` | D3D12 | 实验候选，仅实验构建开放 |
| Dragon's Dogma 2 | `DD2` | D3D12 | 实验候选，仅实验构建开放 |
| Street Fighter 6 | `SF6` | D3D12 | 实验候选，仅实验构建开放 |
| Resident Evil 2 | `RE2` | D3D12 | 实验候选，仅实验构建开放 |
| Resident Evil 3 | `RE3` | D3D12 | 实验候选，仅实验构建开放 |
| Resident Evil 7 | `RE7` | D3D12 | 实验候选，仅实验构建开放 |
| Resident Evil Requiem | `RE9` | D3D12 | 实验候选，仅实验构建开放 |
| Dead Rising Deluxe Remaster | `DRDR` | D3D12 | 实验候选，仅实验构建开放 |
| Ghosts 'n Goblins Resurrection | `GGR` | D3D12 | 实验候选，仅实验构建开放 |
| Apollo Justice: Ace Attorney Trilogy | `GS456` | D3D12 | 实验候选，仅实验构建开放 |
| Kunitsu-Gami: Path of the Goddess | `KUNITSU` | D3D12 | 实验候选，仅实验构建开放 |
| Onimusha 2: Samurai's Destiny | `ONIMUSHA2` | D3D12 | 实验候选，仅实验构建开放 |
| Monster Hunter Stories 3: Twisted Reflection | `MHSTORIES3` | D3D12 | 实验候选，仅实验构建开放 |
| Mega Man Star Force Legacy Collection | `STARFORCE` | D3D12 | 实验候选，仅实验构建开放 |
| Pragmata | `PRAGMATA` | D3D12 | 实验候选，仅实验构建开放 |
| Onimusha: Way of the Sword | `ONIMUSHA_WOTS` | D3D12 | 实验候选，仅实验构建开放 |

普通包保留 Wilds/Rise/RE4 三个既有能力档案，目录中的 `verified` 表示这一路原生接入档案已有历史验证，不等同于 WebView2 全项认证。新的游戏内结论以本表后端范围为准。其余目标必须使用显式实验构建，不属于正式支持。未列出、仅识别、已禁用目标及 D3D11 实例在注册回调和安装输入 Hook 前拒绝初始化，原因记录于 `data/REFF/log/compatibility.log`。

独立回归可验证真实 WebView2/三类前端/Lua、权限边界、设置持久化、输入协议、离屏像素及隐藏 WARP DX12 输出；它不证明游戏输入、系统中文候选窗/剪贴板、实际 DPI 切换、物理设备重置和休眠恢复。游戏帧率及输入延迟未测，不以包体积缩小推导性能收益。

游戏目录的唯一维护源是 `config/game-support.json`。构建系统从该文件生成只读 C++ 档案，部署和资源观测工具读取同一文件，避免目标名、可执行文件和支持状态分别维护。

## 实验构建

实验游戏使用独立的 `build-webview2-compat`，不覆盖普通 `build-webview2-release`：

```powershell
.\tools\build.ps1 -Profile GameTest -ExperimentalGames
.\tools\stage.ps1 -BuildRoot .\build-webview2-compat -StagingRoot .\staging\compatibility-build-modern
```

`staging/build-profile.json` 记录当前 staging 是否允许实验游戏。

为外部测试者生成包含全部未验证现代 D3D12 目标的统一实验包：

```powershell
.\tools\package-compatibility.ps1 -AllExperimental
```

产物位于 `artifacts/compatibility/`，与正式 GitHub Release 资产隔离。目前包含目录中全部 16 个 `experimental` D3D12 目标：RE2、RE3、RE7、RE8、RE9、DRDR、GGR、GS456、KUNITSU、ONIMUSHA2、MHSTORIES3、STARFORCE、PRAGMATA、ONIMUSHA_WOTS、DD2 和 SF6。实验包只允许目录中状态为 `experimental` 的目标，并在归档前验证实验构建标记、IME 代理构建链和 `reframework` 顶层结构。测试步骤见本文档的“验证实验游戏”一节。

实验游戏运行后可生成最小诊断包：

```powershell
.\tools\collect-compatibility.ps1 -ReframeworkRoot "<游戏目录>\reframework"
```

诊断包只包含字段白名单生成的 `report.json`，不收集原始日志、用户设置、缓存或输入文本。宿主尚未退出时 summary 可能尚未写出；缺失字段保持缺失，不推断为正常或已通过。

## 插件声明

manifest 的 `games` 使用上述大写 `target_name`：

```json
{ "games": ["MHWILDS", "MHRISE"] }
```

与游戏对象无关的设置、状态或工具页面可以使用：

```json
{ "games": ["*"] }
```

通配符只控制页面发现。Lua 插件仍负责检查当前游戏、参数、类型和对象生命周期；

