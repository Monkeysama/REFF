import { ReffTransport } from './types';

// 开发预览用内存传输；它只模拟协议，不代表游戏内验收结果。
export function createMockTransport(): ReffTransport {
  let count = 0;
  return { async call(method: string, params: Record<string, unknown> = {}) {
    if (method === 'ui.status') return { ready: true, epoch: 1 };
    if (method === 'ui.plugins') return { plugins: [
      { id: 'example.vue', name: 'Vue 3 接入示例', version: '0.1.0', kind: 'page', entry: 'ui/dist/index.html', methods: ['example.vue.get', 'example.vue.refresh', 'example.vue.set-hp'], events: ['example.vue.refreshed'], fallback: 'none' },
      { id: 'example.react', name: 'React 接入示例', version: '0.1.0', kind: 'page', entry: 'ui/dist/index.html', methods: ['example.react.get', 'example.react.refresh', 'example.react.set-hp'], events: [], fallback: 'none' },
      { id: 'example.html', name: '原生 HTML 接入示例', version: '0.1.0', kind: 'page', entry: 'ui/dist/index.html', methods: ['example.html.get', 'example.html.refresh', 'example.html.set-hp'], events: [], fallback: 'none' },
    ], errors: [] };
    if (method.endsWith('.get') || method.endsWith('.refresh')) return { gameName: 'Mock Game', reframeworkVersion: 'mock', hp: { available: false, adjustable: false }, uptimeSeconds: 1, refreshCount: ++count };
    if (method.endsWith('.set-hp')) return { gameName: 'Mock Game', reframeworkVersion: 'mock', hp: { available: false, adjustable: false }, uptimeSeconds: 1, refreshCount: ++count };
    if (method === 'ui.subscribe' || method === 'ui.unsubscribe') return {};
    throw new Error(`mock method not found: ${method}`);
  } };
}
