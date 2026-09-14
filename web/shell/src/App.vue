<script setup lang="ts">
import { computed, nextTick, onBeforeUnmount, onMounted, reactive, shallowRef, watch } from 'vue';
import { applyReffPreferences, ElMessage, REFF_ELEMENT_LOCALES } from '@reff/ui';
import { createCefTransport, createReffClient, installInputFocusReporter } from '@reff-sdk/index';
import type { ReffSubscription } from '@reff-sdk/types';
import SchemaPanel, { type SchemaDefinition, type SchemaField } from './components/SchemaPanel.vue';
import SettingsPanel from './features/settings/SettingsPanel.vue';
import type { ReffSettings, SettingsPatch } from './features/settings/types';
import { translate } from './locales';

type PluginSummary = {
  id: string;
  name: string;
  localizedName?: Partial<Record<'zh-CN' | 'en-US', string>>;
  version: string;
  kind: 'page' | 'schema';
  mode: 'component' | 'isolated-page';
  entry: string;
  url: string;
  methods: string[];
  events: string[];
  schema?: SchemaDefinition;
};

const reff = createReffClient(createCefTransport());
const reffVersion = __REFF_VERSION__;
const settingsPluginId = 'reff.settings';
const ready = shallowRef(false);
const plugins = shallowRef<PluginSummary[]>([]);
const selectedPluginId = shallowRef<string | null>(settingsPluginId);
const isolatedFrame = shallowRef<HTMLIFrameElement | null>(null);
const isolatedReloadKey = shallowRef(0);
const settings = shallowRef<ReffSettings>({
  schemaVersion: 1,
  language: 'zh-CN',
  appearance: { preset: 'emerald', accent: '#67D3B2', background: '#10151E', surface: '#151D28', backgroundOpacity: 0.94, cornerRadius: 6, surfaceBlur: 8, textScale: 1 },
  input: { mousePassthrough: false, keyboardPassthrough: false },
  window: { rememberGeometry: true },
});
const savingSettings = shallowRef(false);
const schemaValuesByPlugin = reactive<Record<string, Record<string, unknown>>>({});
const pluginErrors = reactive<Record<string, string>>({});
const schemaRevisions = new Map<string, number>();
const schemaSubscriptions = new Map<string, ReffSubscription>();
const isolatedSubscriptions = new Map<string, ReffSubscription>();
let selfTestTimer: number | undefined;
let lifecycleTimer: number | undefined;
let devReloadTimer: number | undefined;
let selfTestStarted = false;
let lifecycleEpoch = -1;
let lifecycleBusy = false;
let inputFocusReporter: (() => void) | null = null;
let devVersion = '';

const activePlugin = computed(() => plugins.value.find(plugin => plugin.id === selectedPluginId.value) || null);
// 根据全局语言选择插件名称，并在缺少翻译时按中英及首个值回退。
function pluginName(plugin: PluginSummary | null | undefined): string {
  if (!plugin) return '';
  return plugin.localizedName?.[settings.value.language] || plugin.localizedName?.['en-US'] || plugin.name || t.value('pluginFallback');
}
const settingsActive = computed(() => selectedPluginId.value === settingsPluginId);
const selectedSchemaPlugin = computed(() => activePlugin.value?.mode === 'component' && activePlugin.value.schema ? activePlugin.value : null);
const isolatedPlugin = computed(() => activePlugin.value?.mode === 'isolated-page' ? activePlugin.value : null);
// 为隔离插件刷新生成新的资源 URL；参数只用于绕过 CEF 文档缓存，不改变插件身份。
const isolatedPluginUrl = computed(() => {
  const plugin = isolatedPlugin.value;
  return plugin ? `${plugin.url}${plugin.url.includes('?') ? '&' : '?'}reff_reload=${isolatedReloadKey.value}` : '';
});
const activeMenu = computed(() => selectedPluginId.value ? `plugin:${selectedPluginId.value}` : '');
const t = computed(() => (key: Parameters<typeof translate>[1]) => translate(settings.value.language, key));
const elementLocale = computed(() => REFF_ELEMENT_LOCALES[settings.value.language]);
const currentPageTitle = computed(() => settingsActive.value ? t.value('settings') : pluginName(activePlugin.value) || t.value('pluginFallback'));
const statusText = computed(() => ready.value ? t.value('connected') : t.value('waiting'));
let settingsQueue: Promise<void> = Promise.resolve();

// Shell 侧监视当前隔离插件的开发标记；即使插件页面自身尚未完成 SDK 初始化，也能触发刷新。
async function pollDevReload() {
  const plugin = isolatedPlugin.value;
  if (!plugin || !ready.value) { devVersion = ''; return; }
  try {
    const result = await reff.call<{ version: string }>('ui.dev.version', { pluginId: plugin.id });
    if (devVersion && result.version !== devVersion) isolatedReloadKey.value += 1;
    devVersion = result.version;
  } catch {
    // 正式插件没有开发标记时保持静默，不影响正常页面运行。
  }
}

watch(isolatedPlugin, () => { devVersion = ''; });

// 为每个 Schema 插件建立独立字段状态；manifest 默认值只在发现或脚本重置时写入。
function initializeSchemaValues(discovered: PluginSummary[]) {
  for (const key of Object.keys(schemaValuesByPlugin)) delete schemaValuesByPlugin[key];
  for (const key of Object.keys(pluginErrors)) delete pluginErrors[key];
  schemaRevisions.clear();
  for (const plugin of discovered) {
    if (plugin.mode !== 'component' || !plugin.schema) continue;
    const values: Record<string, unknown> = {};
    for (const field of plugin.schema.fields) {
      if (field.type !== 'button') values[field.id] = field.default ?? (field.type === 'switch' ? false : field.type === 'text' ? '' : field.min ?? 0);
    }
    schemaValuesByPlugin[plugin.id] = values;
  }
}

// 只合并当前 Schema 声明过的字段，并用 revision 阻止迟到响应覆盖较新的状态。
function applySchemaState(plugin: PluginSummary, state: unknown) {
  if (!plugin.schema || !state || typeof state !== 'object') return;
  const source = state as Record<string, unknown>;
  const revision = typeof source.revision === 'number' ? source.revision : undefined;
  const previous = schemaRevisions.get(plugin.id) ?? -1;
  if (revision !== undefined && revision < previous) return;
  if (revision !== undefined) schemaRevisions.set(plugin.id, revision);
  const values = schemaValuesByPlugin[plugin.id];
  if (!values) return;
  for (const field of plugin.schema.fields) {
    if (field.type !== 'button' && field.id in source) values[field.id] = source[field.id];
  }
}

// Schema 控件只更新自身插件的已声明字段，避免未知键进入跨进程请求。
function setSchemaValue(pluginId: string, id: string, value: unknown) {
  const plugin = plugins.value.find(item => item.id === pluginId);
  if (!plugin?.schema?.fields.some(field => field.id === id && field.type !== 'button' && !field.readonly)) return;
  schemaValuesByPlugin[pluginId][id] = value;
}

// 按 manifest 指定的动作提交可编辑字段；Lua 返回的快照仍是显示状态的权威来源。
async function saveSchema(field?: SchemaField) {
  const plugin = selectedSchemaPlugin.value;
  const action = field?.action;
  if (!plugin?.schema || !action) return;
  const values = schemaValuesByPlugin[plugin.id] || {};
  const params = Object.fromEntries(plugin.schema.fields
    .filter(item => item.type !== 'button' && !item.readonly)
    .map(item => [item.id, values[item.id]]));
  try {
    applySchemaState(plugin, await reff.call(action, params));
    ElMessage.success(t.value('done'));
  } catch (error) {
    ElMessage.error(String(error));
  }
}

// 订阅 Schema 插件声明的状态事件，并在订阅确认后读取一次权威快照。
async function connectSchemaPlugins(discovered: PluginSummary[]) {
  for (const plugin of discovered) {
    if (plugin.mode !== 'component' || !plugin.schema) continue;
    try {
      if (plugin.schema.changeEvent) {
        const subscription = await reff.subscribe(plugin.schema.changeEvent, payload => applySchemaState(plugin, payload));
        schemaSubscriptions.set(plugin.id, subscription);
      }
      if (plugin.schema.load) applySchemaState(plugin, await reff.call(plugin.schema.load));
    } catch (error) {
      pluginErrors[plugin.id] = String(error);
    }
  }
}

// 清理 Shell 代管的全部订阅；断线或脚本重置后不会复用旧 epoch 的句柄。
async function cleanupSubscriptions() {
  const subscriptions = [...schemaSubscriptions.values(), ...isolatedSubscriptions.values()];
  schemaSubscriptions.clear();
  isolatedSubscriptions.clear();
  await Promise.allSettled(subscriptions.map(subscription => subscription.unsubscribe()));
}

// 切换插件时立即使旧 iframe 引用失效，并撤销只属于旧隔离页的订阅代理。
watch(selectedPluginId, async (current, previous) => {
  if (current === previous) return;
  isolatedFrame.value = null;
  const subscriptions = [...isolatedSubscriptions.values()];
  isolatedSubscriptions.clear();
  await Promise.allSettled(subscriptions.map(subscription => subscription.unsubscribe()));
}, { flush: 'sync' });

// 左侧导航只消费宿主返回的 manifest 摘要；Shell 不包含任何具体插件 ID 或业务页面。
function selectPlugin(plugin: PluginSummary) {
  selectedPluginId.value = plugin.id;
}

// 内置设置固定占据首项，不进入第三方 manifest 排序，也不能被同名插件覆盖。
function selectSettings() {
  selectedPluginId.value = settingsPluginId;
}

// 将最新偏好同步到 Shell 与当前隔离页面；插件公共 UI 只消费经过 Core 校验的结构化值。
function applySettings(value: ReffSettings) {
  settings.value = value;
  applyReffPreferences(value);
  isolatedFrame.value?.contentWindow?.postMessage({ type: 'reff:preferences', preferences: value }, '*');
}

// 设置写入串行化，避免快速调整多个控件时迟到响应覆盖后提交的值。
function updateSettings(patch: SettingsPatch) {
  savingSettings.value = true;
  settingsQueue = settingsQueue.then(async () => {
    const value = await reff.call<ReffSettings>('reff.settings.set', patch);
    applySettings(value);
  }).catch(async error => {
    ElMessage.error(String(error));
    try { applySettings(await reff.call<ReffSettings>('reff.settings.get')); } catch { /* 宿主断线时保留当前页面，下一次生命周期轮询负责恢复。 */ }
  }).finally(() => { savingSettings.value = false; });
}

// 恢复默认值同时清除已保存窗口几何，原生层会在下一帧重新居中。
function resetSettings() {
  savingSettings.value = true;
  settingsQueue = settingsQueue.then(async () => {
    const value = await reff.call<ReffSettings>('reff.settings.reset');
    applySettings(value);
    ElMessage.success(t.value('resetDone'));
  }).catch(async error => {
    ElMessage.error(String(error));
    try { applySettings(await reff.call<ReffSettings>('reff.settings.get')); } catch { /* 宿主断线时保留当前页面，下一次生命周期轮询负责恢复。 */ }
  }).finally(() => { savingSettings.value = false; });
}

// 隔离页面请求由 Shell 校验 manifest 能力后代理，避免 iframe 取得 Shell 的任意方法权限。
async function onEmbeddedMessage(event: MessageEvent) {
  const frame = isolatedFrame.value;
  const frameWindow = frame?.contentWindow;
  if (!frameWindow || event.source !== frameWindow) return;
  const plugin = isolatedPlugin.value;
  if ((event.data?.type === 'reff:preferences-ready' || event.data?.type === 'reff:plugin-ready') && plugin) {
    frameWindow.postMessage({ type: 'reff:preferences', preferences: settings.value }, '*');
    if (event.data?.type === 'reff:preferences-ready') return;
  }
  if (event.data?.type === 'reff:navigate' && event.data.action === 'back') {
    selectedPluginId.value = settingsPluginId;
    return;
  }
  if (event.data?.type !== 'reff:request' || !plugin) return;
  const request = event.data;
  const pluginId = plugin.id;
  const isCurrent = () => selectedPluginId.value === pluginId && isolatedFrame.value?.contentWindow === frameWindow;
  const reply = (value: Record<string, unknown>) => frameWindow.postMessage({ type: 'reff:response', id: request.id, ...value }, '*');
  if (typeof request.id !== 'string' || typeof request.method !== 'string') return;
  if (request.method === 'ui.identity') { reply({ ok: true, result: { pageId: `embedded:${plugin.id}`, kind: 'plugin', pluginId: plugin.id } }); return; }
  if (request.method === 'ui.status') { reply({ ok: true, result: { ready: ready.value } }); return; }
  // 隔离页只能刷新当前 iframe；递增 key 会卸载旧页面并清理其订阅与请求。
  if (request.method === 'ui.reload') {
    if (!isCurrent()) return;
    isolatedReloadKey.value += 1;
    reply({ ok: true, result: {} });
    return;
  }
  if (request.method === 'ui.dev.version') {
    if (!isCurrent()) return;
    try {
      // 由原生宿主直接读取开发标记，绕过 CEF 自定义协议的缓存层。
      reply({ ok: true, result: await reff.call('ui.dev.version', { pluginId }) });
    } catch (error) { reply({ ok: false, code: 'DEV_MARKER_NOT_FOUND', message: String(error) }); }
    return;
  }
  if (request.method === 'ui.input.focus') {
    const params = request.params;
    if (!params || typeof params !== 'object' || typeof params.active !== 'boolean') {
      reply({ ok: false, code: 'INVALID_ARGUMENT', message: '输入焦点参数不合法' });
      return;
    }
    if (!isCurrent()) return;
    const source = params as Record<string, unknown>;
    const input: Record<string, unknown> = { active: source.active };
    for (const key of ['inputId', 'text', 'selectionStart', 'selectionEnd', 'x', 'y', 'width', 'height', 'viewportWidth', 'viewportHeight']) {
      if (key in source) input[key] = source[key];
    }
    input.inputId = `embedded:${plugin.id}:${String(input.inputId || 'reff-input')}`;
    if (input.active) {
      const rect = frame.getBoundingClientRect();
      const viewportWidth = Math.max(1, Number(input.viewportWidth) || frame.clientWidth || 1);
      const viewportHeight = Math.max(1, Number(input.viewportHeight) || frame.clientHeight || 1);
      const x = Math.max(0, Math.min(viewportWidth - 1, Number(input.x) || 0));
      const y = Math.max(0, Math.min(viewportHeight - 1, Number(input.y) || 0));
      input.x = Math.round(rect.left + x * frame.clientWidth / viewportWidth);
      input.y = Math.round(rect.top + y * frame.clientHeight / viewportHeight);
      input.width = Math.max(1, Math.round(Number(input.width) || 1));
      input.height = Math.max(1, Math.round(Number(input.height) || 1));
    }
    delete input.viewportWidth;
    delete input.viewportHeight;
    try {
      const result = await reff.call('ui.input.focus', input);
      if (isCurrent()) reply({ ok: true, result });
    } catch (error) {
      reply({ ok: false, code: 'TRANSPORT_ERROR', message: String(error) });
    }
    return;
  }
  if (request.method === 'ui.subscribe') {
    const name = request.params?.eventName;
    const subscriptionId = request.params?.subscriptionId;
    if (!plugin.events.includes(name) || typeof subscriptionId !== 'string') {
      reply({ ok: false, code: 'FORBIDDEN', message: '事件未声明' });
      return;
    }
    try {
      const subscription = await reff.subscribe(name, payload => {
        if (isCurrent()) frameWindow.postMessage({ type: 'reff:event', subscriptionId, payload }, '*');
      });
      if (!isCurrent()) { await subscription.unsubscribe(); return; }
      isolatedSubscriptions.set(subscriptionId, subscription);
      reply({ ok: true, result: {} });
    } catch (error) {
      reply({ ok: false, code: 'TRANSPORT_ERROR', message: String(error) });
    }
    return;
  }
  if (request.method === 'ui.unsubscribe') {
    const id = request.params?.subscriptionId;
    const subscription = isolatedSubscriptions.get(id);
    if (subscription) { isolatedSubscriptions.delete(id); await subscription.unsubscribe(); }
    reply({ ok: true, result: {} });
    return;
  }
  if (!plugin.methods.includes(request.method) || !request.method.startsWith(`${plugin.id}.`)) {
    reply({ ok: false, code: 'FORBIDDEN', message: '方法未声明' });
    return;
  }
  try {
    reply({ ok: true, result: await reff.call(request.method, request.params || {}) });
  } catch (error) {
    reply({ ok: false, code: 'TRANSPORT_ERROR', message: String(error) });
  }
}

// CEF 的浏览器级 IME 提交无法可靠命中子 frame；只把宿主事件转发给当前插件 iframe。
function onEmbeddedIme(event: Event) {
  const detail = (event as CustomEvent<{ action?: string; text?: string; inputId?: string }>).detail;
  const plugin = isolatedPlugin.value;
  const frameWindow = isolatedFrame.value?.contentWindow;
  if (!plugin || !frameWindow || !detail?.inputId?.startsWith(`embedded:${plugin.id}:`)) return;
  frameWindow.postMessage({ type: 'reff:ime', ...detail }, '*');
}

// 重新发现插件并连接 Schema 状态；没有插件时保留可理解的空状态。
async function loadPlugins(preferredId?: string | null, connect = true) {
  const result = await reff.call<{ plugins: PluginSummary[] }>('ui.plugins');
  plugins.value = result.plugins.filter(plugin => !plugin.id.startsWith('reff.'));
  initializeSchemaValues(result.plugins);
  const selected = result.plugins.find(plugin => plugin.id === preferredId);
  selectedPluginId.value = preferredId === settingsPluginId || !selected ? settingsPluginId : selected.id;
  if (connect) await connectSchemaPlugins(result.plugins);
}

// Reset Scripts 后重新取得 epoch、插件清单和订阅，旧页面状态不会进入新脚本会话。
async function recoverAfterScriptReset(epoch: number) {
  if (lifecycleBusy) return;
  lifecycleBusy = true;
  const preferredId = selectedPluginId.value;
  ready.value = false;
  await cleanupSubscriptions();
  plugins.value = [];
  selectedPluginId.value = null;
  lifecycleEpoch = epoch;
  try {
    await reff.ready();
    ready.value = true;
    await loadPlugins(preferredId);
  } catch (error) {
    ElMessage.warning(`${t.value('resetFailure')}：${String(error)}`);
  } finally {
    lifecycleBusy = false;
  }
}

// 低频查询宿主状态；只观察 ready/epoch，不在每帧访问游戏或改变输入状态。
async function pollLifecycle() {
  if (lifecycleBusy) return;
  try {
    const status = await reff.call<{ ready: boolean; epoch: number }>('ui.status');
    if (lifecycleEpoch < 0) lifecycleEpoch = status.epoch;
    if (status.epoch !== lifecycleEpoch) { await recoverAfterScriptReset(status.epoch); return; }
    if (!status.ready && ready.value) ready.value = false;
  } catch { /* 宿主关闭期间由下一次轮询恢复，避免把诊断提示刷满页面。 */ }
}

// 独立 smoke 检查固定头部与正文边界；生产页面不会主动开启此路径。
function verifyShellLayout() {
  const workspace = document.querySelector<HTMLElement>('.reff-workspace');
  const header = workspace?.querySelector<HTMLElement>(':scope > .window-bar');
  const content = workspace?.querySelector<HTMLElement>(':scope > .workspace-content');
  if (!workspace || !header || !content) throw new Error('Shell 工作区结构缺失');
  const workspaceRect = workspace.getBoundingClientRect();
  const headerRect = header.getBoundingClientRect();
  const contentRect = content.getBoundingClientRect();
  if (Math.abs(headerRect.top - workspaceRect.top) > 1 || contentRect.top < headerRect.bottom - 1)
    throw new Error('Shell 固定标题栏与正文发生间距或重叠');
}

// 自检只选择 manifest 声明的隔离页，不把测试插件 ID 固化进正式 Shell。
async function runSelfTest() {
  const marker = (window as Window & { reffSelfTest?: { passed?: boolean } | boolean }).reffSelfTest;
  if (!marker || selfTestStarted || !ready.value || plugins.value.length === 0) return;
  selfTestStarted = true;
  try {
    verifyShellLayout();
    if (!document.querySelector('[data-reff-system-settings]')) throw new Error('内置设置插件未在默认首项渲染');
    const isolated = plugins.value.find(plugin => plugin.mode === 'isolated-page' && plugin.events.length > 0)
      || plugins.value.find(plugin => plugin.mode === 'isolated-page');
    if (!isolated) throw new Error('自检未发现隔离页面插件');
    const isolatedPluginId = isolated.id;
    const pluginReady = new Promise<string>((resolve, reject) => {
      const timeout = window.setTimeout(() => { window.removeEventListener('message', onReady); reject(new Error('隔离页面公共资源加载超时')); }, 3000);
      function onReady(event: MessageEvent) {
        if (event.source !== isolatedFrame.value?.contentWindow || event.data?.type !== 'reff:plugin-ready' || event.data.pluginId !== isolatedPluginId) return;
        window.clearTimeout(timeout);
        window.removeEventListener('message', onReady);
        event.data.styled ? resolve(String(event.data.uiVersion || '')) : reject(new Error('隔离页面公共样式未生效'));
      }
      window.addEventListener('message', onReady);
    });
    selectPlugin(isolated);
    await nextTick();
    const uiVersion = await pluginReady;
    const bridgePassed = typeof marker === 'object' ? marker.passed === true : true;
    await reff.call('test.finished', { passed: bridgePassed && uiVersion === '0.1.0', uiVersion });
  } catch (error) {
    await reff.call('test.finished', { passed: false, error: String(error) }).catch(() => {});
    ElMessage.error(String(error));
  } finally {
    if (selfTestTimer !== undefined) { window.clearInterval(selfTestTimer); selfTestTimer = undefined; }
  }
}

// Shell 生命周期只管理运行时连接与通用插件容器，具体业务状态由各插件持有。
onMounted(async () => {
  inputFocusReporter = installInputFocusReporter(reff);
  window.addEventListener('message', onEmbeddedMessage);
  window.addEventListener('reff:embedded-ime', onEmbeddedIme);
  try { applySettings(await reff.call<ReffSettings>('reff.settings.get')); }
  catch (error) { ElMessage.warning(String(error)); }
  try { await loadPlugins(settingsPluginId, false); }
  catch (error) { ElMessage.warning(String(error)); }
  lifecycleTimer = window.setInterval(() => { void pollLifecycle(); }, 250);
  devReloadTimer = window.setInterval(() => { void pollDevReload(); }, 700);
  selfTestTimer = window.setInterval(() => { void runSelfTest(); }, 100);
  try {
    await reff.ready();
    const status = await reff.call<{ ready: boolean; epoch: number }>('ui.status');
    lifecycleEpoch = status.epoch;
    ready.value = true;
    await connectSchemaPlugins(plugins.value);
    void runSelfTest();
  } catch (error) { ElMessage.warning(String(error)); }
});

// 页面销毁时释放输入监听、计时器、订阅和传输句柄。
onBeforeUnmount(async () => {
  if (selfTestTimer !== undefined) window.clearInterval(selfTestTimer);
  if (lifecycleTimer !== undefined) window.clearInterval(lifecycleTimer);
  if (devReloadTimer !== undefined) window.clearInterval(devReloadTimer);
  inputFocusReporter?.();
  inputFocusReporter = null;
  window.removeEventListener('message', onEmbeddedMessage);
  window.removeEventListener('reff:embedded-ime', onEmbeddedIme);
  await cleanupSubscriptions();
  reff.dispose();
});
</script>

<template>
  <el-config-provider :locale="elementLocale">
  <el-container class="reff-shell">
    <el-aside width="220px">
      <div class="brand"><strong class="brand-name">RE<span>FF</span></strong><small>v{{ reffVersion }}</small></div>
      <el-menu :default-active="activeMenu">
        <el-menu-item :index="`plugin:${settingsPluginId}`" @click="selectSettings">{{ t('settings') }}</el-menu-item>
        <el-menu-item v-for="plugin in plugins" :key="plugin.id" :index="`plugin:${plugin.id}`" @click="selectPlugin(plugin)">
          {{ pluginName(plugin) }}
        </el-menu-item>
      </el-menu>
      <div class="status"><i :class="{ ready }"/> {{ statusText }}</div>
    </el-aside>
    <section class="reff-workspace">
      <div class="window-bar"><strong>{{ currentPageTitle }}</strong></div>
      <el-main class="workspace-content">
        <SettingsPanel v-if="settingsActive" :settings="settings" :plugins="plugins" :version="reffVersion" :saving="savingSettings" @change="updateSettings" @reset="resetSettings" />
        <div v-else-if="isolatedPlugin" class="isolated-host">
          <iframe :key="isolatedReloadKey" ref="isolatedFrame" :src="isolatedPluginUrl" :title="isolatedPlugin.name" />
        </div>
        <SchemaPanel
          v-else-if="selectedSchemaPlugin"
          :schema="selectedSchemaPlugin.schema!"
          :values="schemaValuesByPlugin[selectedSchemaPlugin.id]"
          :ready="ready && !pluginErrors[selectedSchemaPlugin.id]"
          @update="(id, value) => setSchemaValue(selectedSchemaPlugin?.id || '', id, value)"
          @submit="saveSchema"
        />
        <div v-else-if="plugins.length === 0" class="empty-state">
          <strong>{{ t('noPlugins') }}</strong>
          <span>{{ t('noPluginsHint') }}</span>
        </div>
        <el-alert v-else :title="t('noInterface')" type="warning" :closable="false"/>
        <el-alert v-if="selectedPluginId && pluginErrors[selectedPluginId]" :title="pluginErrors[selectedPluginId]" type="warning" :closable="false"/>
        <el-alert v-if="!ready && !settingsActive" :title="t('waitingBackend')" type="info" :closable="false"/>
      </el-main>
    </section>
  </el-container>
  </el-config-provider>
</template>

<style scoped>
.reff-shell { position: relative; height: 100%; overflow: hidden; border-radius: var(--reff-corner-radius); background: transparent; cursor: none; isolation: isolate; }
.reff-shell::before { content: ""; position: absolute; inset: -32px; z-index: 0; pointer-events: none; background: radial-gradient(circle at 18% 12%, color-mix(in srgb, var(--reff-accent) 24%, transparent), transparent 46%), radial-gradient(circle at 82% 88%, color-mix(in srgb, #ffffff 12%, transparent), transparent 50%), repeating-linear-gradient(115deg, rgb(255 255 255 / calc(var(--reff-frost-alpha) * .12)) 0 1px, transparent 1px 5px); opacity: calc(var(--reff-frost-alpha) * 2.5); filter: blur(calc(var(--reff-surface-blur) * 1.5)); }
.reff-shell > * { position: relative; z-index: 1; }
.reff-shell * { cursor: none !important; }
.reff-shell :deep(.el-aside) { position: relative; background: rgb(var(--reff-surface-rgb) / calc(var(--reff-panel-opacity) * .96)); border-right: 1px solid var(--reff-border); -webkit-backdrop-filter: blur(var(--reff-surface-blur)) saturate(1.08); backdrop-filter: blur(var(--reff-surface-blur)) saturate(1.08); }
.brand { display: flex; align-items: baseline; gap: 9px; padding: 28px 24px 22px; font-size: calc(28px * var(--reff-text-scale)); font-weight: 800; letter-spacing: 2px; }
.brand-name { font: inherit; letter-spacing: 0; }
.brand span { color: var(--reff-accent); }
.brand small { color: var(--reff-muted); font-size: calc(11px * var(--reff-text-scale)); font-weight: 600; letter-spacing: 0; }
.reff-shell :deep(.el-menu) { border-right: 0; padding: 8px 10px; }
.reff-shell :deep(.el-menu-item) { height: 42px; line-height: 42px; margin: 3px 0; border-radius: 4px; font-size: calc(14px * var(--reff-text-scale)); }
.reff-shell :deep(.el-menu-item:hover) { color: var(--reff-text); }
.reff-shell :deep(.el-menu-item.is-active) { background: color-mix(in srgb, var(--reff-accent) 18%, var(--reff-surface)); color: var(--reff-accent-strong); }
.status { position: absolute; bottom: 22px; padding-left: 24px; color: var(--reff-muted); font-size: calc(12px * var(--reff-text-scale)); }
.status i { display: inline-block; width: 8px; height: 8px; margin-right: 6px; border-radius: 50%; background: #667080; }
.status i.ready { background: var(--reff-accent); }
.reff-workspace { flex: 1; height: 100%; min-width: 0; min-height: 0; display: flex; flex-direction: column; background: transparent; }
.window-bar { width: 100%; height: 56px; flex: 0 0 56px; padding: 0 22px; box-sizing: border-box; display: flex; align-items: center; border-bottom: 1px solid var(--reff-border); background: rgb(var(--reff-surface-rgb) / calc(var(--reff-panel-opacity) * .96)); color: var(--reff-text); -webkit-backdrop-filter: blur(var(--reff-surface-blur)) saturate(1.08); backdrop-filter: blur(var(--reff-surface-blur)) saturate(1.08); }
.window-bar strong { min-width: 0; overflow: hidden; font-size: calc(17px * var(--reff-text-scale)); letter-spacing: 0; text-overflow: ellipsis; white-space: nowrap; }
.workspace-content { flex: 1; width: 100%; max-width: none; min-width: 0; min-height: 0; margin: 0; padding: 32px 40px; display: flex; flex-direction: column; overflow: auto; overscroll-behavior: contain; background: rgb(var(--reff-bg-rgb) / var(--reff-panel-opacity)); -webkit-backdrop-filter: blur(var(--reff-surface-blur)) saturate(1.08); backdrop-filter: blur(var(--reff-surface-blur)) saturate(1.08); }
.workspace-content > * { flex-shrink: 0; }
.isolated-host { flex: 1; width: 100%; min-width: 0; min-height: 0; display: flex; }
.isolated-host iframe { display: block; flex: 1; width: 100%; height: 100%; min-width: 0; min-height: 0; border: 0; background: transparent; }
.empty-state { flex: 1; min-height: 220px; display: flex; flex-direction: column; align-items: center; justify-content: center; gap: 8px; color: var(--reff-muted); text-align: center; }
.empty-state strong { color: var(--reff-text); font-size: calc(17px * var(--reff-text-scale)); font-weight: 600; }
.empty-state span { font-size: calc(13px * var(--reff-text-scale)); }
.workspace-content::-webkit-scrollbar { width: 10px; }
.workspace-content::-webkit-scrollbar-track { background: rgb(var(--reff-bg-rgb) / 55%); }
.workspace-content::-webkit-scrollbar-thumb { border: 2px solid transparent; border-radius: 5px; background: color-mix(in srgb, var(--reff-muted) 54%, transparent); background-clip: padding-box; }
.workspace-content::-webkit-scrollbar-thumb:hover { background: color-mix(in srgb, var(--reff-muted) 72%, transparent); background-clip: padding-box; }
</style>
