# REFF 游戏兼容性

REFF 使用一套通用 CEF 宿主、Web Shell、Lua SDK 和插件协议。原生 Core 从 REFramework 初始化参数读取游戏 `target_name`，再选择图形与输入能力；不通过窗口标题或安装路径猜测游戏。

## 验证实验游戏

实验包中的目标均为候选状态，不能视为已兼容。安装前退出游戏，将实验包的 `reframework` 目录合并到对应游戏根目录；启动后按快捷键打开 REFF，先检查显示、移动、缩放、页面交互和输入框，再检查输入穿透、Alt-Tab、设备重建、Reset Scripts 和正常退出。

测试完成后，可在游戏退出状态运行实验包内的 `reff/tools/collect-installed-compatibility.ps1`，收集 `compatibility.log`、目标信息、系统版本和 DLL 哈希。不要把 `re2_framework_log.txt`、设置、缓存或输入内容作为诊断包发送。

## 当前矩阵

| 游戏 | target_name | 渲染器 | 状态 |
| --- | --- | --- | --- |
| Monster Hunter Wilds | `MHWILDS` | D3D12  | 已验证 |
| Monster Hunter Rise | `MHRISE` | D3D12  | 已验证 |
| Resident Evil 4 | `RE4` | D3D12  | 已验证 |
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

正式 Runtime 只开放已验证的游戏。其余必须使用显式启用实验游戏的开发构建，因没有完成游戏内验收，不属于正式支持。未列出的目标、仅识别目标、已禁用目标以及使用 D3D11 的运行实例都会在注册回调和安装输入 Hook 前拒绝初始化，并在 `data/REFF/log/compatibility.log` 记录原因。不能仅凭 REFramework 支持某款游戏就认定 REFF 可用。

游戏目录的唯一维护源是 `config/game-support.json`。构建系统从该文件生成只读 C++ 档案，部署和资源观测工具读取同一文件，避免目标名、可执行文件和支持状态分别维护。

## 实验构建

实验游戏使用独立的 `build-compat`，不会覆盖正式 `build-ime`：

```powershell
.\tools\build.ps1 -Profile GameTest -ExperimentalGames
.\tools\stage.ps1 -BuildRoot .\build-compat -IncludeExamples
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

诊断包只包含 `compatibility.log`、目标信息、系统版本和 REFF DLL 的 SHA-256，不收集 REFramework 主日志、用户设置、缓存或输入文本。

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

