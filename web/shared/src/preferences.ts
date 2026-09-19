export type ReffLanguage = 'zh-CN' | 'en-US';

export type ReffAppearance = {
  preset: 'emerald' | 'ocean' | 'violet' | 'amber' | 'custom';
  accent: string;
  background: string;
  surface: string;
  backgroundOpacity: number;
  cornerRadius: number;
  surfaceBlur: number;
  textScale: number;
};

export type ReffPreferences = {
  language: ReffLanguage;
  appearance: ReffAppearance;
};

const colorPattern = /^#[0-9a-f]{6}$/i;

function channels(value: string): [number, number, number] {
  if (!colorPattern.test(value)) return [16, 21, 30];
  return [Number.parseInt(value.slice(1, 3), 16), Number.parseInt(value.slice(3, 5), 16), Number.parseInt(value.slice(5, 7), 16)];
}

function hex(value: [number, number, number]): string {
  return `#${value.map(channel => Math.round(channel).toString(16).padStart(2, '0')).join('')}`;
}

function mix(first: string, second: string, secondWeight: number): string {
  const a = channels(first);
  const b = channels(second);
  return hex(a.map((channel, index) => channel * (1 - secondWeight) + b[index] * secondWeight) as [number, number, number]);
}

// 把持久化外观映射到 REFF 和 Element Plus 语义令牌；只接受 Core 已约束范围内的值。
export function applyReffPreferences(preferences: ReffPreferences): void {
  const appearance = preferences.appearance;
  if (!appearance || !colorPattern.test(appearance.accent) || !colorPattern.test(appearance.background) || !colorPattern.test(appearance.surface)) return;
  const root = document.documentElement;
  const accent = appearance.accent;
  const background = appearance.background;
  const surface = appearance.surface;
  const text = mix(background, '#ffffff', 0.9);
  const muted = mix(background, '#ffffff', 0.58);
  const card = mix(surface, '#ffffff', 0.035);
  const raised = mix(surface, '#ffffff', 0.09);
  const border = mix(surface, '#ffffff', 0.16);
  const surfaceBlur = Math.min(32, Math.max(0, appearance.surfaceBlur));
  const textScale = Math.min(1.4, Math.max(0.8, appearance.textScale ?? 1));
  const frostAlpha = surfaceBlur / 32 * 0.30;
  const values: Record<string, string> = {
    '--reff-bg': background,
    '--reff-bg-rgb': channels(background).join(' '),
    '--reff-surface': surface,
    '--reff-surface-rgb': channels(surface).join(' '),
    '--reff-card': card,
    '--reff-raised': raised,
    '--reff-border': border,
    '--reff-text': text,
    '--reff-muted': muted,
    '--reff-accent': accent,
    '--reff-accent-strong': mix(accent, '#ffffff', 0.2),
    '--reff-panel-opacity': String(Math.min(1, Math.max(0.55, appearance.backgroundOpacity))),
    '--reff-corner-radius': `${Math.min(16, Math.max(0, appearance.cornerRadius))}px`,
    '--reff-surface-blur': `${surfaceBlur}px`,
    '--reff-surface-blur-strength': String(surfaceBlur / 32),
    '--reff-frost-alpha': String(frostAlpha),
    '--reff-text-scale': String(textScale),
    '--reff-font-size-xs': `calc(12px * ${textScale})`,
    '--reff-font-size-sm': `calc(13px * ${textScale})`,
    '--reff-font-size-md': `calc(14px * ${textScale})`,
    '--reff-font-size-lg': `calc(16px * ${textScale})`,
    '--reff-font-size-xl': `calc(18px * ${textScale})`,
    '--reff-control-height-lg': `calc(40px * ${textScale})`,
    '--reff-control-height': `calc(32px * ${textScale})`,
    '--reff-control-height-sm': `calc(24px * ${textScale})`,
    '--reff-control-padding-x': `calc(12px * ${textScale})`,
    '--reff-control-padding-y': `calc(4px * ${textScale})`,
    '--el-font-size-extra-small': `calc(12px * ${textScale})`,
    '--el-font-size-small': `calc(13px * ${textScale})`,
    '--el-font-size-base': `calc(14px * ${textScale})`,
    '--el-font-size-medium': `calc(16px * ${textScale})`,
    '--el-font-size-large': `calc(18px * ${textScale})`,
    '--el-font-size-extra-large': `calc(20px * ${textScale})`,
    '--el-component-size-large': `calc(40px * ${textScale})`,
    '--el-component-size': `calc(32px * ${textScale})`,
    '--el-component-size-small': `calc(24px * ${textScale})`,
    '--el-border-radius-base': `calc(4px * ${textScale})`,
    '--el-border-radius-small': `calc(2px * ${textScale})`,
    '--el-select-font-size': `calc(14px * ${textScale})`,
    '--el-select-input-font-size': `calc(14px * ${textScale})`,
    '--el-input-height': `calc(32px * ${textScale})`,
    '--el-checkbox-font-size': `calc(14px * ${textScale})`,
    '--el-radio-font-size': `calc(14px * ${textScale})`,
    '--el-slider-button-size': `calc(20px * ${textScale})`,
    '--el-slider-button-wrapper-size': `calc(36px * ${textScale})`,
    '--el-color-primary': accent,
    '--el-color-primary-light-3': mix(accent, '#ffffff', 0.3),
    '--el-color-primary-light-5': mix(accent, background, 0.35),
    '--el-color-primary-light-7': mix(accent, background, 0.55),
    '--el-color-primary-light-8': mix(accent, background, 0.68),
    '--el-color-primary-light-9': mix(accent, background, 0.8),
    '--el-color-primary-dark-2': mix(accent, '#ffffff', 0.18),
  };
  for (const [name, value] of Object.entries(values)) root.style.setProperty(name, value);
  root.lang = preferences.language;
}

// 隔离插件页通过父 Shell 获得当前主题和语言；监听器只消费结构化偏好消息，不执行页面传入代码。
export function installReffPreferencesBridge(): () => void {
  const listener = (event: MessageEvent) => {
    if (event.source !== window.parent || event.data?.type !== 'reff:preferences') return;
    applyReffPreferences(event.data.preferences as ReffPreferences);
    window.dispatchEvent(new CustomEvent('reff:preferences-changed', { detail: event.data.preferences }));
  };
  window.addEventListener('message', listener);
  if (window.parent !== window) window.parent.postMessage({ type: 'reff:preferences-ready' }, '*');
  return () => window.removeEventListener('message', listener);
}
