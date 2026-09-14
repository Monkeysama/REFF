<script setup lang="ts">
import { computed } from 'vue';
import { RefreshLeft } from '@element-plus/icons-vue';
import type { ReffLanguage } from '@reff/ui';
import { translate } from '../../locales';
import AppearanceSection from './AppearanceSection.vue';
import InputSection from './InputSection.vue';
import PluginSection from './PluginSection.vue';
import WindowSection from './WindowSection.vue';
import type { LoadedPlugin, ReffSettings, SettingsPatch } from './types';

const props = defineProps<{ settings: ReffSettings; plugins: LoadedPlugin[]; version: string; saving: boolean }>();
const emit = defineEmits<{ change: [value: SettingsPatch]; reset: [] }>();
const t = computed(() => (key: Parameters<typeof translate>[1]) => translate(props.settings.language, key));

function setLanguage(value: string) {
  if (value === 'zh-CN' || value === 'en-US') emit('change', { language: value as ReffLanguage });
}
</script>

<template>
  <div class="settings-panel" data-reff-system-settings>
    <section class="general-section">
      <h2>{{ t('general') }}</h2>
      <label class="setting-row">
        <span>{{ t('language') }}</span>
        <el-select :model-value="settings.language" class="language-select" @change="setLanguage">
          <el-option value="zh-CN" :label="t('chinese')" />
          <el-option value="en-US" :label="t('english')" />
        </el-select>
      </label>
    </section>
    <AppearanceSection :appearance="settings.appearance" :language="settings.language" @change="emit('change', { appearance: $event })" />
    <InputSection :input="settings.input" :language="settings.language" @change="emit('change', { input: $event })" />
    <WindowSection :window-settings="settings.window" :language="settings.language" @change="emit('change', { window: $event })" />
    <PluginSection :plugins="plugins" :language="settings.language" :version="version" />
    <footer>
      <el-button :icon="RefreshLeft" :loading="saving" @click="emit('reset')">{{ saving ? t('saving') : t('restoreDefaults') }}</el-button>
    </footer>
  </div>
</template>

<style scoped>
.settings-panel { width: 100%; max-width: 920px; margin: 0 auto; }
.general-section { padding: 0 0 28px; border-bottom: 1px solid var(--reff-border); }
h2 { margin: 0 0 12px; font-size: calc(16px * var(--reff-text-scale)); font-weight: 650; letter-spacing: 0; }
.setting-row { min-height: 44px; display: flex; align-items: center; justify-content: space-between; gap: 24px; color: var(--reff-text); font-size: calc(13px * var(--reff-text-scale)); }
.language-select { width: 180px; }
footer { display: flex; justify-content: flex-end; padding: 22px 0 4px; }
</style>
