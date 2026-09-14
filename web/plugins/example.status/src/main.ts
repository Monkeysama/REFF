import { createApp } from 'vue';
import { installReffUi } from '@reff/ui';
import App from './App.vue';
import './style.css';
import './layout.css';

// 隔离页只保留自身业务代码和主题覆盖，公共 Vue/Element Plus 资源由 REFF Core 提供。
installReffUi(createApp(App)).mount('#app');
