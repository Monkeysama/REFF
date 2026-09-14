<script setup lang="ts">
import { nextTick, onBeforeUnmount, onMounted, shallowRef } from 'vue';
import { ElMessage, REFF_UI_VERSION } from '@reff/ui';
import { createEmbeddedTransport, createReffClient, installInputFocusReporter } from '@reff-sdk/index';

const reff = createReffClient(createEmbeddedTransport());
const ready = shallowRef(false);
const uptime = shallowRef(0);
const imeText = shallowRef('');
let inputFocusReporter: (() => void) | null = null;

// 页面只读取自己的低频状态摘要，不访问游戏对象或其他插件命名空间。
async function refresh() {
  try {
    const state = await reff.call<{ connected: boolean; uptime: number }>('example.status.get');
    ready.value = state.connected;
    uptime.value = state.uptime;
  } catch (error) {
    ElMessage.error(String(error));
  }
}

// 页面资源和公共样式就绪后通知 Shell，供独立 smoke 验证实际插件内容。
function reportPageReady() {
  const button = document.querySelector('.el-button');
  const themeReady = getComputedStyle(document.documentElement).getPropertyValue('--reff-bg').trim().length > 0;
  const styled = !!button && themeReady && button.getBoundingClientRect().width >= 32;
  window.parent.postMessage({ type: 'reff:plugin-ready', pluginId: 'example.status', uiVersion: REFF_UI_VERSION, styled }, '*');
}

// 隔离页独立安装输入焦点上报，Shell 负责把 iframe 坐标换算到游戏客户区。
onMounted(async () => {
  inputFocusReporter = installInputFocusReporter(reff);
  try {
    await reff.ready();
    const identity = await reff.call<{ pluginId: string | null }>('ui.identity');
    if (identity.pluginId !== 'example.status') throw new Error('插件身份不匹配');
    await refresh();
    await nextTick();
    reportPageReady();
  } catch (error) {
    ElMessage.error(String(error));
  }
});

// 页面销毁时释放输入监听和传输句柄，避免焦点残留到下一插件。
onBeforeUnmount(() => {
  inputFocusReporter?.();
  inputFocusReporter = null;
  reff.dispose();
});
</script>

<template>
  <main class="plugin-page">
    <el-card shadow="never">
      <el-descriptions :column="1" border>
        <el-descriptions-item label="连接状态">{{ ready ? '已连接' : '等待 Lua 后端' }}</el-descriptions-item>
        <el-descriptions-item label="运行时间">{{ uptime }} 秒</el-descriptions-item>
      </el-descriptions>
      <el-button class="refresh" @click="refresh">刷新</el-button>
    </el-card>
    <el-card class="ime-test" shadow="never">
      <el-input id="status-ime-input" v-model="imeText" placeholder="测试中文输入" />
    </el-card>
  </main>
</template>
