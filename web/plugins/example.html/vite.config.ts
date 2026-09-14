import { defineConfig } from 'vite';
import { fileURLToPath, URL } from 'node:url';

const root = fileURLToPath(new URL('./ui', import.meta.url));
const sdk = fileURLToPath(new URL('../../sdk', import.meta.url));

// 原生 HTML 示例仅打包标准 DOM 与 REFF SDK，不引入 Vue 或组件库运行时。
export default defineConfig({
  root,
  base: './',
  resolve: { alias: { '@reff-sdk': sdk } },
  build: {
    target: 'es2020',
    outDir: 'dist',
    emptyOutDir: true,
    rollupOptions: { input: fileURLToPath(new URL('./ui/index.html', import.meta.url)) },
  },
});
