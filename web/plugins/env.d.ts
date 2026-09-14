interface Window {
  cefQuery(options: {
    request: string;
    onSuccess(response: string): void;
    onFailure(code: number, message: string): void;
  }): void;
}

declare module 'element-plus/es/locale/lang/en.mjs' {
  const locale: any;
  export default locale;
}
declare module 'element-plus/es/locale/lang/zh-cn.mjs' {
  const locale: any;
  export default locale;
}
