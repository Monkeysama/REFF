<script setup lang="ts">
import { computed } from 'vue';
import { RefreshLeft } from '@element-plus/icons-vue';
import { translate } from '../../locales';
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
    <GeneralSection :language="settings.language" :startup="settings.startup" @change="emit('change', $event)" />
    <HotkeySection :hotkey="settings.hotkey" :language="settings.language" @change="emit('change', { hotkey: $event })" @capture="emit('hotkeyCapture', $event)" />
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
</style>
