import React, { useEffect, useRef, useState } from 'react';
import { createRoot } from 'react-dom/client';
import { createEmbeddedTransport, createReffClient } from '@reff-sdk/index';
import './style.css';

type Snapshot = { gameName: string; reframeworkVersion: string; hp: { available: boolean; current?: number; max?: number; percent?: number; adjustable: boolean }; uptimeSeconds: number; refreshCount: number };
const reff = createReffClient(createEmbeddedTransport());

// React 示例展示与 Vue 示例相同的 REFF 服务数据，突出 SDK 不依赖 Vue。
function App() {
  const [snapshot, setSnapshot] = useState<Snapshot | null>(null);
  const [ready, setReady] = useState(false);
  const [hpPercent, setHpPercent] = useState(100);
  const requestInFlight = useRef(false);
  const hpInitialized = useRef(false);
  // 轮询 REFF 快照，让运行时间和玩家血量持续更新；请求重叠时跳过本轮。
  const refresh = async (silent = false) => {
    if (requestInFlight.current) return;
    requestInFlight.current = true;
    try { const value = await reff.call<Snapshot>('example.react.refresh'); setSnapshot(value); if (!hpInitialized.current && value.hp.percent != null) { setHpPercent(Math.round(value.hp.percent)); hpInitialized.current = true; } }
    catch (error) { if (!silent) { const output = document.querySelector('#error'); if (output) output.textContent = String(error); } }
    finally { requestInFlight.current = false; }
  };
  // 提交血量百分比，使用后端返回快照立即刷新当前值。
  const setHp = async () => {
    if (requestInFlight.current) return;
    requestInFlight.current = true;
    try { setSnapshot(await reff.call<Snapshot>('example.react.set-hp', { percent: hpPercent })); }
    catch (error) { const output = document.querySelector('#error'); if (output) output.textContent = String(error); }
    finally { requestInFlight.current = false; }
  };
  useEffect(() => { let active = true; let timer: number | undefined; void (async () => { try { await reff.ready(); const identity = await reff.call<{ pluginId: string | null }>('ui.identity'); if (identity.pluginId !== 'example.react') throw new Error('插件身份不匹配'); await refresh(); if (active) { setReady(true); timer = window.setInterval(() => { void refresh(true); }, 500); window.parent.postMessage({ type: 'reff:plugin-ready', pluginId: 'example.react', uiVersion: '0.1.0', styled: true }, '*'); } } catch (error) { const output = document.querySelector('#error'); if (output) output.textContent = String(error); } })(); return () => { active = false; if (timer !== undefined) window.clearInterval(timer); reff.dispose(); }; }, []);
  return <main className="plugin-page"><section className="panel"><h2>运行信息</h2>{snapshot ? <dl>{[['游戏', snapshot.gameName], ['REFramework', snapshot.reframeworkVersion], ['REFF 运行时间', `${snapshot.uptimeSeconds} 秒`], ['刷新次数', String(snapshot.refreshCount)]].map(([label, value]) => <div className="row" key={label}><dt>{label}</dt><dd>{value}</dd></div>)}</dl> : <p>等待 REFF 服务</p>}<button disabled={!ready} onClick={() => void refresh()}>调用 REFF 刷新</button><p id="error" /></section><section className="panel hp-panel"><h2>玩家血量</h2>{snapshot?.hp.available ? <><div className="hp-value">当前：{snapshot.hp.current?.toFixed(0)} / {snapshot.hp.max?.toFixed(0)}（{snapshot.hp.percent?.toFixed(1)}%）</div><div className="hp-control"><label>调整血量：{hpPercent}% <input type="range" min="0" max="100" value={hpPercent} disabled={!snapshot.hp.adjustable} onInput={event => setHpPercent(Number((event.target as HTMLInputElement).value))} /></label><button disabled={!ready || !snapshot.hp.adjustable} onClick={() => void setHp()}>应用血量</button></div></> : <p>当前游戏不支持读取玩家血量</p>}</section></main>;
}
createRoot(document.getElementById('root')!).render(<App />);
