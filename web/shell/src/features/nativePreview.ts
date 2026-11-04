import { shallowRef } from 'vue';
import type { ReffClient } from '@reff-sdk/types';
import { previewElementBounds, type PreviewLayout, type PreviewBox } from '@reff-sdk/preview';

// Shell 只投影当前 iframe；业务身份/槽位权限由原生文档绑定，不经 parent 代理注册。
export function createNativePreviewProjection(client: ReffClient, currentFrame: () => HTMLIFrameElement | null) {
  const active = shallowRef(false);
  let source: PreviewLayout | null = null, frameId = 0, stopped = false, busy = false, previous = '', sent = 0;
  const tick = (now: number) => {
    if (stopped) return;
    const iframe = currentFrame();
    if (source && iframe && !busy) {
      const outer = previewElementBounds(iframe), scaleX = (outer?.rect[2] || 0) / source.viewportWidth, scaleY = (outer?.rect[3] || 0) / source.viewportHeight;
      const project = (b: PreviewBox): PreviewBox => [(outer?.rect[0] || 0) + b[0] * scaleX, (outer?.rect[1] || 0) + b[1] * scaleY, b[2] * scaleX, b[3] * scaleY];
      const slots = outer ? source.slots.map(slot => {
        const clip = project(slot.clip), boundary = outer.clip;
        const x = Math.max(clip[0], boundary[0]), y = Math.max(clip[1], boundary[1]);
        return { slotId: slot.slotId, rect: project(slot.rect), clip: [x, y, Math.min(clip[0] + clip[2], boundary[0] + boundary[2]) - x, Math.min(clip[1] + clip[3], boundary[1] + boundary[3]) - y] };
      }).filter(slot => slot.clip[2] > 0 && slot.clip[3] > 0) : [];
      const params = { pageId: source.pageId, viewportWidth: innerWidth, viewportHeight: innerHeight, slots };
      const signature = JSON.stringify(params);
      if (signature !== previous || now - sent > 200) {
        busy = true; previous = signature; sent = now;
        void client.call('ui.preview.present', params).catch(() => { previous = ''; }).finally(() => { busy = false; });
      }
    }
    frameId = requestAnimationFrame(tick);
  };
  frameId = requestAnimationFrame(tick);
  return { active,
    receive(event: MessageEvent) {
      if (event.source !== currentFrame()?.contentWindow || event.data?.type !== 'reff:preview-layout') return false;
      const data = event.data as PreviewLayout;
      if (typeof data.pageId !== 'string' || !Number.isFinite(data.viewportWidth) || data.viewportWidth <= 0 || !Number.isFinite(data.viewportHeight) || data.viewportHeight <= 0 || !Array.isArray(data.slots) || data.slots.length > 4) return true;
      if (!data.slots.every(s => typeof s.slotId === 'string' && [s.rect, s.clip].every(b => Array.isArray(b) && b.length === 4 && b.every(Number.isFinite)))) return true;
      source = data; active.value = data.slots.length > 0; return true;
    },
    clear() { source = null; previous = ''; active.value = false; },
    dispose() { stopped = true; cancelAnimationFrame(frameId); source = null; active.value = false; },
  };
}
