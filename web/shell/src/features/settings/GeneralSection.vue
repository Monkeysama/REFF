<script setup lang="ts">
import { computed } from 'vue';
import type { ReffLanguage } from '@reff/ui';
import { translate } from '../../locales';
import type { ReffSettings, SettingsPatch } from './types';

const props = defineProps<{ language: ReffLanguage; startup: ReffSettings['startup'] }>();
const emit = defineEmits<{ change: [value: SettingsPatch] }>();
const t = computed(() => (key: Parameters<typeof translate>[1]) => translate(props.language, key));

// 常规设置只发出差量，由 Core 完成校验与持久化；组件不保留第二份设置状态。
function setLanguage(value: string) {
  if (value === 'zh-CN' || value === 'en-US') emit('change', { language: value as ReffLanguage });
}
</script>

<template>
  <section class="general-section">
    <h2>{{ t('general') }}</h2>
    <label class="setting-row">
      <span>{{ t('language') }}</span>
      <el-select :model-value="language" class="language-select" @change="setLanguage">
        <el-option value="zh-CN" :label="t('chinese')" />
        <el-option value="en-US" :label="t('english')" />
      </el-select>
    </label>
    <label class="setting-row">
      <span class="setting-copy">
        <strong>{{ t('preload') }}</strong>
        <small>{{ t('preloadHint') }}</small>
      </span>
      <el-switch :model-value="startup.preload" @change="emit('change', { startup: { preload: Boolean($event) } })" />
    </label>
  </section>
</template>

<style scoped>
.general-section { padding: 0 0 28px; border-bottom: 1px solid var(--reff-border); }
h2 { margin: 0 0 12px; font-size: calc(16px * var(--reff-text-scale)); font-weight: 650; letter-spacing: 0; }
.setting-row { min-height: 44px; display: flex; align-items: center; justify-content: space-between; gap: 24px; color: var(--reff-text); font-size: calc(13px * var(--reff-text-scale)); }
.setting-copy { display: grid; gap: 3px; }
.setting-copy strong { font: inherit; font-weight: 500; }
.setting-copy small { color: var(--reff-muted); font-size: calc(11px * var(--reff-text-scale)); line-height: 1.4; }
.language-select { width: 180px; }
</style>
