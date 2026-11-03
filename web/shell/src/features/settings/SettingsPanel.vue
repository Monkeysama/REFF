<script setup lang="ts">
import { computed } from 'vue';
import { RefreshLeft } from '@element-plus/icons-vue';
import { translate } from '../../locales';
import LayoutSection from './LayoutSection.vue';
import AppearanceSection from './AppearanceSection.vue';
import GeneralSection from './GeneralSection.vue';
import InputSection from './InputSection.vue';
import PluginSection from './PluginSection.vue';
import WindowSection from './WindowSection.vue';
import HotkeySection from './HotkeySection.vue';
import type { LoadedPlugin, ReffSettings, SettingsPatch } from './types';

const props = defineProps<{ settings: ReffSettings; plugins: LoadedPlugin[]; version: string; saving: boolean }>();
const emit = defineEmits<{ change: [value: SettingsPatch]; reset: []; hotkeyCapture: [active: boolean] }>();
const t = computed(() => (key: Parameters<typeof translate>[1]) => translate(props.settings.language, key));
</script>

<template>
  <div class="settings-panel" data-reff-system-settings>
    <GeneralSection :language="settings.language" :startup="settings.startup" :navigation="settings.navigation" @change="emit('change', $event)" />
    <HotkeySection :hotkey="settings.hotkey" :language="settings.language" @change="emit('change', { hotkey: $event })" @capture="emit('hotkeyCapture', $event)" />
    <LayoutSection :layout="settings.layout" :language="settings.language" @change="emit('change', { layout: $event })" />
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
footer { display: flex; justify-content: flex-end; padding: 22px 0 4px; }
/* 设置页统一收敛到现有语义字号；窄窗口改为两行控件而非挤小文字。 */
.settings-panel :deep(.setting-copy small) { font-size: var(--reff-font-size-xs); line-height: 1.5; }
@media (max-width: 600px) {
  .settings-panel :deep(.setting-row) { gap: 12px; }
  .settings-panel :deep(.hotkey-control) { width: 100%; display: grid; grid-template-columns: minmax(0, 1fr) minmax(0, 1fr); }
  .settings-panel :deep(.hotkey-control > *) { width: 100%; min-width: 0; }
  .settings-panel :deep(.settings-section > .setting-row:has(.hotkey-control)) { flex-wrap: wrap; }
  .settings-panel :deep(.plugin-row) { flex-wrap: wrap; }
  .settings-panel :deep(.plugin-row > div:first-child) { max-width: 100%; }
  .settings-panel :deep(.plugin-row span) { overflow-wrap: anywhere; }
  .settings-panel :deep(.el-switch) { flex-shrink: 0; }

  .settings-panel :deep(.appearance-section .setting-row:not(.setting-row-stack)) { grid-template-columns: minmax(0, 1fr) 48px; padding: 8px 0; }
  .settings-panel :deep(.appearance-section .setting-row > .setting-label) { grid-column: 1 / -1; }
  .settings-panel :deep(.setting-copy) { min-width: 0; }
  .settings-panel :deep(.language-select) { width: 140px; flex-shrink: 0; }
}
@media (max-width: 440px) {
  .settings-panel :deep(.general-section > .setting-row:first-of-type) { flex-wrap: wrap; gap: 8px; padding-bottom: 12px; }
  .settings-panel :deep(.language-select) { width: 100%; }
}
</style>
