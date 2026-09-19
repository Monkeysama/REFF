import type { ReffLanguage } from '@reff/ui';
import { createI18n } from 'vue-i18n';

const messages = {
  'zh-CN': {
    settings: 'REFF 设置', pluginFallback: '插件', connected: '运行时已连接', waiting: '等待脚本',
    noPlugins: '尚未发现支持 REFF 的插件', noPluginsHint: '已接入的插件会显示在左侧列表中。',
    noInterface: '当前插件没有可用界面', waitingBackend: '正在等待 REFF Lua 后端就绪',
    resetFailure: 'Lua 脚本重置后重新连接失败', done: '设置已保存', resetDone: '已恢复默认设置',
    general: '常规', language: '语言/Language', chinese: '简体中文', english: 'English', hotkey: '快捷键', togglePanelHotkey: '打开/关闭面板', changeBinding: '更换绑定', cancelBinding: '取消绑定', waitingForKey: '等待输入…',
    appearance: '外观', theme: '主题方案', customColors: '自定义颜色', accent: '强调色', background: '背景色', surface: '表面色',
    emerald: '翡翠', ocean: '海洋', violet: '紫罗兰', amber: '琥珀', custom: '自定义',
    backgroundOpacity: '背景透明度', cornerRadius: '窗口圆角', surfaceBlur: '表面模糊', textSize: '文字大小',
    input: '输入', mousePassthrough: '鼠标输入穿透', keyboardPassthrough: '键盘输入穿透',
    passthroughWarning: '穿透后，操作面板时游戏也可能响应同一输入。文本输入、窗口移动和缩放仍由 REFF 独占。',
    window: '窗口', rememberGeometry: '记住窗口位置和大小',
    plugins: '已加载插件', official: '官方', component: '组件', isolated: '隔离页面', loaded: '已载入',
    noExternalPlugins: '当前没有加载第三方插件', restoreDefaults: '恢复默认设置', saving: '正在保存',
  },
  'en-US': {
    settings: 'REFF Settings', pluginFallback: 'Plugin', connected: 'Runtime connected', waiting: 'Waiting for scripts',
    noPlugins: 'No REFF plugins found', noPluginsHint: 'Connected plugins appear in the navigation.',
    noInterface: 'This plugin has no available interface', waitingBackend: 'Waiting for the REFF Lua backend',
    resetFailure: 'Failed to reconnect after Lua script reset', done: 'Settings saved', resetDone: 'Defaults restored',
    general: 'General', language: '语言/Language', chinese: '简体中文', english: 'English', hotkey: 'Hotkey', togglePanelHotkey: 'Open/close panel', changeBinding: 'Change binding', cancelBinding: 'Cancel binding', waitingForKey: 'Press a key…',
    appearance: 'Appearance', theme: 'Color scheme', customColors: 'Custom colors', accent: 'Accent', background: 'Background', surface: 'Surface',
    emerald: 'Emerald', ocean: 'Ocean', violet: 'Violet', amber: 'Amber', custom: 'Custom',
    backgroundOpacity: 'Background opacity', cornerRadius: 'Window corners', surfaceBlur: 'Surface blur', textSize: 'Text size',
    input: 'Input', mousePassthrough: 'Pass mouse input to game', keyboardPassthrough: 'Pass keyboard input to game',
    passthroughWarning: 'The game may react while you operate the panel. Text input, window movement, and resizing remain exclusive to REFF.',
    window: 'Window', rememberGeometry: 'Remember window position and size',
    plugins: 'Loaded plugins', official: 'Official', component: 'Component', isolated: 'Isolated page', loaded: 'Loaded',
    noExternalPlugins: 'No third-party plugins are loaded', restoreDefaults: 'Restore defaults', saving: 'Saving',
  },
} as const;

export type MessageKey = keyof typeof messages['zh-CN'];

// REFF Shell 共用 i18n 实例；插件通过命名空间注册自己的 messages，统一跟随全局语言。
export const i18n = createI18n({ legacy: false, locale: 'zh-CN', fallbackLocale: 'en-US', messages });

// Shell 与官方设置页只使用一份受类型约束的词典；插件业务文案仍由插件作者负责。
export function translate(language: ReffLanguage, key: MessageKey): string {
  i18n.global.locale.value = language;
  return String(i18n.global.t(key));
}
