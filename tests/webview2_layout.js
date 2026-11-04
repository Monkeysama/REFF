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
 const leave=async()=>{await move(Math.round(innerWidth*.75),Math.round(innerHeight*.7));await wait(()=>['sidebar','header'].every(side=>shown(side)===!root().classList.contains(side+'-collapsed')),'chrome visibility follows auto-hide');};
 const menu=id=>document.querySelector(`[data-plugin-id="${id}"]`);
 const select=async id=>{await hover('sidebar');await click(`[data-plugin-id="${id}"]`);await leave();await wait(()=>id==='reff.settings'?document.querySelector('.settings-panel'):document.querySelector('iframe')?.src.includes(id+'.plugin'),'select '+id);await pause(180);};
 const saved=async predicate=>{for(let i=0;i<160;i++){if(predicate(await call('reff.settings.get')))return;await pause(25);}throw Error('settings not persisted');};
 let probe=0;
 const plugin=async(method,params={})=>{const frame=document.querySelector('iframe'),id=++probe;return new Promise((resolve,reject)=>{
  const done=e=>{if(e.source!==frame.contentWindow||e.data?.type!=='layout-result'||e.data.id!==id)return;clearTimeout(timer);removeEventListener('message',done);e.data.error?reject(Error(e.data.error)):resolve(e.data.value);};
  const timer=setTimeout(()=>{removeEventListener('message',done);reject(Error('plugin probe timeout'));},5000);addEventListener('message',done);frame.contentWindow.postMessage({type:'layout-probe',id,method,params},'*');});};
 const padding=async value=>wait(()=>parseFloat(getComputedStyle(content()).paddingLeft)===value,'effective padding '+value);
 // 临时零尺寸行内标记读出真实文字基线，不以字号不同的外框中心代替对齐证据。
 const brandAlignment=()=>{
  const baseline=selector=>{const marker=document.createElement('i');marker.style.cssText='display:inline-block;width:0;height:0;padding:0;border:0;vertical-align:baseline';document.querySelector(selector).append(marker);const y=marker.getBoundingClientRect().top;marker.remove();return y;};
  check(Math.abs(baseline('.brand-name')-baseline('.brand small'))<1,'brand/version baseline');
  const row=document.querySelector('.brand').getBoundingClientRect(),button=document.querySelector('.nav-pin').getBoundingClientRect();
  check(Math.abs(row.top+row.height/2-button.top-button.height/2)<1,'sidebar arrow row center');
 };
 const sidebarArrow=compact=>{const button=document.querySelector('.nav-pin'),transform=getComputedStyle(button.querySelector('.chrome-icon')).transform;
  check(button.getAttribute('aria-expanded')===String(!compact),'sidebar arrow expanded state');
  check(compact?transform==='none':transform==='matrix(-1, 0, 0, -1, 0, 0)','sidebar arrow direction');};
 const capture=async index=>{await pause(250);const points=[];
  if(shown('header')){for(const [selector,drag] of [['.window-title',true],['.header-toggle',false]]){const r=document.querySelector(selector).getBoundingClientRect();points.push({x:Math.round(r.left+r.width/2),y:Math.round(r.top+r.height/2),drag});}}
  else points.push({x:Math.round(innerWidth/2),y:30,drag:false});
  points.push({x:Math.round(innerWidth*.75),y:Math.round(innerHeight*.7),drag:false});await call('test.finished',{checkpoint:'layout-capture',index,points});};
 const viewport=async(width,height,generation)=>{await batch([{type:'viewport',width,height,generation}]);await wait(()=>innerWidth===width&&innerHeight===height,'viewport');await pause(100);};
 try{
  await wait(()=>innerWidth===1458&&document.querySelector('.el-menu-item'),'initialization');
  if(phase===0){
   await wait(()=>document.querySelector('.settings-panel'),'settings');const d=await call('reff.settings.get');check(!d.layout.sidebarCollapsed&&d.layout.headerCollapsed&&d.layout.sidebarCompact&&!d.layout.headerCompact&&d.layout.pluginPadding===24&&d.navigation.rememberLastPlugin,'defaults');
   check(document.querySelector('.padding-target').textContent.includes('所有插件（默认）'),'global target label');await leave();check(content().getBoundingClientRect().left===56&&content().getBoundingClientRect().top===0,'default compact rail reserves 56px');check(!document.querySelector('.sidebar-toggle'),'redundant top-left button');check(document.querySelector('.navigation').getBoundingClientRect().width===56,'default icon rail');await capture(0);
   sidebarArrow(true);await click('.nav-pin');await saved(s=>!s.layout.sidebarCompact&&!s.layout.sidebarCollapsed);check(document.querySelector('.navigation').getBoundingClientRect().width===208,'expanded sidebar');brandAlignment();sidebarArrow(false);await capture(17);await click('.nav-pin');await saved(s=>s.layout.sidebarCompact);sidebarArrow(true);
   await hover('header');check(document.querySelector('.window-bar').getBoundingClientRect().height===64,'full top bar default');await click('.header-toggle');await saved(s=>s.layout.headerCompact&&s.layout.headerCollapsed);check(document.querySelector('.window-bar').getBoundingClientRect().height===40,'compact top bar');await capture(18);await click('.header-toggle');await saved(s=>!s.layout.headerCompact);await leave();
   await hover('sidebar');await capture(1);await leave();await hover('header');await capture(2);await leave();
   menu('example.html').focus();await batch([{type:'key',action:'down',key:9},{type:'key',action:'up',key:9}]);await wait(()=>shown('header')&&document.activeElement?.classList.contains('header-toggle'),'keyboard reveal');content().focus();await leave();
   for(const [w,h,g,i] of [[768,640,2,3],[390,640,3,4],[320,240,4,5]]){await viewport(w,h,g);check(content().scrollWidth<=content().clientWidth+1,'overflow '+w);await capture(i);}
   const scale=document.querySelectorAll('.appearance-section .el-slider__button-wrapper')[3];scale.focus();await batch([{type:'key',action:'down',key:35},{type:'key',action:'up',key:35}]);await saved(s=>s.appearance.textScale===1.4);
   await viewport(390,640,5);content().scrollTop=0;content().focus();await leave();check(content().scrollWidth<=content().clientWidth+1,'large type overflow');await capture(14);await hover('sidebar');await capture(15);await leave();await hover('header');await capture(16);await leave();await click('.nav-pin');await saved(s=>!s.layout.sidebarCompact);const pin=document.querySelector('.nav-pin').getBoundingClientRect();check(pin.right<=208&&pin.left>=0,'narrow large-type toggle reachable');brandAlignment();sidebarArrow(false);await capture(19);await click('.nav-pin');await saved(s=>s.layout.sidebarCompact);sidebarArrow(true);
   scale.focus();await batch([{type:'key',action:'down',key:36},{type:'key',action:'up',key:36},...Array.from({length:4},()=>[{type:'key',action:'down',key:39},{type:'key',action:'up',key:39}]).flat()]);await saved(s=>s.appearance.textScale===1);content().scrollTop=0;content().focus();
   await viewport(1458,818,6);
   // 设置页四个开关可任意组合；检查实际占位和尺寸，避免把折叠再误作隐藏。
   const labels={sidebarCompact:'折叠侧边栏（仅显示图标）',headerCompact:'折叠顶部栏（紧凑标题）',sidebarCollapsed:'自动隐藏侧边栏',headerCollapsed:'自动隐藏顶部栏'};
   const set=async(key,value)=>{const toggle=[...document.querySelectorAll(".layout-section .layout-row")].find(row=>row.querySelector("span")?.textContent===labels[key])?.querySelector(".el-switch");check(toggle,'setting '+key);if(toggle.classList.contains('is-checked')!==value)toggle.click();await saved(s=>s.layout[key]===value);content().focus();await leave();};
   for(const compact of [false,true])for(const hide of [false,true]){
    await set('sidebarCompact',compact);await set('headerCompact',compact);await set('sidebarCollapsed',hide);await set('headerCollapsed',hide);
    check(content().getBoundingClientRect().left===(hide?0:compact?56:208),'independent sidebar footprint');
    check(content().getBoundingClientRect().top===(hide?0:compact?40:64),'independent header footprint');
    await hover('sidebar');check(document.querySelector('.navigation').getBoundingClientRect().width===(compact?56:208),'sidebar shown width');await leave();
    await hover('header');check(document.querySelector('.window-bar').getBoundingClientRect().height===(compact?40:64),'header shown height');await capture(6);await leave();
   }
   await set('sidebarCollapsed',false);await set('headerCompact',false);
   await select('example.vue');await padding(24);await capture(7);const f=document.querySelector('iframe');await hover('sidebar');await leave();await hover('header');await leave();check(document.querySelector('iframe')===f,'hover recreated plugin');
   check((await plugin('ui.layout.get')).defaultPadding===24,'plugin default');let denied=false;try{await plugin('ui.layout.set',{padding:2,pluginId:'example.react'});}catch{denied=true;}check(denied,'foreign target accepted');
   await plugin('ui.layout.set',{padding:0});await padding(0);await capture(8);await select('example.react');check((await plugin('ui.layout.get')).override===null,'override leaked');await padding(24);await plugin('ui.layout.set',{padding:48});await padding(48);await capture(9);
   await select('example.vue');await padding(0);await plugin('ui.layout.set',{padding:null});await padding(24);check((await plugin('ui.layout.get')).override===null,'null inheritance');await plugin('ui.layout.set',{padding:8});await padding(8);
   await saved(s=>s.layout.pluginPaddingOverrides['example.vue']===8&&s.layout.pluginPaddingOverrides['example.react']===48&&s.navigation.lastPluginId==='example.vue');
  }else if(phase===1){
   await wait(()=>document.querySelector('iframe')?.src.includes('example.vue.plugin'),'restart Vue');await leave();await padding(8);const persisted=await call('reff.settings.get');check(!persisted.layout.sidebarCollapsed&&persisted.layout.headerCollapsed&&persisted.layout.sidebarCompact&&!persisted.layout.headerCompact,'chrome restart persistence');check((await plugin('ui.layout.get')).override===8,'override persistence');await capture(10);await select('example.react');await padding(48);await select('reff.settings');
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
  await call('test.finished',{passed:true,layoutPhase:phase,autoHide:true,independentCompact:true,defaultIconRail:true,noHeaderLeftButton:true,hoverKeepsPlugin:true,scopedPluginPadding:true,rememberLastPlugin:true});
 }catch(error){await call('test.finished',{passed:false,layoutPhase:phase,error:String(error.stack||error),trace,classes:root()?.className,active:document.activeElement?.className,hover:[...document.querySelectorAll(':hover')].map(e=>e.className)});}
})();
