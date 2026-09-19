<script setup lang="ts">
import { computed, onBeforeUnmount, ref } from 'vue';
import type { ReffLanguage } from '@reff/ui';
import { translate } from '../../locales';

const props = defineProps<{ hotkey: { key: number; modifiers: number }; language: ReffLanguage }>();
const emit = defineEmits<{ change: [value: { key: number; modifiers: number }]; capture: [active: boolean] }>();
const capturing = ref(false);
const t = computed(() => (key: Parameters<typeof translate>[1]) => translate(props.language, key));

const keyNames: Record<number, string> = {
  8: 'Backspace', 9: 'Tab', 13: 'Enter', 16: 'Shift', 17: 'Ctrl', 18: 'Alt', 19: 'Pause', 20: 'Caps Lock',
  27: 'Esc', 32: 'Space', 33: 'Page Up', 34: 'Page Down', 35: 'End', 36: 'Home', 37: 'Left Arrow', 38: 'Up Arrow',
  39: 'Right Arrow', 40: 'Down Arrow', 44: 'Print Screen', 45: 'Insert', 46: 'Delete', 91: 'Left Win', 92: 'Right Win',
  93: 'Menu', 144: 'Num Lock', 145: 'Scroll Lock', 106: 'Numpad *', 107: 'Numpad +', 109: 'Numpad -', 110: 'Numpad .', 111: 'Numpad /',
};
for (let index = 0; index < 10; index += 1) keyNames[0x30 + index] = String(index);
for (let index = 0; index < 26; index += 1) keyNames[0x41 + index] = String.fromCharCode(65 + index);
for (let index = 0; index < 24; index += 1) keyNames[0x70 + index] = `F${index + 1}`;
for (let index = 0; index < 10; index += 1) keyNames[0x60 + index] = `Numpad ${index}`;

function keyName(key: number): string {
  const punctuation: Record<number, string> = { 186: ';', 187: '=', 188: ',', 189: '-', 190: '.', 191: '/', 192: '`', 219: '[', 220: '\\', 221: ']', 222: "'", 226: '\\' };
  return keyNames[key] || punctuation[key] || 'Unknown key';
}

function displayName(binding: { key: number; modifiers: number }): string {
  const modifiers = ['Shift', 'Ctrl', 'Alt', 'Win'].filter((_, index) => binding.modifiers & (1 << index));
  return [...modifiers, keyName(binding.key)].join(' + ');
}

function codeToKey(code: string): number | null {
  if (/^Key[A-Z]$/.test(code)) return code.charCodeAt(3);
  if (/^Digit[0-9]$/.test(code)) return code.charCodeAt(5);
  if (/^Numpad[0-9]$/.test(code)) return 0x60 + Number(code.slice(-1));
  if (/^F([1-9]|1[0-9]|2[0-4])$/.test(code)) return 0x6f + Number(code.slice(1));
  const keys: Record<string, number> = {
    Escape: 27, Tab: 9, Enter: 13, Space: 32, Backspace: 8, Insert: 45, Delete: 46, Home: 36, End: 35, PageUp: 33, PageDown: 34,
    ArrowLeft: 37, ArrowUp: 38, ArrowRight: 39, ArrowDown: 40, CapsLock: 20, NumLock: 144, ScrollLock: 145, Pause: 19, PrintScreen: 44,
    ContextMenu: 93, ShiftLeft: 16, ShiftRight: 16, ControlLeft: 17, ControlRight: 17, AltLeft: 18, AltRight: 18, MetaLeft: 91, MetaRight: 92,
    NumpadMultiply: 106, NumpadAdd: 107, NumpadEnter: 13, NumpadSubtract: 109, NumpadDecimal: 110, NumpadDivide: 111, NumpadEqual: 146, NumpadComma: 188, Minus: 189, Equal: 187,
    BracketLeft: 219, BracketRight: 221, Backslash: 220, Semicolon: 186, Quote: 222, Comma: 188, Period: 190, Slash: 191, Backquote: 192, IntlBackslash: 226,
  };
  return keys[code] ?? null;
}

// 某些 CEF/键盘布局组合可能只提供 key 而不提供 code；用标准 DOM key 值兜底，仍转换为 Windows 虚拟键码。
function eventToKey(event: KeyboardEvent): number | null {
  const byCode = codeToKey(event.code);
  if (byCode !== null) return byCode;
  if (/^[a-zA-Z]$/.test(event.key)) return event.key.toUpperCase().charCodeAt(0);
  if (/^[0-9]$/.test(event.key)) return event.key.charCodeAt(0);
  if (/^F([1-9]|1[0-9]|2[0-4])$/.test(event.key)) return 0x6f + Number(event.key.slice(1));
  const byKey: Record<string, number> = { Escape: 27, Tab: 9, Enter: 13, ' ': 32, Backspace: 8, Insert: 45, Delete: 46, Home: 36, End: 35, PageUp: 33, PageDown: 34, ArrowLeft: 37, ArrowUp: 38, ArrowRight: 39, ArrowDown: 40 };
  return byKey[event.key] ?? null;
}

function captureKey(event: KeyboardEvent) {
  if (!capturing.value) return;
  const key = eventToKey(event);
  if (key === null) return;
  event.preventDefault(); event.stopPropagation();
  const modifiers = (event.shiftKey ? 1 : 0) | (event.ctrlKey ? 2 : 0) | (event.altKey ? 4 : 0) | (event.metaKey ? 8 : 0);
  emit('change', { key, modifiers });
  capturing.value = false;
  emit('capture', false);
}
function beginCapture() { if (!capturing.value) { capturing.value = true; emit('capture', true); } }
function cancelCapture() { if (capturing.value) { capturing.value = false; emit('capture', false); } }

window.addEventListener('keydown', captureKey, true);
document.addEventListener('keydown', captureKey, true);
onBeforeUnmount(() => {
  window.removeEventListener('keydown', captureKey, true);
  document.removeEventListener('keydown', captureKey, true);
  if (capturing.value) emit('capture', false);
});
</script>

<template>
  <section class="settings-section hotkey-section">
    <h2>{{ t('hotkey') }}</h2>
    <div class="setting-row">
      <span>{{ t('togglePanelHotkey') }}</span>
      <div class="hotkey-control">
        <kbd v-if="!capturing">{{ displayName(hotkey) }}</kbd>
        <span v-else class="hotkey-listening">{{ t('waitingForKey') }}</span>
        <el-button class="hotkey-action" @click="capturing ? cancelCapture() : beginCapture()">
          <span class="hotkey-action-label">{{ capturing ? t('cancelBinding') : t('changeBinding') }}</span>
        </el-button>
      </div>
    </div>
  </section>
</template>

<style scoped>
.settings-section { padding: 28px 0; border-bottom: 1px solid var(--reff-border); }
h2 { margin: 0 0 12px; font-size: calc(16px * var(--reff-text-scale)); font-weight: 650; }
.setting-row { min-height: 44px; display: flex; align-items: center; justify-content: space-between; gap: 24px; color: var(--reff-text); font-size: calc(13px * var(--reff-text-scale)); }
.hotkey-control { display: flex; align-items: center; gap: 10px; }
kbd, .hotkey-listening, .hotkey-action { width: 132px; height: var(--reff-control-height); flex: 0 0 132px; }
kbd, .hotkey-listening { display: inline-flex; align-items: center; justify-content: center; padding: 0 var(--reff-control-padding-x); }
kbd { border: 1px solid var(--reff-border); border-radius: var(--el-border-radius-base); background: var(--reff-card); color: var(--reff-text); font: inherit; text-align: center; }
.hotkey-listening { color: var(--reff-accent-strong); text-align: center; }
.hotkey-action { position: relative; display: inline-flex; align-items: center; justify-content: center; padding-top: 0; padding-bottom: 0; line-height: 1; }
.hotkey-action-label { position: absolute; inset: 0; display: grid; place-items: center; line-height: normal; }
</style>
