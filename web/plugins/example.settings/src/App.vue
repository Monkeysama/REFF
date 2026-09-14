<script setup lang="ts">
import { onMounted, ref } from 'vue';
import { ElMessage } from '@reff/ui';
const call = (method: string, params: Record<string, unknown> = {}) => new Promise<any>((resolve, reject) => window.cefQuery({ request: JSON.stringify({ method, params }), onSuccess: (raw: string) => resolve(JSON.parse(raw)), onFailure: (code: number, reason: string) => reject(new Error(`${code}: ${reason}`)) }));
const enabled = ref(true), intensity = ref(50), note = ref(''), ready = ref(false);
// 页面挂载后读取 Lua 权威状态；网络和输入均由 REFF 宿主提供。
onMounted(async () => { try { const identity = await call('ui.identity'); if (identity.pluginId !== 'example.settings') throw new Error('插件身份不匹配'); const state = await call('example.settings.get'); enabled.value = state.enabled; intensity.value = state.intensity; note.value = state.note || ''; ready.value = true; } catch (error) { ElMessage.error(String(error)); } });
// 通过 Shell 的受控导航消息返回工作台，避免隔离 iframe 直接导航到 Shell 来源。
const back = () => window.parent.postMessage({ type: 'reff:navigate', action: 'back' }, '*');
const save = async () => { try { await call('example.settings.set', { enabled: enabled.value, intensity: intensity.value, note: note.value }); ElMessage.success('设置已保存'); } catch (error) { ElMessage.error(String(error)); } };
</script>
<template><el-container class="page"><el-header><b>REFF</b><span>设置示例</span><el-button text @click="back">返回 REFF</el-button></el-header><el-main><el-card><template #header><div class="title">设置示例 <el-tag type="success">Vue 3 · Element Plus</el-tag></div></template><el-form label-width="80px"><el-form-item label="启用"><el-switch v-model="enabled" :disabled="!ready"/></el-form-item><el-form-item label="强度"><el-slider v-model="intensity" :disabled="!ready"/><span>{{ intensity }}</span></el-form-item><el-form-item label="备注"><el-input v-model="note" maxlength="64" show-word-limit :disabled="!ready"/></el-form-item><el-button type="primary" :disabled="!ready" @click="save">保存设置</el-button></el-form></el-card></el-main></el-container></template>
