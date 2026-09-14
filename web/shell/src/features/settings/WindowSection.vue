<script setup lang="ts">
import { computed } from 'vue';
import type { ReffLanguage } from '@reff/ui';
import type { ReffSettings } from './types';
import { translate } from '../../locales';

const props = defineProps<{ windowSettings: ReffSettings['window']; language: ReffLanguage }>();
const emit = defineEmits<{ change: [value: { rememberGeometry?: boolean }] }>();
const t = computed(() => (key: Parameters<typeof translate>[1]) => translate(props.language, key));
</script>

<template>
  <section class="settings-section">
    <h2>{{ t('window') }}</h2>
    <label class="setting-row"><span>{{ t('rememberGeometry') }}</span><el-switch :model-value="windowSettings.rememberGeometry" @change="emit('change', { rememberGeometry: Boolean($event) })" /></label>
  </section>
</template>

<style scoped>
.settings-section { padding: 28px 0; border-bottom: 1px solid var(--reff-border); }
h2 { margin: 0 0 12px; font-size: calc(16px * var(--reff-text-scale)); font-weight: 650; letter-spacing: 0; }
.setting-row { min-height: 44px; display: flex; align-items: center; justify-content: space-between; gap: 24px; color: var(--reff-text); font-size: calc(13px * var(--reff-text-scale)); }
</style>
