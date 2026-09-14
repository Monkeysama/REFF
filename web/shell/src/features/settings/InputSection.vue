<script setup lang="ts">
import { computed } from 'vue';
import type { ReffLanguage } from '@reff/ui';
import type { ReffSettings } from './types';
import { translate } from '../../locales';

const props = defineProps<{ input: ReffSettings['input']; language: ReffLanguage }>();
const emit = defineEmits<{ change: [value: Partial<ReffSettings['input']>] }>();
const t = computed(() => (key: Parameters<typeof translate>[1]) => translate(props.language, key));
</script>

<template>
  <section class="settings-section">
    <h2>{{ t('input') }}</h2>
    <label class="setting-row"><span>{{ t('mousePassthrough') }}</span><el-switch :model-value="input.mousePassthrough" @change="emit('change', { mousePassthrough: Boolean($event) })" /></label>
    <label class="setting-row"><span>{{ t('keyboardPassthrough') }}</span><el-switch :model-value="input.keyboardPassthrough" @change="emit('change', { keyboardPassthrough: Boolean($event) })" /></label>
    <el-alert v-if="input.mousePassthrough || input.keyboardPassthrough" :title="t('passthroughWarning')" type="warning" :closable="false" show-icon />
  </section>
</template>

<style scoped>
.settings-section { padding: 28px 0; border-bottom: 1px solid var(--reff-border); }
h2 { margin: 0 0 12px; font-size: calc(16px * var(--reff-text-scale)); font-weight: 650; letter-spacing: 0; }
.setting-row { min-height: 44px; display: flex; align-items: center; justify-content: space-between; gap: 24px; color: var(--reff-text); font-size: calc(13px * var(--reff-text-scale)); }
.settings-section :deep(.el-alert) { margin-top: 10px; }
</style>
