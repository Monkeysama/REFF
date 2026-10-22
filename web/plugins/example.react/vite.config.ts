import { fileURLToPath, URL } from 'node:url';
import { join } from 'node:path';
import { existsSync } from 'node:fs';

const root = fileURLToPath(new URL('./ui', import.meta.url));
const sdk = fileURLToPath(new URL('../../sdk', import.meta.url));
// 独立开发优先使用本插件依赖，仓库统一构建仍可复用 Shell 的锁定工具链。
const shell = fileURLToPath(new URL(existsSync(new URL('./node_modules/react/package.json', import.meta.url)) ? './' : '../../shell/', import.meta.url));

// React 示例只依赖自带打包产物和框架无关的 REFF SDK。
// React 示例使用 Vite 内置的 esbuild TSX 转换，避免把 React Refresh 运行时带入游戏内页面。
export default { root, base: './', resolve: { alias: { '@reff-sdk': sdk, react: join(shell, 'node_modules', 'react'), 'react-dom': join(shell, 'node_modules', 'react-dom') } }, build: { target: 'es2020', outDir: 'dist', emptyOutDir: true, rollupOptions: { input: fileURLToPath(new URL('./ui/index.html', import.meta.url)) } } };
