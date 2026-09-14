<script setup lang="ts">
import { nextTick, onMounted, shallowRef } from 'vue';
import { ElMessage, REFF_UI_VERSION } from '@reff/ui';
import { createEmbeddedTransport, createReffClient } from '@reff-sdk/index';

const dialogVisible = shallowRef(false);
const lastCommand = shallowRef('尚未选择');

// 下拉菜单示例只更新当前页面状态，不访问游戏或其他插件数据。
function onCommand(command: string) {
  lastCommand.value = command;
  ElMessage.success(`已选择：${command}`);
}

// 确认隔离页身份和公共样式均可用，再向 Shell 发出自检就绪消息。
onMounted(async () => {
  const reff = createReffClient(createEmbeddedTransport());
  try {
    await reff.ready();
    const identity = await reff.call<{ pluginId: string | null }>('ui.identity');
    if (identity.pluginId !== 'example.components') throw new Error('插件身份不匹配');
    await nextTick();
    const button = document.querySelector('.el-button');
    const themeReady = getComputedStyle(document.documentElement).getPropertyValue('--reff-bg').trim().length > 0;
    const styled = !!button && themeReady && button.getBoundingClientRect().width >= 32;
    window.parent.postMessage({ type: 'reff:plugin-ready', pluginId: 'example.components', uiVersion: REFF_UI_VERSION, styled }, '*');
  } catch (error) {
    ElMessage.error(String(error));
  } finally {
    reff.dispose();
  }
});
</script>

<template>
  <main class="plugin-page">
    <div class="component-actions">
      <el-tooltip content="打开公共弹窗" placement="bottom">
        <el-button type="primary" @click="dialogVisible = true">打开弹窗</el-button>
      </el-tooltip>
      <el-dropdown trigger="click" @command="onCommand">
        <el-button>操作菜单</el-button>
        <template #dropdown>
          <el-dropdown-menu>
            <el-dropdown-item command="刷新状态">刷新状态</el-dropdown-item>
            <el-dropdown-item command="复制摘要">复制摘要</el-dropdown-item>
            <el-dropdown-item command="清除选择" divided>清除选择</el-dropdown-item>
          </el-dropdown-menu>
        </template>
      </el-dropdown>
    </div>
    <p class="component-status">最近操作：{{ lastCommand }}</p>
    <el-dialog v-model="dialogVisible" title="公共弹窗" width="420px" :close-on-click-modal="false">
      <p class="dialog-copy">弹窗会随插件内容区重新布局。</p>
      <template #footer><el-button @click="dialogVisible = false">关闭</el-button><el-button type="primary" @click="dialogVisible = false">确认</el-button></template>
    </el-dialog>
  </main>
</template>
