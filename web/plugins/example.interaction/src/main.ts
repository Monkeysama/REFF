import { createApp } from 'vue';
import { installReffUi } from '@reff/ui';
import App from './App.vue';
import './layout.css';

// 隔离示例拥有独立 Vue 应用实例，只复用 REFF 提供的公共模块和主题资源。
installReffUi(createApp(App)).mount('#app');
