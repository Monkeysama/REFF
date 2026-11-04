import { shallowRef } from 'vue';
import type { ReffClient } from '@reff-sdk/types';
import { previewElementBounds, type PreviewLayout, type PreviewBox } from '@reff-sdk/preview';

// Shell 只投影当前 iframe；业务身份/槽位权限由原生文档绑定，不经 parent 代理注册。
export function createNativePreviewProjection(client: ReffClient, currentFrame: () => HTMLIFrameElement | null) {
  const active = shallowRef(false);
  let token = 0, observed = '';
  const stamp = document.createElement('i');
  stamp.style.cssText = 'position:fixed!important;left:104px!important;top:22px!important;width:3px!important;height:3px!important;z-index:2147483647!important;pointer-events:none!important;opacity:1!important';
  document.documentElement.append(stamp);
  let source: PreviewLayout | null = null, frameId = 0, stopped = false, busy = false, previous = '', sent = 0;
  const tick = (now: number) => {
    if (stopped) return;
    const iframe = currentFrame();
    if (source && iframe) {
      const box = iframe.getBoundingClientRect(), scaleX = box.width / source.viewportWidth, scaleY = box.height / source.viewportHeight;
      const project = (b: PreviewBox): PreviewBox => [box.x + b[0] * scaleX, box.y + b[1] * scaleY, b[2] * scaleX, b[3] * scaleY];
      // 按每个槽位检查祖先裁剪，不能用铺满正文的 iframe 是否触及角区代替槽位自身的判断。
      const slots = source.slots.flatMap(slot => {
        const outer = previewElementBounds(iframe, project(slot.clip));
        return outer ? [{ slotId: slot.slotId, rect: project(slot.rect), clip: outer.clip }] : [];
      });
      const point = project([source.paint[0], source.paint[1], 0, 0]);
      const layout = { pageId: source.pageId, viewportWidth: innerWidth, viewportHeight: innerHeight, paint: [point[0], point[1], source.paint[2]], slots };
      const current = JSON.stringify(layout);
      // 即使上一请求尚未完成，也先让新 DOM 布局失去旧 Native 修订的绘制权。
      if (current !== observed) { observed = current; token = token % 0xffffff + 1; stamp.style.setProperty('background', `rgb(${token & 255},${token >> 8 & 255},${token >> 16 & 255})`, 'important'); }
      const params = { ...layout, layoutToken: token };
      const signature = JSON.stringify(params);
      if (!busy && (signature !== previous || now - sent > 200)) {
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
      if (!Array.isArray(data.paint) || data.paint.length !== 3 || !data.paint.every(Number.isFinite) || !Number.isInteger(data.paint[2]) || data.paint[2] < 0 || data.paint[2] > 0xffffff) return true;
      source = data; active.value = data.slots.length > 0; return true;
    },
    clear() { source = null; previous = ''; active.value = false; },
    dispose() { stamp.remove(); stopped = true; cancelAnimationFrame(frameId); source = null; active.value = false; },
  };
}
