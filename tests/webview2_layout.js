// 自动隐藏、单插件留白和重启夹具；只操作本测试拥有的隐藏网页与 IPC。
(async()=>{
 const phase=__LAYOUT_PHASE__,call=(m,p={})=>window.reffNativeCall(m,p),pause=ms=>new Promise(r=>setTimeout(r,ms));
 if(window!==parent){addEventListener('message',async e=>{if(e.source!==parent||e.data?.type!=='layout-probe')return;
  try{parent.postMessage({type:'layout-result',id:e.data.id,value:await call(e.data.method,e.data.params||{})},'*');}
  catch(error){parent.postMessage({type:'layout-result',id:e.data.id,error:String(error)},'*');}});return;}
 const check=(ok,label)=>{if(!ok)throw Error(label);};
 const wait=async(fn,label)=>{for(let i=0;i<320;i++){if(fn())return;await pause(25);}throw Error('wait: '+label);};
 const trace=[];for(const name of ['mouseover','mouseout','focusin','focusout'])document.addEventListener(name,e=>{trace.push([name,e.target.className,e.relatedTarget?.className]);if(trace.length>30)trace.shift();},true);
 const batch=messages=>call('test.finished',{checkpoint:'input',messages});
 const root=()=>document.querySelector('.reff-shell'),content=()=>document.querySelector('#workspace-content'),shown=s=>root().classList.contains(s+'-shown');
 const move=async(x,y)=>{await batch([{type:'mouse',action:'move',x,y}]);await pause(120);};
 const click=async selector=>{const r=document.querySelector(selector).getBoundingClientRect(),p={type:'mouse',x:Math.round(r.left+r.width/2),y:Math.round(r.top+r.height/2),button:0};await batch([{...p,action:'move'},{...p,action:'down'},{...p,action:'up'}]);await pause(130);};
 const hover=async side=>{await move(side==='sidebar'?3:Math.round(innerWidth/2),side==='sidebar'?Math.round(innerHeight/2):3);await wait(()=>shown(side),side+' hover');};
 const leave=async()=>{await move(Math.round(innerWidth*.75),Math.round(innerHeight*.7));await wait(()=>!shown('sidebar')&&!shown('header'),'chrome hides');};
 const menu=id=>document.querySelector(`[data-plugin-id="${id}"]`);
 const select=async id=>{await hover('sidebar');await click(`[data-plugin-id="${id}"]`);await leave();await wait(()=>id==='reff.settings'?document.querySelector('.settings-panel'):document.querySelector('iframe')?.src.includes(id+'.plugin'),'select '+id);await pause(180);};
 const saved=async predicate=>{for(let i=0;i<160;i++){if(predicate(await call('reff.settings.get')))return;await pause(25);}throw Error('settings not persisted');};
 let probe=0;
 const plugin=async(method,params={})=>{const frame=document.querySelector('iframe'),id=++probe;return new Promise((resolve,reject)=>{
  const done=e=>{if(e.source!==frame.contentWindow||e.data?.type!=='layout-result'||e.data.id!==id)return;clearTimeout(timer);removeEventListener('message',done);e.data.error?reject(Error(e.data.error)):resolve(e.data.value);};
  const timer=setTimeout(()=>{removeEventListener('message',done);reject(Error('plugin probe timeout'));},5000);addEventListener('message',done);frame.contentWindow.postMessage({type:'layout-probe',id,method,params},'*');});};
 const padding=async value=>wait(()=>parseFloat(getComputedStyle(content()).paddingLeft)===value,'effective padding '+value);
 const capture=async index=>{await pause(250);const points=[];
  if(shown('header')){for(const [selector,drag] of [['.window-title',true],['.sidebar-toggle',false],['.header-toggle',false]]){const r=document.querySelector(selector).getBoundingClientRect();points.push({x:Math.round(r.left+r.width/2),y:Math.round(r.top+r.height/2),drag});}}
  else points.push({x:Math.round(innerWidth/2),y:30,drag:false});
  points.push({x:Math.round(innerWidth*.75),y:Math.round(innerHeight*.7),drag:false});await call('test.finished',{checkpoint:'layout-capture',index,points});};
 const viewport=async(width,height,generation)=>{await batch([{type:'viewport',width,height,generation}]);await wait(()=>innerWidth===width&&innerHeight===height,'viewport');await pause(100);};
 try{
  await wait(()=>innerWidth===1458&&document.querySelector('.el-menu-item'),'initialization');
  if(phase===0){
   await wait(()=>document.querySelector('.settings-panel'),'settings');const d=await call('reff.settings.get');check(d.layout.sidebarCollapsed&&d.layout.headerCollapsed&&d.layout.pluginPadding===24&&d.navigation.rememberLastPlugin,'defaults');
   check(document.querySelector('.padding-target').textContent.includes('所有插件（默认）'),'global target label');await leave();check(content().getBoundingClientRect().left===0&&content().getBoundingClientRect().top===0,'hidden chrome reserves space');await capture(0);
   await hover('sidebar');await capture(1);await leave();await hover('header');await capture(2);await leave();
   document.querySelector('.sidebar-hint').focus();await batch([{type:'key',action:'down',key:9},{type:'key',action:'up',key:9}]);await wait(()=>shown('header')&&document.activeElement?.classList.contains('sidebar-toggle'),'keyboard reveal');content().focus();await leave();
   for(const [w,h,g,i] of [[768,640,2,3],[390,640,3,4],[320,240,4,5]]){await viewport(w,h,g);check(content().scrollWidth<=content().clientWidth+1,'overflow '+w);await capture(i);}
   const scale=document.querySelectorAll('.appearance-section .el-slider__button-wrapper')[3];scale.focus();await batch([{type:'key',action:'down',key:35},{type:'key',action:'up',key:35}]);await saved(s=>s.appearance.textScale===1.4);
   await viewport(390,640,5);content().scrollTop=0;content().focus();await leave();check(content().scrollWidth<=content().clientWidth+1,'large type overflow');await capture(14);await hover('sidebar');await capture(15);await leave();await hover('header');await capture(16);await leave();
   scale.focus();await batch([{type:'key',action:'down',key:36},{type:'key',action:'up',key:36},...Array.from({length:4},()=>[{type:'key',action:'down',key:39},{type:'key',action:'up',key:39}]).flat()]);await saved(s=>s.appearance.textScale===1);content().scrollTop=0;content().focus();
   await viewport(1458,818,6);await hover('sidebar');await click('.nav-pin');await saved(s=>!s.layout.sidebarCollapsed);
   await hover('header');check(document.querySelector('.window-bar').getBoundingClientRect().left===content().getBoundingClientRect().left,'header alignment');await click('.header-toggle');await saved(s=>!s.layout.headerCollapsed);await capture(6);
   await click('.nav-pin');await saved(s=>s.layout.sidebarCollapsed);await click('.header-toggle');await saved(s=>s.layout.headerCollapsed);await leave();
   await select('example.vue');await padding(24);await capture(7);const f=document.querySelector('iframe');await hover('sidebar');await leave();await hover('header');await leave();check(document.querySelector('iframe')===f,'hover recreated plugin');
   check((await plugin('ui.layout.get')).defaultPadding===24,'plugin default');let denied=false;try{await plugin('ui.layout.set',{padding:2,pluginId:'example.react'});}catch{denied=true;}check(denied,'foreign target accepted');
   await plugin('ui.layout.set',{padding:0});await padding(0);await capture(8);await select('example.react');check((await plugin('ui.layout.get')).override===null,'override leaked');await padding(24);await plugin('ui.layout.set',{padding:48});await padding(48);await capture(9);
   await select('example.vue');await padding(0);await plugin('ui.layout.set',{padding:null});await padding(24);check((await plugin('ui.layout.get')).override===null,'null inheritance');await plugin('ui.layout.set',{padding:8});await padding(8);
   await saved(s=>s.layout.pluginPaddingOverrides['example.vue']===8&&s.layout.pluginPaddingOverrides['example.react']===48&&s.navigation.lastPluginId==='example.vue');
  }else if(phase===1){
   await wait(()=>document.querySelector('iframe')?.src.includes('example.vue.plugin'),'restart Vue');await leave();await padding(8);check((await plugin('ui.layout.get')).override===8,'override persistence');await capture(10);await select('example.react');await padding(48);await select('reff.settings');
   document.querySelector('#padding-target').click();await wait(()=>[...document.querySelectorAll('.el-select-dropdown__item')].some(e=>e.textContent.includes('Vue')),'targets');[...document.querySelectorAll('.el-select-dropdown__item')].find(e=>e.textContent.includes('Vue')).click();await pause(100);
   const inherit=document.querySelector('.layout-section .layout-row:last-of-type .el-switch');check(inherit&&!inherit.classList.contains('is-checked'),'override inheritance UI');inherit.click();await saved(s=>s.layout.pluginPaddingOverrides['example.vue']===undefined&&s.layout.pluginPaddingOverrides['example.react']===48);await capture(11);
   document.querySelector('.remember-plugin-row .el-switch').click();await saved(s=>!s.navigation.rememberLastPlugin);await select('example.react');
  }else if(phase===2){
   await wait(()=>document.querySelector('.settings-panel'),'memory off startup');await leave();document.querySelector('.remember-plugin-row .el-switch').click();await saved(s=>s.navigation.rememberLastPlugin);
   const slider=document.querySelector('.layout-section .el-slider__button-wrapper');slider.focus();await batch([{type:'key',action:'down',key:36},{type:'key',action:'up',key:36}]);await saved(s=>s.layout.pluginPadding===0);slider.focus();await batch([{type:'key',action:'down',key:35},{type:'key',action:'up',key:35}]);await saved(s=>s.layout.pluginPadding===48);
   await select('example.react');await plugin('ui.layout.set',{padding:null});await padding(48);check((await plugin('ui.layout.get')).override===null,'React inheritance');await saved(s=>s.navigation.lastPluginId==='example.react');await capture(12);
  }else{
   await wait(()=>document.querySelector('.settings-panel'),'uninstalled fallback');await leave();check(!menu('example.react'),'uninstalled plugin listed');await saved(s=>s.navigation.lastPluginId==='reff.settings');await capture(13);
  }
  await call('test.finished',{passed:true,layoutPhase:phase,autoHide:true,hoverKeepsPlugin:true,scopedPluginPadding:true,rememberLastPlugin:true});
 }catch(error){await call('test.finished',{passed:false,layoutPhase:phase,error:String(error.stack||error),trace,classes:root()?.className,active:document.activeElement?.className,hover:[...document.querySelectorAll(':hover')].map(e=>e.className)});}
})();
