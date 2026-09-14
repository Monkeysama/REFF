<script setup lang="ts">
import { nextTick, onBeforeUnmount, onMounted, shallowRef } from 'vue';
import { ElMessage, REFF_UI_VERSION } from '@reff/ui';
import { createEmbeddedTransport, createReffClient } from '@reff-sdk/index';
import type { ReffSubscription } from '@reff-sdk/types';

const reff = createReffClient(createEmbeddedTransport());
const ready = shallowRef(false);
const count = shallowRef(0);
let revision = -1;
let subscription: ReffSubscription | null = null;

// 只接受不早于当前修订号的快照，连续点击的迟到响应不能回滚界面。
function applyState(state: { count: number; revision?: number }) {
  const nextRevision = state.revision ?? revision + 1;
  if (nextRevision < revision) return;
  revision = nextRevision;
  count.value = state.count;
}

// 每次点击都发出独立请求；Lua 修订号负责合并并发返回结果。
async function change(amount: number) {
  try {
    applyState(await reff.call('example.interaction.change', { amount }));
  } catch (error) {
    ElMessage.error(String(error));
  }
}

// 页面资源和桥接就绪后通知 Shell，供独立 smoke 验证隔离页确实完成渲染。
function reportPageReady() {
  const button = document.querySelector('.el-button');
  const themeReady = getComputedStyle(document.documentElement).getPropertyValue('--reff-bg').trim().length > 0;
  const styled = !!button && themeReady && button.getBoundingClientRect().width >= 32;
  window.parent.postMessage({ type: 'reff:plugin-ready', pluginId: 'example.interaction', uiVersion: REFF_UI_VERSION, styled }, '*');
}

// 隔离页先订阅再读取快照，覆盖订阅建立期间可能发生的状态变化。
onMounted(async () => {
  try {
    await reff.ready();
    const identity = await reff.call<{ pluginId: string | null }>('ui.identity');
    if (identity.pluginId !== 'example.interaction') throw new Error('插件身份不匹配');
    subscription = await reff.subscribe('example.interaction.changed', applyState);
    applyState(await reff.call('example.interaction.get'));
    ready.value = true;
    await nextTick();
    reportPageReady();
  } catch (error) {
    ElMessage.error(String(error));
  }
});

// 页面卸载时取消本插件订阅并释放 transport，避免旧 iframe 接收事件。
onBeforeUnmount(async () => {
  await subscription?.unsubscribe();
  subscription = null;
  reff.dispose();
});
</script>

<template>
  <main class="plugin-page">
    <el-card class="counter-card" shadow="never">
      <div class="counter">
        <el-button circle :disabled="!ready" aria-label="减少" @click="change(-1)">−</el-button>
        <output>{{ count }}</output>
        <el-button circle :disabled="!ready" aria-label="增加" @click="change(1)">+</el-button>
      </div>
    </el-card>
  </main>
</template>
