import { ReffTransport } from './types';

// 开发预览用内存传输；它只模拟协议，不代表游戏内验收结果。
export function createMockTransport(): ReffTransport {
  let count = 0;
  return { async call(method: string, params: Record<string, unknown> = {}) {
    if (method === 'ui.status') return { ready: true, epoch: 1 };
    if (method === 'ui.plugins') return { plugins: [{ id: 'example.settings', name: '设置示例', version: '0.1.0', kind: 'schema', entry: 'ui/index.html', methods: [], events: ['example.settings.changed'], fallback: 'plugin-managed' }, { id: 'example.status', name: '状态示例', version: '0.1.0', kind: 'page', entry: 'ui/index.html', methods: [], events: [], fallback: 'none' }], errors: [] };
    if (method === 'example.counter.get') return { count, revision: 0 };
    if (method === 'example.counter.increment') { count += Number(params.amount ?? 1); return { count, revision: 0 }; }
    if (method === 'ui.subscribe' || method === 'ui.unsubscribe') return {};
    throw new Error(`mock method not found: ${method}`);
  } };
}
