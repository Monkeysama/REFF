# 游戏兼容性

REFF v1.0.0 默认面向全部 RE Engine 游戏，使用同一个正式包，不再区分实验包。需要游戏已接入 REFramework，并以 D3D12 运行；当前未实现 D3D11 绘制后端。未列出的 REFramework 目标使用通用档案，安装器也提供通用安装路径。

## 游戏列表

“状态”仅表示是否已有游戏内测试记录，不决定能否启用 REFF。“已经测试”记录来自 Wilds、Rise 的人工测试；更新后的二进制仍需按实际运行结果排查兼容问题。

| 游戏 | target_name | 渲染器 | 状态 |
| --- | --- | --- | --- |
| Monster Hunter Wilds | `MHWILDS` | D3D12 | 已经测试 |
| Monster Hunter Rise | `MHRISE` | D3D12 | 已经测试 |
| Resident Evil 4 | `RE4` | D3D12 | 未测试 |
| Resident Evil Village | `RE8` | D3D12 | 未测试 |
| Dragon's Dogma 2 | `DD2` | D3D12 | 未测试 |
| Street Fighter 6 | `SF6` | D3D12 | 未测试 |
| Resident Evil 2 | `RE2` | D3D12 | 未测试 |
| Resident Evil 3 | `RE3` | D3D12 | 未测试 |
| Resident Evil 7 | `RE7` | D3D12 | 未测试 |
| Resident Evil Requiem | `RE9` | D3D12 | 未测试 |
| Dead Rising Deluxe Remaster | `DRDR` | D3D12 | 未测试 |
| Ghosts 'n Goblins Resurrection | `GGR` | D3D12 | 未测试 |
| Apollo Justice: Ace Attorney Trilogy | `GS456` | D3D12 | 未测试 |
| Kunitsu-Gami: Path of the Goddess | `KUNITSU` | D3D12 | 未测试 |
| Onimusha 2: Samurai's Destiny | `ONIMUSHA2` | D3D12 | 未测试 |
| Monster Hunter Stories 3: Twisted Reflection | `MHSTORIES3` | D3D12 | 未测试 |
| Mega Man Star Force Legacy Collection | `STARFORCE` | D3D12 | 未测试 |
| Pragmata | `PRAGMATA` | D3D12 | 未测试 |
| Onimusha: Way of the Sword | `ONIMUSHA_WOTS` | D3D12 | 未测试 |
| Devil May Cry 5 | `DMC5` | D3D12 | 未测试 |

游戏目录维护于 `config/game-support.json`；构建时生成原生档案，测试状态采用 `tested` / `untested`。未列出的游戏使用 REFramework 提供的目标标识，不受名单限制。游戏专属 Lua 功能仍须由插件自行判断当前游戏与对象生命周期。

## 安装与测试

按 [README](../README.md) 安装统一正式包。退出游戏后再安装或恢复。启动游戏后检查 F8、显示、拖动、连续缩放、插件调用、中文输入、快捷键、键鼠穿透、Alt-Tab 和 Reset Scripts；游戏操作由测试者手动完成。

独立浏览器、Lua 和 DX12 回归不替代游戏测试。游戏帧率及输入延迟未测，包体积不代表性能收益。

最小诊断只收集版本、哈希、数值和初始化状态，不复制原始日志、用户设置、缓存或输入文本：

```powershell
& "<解压目录>\reframework\reff\tools\collect-installed-compatibility.ps1" -ReframeworkRoot "<游戏目录>\reframework"
```

## 插件声明

与游戏对象有关的插件应列出目标：

```json
{ "games": ["MHWILDS", "MHRISE"] }
```

通用工具可使用 `{"games":["*"]}`。它只控制页面发现，不授予额外权限，也不替代 Lua 的参数、类型与生命周期校验。
