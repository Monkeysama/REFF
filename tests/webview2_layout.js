// 布局与真实宿主重启夹具；只操作本测试进程拥有的隐藏网页。
(async()=>{
 if(window!==parent)return;
 const phase=__LAYOUT_PHASE__,call=(method,params={})=>window.reffNativeCall(method,params),pause=ms=>new Promise(r=>setTimeout(r,ms));
 const check=(ok,label)=>{if(!ok)throw Error(label);};
 const wait=async(fn,label)=>{for(let i=0;i<320;i++){if(fn())return;await pause(25);}throw Error('wait: '+label);};
 const batch=messages=>call('test.finished',{checkpoint:'input',messages});
 const menu=id=>document.querySelector(`[data-plugin-id="${id}"]`);
 const saved=async predicate=>{for(let i=0;i<160;i++){if(predicate(await call('reff.settings.get')))return;await pause(25);}throw Error('settings not persisted');};
 const capture=async index=>{
  await pause(300);const r=document.querySelector('.window-title').getBoundingClientRect();
  const points=[{x:Math.round(r.left+r.width/2),y:Math.round(r.top+r.height/2),drag:true}];
  for(const selector of ['.sidebar-toggle','.header-toggle','#workspace-content']){
   const b=document.querySelector(selector).getBoundingClientRect();points.push({x:Math.round(b.left+b.width/2),y:Math.round(b.top+b.height/2),drag:false});
  }
  await call('test.finished',{checkpoint:'layout-capture',index,points});
 };
 try{
  await wait(()=>innerWidth===1458&&document.querySelector('.el-menu-item'),'initialization');
  if(phase===0){
   await wait(()=>document.querySelector('.settings-panel'),'initial settings');
   const defaults=await call('reff.settings.get');check(defaults.layout.sidebarCollapsed&&defaults.layout.headerCollapsed&&defaults.navigation.rememberLastPlugin,'compact/remembered defaults');await capture(0);
   for(const [index,width,height] of [[1,768,640],[2,390,640],[3,320,240],[4,1440,900]]){
    await batch([{type:'viewport',width,height,generation:index+2}]);await wait(()=>innerWidth===width&&innerHeight===height,'viewport');
    const content=document.querySelector('#workspace-content');check(content.scrollWidth<=content.clientWidth+1,'settings horizontal overflow at '+width);await capture(index);
   }
   // 鼠标走真实宿主输入，声明拖动区由原生公共命中函数检查，不能吞掉两个按钮。
   for(const selector of ['.sidebar-toggle','.header-toggle']){
    const r=document.querySelector(selector).getBoundingClientRect(),point={type:'mouse',x:Math.round(r.left+r.width/2),y:Math.round(r.top+r.height/2),button:0};
    await batch([{...point,action:'move'},{...point,action:'down'},{...point,action:'up'}]);await pause(180);
   }
   await saved(s=>!s.layout.sidebarCollapsed&&!s.layout.headerCollapsed);await capture(5);
   // 展开导航与最大字号也必须保留窄视口的可用正文，不只检查默认折叠状态。
   await batch([{type:'viewport',width:390,height:640,generation:7}]);await wait(()=>innerWidth===390,'narrow expanded');await pause(100);
   const textSlider=document.querySelectorAll('.appearance-section .el-slider__button-wrapper')[3];textSlider.focus();await batch([{type:'key',action:'down',key:35},{type:'key',action:'up',key:35}]);await saved(s=>s.appearance.textScale===1.4);
   for(const [width,height,generation,index] of [[390,640,8,10],[320,240,9,11]]){
    await batch([{type:'viewport',width,height,generation}]);await wait(()=>innerWidth===width,'expanded viewport');await pause(150);
    const c=document.querySelector('#workspace-content');check(c.scrollWidth<=c.clientWidth+1,'expanded/scaled settings overflow at '+width);await capture(index);
   }
   await batch([{type:'viewport',width:1440,height:900,generation:10}]);await wait(()=>innerWidth===1440,'restore desktop');
   document.querySelector('.sidebar-toggle').click();document.querySelector('.header-toggle').click();await saved(s=>s.layout.sidebarCollapsed&&s.layout.headerCollapsed);
   document.querySelector('.layout-section .el-slider__button-wrapper').focus();await batch([{type:'key',action:'down',key:36},{type:'key',action:'up',key:36}]);await saved(s=>s.layout.pluginPadding===0);
   menu('example.vue').click();await wait(()=>document.querySelector('iframe'),'Vue');await saved(s=>s.navigation.lastPluginId==='example.vue');
   check(parseFloat(getComputedStyle(document.querySelector('#workspace-content')).paddingLeft)===0,'zero padding');await capture(6);
   const frame=document.querySelector('iframe');document.querySelector('.sidebar-toggle').click();await saved(s=>!s.layout.sidebarCollapsed);check(document.querySelector('iframe')===frame,'collapse recreated plugin');
  }else if(phase===1){
   await wait(()=>document.querySelector('iframe')?.src.includes('example.vue.plugin'),'restart restores Vue');check(!document.querySelector('.reff-shell').classList.contains('sidebar-collapsed'),'sidebar persistence');await capture(7);
   menu('reff.settings').click();await wait(()=>document.querySelector('.settings-panel'),'settings');document.querySelector('.remember-plugin-row .el-switch').click();await saved(s=>!s.navigation.rememberLastPlugin);
   menu('example.react').click();await wait(()=>document.querySelector('iframe')?.src.includes('example.react.plugin'),'React');
  }else if(phase===2){
   await wait(()=>document.querySelector('.settings-panel'),'remember disabled startup');document.querySelector('.remember-plugin-row .el-switch').click();await saved(s=>s.navigation.rememberLastPlugin);
   document.querySelector('.layout-section .el-slider__button-wrapper').focus();await batch([{type:'key',action:'down',key:35},{type:'key',action:'up',key:35}]);await saved(s=>s.layout.pluginPadding===48);
   menu('example.react').click();await wait(()=>document.querySelector('iframe'),'React');await saved(s=>s.navigation.lastPluginId==='example.react');
   check(parseFloat(getComputedStyle(document.querySelector('#workspace-content')).paddingLeft)===48,'maximum padding');await capture(8);
  }else{
   await wait(()=>document.querySelector('.settings-panel'),'uninstalled plugin fallback');check(!menu('example.react'),'uninstalled plugin still listed');await saved(s=>s.navigation.lastPluginId==='reff.settings');await capture(9);
  }
  await call('test.finished',{passed:true,layoutPhase:phase,compactChrome:true,pluginPadding:true,rememberLastPlugin:true});
 }catch(error){await call('test.finished',{passed:false,layoutPhase:phase,error:String(error.stack||error)});}
})();
