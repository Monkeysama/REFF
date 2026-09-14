import { createApp } from 'vue';
import { installReffUi } from '@reff/ui';
import App from './App.vue';
import './style.css';

// 示例独立页只引用 REFF 发布的公共运行时，不在插件目录复制框架和组件库。
installReffUi(createApp(App)).mount('#app');
