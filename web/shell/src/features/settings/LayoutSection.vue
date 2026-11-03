<script setup lang="ts">
import { computed, shallowRef, watch } from 'vue';
import type { ReffLanguage } from '@reff/ui';
import { translate } from '../../locales';
import type { ReffSettings } from './types';

const props = defineProps<{ layout: ReffSettings['layout']; language: ReffLanguage }>();
const emit = defineEmits<{ change: [value: Partial<ReffSettings['layout']>] }>();
const t = computed(() => (key: Parameters<typeof translate>[1]) => translate(props.language, key));
// 滑动时只保留草稿，松开后提交一次 Core，避免连续磁盘写入。
const padding = shallowRef(props.layout.pluginPadding);
watch(() => props.layout.pluginPadding, value => { padding.value = value; });
</script>

<template>
  <section class="layout-section">
    <h2>{{ t('layout') }}</h2>
    <label class="layout-row"><span>{{ t('sidebarCollapsed') }}</span><el-switch :aria-label="t('sidebarCollapsed')" :model-value="layout.sidebarCollapsed" @change="emit('change', { sidebarCollapsed: Boolean($event) })" /></label>
    <label class="layout-row"><span>{{ t('headerCollapsed') }}</span><el-switch :aria-label="t('headerCollapsed')" :model-value="layout.headerCollapsed" @change="emit('change', { headerCollapsed: Boolean($event) })" /></label>
    <div class="padding-row">
      <label id="plugin-padding-label">{{ t('pluginPadding') }}<small>{{ t('pluginPaddingHint') }}</small></label>
      <el-slider v-model="padding" :min="0" :max="48" :step="1" aria-labelledby="plugin-padding-label" @change="emit('change', { pluginPadding: padding })" />
      <output>{{ padding }} px</output>
    </div>
  </section>
</template>

<style scoped>
.layout-section { padding: 24px 0; border-bottom: 1px solid var(--reff-border); }
h2 { margin: 0 0 12px; font-size: calc(16px * var(--reff-text-scale)); font-weight: 650; }
.layout-row { min-height: 44px; display: flex; align-items: center; justify-content: space-between; gap: 16px; font-size: var(--reff-font-size-sm); }
.padding-row { display: grid; grid-template-columns: minmax(180px, 1fr) minmax(100px, 240px) 48px; align-items: center; gap: 18px; padding-top: 8px; font-size: var(--reff-font-size-sm); }
small { display: block; max-width: 46ch; margin-top: 4px; color: var(--reff-muted); font-size: var(--reff-font-size-xs); line-height: 1.5; }
output { color: var(--reff-muted); text-align: right; font-variant-numeric: tabular-nums; white-space: nowrap; }
@media (max-width: 600px) { .padding-row { grid-template-columns: minmax(0, 1fr) 48px; } .padding-row label { grid-column: 1 / -1; } }
</style>
