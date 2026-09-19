<script setup lang="ts">
// 中文输入测试卡只持有页面本地文本；不调用游戏方法，也不把测试内容写入配置或存档。
const text = defineModel<string>({ required: true });
</script>

<template>
  <el-card shadow="never" header="输入法测试" class="section-card">
    <el-input v-model="text" clearable placeholder="请输入中文或英文" />
    <p class="input-preview">当前输入：{{ text || '暂无内容' }}</p>
  </el-card>
</template>

<style scoped>
.input-preview { margin: 12px 0 0; color: var(--reff-muted, #aab5c5); overflow-wrap: anywhere; }
</style>
