// 使用现有插件客户端；普通页不调用此接口，也不需要额外依赖。
import { registerNativePreviewSlot } from '../../web/sdk/preview';
import type { ReffClient } from '../../web/sdk/types';

export async function mountPortrait(client: ReffClient, element: HTMLElement) {
  // element 和其祖先保持透明；背景、边框、控件和弹窗由网页前景绘制。
  // 挂载前可保留原生旧预览；能力缺失时继续原路径，不移除旧入口。
  const slot = await registerNativePreviewSlot(client, element, 'portrait');
  return () => slot.dispose();
}
