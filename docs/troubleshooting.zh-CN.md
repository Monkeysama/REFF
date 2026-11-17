# REFF 故障排查

以下路径均相对于游戏的 `reframework` 目录；安装步骤见 [README](../README.md#安装)。

- 面板不显示：确认 REFramework 已加载，关闭原生菜单后按 Home；检查 `data/REFF/log/`。
- 页面纯灰或资源缺失：重新安装正式核心包；自建插件请核对资源路径及 API 版本。
- 运行时缺失或过旧：查看 `data/REFF/log/webview2/summary.json`。若未安装 WebView2 Evergreen 稳定版，或已安装的版本缺少宿主所需接口，请安装或更新运行时。REFF 不自动下载或提权安装依赖。
- 中文输入异常：默认构建已启用透明 IME 代理；检查 `data/REFF/log/` 并说明发生在主页面还是插件输入框。
- 修改 Lua 后：执行 REFramework 的 Reset Scripts；修改网页资源后重新打开插件页面即可，必要时增加资源版本号避免缓存。
- 最小诊断：从[源码仓库](https://github.com/Monkeysama/REFF)运行 `tools/collect-installed-compatibility.ps1 -ReframeworkRoot "<游戏目录>\reframework"`。只提取版本、哈希、数值和初始化状态，不复制原始日志、设置、缓存或输入文本。
- 开发热重载默认关闭，启用方法见[插件开发](plugin-development.zh-CN.md)。

## RE4 输入与页面问题的开发分支跟进

[议题 #1](https://github.com/Monkeysama/REFF/issues/1) 的修复在独立开发分支上验证；下列行为不表示已包含在 v1.0.3，也不表示 RE4 游戏验收通过。

- **激活点击与关闭后的释放**：窗口消息按首次按下记录游戏/REFF 归属，后续重复与抬起沿用同一归属；后台激活点击已经放给游戏时，其抬起不能因恢复捕获而被吞。REFF 拦截的按键即使面板隐藏，也应等对应抬起后再交还该键。新按键及显式穿透仍遵循原有设置，不是全局封锁所有物理键。
- **输入覆盖边界**：只覆盖实际抵达游戏窗口回调的 legacy/Raw Input。混合多个归属的 Raw Mouse 包只能整包处理，优先保证游戏已收到的按下能收到释放，可能连带放行同包其他事件；不能声称逐事件隔离、阻断 `GetAsyncKeyState` 或引擎内部轮询。设备移除或上抬消息永久缺失时，待释放记录可能保守保留；IME 无物理扫描码、按键释放后迟到的派生字符不能可靠归属。
- **光标回中心/残留**：恢复焦点后在窗口线程重新建立虚拟光标输入源；系统显示计数仍只归还 REFF 自己的租约。没有默认写入 `via.hid.Mouse` 模式、`share.hid.Device` 字段、系统裁剪状态，也没有重新启用进程级 `SetCursorPos` Hook。RE4 的回中心和残留光标仍须实际反馈，不能把这次改动称为彻底修复引擎光标。
- **页面拖放**：公共文档层取消 HTML 原生 `dragstart`；支持相应接口的 Runtime 同时关闭外部拖入并接管原生拖出。普通文本选择/复制保留，排序等交互应改用 Pointer 事件。旧 Runtime 的原生接口不可用时仅文档层兜底，参见宿主摘要的 `externalDropGuardAvailable` 和 `nativeDragGuardAvailable`。
- **原生下拉菜单**：离屏捕获无法保证 `<select>` 的桌面弹层，插件应使用公共 Element Plus `ElSelect`/`ElOption`；开发模式提供动态元素提示，详见[插件开发](plugin-development.zh-CN.md)。
- **其他自绘面板**：核心在游戏更新阶段维护 `_G.__AVM_UI_INPUT_BLOCKER_STATES.REFF`，显示或尚有待释放输入时登记 `true`，结束/Reset Scripts 时仅撤销自己写入的槽位，不清空其他插件状态。新原生查询 `reff_native.is_input_blocking()` 是只读协作摘要，不是操作系统或游戏输入已经完全被隔离的证明；其他插件必须主动尊重共享约定。旧 DLL 仅可见性回退不具备完整待释放语义。

### 人工验收与反馈

1. 退出游戏后再成对更新测试分支的 DLL、宿主及核心 Lua，保留设置；不要仅替换单个二进制。
2. 在 RE4 D3D12、鼠标/键盘穿透均关闭时打开面板，Alt-Tab 后用左键切回，松开后角色不得持续攻击；同时检查面板光标能否移动。
3. 在面板内按住普通键/鼠标，关闭面板后再松开，检查游戏无持续按下；分别复测开启穿透后的行为和按住按键切换穿透的边界。
4. 拖动普通选中文字、`draggable` 元素及 AVM 排序项，页面不应卡住；输入框仍可选择与编辑，Pointer 排序不能依赖被禁止的原生拖拽。
5. 打开/关闭 SKS 等其他面板并交替关闭 REFF，检查其他面板的保护状态不被撤销；最后关闭所有面板后检查系统光标恢复。

请反馈游戏、REFramework、Runtime 和测试提交版本，穿透配置，AVM/SKS 是否同时打开，以及最短复现步骤。独立测试不访问真实游戏对象，也不能验证角色攻击、引擎轮询或多个第三方面板的实际行为。
