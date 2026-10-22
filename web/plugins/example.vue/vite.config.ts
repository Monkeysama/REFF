import { fileURLToPath, URL } from 'node:url';
import { join } from 'node:path';
import { existsSync } from 'node:fs';
import { createRequire } from 'node:module';

const root = fileURLToPath(new URL('./ui', import.meta.url));
// 示例可独立安装依赖；仓库统一构建尚未安装本地依赖时，复用锁定的 Shell 工具链。
const shell = fileURLToPath(new URL(existsSync(new URL('./node_modules/@vitejs/plugin-vue/package.json', import.meta.url)) ? './' : '../../shell/', import.meta.url));
const require = createRequire(join(shell, 'package.json'));
const vue = require('@vitejs/plugin-vue');
const shared = 'reff://shell/shared/reff-ui.mjs';

// Vue 3 示例使用独立页面，公共主题和 SDK 由 REFF 运行时提供。
export default {
  root,
  plugins: [vue()],
  base: './',
  resolve: { alias: { vue: join(shell, 'node_modules', 'vue'), '@reff/ui': fileURLToPath(new URL('../../shared/src/index.ts', import.meta.url)), '@reff-sdk': fileURLToPath(new URL('../../sdk', import.meta.url)) } },
  build: { target: 'es2020', outDir: 'dist', emptyOutDir: true, rollupOptions: { input: fileURLToPath(new URL('./ui/index.html', import.meta.url)), external: ['vue', '@reff/ui'], output: { paths: { vue: shared, '@reff/ui': shared } } } },
};
