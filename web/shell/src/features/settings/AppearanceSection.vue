<script setup lang="ts">
import { computed, shallowRef, watch } from 'vue';
import type { ReffAppearance, ReffLanguage } from '@reff/ui';
import { translate } from '../../locales';

const props = defineProps<{ appearance: ReffAppearance; language: ReffLanguage }>();
const emit = defineEmits<{ change: [value: Partial<ReffAppearance>] }>();
const t = computed(() => (key: Parameters<typeof translate>[1]) => translate(props.language, key));
const opacity = shallowRef(props.appearance.backgroundOpacity);
const radius = shallowRef(props.appearance.cornerRadius);
const blur = shallowRef(props.appearance.surfaceBlur);
const textScale = shallowRef(props.appearance.textScale ?? 1);
const presets = [
  { id: 'emerald', color: '#67D3B2' },
  { id: 'ocean', color: '#5DADE2' },
  { id: 'violet', color: '#A78BFA' },
  { id: 'amber', color: '#F2B84B' },
] as const;

watch(() => props.appearance, value => {
  opacity.value = value.backgroundOpacity;
  radius.value = value.cornerRadius;
  blur.value = value.surfaceBlur;
  textScale.value = value.textScale ?? 1;
}, { deep: true });

function updateNumber(target: 'opacity' | 'radius' | 'blur' | 'textScale', value: number | number[]) {
  const next = Number(Array.isArray(value) ? value[0] : value);
  if (Number.isFinite(next)) {
    if (target === 'opacity') opacity.value = next;
    if (target === 'radius') radius.value = next;
    if (target === 'blur') blur.value = next;
    if (target === 'textScale') textScale.value = next;
  }
}

function changeColor(key: 'accent' | 'background' | 'surface', value: string | null) {
  if (value) emit('change', { [key]: value });
}
</script>

<template>
  <section class="settings-section">
    <h2>{{ t('appearance') }}</h2>
    <div class="setting-row setting-row-stack">
      <span class="setting-label">{{ t('theme') }}</span>
      <div class="preset-grid">
        <button
          v-for="item in presets"
          :key="item.id"
          type="button"
          class="preset-button"
          :class="{ active: appearance.preset === item.id }"
          @click="emit('change', { preset: item.id })"
        >
          <i :style="{ backgroundColor: item.color }" />
          <span>{{ t(item.id) }}</span>
        </button>
      </div>
    </div>
    <div class="setting-row setting-row-stack">
      <span class="setting-label">{{ t('customColors') }}</span>
      <div class="color-grid">
        <label><span>{{ t('accent') }}</span><el-color-picker :model-value="appearance.accent" @change="changeColor('accent', $event)" /></label>
        <label><span>{{ t('background') }}</span><el-color-picker :model-value="appearance.background" @change="changeColor('background', $event)" /></label>
        <label><span>{{ t('surface') }}</span><el-color-picker :model-value="appearance.surface" @change="changeColor('surface', $event)" /></label>
      </div>
    </div>
    <div class="setting-row">
      <span class="setting-label">{{ t('backgroundOpacity') }}</span>
      <el-slider :model-value="opacity" :min="0.55" :max="1" :step="0.01" :format-tooltip="(value: number) => `${Math.round(value * 100)}%`" @input="updateNumber('opacity', $event)" @change="emit('change', { backgroundOpacity: opacity })" />
      <output>{{ Math.round(opacity * 100) }}%</output>
    </div>
    <div class="setting-row">
      <span class="setting-label">{{ t('cornerRadius') }}</span>
      <el-slider :model-value="radius" :min="0" :max="16" :step="1" @input="updateNumber('radius', $event)" @change="emit('change', { cornerRadius: radius })" />
      <output>{{ radius }}</output>
    </div>
    <div class="setting-row">
      <span class="setting-label">{{ t('surfaceBlur') }}</span>
      <el-slider :model-value="blur" :min="0" :max="32" :step="1" @input="updateNumber('blur', $event)" @change="emit('change', { surfaceBlur: blur })" />
      <output>{{ blur }}</output>
    </div>
    <div class="setting-row">
      <span class="setting-label">{{ t('textSize') }}</span>
      <el-slider :model-value="textScale" :min="0.8" :max="1.4" :step="0.05" :format-tooltip="(value: number) => `${Math.round(value * 100)}%`" @input="updateNumber('textScale', $event)" @change="emit('change', { textScale })" />
      <output>{{ Math.round(textScale * 100) }}%</output>
    </div>
  </section>
</template>

<style scoped>
.settings-section { padding: 28px 0; border-bottom: 1px solid var(--reff-border); }
h2 { margin: 0 0 18px; font-size: calc(16px * var(--reff-text-scale)); font-weight: 650; letter-spacing: 0; }
.setting-row { min-height: 44px; display: grid; grid-template-columns: 170px minmax(160px, 1fr) 58px; align-items: center; gap: 18px; font-size: calc(13px * var(--reff-text-scale)); }
.setting-row-stack { display: block; margin-bottom: 18px; }
.setting-label { color: var(--reff-text); font-size: calc(13px * var(--reff-text-scale)); }
.setting-row-stack > .setting-label { display: block; margin-bottom: 10px; }
.preset-grid { display: grid; grid-template-columns: repeat(4, minmax(92px, 1fr)); gap: 8px; }
.preset-button { height: 40px; display: flex; align-items: center; gap: 9px; padding: 0 12px; border: 1px solid var(--reff-border); border-radius: 5px; background: color-mix(in srgb, var(--reff-card) 88%, transparent); color: var(--reff-text); font: inherit; }
.preset-button:hover, .preset-button.active { border-color: var(--reff-accent); background: color-mix(in srgb, var(--reff-accent) 13%, var(--reff-card)); }
.preset-button i { width: 14px; height: 14px; flex: 0 0 14px; border: 1px solid rgb(255 255 255 / 28%); border-radius: 3px; }
.color-grid { display: grid; grid-template-columns: repeat(3, minmax(116px, 1fr)); gap: 10px; }
.color-grid label { height: 42px; display: flex; align-items: center; justify-content: space-between; padding: 0 11px; border: 1px solid var(--reff-border); border-radius: 5px; background: color-mix(in srgb, var(--reff-card) 78%, transparent); color: var(--reff-muted); font-size: 12px; }
.setting-row :deep(.el-slider) { width: 100%; }
output { color: var(--reff-muted); font-size: calc(12px * var(--reff-text-scale)); text-align: right; }
@media (max-width: 720px) {
  .preset-grid { grid-template-columns: repeat(2, minmax(92px, 1fr)); }
  .color-grid { grid-template-columns: 1fr; }
  .setting-row { grid-template-columns: 140px minmax(110px, 1fr) 52px; gap: 10px; }
}
</style>
