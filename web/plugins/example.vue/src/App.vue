<script setup lang="ts">
import { onBeforeUnmount, onMounted, shallowRef } from 'vue';
import { ElMessage, REFF_UI_VERSION } from '@reff/ui';
import { createEmbeddedTransport, createReffClient } from '@reff-sdk/index';

interface Snapshot { gameName: string; reframeworkVersion: string; hp: { available: boolean; current?: number; max?: number; percent?: number; adjustable: boolean }; uptimeSeconds: number; refreshCount: number; }
const reff = createReffClient(createEmbeddedTransport());
const snapshot = shallowRef<Snapshot | null>(null);
const ready = shallowRef(false);
const hpPercent = shallowRef(100);
const hpInitialized = shallowRef(false);
let refreshTimer: number | undefined;
let requestInFlight = false;

// 读取 Vue 示例自己的命名空间，所有字段均由 Lua 后端校验并返回。
// 轮询 REFF 快照，让运行时间和玩家血量在页面打开期间持续更新；同一时刻只允许一个请求。
async function refresh(silent = false) {
  if (requestInFlight) return;
  requestInFlight = true;
  try {
    const value = await reff.call<Snapshot>('example.vue.refresh');
    snapshot.value = value;
    if (!hpInitialized.value && value.hp.percent != null) {
      hpPercent.value = Math.round(value.hp.percent);
      hpInitialized.value = true;
    }
  } catch (error) {
    if (!silent) ElMessage.error(String(error));
  } finally {
    requestInFlight = false;
  }
}
// 提交玩家血量目标值，并立即用后端返回的快照更新显示。
async function setHp() {
  if (requestInFlight) return;
  requestInFlight = true;
  try {
    snapshot.value = await reff.call<Snapshot>('example.vue.set-hp', { percent: hpPercent.value });
  } catch (error) { ElMessage.error(String(error)); }
  finally { requestInFlight = false; }
}
function reportReady() { window.parent.postMessage({ type: 'reff:plugin-ready', pluginId: 'example.vue', uiVersion: REFF_UI_VERSION, styled: !!document.querySelector('.el-card') }, '*'); }
onMounted(async () => {
  try {
    await reff.ready();
    const identity = await reff.call<{ pluginId: string | null }>('ui.identity');
    if (identity.pluginId !== 'example.vue') throw new Error('插件身份不匹配');
    await refresh();
    ready.value = true;
    refreshTimer = window.setInterval(() => { void refresh(true); }, 500);
    reportReady();
  } catch (error) { ElMessage.error(String(error)); }
});
onBeforeUnmount(() => { if (refreshTimer !== undefined) window.clearInterval(refreshTimer); reff.dispose(); });
</script>

<template>
  <main class="plugin-page">
    <el-card shadow="never" header="运行信息">
      <el-descriptions v-if="snapshot" :column="1" border>
        <el-descriptions-item label="游戏">{{ snapshot.gameName }}</el-descriptions-item>
        <el-descriptions-item label="REFramework">{{ snapshot.reframeworkVersion }}</el-descriptions-item>
        <el-descriptions-item label="REFF 运行时间">{{ snapshot.uptimeSeconds }} 秒</el-descriptions-item>
        <el-descriptions-item label="刷新次数">{{ snapshot.refreshCount }}</el-descriptions-item>
      </el-descriptions>
      <el-button type="primary" :disabled="!ready" @click="refresh">调用 REFF 刷新</el-button>
    </el-card>
    <el-card shadow="never" header="玩家血量" class="hp-card">
      <template v-if="snapshot?.hp.available">
        <div class="hp-value">当前：{{ snapshot.hp.current?.toFixed(0) }} / {{ snapshot.hp.max?.toFixed(0) }}（{{ snapshot.hp.percent?.toFixed(1) }}%）</div>
        <div class="hp-control">
          <span>调整血量：{{ hpPercent }}%</span>
          <el-slider v-model="hpPercent" :min="0" :max="100" :step="1" :disabled="!snapshot.hp.adjustable" />
          <el-button type="primary" :disabled="!ready || !snapshot.hp.adjustable" @click="setHp">应用血量</el-button>
        </div>
      </template>
      <el-empty v-else description="当前游戏不支持读取玩家血量" />
    </el-card>
  </main>
</template>
