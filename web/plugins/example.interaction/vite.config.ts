import { defineConfig } from 'vite';
import { fileURLToPath, URL } from 'node:url';
import vue from '../../shell/node_modules/@vitejs/plugin-vue/dist/index.mjs';

const shell = new URL('../../shell/', import.meta.url);
const root = fileURLToPath(new URL('./', import.meta.url));
const shared = 'reff://shell/shared/reff-ui.mjs';

// 插件业务产物引用 REFF 公共 Vue/Element Plus 模块，不重复打包框架运行时。
export default defineConfig({ root, plugins: [vue()], base: './', resolve: { alias: { vue: fileURLToPath(new URL('node_modules/vue', shell)), '@reff/ui': fileURLToPath(new URL('../shared/src/index.ts', shell)), '@reff-sdk': fileURLToPath(new URL('../sdk', shell)) } }, build: { target: 'es2020', outDir: 'ui/dist', emptyOutDir: true, rollupOptions: { input: fileURLToPath(new URL('./ui-vue.html', import.meta.url)), external: ['vue', '@reff/ui'], output: { paths: { vue: shared, '@reff/ui': shared } } } } });
