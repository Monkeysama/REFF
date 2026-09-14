import {defineConfig} from 'vite'; import {fileURLToPath,URL} from 'node:url'; import vue from '../../shell/node_modules/@vitejs/plugin-vue/dist/index.mjs';
const shell=new URL('../../shell/',import.meta.url); const root=fileURLToPath(new URL('./',import.meta.url)); const shared='reff://shell/shared/reff-ui.mjs';
// 独立设置页与状态页采用同一公共模块映射，避免插件包携带重复框架资源。
export default defineConfig({root,plugins:[vue()],base:'./',resolve:{alias:{vue:fileURLToPath(new URL('node_modules/vue',shell)),'@reff/ui':fileURLToPath(new URL('../shared/src/index.ts',shell))}},build:{target:'es2020',outDir:'ui/dist',emptyOutDir:true,rollupOptions:{input:fileURLToPath(new URL('./ui-vue.html',import.meta.url)),external:['vue','@reff/ui'],output:{paths:{vue:shared,'@reff/ui':shared}}}}});
