# REFF 游戏兼容性

REFF 使用一套通用 CEF 宿主、Web Shell、Lua SDK 和插件协议。原生 Core 从 REFramework 初始化参数读取游戏 `target_name`，再选择图形与输入能力；不通过窗口标题或安装路径猜测游戏。

## 当前矩阵

| 游戏 | target_name | 渲染器 | 键鼠输入 | 状态 |
| --- | --- | --- | --- | --- |
| Monster Hunter Wilds | `MHWILDS` | D3D12 | Win32、Raw Input、DirectInput 键盘过滤 | 已验证 |
| Monster Hunter Rise | `MHRISE` | D3D12 | Win32、Raw Input、DirectInput 键盘过滤 | 实验支持，等待游戏内验收 |

未列出的目标会在注册回调和安装输入 Hook 前拒绝初始化，并在 REFramework 日志记录目标名。D3D11 后端尚未实现，因此不能仅凭 REFramework 支持某款游戏就认定 REFF 可用。

## 插件声明

manifest 的 `games` 使用上述大写 `target_name`：

```json
{ "games": ["MHWILDS", "MHRISE"] }
```

与游戏对象无关的设置、状态或工具页面可以使用：

```json
{ "games": ["*"] }
```

通配符只控制页面发现。Lua 插件仍负责检查当前游戏、参数、类型和对象生命周期；Wilds 的玩家、血量或管理器类型不能直接用于 Rise。

## 新游戏验收

每款新游戏至少验证面板显示与实时缩放、三种窗口模式、DPI、鼠标移动与回中、键盘和鼠标穿透、中文 IME、Alt-Tab、设备重建、Reset Scripts、宿主回收重建及基础资源开销。完成这些人工步骤前只标记为实验支持。

D3D12 游戏先复用现有合成后端，再按实际输入路径增加能力档案。D3D11 游戏需要独立渲染后端，不在 D3D12 适配中增加条件分支模拟支持。
