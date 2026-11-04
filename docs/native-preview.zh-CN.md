# 在网页中嵌入原生预览

用透明网页槽显示原生人物取景时，网页只报告位置，原生插件保留相机、模型、姿态、纹理与取景裁切。REFF 在同一 Present 命令流中依次绘制 **原生预览 → 浏览器真实 alpha 前景 → 虚拟光标**。弹窗和半透明遮罩由浏览器正常合成，不把纹理最后贴在整张网页上。

这是可选能力；能力缺失时保留原页面和原预览入口。不要在替代画面完成游戏验收前移除旧入口。

## 网页注册一个透明矩形

```ts
import { registerNativePreviewSlot } from '@reff-sdk/preview';
// client 是原有 createEmbeddedTransport() 创建的客户端。
await client.ready();
const preview = await registerNativePreviewSlot(client, element, 'portrait');
// 组件卸载时：
preview.dispose();
```

纯 HTML 可从 `reff://shell/shared/reff-ui.mjs` 导入同名函数。槽位元素以及通往 iframe 根的祖先背景保持透明；普通控件、滚动条、边框和遮罩保持正常网页前景。Shell 仅在当前插件报告预览时将正文底板变透明，未使用预览的插件外观不变。

- `ui.preview.capabilities` 返回 `{version:1,maxSlots:4,composition:'underlay-alpha',color:'sdr-unorm8'}`。这表示宿主的槽位能力，不表示已有某个原生提供者。
- `registerNativePreviewSlot(client, element, slotId)` 自动查询能力、以真实隔离文档身份注册并报告位置。槽位 ID 接受小写字母、数字、`.`、`-`、`_`，最长 64；原生插件 ID 最长 96，须与实际 manifest ID 一致。
- `ui.preview.register/remove` 只对当前隔离页生效；`ui.preview.present` 仅 Shell 可调用。页面不能自行指定另一个插件的原生身份。
- 进程最多注册 4 个原生提供者；每页同时最多 4 个**互不重叠的可见矩形**；重叠投影停画。无提供者时只显示原网页，不报致命错误。

v1 支持滚动容器的矩形 overflow 裁剪、平移、正向轴对齐缩放和 CSS zoom。不支持旋转、透视、负缩放、clip-path、mask、祖先 opacity 小于 1、filter、paint containment 或带圆角的 overflow 裁剪；这些状态停画，不能用外包矩形冒充正确裁剪。需要圆角时先将槽位设计为矩形，或保留原预览路径。

## 原生插件查询 C ABI

公共头：[native_preview.h](../native/include/reff/native_preview.h)。不需要链接 REFF import library：

```cpp
auto module = GetModuleHandleW(L"REFF.dll");
auto get = module ? reinterpret_cast<REFFGetNativePreviewAPI>(
    GetProcAddress(module, "reff_get_native_preview_api")) : nullptr;
const auto* api = get ? get(REFF_PREVIEW_ABI_V1) : nullptr;
// 检查 api、size、abi_version 及 capabilities；缺失则维持原功能。
```

`capabilities` 位：1 = DX12 回调，2 = 网页下层合成，4 = SDR UNORM8，8 = 显式 SDR10。`register_provider` 返回非零 handle；`update_provider` 和 `unregister_provider` 返回 1 表示成功。接口拒绝不支持的 ABI 版本、重复 plugin/slot 组合、无效回调和超额注册。

`REFFPreviewProvider` 传入 `plugin_id`、`slot_id`、独立 `user`、`draw`、`release` 和 flags。字符串在调用内复制；成功才接管该实例，失败由调用方清理 `user`。`draw/release` 必须位于同一已加载模块，REFF 保留模块引用。不要从回调重入注册 API。

### 线程和资源归属

- 注册、替换和注销可从消费者自己的生命周期线程调用。注销返回后不再开始该实例的新 draw；正在录制的回调可能使注销短暂等待，不等待 GPU。
- `draw(user, context)` 在 REFF 的 Present 录制线程运行。只向借用的 `commands` 录制绘制命令，不提交/关闭/重置命令列表，不等待 CPU/GPU，不操作窗口、Lua 或浏览器，不转换 target 的状态，不抛出异常。target 已为 `RENDER_TARGET`。
- `device/queue/commands/target/completion_fence` 均为借用对象；不可释放或信号通知 REFF 的 fence。跨回调保存 COM 对象必须自行 AddRef。用 `destination` 建立完整取景 viewport，用 `clip` 建立精确 scissor；裁剪不能改变取景 UV 或把剩余局部重新拉伸。
- 每次 draw 的所有 PSO、纹理、描述符堆和上传资源，至少保留到 `completion_fence->GetCompletedValue() >= completion_value`。REFF 为该注册实例保留 GPU 租约，但不会追踪消费者在同一 user 内提前替换/释放的成员。
- 最简单的资源替换方式是构建**独立的新 user 实例**再 `update_provider`，旧实例在最后一次 GPU 使用完成后交给 `release`。不要让新旧实例复用一个会被旧 release 删除的 user 指针。
- 若原图由另一队列生产，消费者先完成必要的跨队列同步，再让回调采样可安全读取、状态明确的纹理。REFF 不猜测纹理所有权、隐式屏障或生产 fence；禁止在 draw 内 CPU 等待。
- `release(user)` 线程不固定，不能依赖 Lua/页面仍存活，不能抛异常。设备重建等待超时后 REFF 继续保留旧 fence/实例，直至安全回收；不要用强制卸载 DLL 绕过租约。

### 坐标、颜色和输入格式

SDK 从插件实际 `getBoundingClientRect()` 和祖先滚动裁剪计算 CSS 像素；Shell 将当前 iframe 的坐标映射到宿主视口。槽位修订标记与浏览器像素同帧采集，并携带完整 viewport 代次。Renderer 再以实际 panel 客户区位置、浏览器拟合矩形和 swapchain 尺寸映射到渲染像素。

回调中的 `destination`、`clip` 已是 **当前 target 的渲染像素**；不能再乘 DPI、公共字号或窗口缩放。新 viewport 尚无匹配网页帧、文档切换或修订不匹配时不画预览，避免把旧位置套到新帧。

输出契约为 **SDR、sRGB 编码数值、预乘 alpha**。v1 接受 `R8G8B8A8_UNORM` 和 `B8G8R8A8_UNORM` target；`R10G10B10A2_UNORM` 仅在消费者已确认现有输出确为 SDR10，并注册 `REFF_PREVIEW_SDR10_CONFIRMED` 时调用。显示器开启 HDR 不代表交换链就是该契约；float16/HDR target 停画。

REFF 不接收固定格式的输入纹理，而是受控 draw 回调：输入纹理格式、采样/resolve、色彩转换与 alpha 转换由消费者处理，最终必须符合上述输出契约。AVM 等既有取景输出的视野、裁切和姿态含义不变；不得通过重新拍摄或随槽位剪裁改变取景业务。

槽位不建立新的输入通道；按钮、滚动、命中区域和 IME 继续使用现有网页/REFF 路径。

## 生命周期与最小样例

页面切换/销毁、脚本 epoch 变化、隐藏、视口更换与宿主重启均撤销旧投影。原生提供者注册独立于页面，可保留至消费者关闭，再显式注销；缺少匹配页面时不调用 draw。Reset Scripts 后新页面重新注册。设备和窗口重建时检查 context 的 device/target，不复用旧设备资源。

- [原生提供者](../examples/native-preview/provider.cpp)：动态查询 ABI，以单色 Clear 演示注册、裁剪和注销；正式消费者将 Record 替换为自身纹理采样。
- [网页挂载](../examples/native-preview/page.ts)：配合现有客户端注册 `portrait`，卸载时 dispose。

内部 IPC 和共享帧布局升级到版本 2，DLL 与宿主必须成对更新。外部 manifest、Lua/Web 方法及普通插件原有使用条件不变。该扩展尚需游戏中的真实提供者画面验收；离线 WARP/浏览器验证不代表 AVM 取景或 HDR 已验收。
