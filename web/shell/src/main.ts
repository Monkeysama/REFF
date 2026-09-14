import { createApp } from 'vue';
import { installReffUi } from '@reff/ui';

import App from './App.vue';
import { i18n } from './locales';

// Shell 与隔离插件页使用同一安装器，避免各自打包 Vue、Element Plus 和基础样式。
const app = createApp(App);
installReffUi(app);
app.use(i18n);
app.mount('#app');
