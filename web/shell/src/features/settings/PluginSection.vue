<script setup lang="ts">
import { computed } from 'vue';
import type { ReffLanguage } from '@reff/ui';
import type { LoadedPlugin } from './types';
import { translate } from '../../locales';

const props = defineProps<{ plugins: LoadedPlugin[]; language: ReffLanguage; version: string }>();
const t = computed(() => (key: Parameters<typeof translate>[1]) => translate(props.language, key));
</script>

<template>
  <section class="settings-section">
    <h2>{{ t('plugins') }}</h2>
    <div class="plugin-row official-row">
      <div><strong>REFF Settings</strong><span>reff.settings · v{{ version }}</span></div>
      <div class="plugin-tags"><el-tag size="small" effect="plain">{{ t('official') }}</el-tag><el-tag size="small" type="success">{{ t('loaded') }}</el-tag></div>
    </div>
    <div v-for="plugin in plugins" :key="plugin.id" class="plugin-row">
      <div><strong>{{ plugin.name }}</strong><span>{{ plugin.id }} · v{{ plugin.version }}</span></div>
      <div class="plugin-tags"><el-tag size="small" effect="plain">{{ plugin.mode === 'component' ? t('component') : t('isolated') }}</el-tag><el-tag size="small" type="success">{{ t('loaded') }}</el-tag></div>
    </div>
    <div v-if="plugins.length === 0" class="empty">{{ t('noExternalPlugins') }}</div>
  </section>
</template>

<style scoped>
.settings-section { padding: 28px 0 12px; }
h2 { margin: 0 0 14px; font-size: calc(16px * var(--reff-text-scale)); font-weight: 650; letter-spacing: 0; }
.plugin-row { min-height: 58px; display: flex; align-items: center; justify-content: space-between; gap: 18px; padding: 9px 0; border-bottom: 1px solid color-mix(in srgb, var(--reff-border) 64%, transparent); }
.plugin-row > div:first-child { min-width: 0; display: flex; flex-direction: column; gap: 4px; }
.plugin-row strong { overflow: hidden; color: var(--reff-text); font-size: calc(13px * var(--reff-text-scale)); font-weight: 600; text-overflow: ellipsis; white-space: nowrap; }
.plugin-row span { color: var(--reff-muted); font-size: calc(11px * var(--reff-text-scale)); }
.plugin-tags { display: flex; flex: 0 0 auto; gap: 6px; }
.plugin-tags :deep(.el-tag) { color: var(--reff-text); border-color: var(--reff-border); background: color-mix(in srgb, var(--reff-card) 76%, transparent); }
.plugin-tags :deep(.el-tag--success) { color: var(--reff-accent-strong); border-color: color-mix(in srgb, var(--reff-accent) 62%, var(--reff-border)); background: color-mix(in srgb, var(--reff-accent) 14%, var(--reff-card)); }
.official-row { border-top: 1px solid color-mix(in srgb, var(--reff-border) 64%, transparent); }
.empty { padding: 20px 0; color: var(--reff-muted); font-size: calc(13px * var(--reff-text-scale)); }
</style>
