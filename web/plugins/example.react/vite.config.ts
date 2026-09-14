import { defineConfig } from 'vite';
import { fileURLToPath, URL } from 'node:url';
import { join } from 'node:path';

const root = fileURLToPath(new URL('./ui', import.meta.url));
const sdk = fileURLToPath(new URL('../../sdk', import.meta.url));
const shell = fileURLToPath(new URL('../../shell', import.meta.url));

// React 示例只依赖自带打包产物和框架无关的 REFF SDK。
// React 示例使用 Vite 内置的 esbuild TSX 转换，避免把 React Refresh 运行时带入游戏内页面。
export default defineConfig({ root, base: './', resolve: { alias: { '@reff-sdk': sdk, react: join(shell, 'node_modules', 'react'), 'react-dom': join(shell, 'node_modules', 'react-dom') } }, build: { target: 'es2020', outDir: 'dist', emptyOutDir: true, rollupOptions: { input: fileURLToPath(new URL('./ui/index.html', import.meta.url)) } } });
