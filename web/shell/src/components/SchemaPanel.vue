<script setup lang="ts">
export type SchemaField = {
  id: string;
  type: 'switch' | 'slider' | 'number' | 'select' | 'text' | 'button';
  label?: string;
  min?: number;
  max?: number;
  step?: number;
  maxLength?: number;
  placeholder?: string;
  options?: Array<{ value: string; label: string }>;
  action?: string;
  default?: unknown;
  readonly?: boolean;
};

export interface SchemaDefinition { version: number; title: string; load?: string; changeEvent?: string; fields: SchemaField[]; }

const props = defineProps<{ schema: SchemaDefinition; values: Record<string, unknown>; ready: boolean }>();
const emit = defineEmits<{ update: [id: string, value: unknown]; submit: [field?: SchemaField] }>();
const inputFields = () => props.schema.fields.filter(field => field.type !== 'button');
const actionFields = () => props.schema.fields.filter(field => field.type === 'button' && field.action);
</script>

<template>
  <el-card class="schema-panel" shadow="never">
    <template #header><div class="card-heading"><b>{{ schema.title }}</b></div></template>
    <el-form label-position="left" label-width="80px">
      <el-form-item v-for="field in inputFields()" :key="field.id" :label="field.label || field.id">
        <el-switch v-if="field.type === 'switch'" :model-value="Boolean(values[field.id])" :disabled="!ready || field.readonly" @update:model-value="emit('update', field.id, $event)"/>
        <template v-else-if="field.type === 'slider'"><el-slider :model-value="Number(values[field.id] ?? field.default ?? field.min ?? 0)" :min="field.min" :max="field.max" :step="field.step || 1" :disabled="!ready || field.readonly" @update:model-value="emit('update', field.id, $event)"/><span class="slider-value">{{ values[field.id] }}</span></template>
        <el-input-number v-else-if="field.type === 'number'" :model-value="Number(values[field.id] ?? field.default ?? field.min ?? 0)" :min="field.min" :max="field.max" :step="field.step || 1" :disabled="!ready || field.readonly" @update:model-value="emit('update', field.id, $event)"/>
        <el-select v-else-if="field.type === 'select'" :model-value="String(values[field.id] ?? field.default ?? '')" :disabled="!ready || field.readonly" @update:model-value="emit('update', field.id, $event)"><el-option v-for="option in field.options || []" :key="option.value" :label="option.label" :value="option.value"/></el-select>
        <el-input v-else-if="field.type === 'text'" :model-value="String(values[field.id] ?? field.default ?? '')" :maxlength="field.maxLength" :disabled="!ready || field.readonly" :placeholder="field.placeholder" @update:model-value="emit('update', field.id, $event)"/>
      </el-form-item>
      <el-form-item v-for="field in actionFields()" :key="`action-${field.id}`"><el-button type="primary" :disabled="!ready" @click="emit('submit', field)">{{ field.label || '执行' }}</el-button></el-form-item>
    </el-form>
  </el-card>
</template>

<style scoped>
.schema-panel { margin-top: 14px; }
.card-heading { display: flex; align-items: center; }
.card-heading b { display: block; font-size: calc(18px * var(--reff-text-scale)); line-height: 1.3; }
.schema-panel :deep(.el-select) { width: 100%; }
.slider-value { width: 30px; text-align: right; color: var(--reff-muted); font-size: calc(13px * var(--reff-text-scale)); }
</style>
