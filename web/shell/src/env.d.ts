// 产品版本由根目录 version.json 在 Vite 构建期注入，运行时页面不读取开发目录。
declare const __REFF_VERSION__: string;

// Element Plus 2.10.7 ships locale declarations beside `.mjs` files but its export map does not associate them for this external shared build.
declare module 'element-plus/es/locale/lang/en.mjs' {
  const locale: any;
  export default locale;
}
declare module 'element-plus/es/locale/lang/zh-cn.mjs' {
  const locale: any;
  export default locale;
}
