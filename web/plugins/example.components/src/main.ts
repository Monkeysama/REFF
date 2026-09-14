import { createApp } from 'vue';
import { installReffUi } from '@reff/ui';
import App from './App.vue';
import './layout.css';

// 公共组件示例运行在独立文档中，用于验证插件可复用 REFF 的组件和主题资源。
installReffUi(createApp(App)).mount('#app');
