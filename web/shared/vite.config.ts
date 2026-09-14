import { fileURLToPath, URL } from 'node:url';
import { defineConfig } from '../shell/node_modules/vite/dist/node/index.js';

const shellRoot = new URL('../shell/', import.meta.url);

// 公共运行时构建为单个 ESM 和单个 CSS；Shell 与隔离页均引用同一份发布资源。
export default defineConfig({
  root: fileURLToPath(new URL('./', import.meta.url)),
  define: { 'process.env.NODE_ENV': JSON.stringify('production') },
  resolve: {
    alias: {
      vue: fileURLToPath(new URL('node_modules/vue', shellRoot)),
      'element-plus': fileURLToPath(new URL('node_modules/element-plus', shellRoot)),
    },
  },
  build: {
    target: 'es2020',
    outDir: 'dist/shared',
    emptyOutDir: true,
    sourcemap: false,
    cssCodeSplit: false,
    lib: {
      entry: fileURLToPath(new URL('./src/index.ts', import.meta.url)),
      formats: ['es'],
      fileName: () => 'reff-ui.mjs',
      cssFileName: 'reff-ui',
    },
  },
});
