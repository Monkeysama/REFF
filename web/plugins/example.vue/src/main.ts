import { createApp } from 'vue';
import { installReffUi } from '@reff/ui';
import App from './App.vue';
import './style.css';

// Vue 3 示例入口只负责安装公共 UI 并挂载业务组件。
installReffUi(createApp(App)).mount('#app');
