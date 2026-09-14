import { defineConfig } from 'vite';
import vue from '@vitejs/plugin-vue';
import { fileURLToPath, URL } from 'node:url';
import { readFileSync } from 'node:fs';

const sharedRuntimeUrl = 'reff://shell/shared/reff-ui.mjs';
const { version: reffVersion } = JSON.parse(readFileSync(fileURLToPath(new URL('../../version.json', import.meta.url)), 'utf8')) as { version: string };

// 公共 Shell 只构建自身业务代码；Vue 与组件库保留为 REFF 公共运行时的绝对本地引用。
export default defineConfig({
  plugins: [vue()],
  define: { __REFF_VERSION__: JSON.stringify(reffVersion) },
  base: './',
  publicDir: fileURLToPath(new URL('../shared/dist', import.meta.url)),
  build: {
    target: 'es2020',
    sourcemap: false,
    cssCodeSplit: false,
    rollupOptions: {
      external: ['vue', '@reff/ui'],
      output: { paths: { vue: sharedRuntimeUrl, '@reff/ui': sharedRuntimeUrl } },
    },
  },
  resolve: {
    alias: {
      '@reff-sdk': fileURLToPath(new URL('../sdk', import.meta.url)),
      '@reff/ui': fileURLToPath(new URL('../shared/src/index.ts', import.meta.url)),
      'element-plus': fileURLToPath(new URL('./node_modules/element-plus', import.meta.url)),
    },
  },
});
