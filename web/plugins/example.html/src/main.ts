import { createEmbeddedTransport, createReffClient, installDevReload, installInputFocusReporter } from '@reff-sdk/index';
import './style.css';

const reff = createReffClient(createEmbeddedTransport());
const stopDevReload = installDevReload(reff);
const removeInputFocusReporter = installInputFocusReporter(reff);
const snapshot = document.querySelector('#snapshot')!;
const refreshButton = document.querySelector<HTMLButtonElement>('#refresh')!;
type Snapshot = { gameName: string; reframeworkVersion: string; hp: { available: boolean; current?: number; max?: number; percent?: number; adjustable: boolean }; uptimeSeconds: number; refreshCount: number };
let hpPercent = 100;
let hpInitialized = false;
let requestInFlight = false;

// 原生页面只使用标准 DOM，调用方式与 Vue、React 示例完全相同。
function render(value: Snapshot) {
  const rows: [string, string][] = [['游戏', value.gameName], ['REFramework', value.reframeworkVersion], ['REFF 运行时间', `${value.uptimeSeconds} 秒`], ['刷新次数', String(value.refreshCount)]];
  if (!hpInitialized && value.hp.percent != null) { hpPercent = Math.round(value.hp.percent); hpInitialized = true; }
  snapshot.replaceChildren(...rows.map(([label, text]) => { const row = document.createElement('div'); row.className = 'row'; row.innerHTML = `<dt>${label}</dt><dd></dd>`; row.querySelector('dd')!.textContent = text; return row; }));
  const health = document.querySelector<HTMLElement>('#health'); if (health) health.hidden = !value.hp.available;
  const healthValue = document.querySelector<HTMLElement>('#health-value'); if (healthValue) healthValue.textContent = value.hp.available ? `当前：${value.hp.current?.toFixed(0)} / ${value.hp.max?.toFixed(0)}（${value.hp.percent?.toFixed(1)}%）` : '当前游戏不支持读取玩家血量';
  const range = document.querySelector<HTMLInputElement>('#hp-range'); if (range) { range.disabled = !value.hp.adjustable; }
}
// 轮询 REFF 快照，让运行时间和玩家血量持续更新；请求重叠时跳过本轮。
async function refresh(silent = false) { if (requestInFlight) return; requestInFlight = true; try { render(await reff.call<Snapshot>('example.html.refresh')); } catch (error) { if (!silent) document.querySelector('#error')!.textContent = String(error); } finally { requestInFlight = false; } }
// 提交血量百分比，使用后端返回快照立即刷新当前值。
async function setHp() { if (requestInFlight) return; requestInFlight = true; try { render(await reff.call<Snapshot>('example.html.set-hp', { percent: hpPercent })); } catch (error) { document.querySelector('#error')!.textContent = String(error); } finally { requestInFlight = false; } }
void (async () => { try { await reff.ready(); const identity = await reff.call<{ pluginId: string | null }>('ui.identity'); if (identity.pluginId !== 'example.html') throw new Error('插件身份不匹配'); await refresh(); refreshButton.disabled = false; window.setInterval(() => { void refresh(true); }, 500); window.parent.postMessage({ type: 'reff:plugin-ready', pluginId: 'example.html', uiVersion: '0.1.0', styled: true }, '*'); } catch (error) { document.querySelector('#error')!.textContent = String(error); } })();
refreshButton.addEventListener('click', () => void refresh());
document.querySelector<HTMLInputElement>('#hp-range')!.addEventListener('input', event => { hpPercent = Number((event.target as HTMLInputElement).value); document.querySelector('#hp-percent')!.textContent = `${hpPercent}%`; });
document.querySelector<HTMLButtonElement>('#set-hp')!.addEventListener('click', () => void setHp());
document.querySelector<HTMLInputElement>('#text-input')!.addEventListener('input', event => {
  const value = (event.target as HTMLInputElement).value;
  document.querySelector('#input-preview')!.textContent = `当前输入：${value || '暂无内容'}`;
});
window.addEventListener('pagehide', () => { removeInputFocusReporter(); stopDevReload(); reff.dispose(); }, { once: true });
