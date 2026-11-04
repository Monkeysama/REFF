import type { ReffClient } from './types';

export interface NativePreviewCapabilities { version: number; layoutProtocol: number; maxSlots: number; composition: 'underlay-alpha'; color: 'sdr-unorm8'; }
export interface NativePreviewSlot { dispose(): void; }
export type PreviewBox = [number, number, number, number];
export interface PreviewLayout { pageId: string; viewportWidth: number; viewportHeight: number; paint: [number, number, number]; slots: { slotId: string; rect: PreviewBox; clip: PreviewBox }[]; }

// DOM 的实际 CSS 像素（包括 CSS zoom/平移/轴对齐缩放）；裁剪祖先的 overflow 内容框。
// 非矩形裁剪/旋转/透视不是 v1 槽位，不猜测其外包矩形，直接停画。
export function previewElementBounds(element: HTMLElement, visibleClip?: PreviewBox): { rect: PreviewBox; clip: PreviewBox } | null {
  const r = element.getBoundingClientRect();
  if (!element.isConnected || r.width <= 0 || r.height <= 0) return null;
  let left = Math.max(0, r.left), top = Math.max(0, r.top), right = Math.min(innerWidth, r.right), bottom = Math.min(innerHeight, r.bottom);
  if (visibleClip) { left = Math.max(left, visibleClip[0]); top = Math.max(top, visibleClip[1]); right = Math.min(right, visibleClip[0] + visibleClip[2]); bottom = Math.min(bottom, visibleClip[1] + visibleClip[3]); }
  for (let node: HTMLElement | null = element; node; node = node.parentElement) {
    const style = getComputedStyle(node), box = node.getBoundingClientRect();
    if (style.display === 'none' || style.visibility !== 'visible' || Number(style.opacity) !== 1 || style.clipPath !== 'none' || style.maskImage !== 'none' || style.filter !== 'none' || style.perspective !== 'none' || style.rotate !== 'none') return null;
    if (style.scale !== 'none' && style.scale.split(/\s+/).some(v => !Number.isFinite(Number(v)) || Number(v) <= 0)) return null;
    if (style.clip !== 'auto' || /paint|strict|content/.test(style.contain)) return null;
    if (style.transform !== 'none') { const m = new DOMMatrixReadOnly(style.transform); if (!m.is2D || m.b !== 0 || m.c !== 0 || m.a <= 0 || m.d <= 0) return null; }
    if (node !== element) {
      const sx = box.width / Math.max(1, node.offsetWidth), sy = box.height / Math.max(1, node.offsetHeight);
      if (/(hidden|clip|scroll|auto)/.test(style.overflowX)) { left = Math.max(left, box.left + node.clientLeft * sx); right = Math.min(right, box.left + (node.clientLeft + node.clientWidth) * sx); }
      if (/(hidden|clip|scroll|auto)/.test(style.overflowY)) { top = Math.max(top, box.top + node.clientTop * sy); bottom = Math.min(bottom, box.top + (node.clientTop + node.clientHeight) * sy); }
    }
    // 圆角祖先不等于槽位被圆角裁剪；仅当实际可见矩形进入曲线角区时停画。
    // 使用圆角的保守角区边界，不把不可表达的曲线近似为可绘制矩形。
    if (/(hidden|clip|scroll|auto)/.test(style.overflow)) {
      const sx = box.width / Math.max(1, node.offsetWidth), sy = box.height / Math.max(1, node.offsetHeight);
      const corners = [style.borderTopLeftRadius, style.borderTopRightRadius, style.borderBottomLeftRadius, style.borderBottomRightRadius];
      for (const [index, value] of corners.entries()) {
        const parts = value.split(/\s+/), radius = (v: string, extent: number, scale: number) => v.endsWith('%') ? parseFloat(v) * extent / 100 : parseFloat(v) * scale;
        const rx = Math.min(box.width, radius(parts[0], box.width, sx)), ry = Math.min(box.height, radius(parts[1] || parts[0], box.height, sy));
        if (!Number.isFinite(rx) || !Number.isFinite(ry)) return null;
        const x = index % 2 ? box.right - rx : box.left, y = index > 1 ? box.bottom - ry : box.top;
        if (rx > 0 && ry > 0 && right > x && left < x + rx && bottom > y && top < y + ry) return null;
      }
    }
  }
  return right > left && bottom > top ? { rect: [r.x, r.y, r.width, r.height], clip: [left, top, right - left, bottom - top] } : null;
}

const reporters = new WeakMap<ReffClient, { pageId: string; elements: Map<string, HTMLElement>; resize: ResizeObserver; stop: () => void }>();
/** 可选功能：在原生能力查询成功后注册当前隔离页槽位。祖先/槽位背景须透明，网页前景保持正常。 */
export async function registerNativePreviewSlot(client: ReffClient, element: HTMLElement, slotId: string): Promise<NativePreviewSlot> {
  const capabilities = await client.call<NativePreviewCapabilities>('ui.preview.capabilities');
  if (capabilities.version !== 1) throw new Error('Unsupported native preview ABI');
  if (capabilities.layoutProtocol !== 2) throw new Error('Native preview layout protocol 2 is required');
  const { pageId } = await client.call<{ pageId: string }>('ui.preview.register', { slotId });
  let reporter = reporters.get(client);
  if (!reporter) {
    const elements = new Map<string, HTMLElement>(); let frame = 0, stopped = false, previous = '', sent = 0, revision = 0;
    // 子文档的布局标记与透明孔一同 paint；父页消息和 DOM 重排不是同一事务。
    // Mutation/ResizeObserver 在绘制前更新标记，覆盖晚于 rAF 的字号/zoom 修改；标记本身不触发布局反馈。
    const stamp = document.createElement('i');
    stamp.style.cssText = 'position:fixed!important;width:3px!important;height:3px!important;left:0!important;top:0!important;z-index:2147483647!important;pointer-events:none!important;opacity:1!important;display:block!important;visibility:visible!important;margin:0!important;padding:0!important;border:0!important;transform:none!important';
    document.documentElement.append(stamp);
    const report = (now: number) => {
      if (stopped) return;
      const slots: PreviewLayout['slots'] = [];
      for (const [id, target] of elements) { const bounds = previewElementBounds(target); if (bounds) slots.push({ slotId: id, ...bounds }); }
      const signature = JSON.stringify([innerWidth, innerHeight, slots]);
      if (signature !== previous || now - sent > 200) {
        if (signature !== previous) {
          revision = revision % 0xffffff + 1;
          stamp.style.setProperty('background', `rgb(${revision & 255},${revision >> 8 & 255},${revision >> 16 & 255})`, 'important');
          const size = stamp.getBoundingClientRect();
          stamp.style.setProperty('left', `${Math.min(98, Math.max(0, innerWidth - 4)) / (size.width / 3 || 1)}px`, 'important');
          stamp.style.setProperty('top', `${Math.max(0, innerHeight - 24) / (size.height / 3 || 1)}px`, 'important');
        }
        const r = stamp.getBoundingClientRect();
        parent.postMessage({ type: 'reff:preview-layout', pageId, viewportWidth: innerWidth, viewportHeight: innerHeight,
          paint: [r.x + r.width / 2, r.y + r.height / 2, revision], slots }, '*');
        previous = signature; sent = now;
      }
    };
    const changed = () => report(performance.now());
    const mutation = new MutationObserver(records => { if (records.some(record => record.target !== stamp)) changed(); });
    mutation.observe(document.documentElement, { attributes: true, childList: true, characterData: true, subtree: true });
    const resize = new ResizeObserver(changed); resize.observe(document.documentElement);
    const tick = (now: number) => { report(now); if (!stopped) frame = requestAnimationFrame(tick); };
    addEventListener('resize', changed); addEventListener('scroll', changed, true);
    reporter = { pageId, elements, resize, stop: () => {
      stopped = true; cancelAnimationFrame(frame); mutation.disconnect(); resize.disconnect();
      removeEventListener('resize', changed); removeEventListener('scroll', changed, true);
      parent.postMessage({ type: 'reff:preview-layout', pageId, viewportWidth: innerWidth, viewportHeight: innerHeight, paint: [0, 0, 0], slots: [] }, '*');
      stamp.remove(); reporters.delete(client);
    } };
    reporters.set(client, reporter); frame = requestAnimationFrame(tick);
    addEventListener('pagehide', reporter.stop, { once: true });
  }
  if (reporter.elements.has(slotId)) throw new Error('Preview slot already attached');
  reporter.elements.set(slotId, element); reporter.resize.observe(element);
  let disposed = false;
  return { dispose() { if (disposed) return; disposed = true; reporter.elements.delete(slotId); if (![...reporter.elements.values()].includes(element)) reporter.resize.unobserve(element); void client.call('ui.preview.remove', { slotId }).catch(() => {}); if (!reporter.elements.size) reporter.stop(); } };
}
