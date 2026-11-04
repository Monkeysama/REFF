<script setup lang="ts">
import { computed, shallowRef, watch } from 'vue';
import type { ReffLanguage } from '@reff/ui';
import { translate } from '../../locales';
import type { LoadedPlugin, ReffSettings, SettingsPatch } from './types';

const props = defineProps<{ layout: ReffSettings['layout']; language: ReffLanguage; plugins: LoadedPlugin[] }>();
const emit = defineEmits<{ change: [value: NonNullable<SettingsPatch['layout']>] }>();
const t = computed(() => (key: Parameters<typeof translate>[1]) => translate(props.language, key));
// 滑动时只保留草稿，松开后提交一次 Core，避免连续磁盘写入。
// 全局选项使用非空哨兵值，避免 Element Plus 将空字符串显示为未选择。
const globalTarget = '*';
const target = shallowRef(globalTarget);
const inherited = computed(() => target.value !== globalTarget && props.layout.pluginPaddingOverrides[target.value] === undefined);
const effectivePadding = computed(() => props.layout.pluginPaddingOverrides[target.value] ?? props.layout.pluginPadding);
const padding = shallowRef(effectivePadding.value);
watch(effectivePadding, value => { padding.value = value; });
// 全局与单插件使用同一范围，只有覆盖值写入当前插件项，关闭开关即删除覆盖记录。
function changePadding(value: number | null) {
  if (target.value !== globalTarget) emit('change', { pluginPaddingOverrides: { [target.value]: value } });
  else if (value !== null) emit('change', { pluginPadding: value });
}
</script>

<template>
  <section class="layout-section">
    <h2>{{ t('layout') }}</h2>
    <label class="layout-row"><span>{{ t('sidebarCompact') }}</span><el-switch :aria-label="t('sidebarCompact')" :model-value="layout.sidebarCompact" @change="emit('change', { sidebarCompact: Boolean($event) })" /></label>
    <label class="layout-row"><span>{{ t('sidebarCollapsed') }}</span><el-switch :aria-label="t('sidebarCollapsed')" :model-value="layout.sidebarCollapsed" @change="emit('change', { sidebarCollapsed: Boolean($event) })" /></label>
    <label class="layout-row"><span>{{ t('headerCompact') }}</span><el-switch :aria-label="t('headerCompact')" :model-value="layout.headerCompact" @change="emit('change', { headerCompact: Boolean($event) })" /></label>
    <label class="layout-row"><span>{{ t('headerCollapsed') }}</span><el-switch :aria-label="t('headerCollapsed')" :model-value="layout.headerCollapsed" @change="emit('change', { headerCollapsed: Boolean($event) })" /></label>
    <div class="padding-target">
      <label for="padding-target">{{ t('paddingTarget') }}</label>
      <el-select id="padding-target" v-model="target" :aria-label="t('paddingTarget')">
        <el-option :label="t('globalPadding')" :value="globalTarget" />
        <el-option v-for="plugin in plugins" :key="plugin.id" :label="plugin.localizedName?.[language] || plugin.name" :value="plugin.id" />
      </el-select>
    </div>
    <label v-if="target !== globalTarget" class="layout-row"><span>{{ t('inheritPadding') }}</span><el-switch :aria-label="t('inheritPadding')" :model-value="inherited" @change="changePadding($event ? null : layout.pluginPadding)" /></label>
    <div class="padding-row">
      <label id="plugin-padding-label">{{ t('pluginPadding') }}<small>{{ t('pluginPaddingHint') }}</small></label>
      <el-slider v-model="padding" :disabled="inherited" :min="0" :max="48" :step="1" aria-labelledby="plugin-padding-label" @change="changePadding(padding)" />
      <output>{{ padding }} px</output>
    </div>
  </section>
</template>

<style scoped>
.layout-section { padding: 24px 0; border-bottom: 1px solid var(--reff-border); }
h2 { margin: 0 0 12px; font-size: calc(16px * var(--reff-text-scale)); font-weight: 650; }
.layout-row { min-height: 44px; display: flex; align-items: center; justify-content: space-between; gap: 16px; font-size: var(--reff-font-size-sm); }
.padding-target { display: flex; align-items: center; justify-content: space-between; gap: 16px; margin-top: 12px; font-size: var(--reff-font-size-sm); }
.padding-target .el-select { width: 240px; max-width: 60%; }
.padding-row { display: grid; grid-template-columns: minmax(180px, 1fr) minmax(100px, 240px) 48px; align-items: center; gap: 18px; padding-top: 8px; font-size: var(--reff-font-size-sm); }
small { display: block; max-width: 46ch; margin-top: 4px; color: var(--reff-muted); font-size: var(--reff-font-size-xs); line-height: 1.5; }
output { color: var(--reff-muted); text-align: right; font-variant-numeric: tabular-nums; white-space: nowrap; }
@media (max-width: 600px) { .padding-row { grid-template-columns: minmax(0, 1fr) 48px; } .padding-row label { grid-column: 1 / -1; } }
</style>
