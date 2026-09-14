import { defineConfig } from 'vite';
import { fileURLToPath, URL } from 'node:url';
import { join } from 'node:path';
import vue from '../../../web/shell/node_modules/@vitejs/plugin-vue/dist/index.mjs';

const root = fileURLToPath(new URL('./ui', import.meta.url));
const shell = fileURLToPath(new URL('../../shell', import.meta.url));
const shared = 'reff://shell/shared/reff-ui.mjs';

// Vue 3 示例使用独立页面，公共主题和 SDK 由 REFF 运行时提供。
export default defineConfig({
  root,
  plugins: [vue()],
  base: './',
  resolve: { alias: { vue: join(shell, 'node_modules', 'vue'), '@reff/ui': fileURLToPath(new URL('../../shared/src/index.ts', import.meta.url)), '@reff-sdk': fileURLToPath(new URL('../../sdk', import.meta.url)) } },
  build: { target: 'es2020', outDir: 'dist', emptyOutDir: true, rollupOptions: { input: fileURLToPath(new URL('./ui/index.html', import.meta.url)), external: ['vue', '@reff/ui'], output: { paths: { vue: shared, '@reff/ui': shared } } } },
});
