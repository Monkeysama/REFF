// 独立隐藏浏览器夹具：验证生产设置组件、SDK 与 IPC 输入，不操作游戏或系统剪贴板。
(async()=>{
 const call=(method,params={})=>window.reffNativeCall(method,params),pause=ms=>new Promise(r=>setTimeout(r,ms));
 const check=(ok,why)=>{if(!ok)throw Error(why);};
 const wait=async(fn,label)=>{for(let i=0;i<320;i++){const v=fn();if(v)return v;await pause(25);}throw Error('wait: '+label);};
 const batch=messages=>call('test.finished',{checkpoint:'input',messages});
 const key=(action,key,modifiers=0)=>({type:'key',action,key,modifiers});
 let pointerOffset={x:0,y:0};
 const input=async()=>{
  const field=document.createElement('textarea');field.id='p2-input';field.style.cssText='position:fixed;left:20px;top:80px;z-index:10000;width:180px;height:32px';
  document.body.append(field);field.focus();await pause(120);
  await batch([{type:'ime',action:'composition',text:'中文'},{type:'ime',action:'commit',text:'中文'}]);await wait(()=>field.value==='中文','Chinese commit once');
  await batch([key('down',65,4),key('up',65,4),key('down',8),key('up',8)]);await wait(()=>field.value==='','Ctrl+A / Backspace');
  await batch([key('down',65),key('char',97),key('up',65)]);await wait(()=>field.value==='a','ordinary character');
  let downs=0,ups=0,suspends=0;const down=e=>{if(e.code==='KeyK')downs++;},up=e=>{if(e.code==='KeyK')ups++;};
  addEventListener('keydown',down);addEventListener('keyup',up);addEventListener('reff:input-suspend',()=>suspends++,{once:true});
  await batch([key('down',75)]);await wait(()=>downs===1,'held key');
  await batch([{type:'focus',value:false},{type:'focus',value:false},key('down',75),key('char',120)]);await wait(()=>suspends===1&&ups===1,'focus releases pressed keys');
  check(downs===1&&field.value==='a'&&document.activeElement!==field,'unfocused input leaked');
  await batch([{type:'focus',value:true}]);await pause(120);field.focus();await pause(120);
  await batch([{type:'ime',action:'commit',text:'好'}]);await wait(()=>field.value==='a好','fresh focus input');
  let doubled=false;field.addEventListener('dblclick',event=>doubled=event.detail===2,{once:true});
  const rect=field.getBoundingClientRect(),point={type:'mouse',x:Math.round(rect.left+30+pointerOffset.x),y:Math.round(rect.top+15+pointerOffset.y),button:0};
  await batch([{...point,action:'move'},{...point,action:'down'},{...point,action:'up'},{...point,action:'down',clickCount:2},{...point,action:'up'}]);
  await wait(()=>doubled,'native double click');
  await batch([{type:'visible',value:false}]);await wait(()=>document.activeElement!==field,'hide blur');await batch([{type:'visible',value:true}]);await pause(100);
  removeEventListener('keydown',down);removeEventListener('keyup',up);field.remove();
 };
 const identity=await call('ui.identity');
 if(window!==parent){
  parent.postMessage({type:'p2-child',pageId:identity.pageId},'reff://shell');
  addEventListener('message',async event=>{
   if(event.source!==parent||event.data?.type!=='p2-input')return;
   try{pointerOffset=event.data.offset;await wait(()=>getComputedStyle(document.documentElement).getPropertyValue('--reff-accent').trim()==='#5DADE2','iframe preferences');await input();parent.postMessage({type:'p2-child-input',passed:true},'reff://shell');}
   catch(error){parent.postMessage({type:'p2-child-input',passed:false,error:String(error.stack||error)},'reff://shell');}
  });return;
 }
 const reports=[];addEventListener('message',e=>{if(e.source===document.querySelector('iframe')?.contentWindow&&e.data?.type==='p2-child')reports.push(e.data);});
 try{
  await wait(()=>innerWidth===1458&&innerHeight===818,'first open restored viewport without dragging');
  await wait(()=>document.querySelector('.settings-panel'),'settings ready');document.querySelectorAll('.preset-button')[1].click();
  await wait(()=>getComputedStyle(document.documentElement).getPropertyValue('--reff-accent').trim()==='#5DADE2','theme');
  document.querySelector('.language-select').click();const english=await wait(()=>[...document.querySelectorAll('.el-select-dropdown__item')].find(e=>e.textContent.includes('English')),'language option');english.click();
  await wait(()=>document.querySelector('.settings-panel').textContent.includes('General'),'English UI');
  document.querySelector('.hotkey-action').click();await wait(()=>document.querySelector('.hotkey-listening'),'capture');
  const previousHotkey=(await call('reff.settings.get')).hotkey;
  // 使用真实修饰键的按下/松开序列，不能只在 K 事件上附带 ctrlKey 掩盖提前提交的问题。
  for(const [vk,flags] of [[17,4],[16,2],[18,8],[91,128],[92,128]]){
   await batch([key('down',vk,flags),key('up',vk)]);await pause(180);
   check(document.querySelector('.hotkey-listening'),'modifier alone ended capture: '+vk);
   check(JSON.stringify((await call('reff.settings.get')).hotkey)===JSON.stringify(previousHotkey),'modifier alone saved binding: '+vk);
  }
  await batch([key('down',17,4),key('down',16,6)]);await pause(180);
  check(document.querySelector('.hotkey-listening'),'modifier chord ended capture');
  await batch([key('down',75,6),key('up',75,6),key('up',16,4),key('up',17)]);
  await wait(()=>document.querySelector('kbd')?.textContent.includes('Shift + Ctrl + K'),'hotkey K');
  document.querySelector('.hotkey-action').click();await wait(()=>document.querySelector('.hotkey-listening'),'capture Ctrl+K');
  await batch([key('down',17,4)]);await pause(180);check(document.querySelector('.hotkey-listening'),'Ctrl ended capture before K');
  await batch([key('down',75,4),key('up',75,4),key('up',17)]);await wait(()=>document.querySelector('kbd')?.textContent==='Ctrl + K','hotkey Ctrl+K');
  document.querySelector('.hotkey-action').click();await pause(80);await batch([key('down',135),key('up',135)]);await wait(()=>document.querySelector('kbd')?.textContent==='F24','hotkey F24');
  document.querySelector('.hotkey-action').click();await pause(80);await batch([key('down',186,4),key('up',186,4)]);await wait(()=>document.querySelector('kbd')?.textContent==='Ctrl + ;','punctuation hotkey');
  const sliders=[...document.querySelectorAll('.el-slider__button-wrapper')];check(sliders.length===4,'appearance sliders');
  for(const slider of sliders){slider.focus();await batch([key('down',35),key('up',35)]);await pause(120);}
  await wait(()=>getComputedStyle(document.documentElement).getPropertyValue('--reff-text-scale').trim()==='1.4','text scaling');
  const switches=[...document.querySelectorAll('.el-switch')];check(switches.length>=4,'settings switches missing');switches[0].click();switches[1].click();switches[2].click();await pause(400);
  let settings=await call('reff.settings.get');check(settings.language==='en-US'&&settings.appearance.preset==='ocean'&&!settings.startup.preload&&settings.input.mousePassthrough&&settings.input.keyboardPassthrough,'settings patch serialization');
  check(settings.appearance.backgroundOpacity===1&&settings.appearance.cornerRadius===16&&settings.appearance.surfaceBlur===32&&settings.appearance.textScale===1.4,'appearance persistence');
  await call('test.finished',{checkpoint:'settings-reopen'});await pause(150);settings=await call('reff.settings.get');check(settings.window.geometry?.width===960,'settings geometry persistence');
  await input();const plugins=(await call('ui.plugins')).plugins,vue=plugins.find(p=>p.id==='example.vue');
  const menu=[...document.querySelectorAll('.el-menu-item')].find(e=>e.textContent.includes(vue.localizedName?.['en-US']||vue.name));check(menu,'Vue menu');menu.click();await wait(()=>reports.length===1,'first iframe');
  await pause(1700);await call('test.finished',{checkpoint:'marker'});await wait(()=>reports.length>=2,'hot reload');await pause(1700);
  check(reports.length===2&&reports[0].pageId!==reports[1].pageId,'duplicate reload / identity reuse');
  const childInput=new Promise((resolve,reject)=>{const timer=setTimeout(()=>reject(Error('child input timeout')),10000);const listener=e=>{if(e.source!==document.querySelector('iframe')?.contentWindow||e.data?.type!=='p2-child-input')return;clearTimeout(timer);removeEventListener('message',listener);resolve(e.data);};addEventListener('message',listener);});
  const iframe=document.querySelector('iframe'),frameRect=iframe.getBoundingClientRect();
  iframe.contentWindow.postMessage({type:'p2-input',offset:{x:frameRect.left,y:frameRect.top}},'*');const child=await childInput;check(child.passed,child.error);
  document.querySelector('.el-menu-item').click();await wait(()=>!document.querySelector('iframe'),'iframe cleanup');document.querySelector('.settings-panel footer button').click();
  await wait(()=>document.querySelector('kbd')?.textContent==='F8','restore defaults');const defaults=await call('reff.settings.get');check(defaults.language==='zh-CN'&&defaults.startup.preload&&!defaults.input.keyboardPassthrough&&!defaults.window.geometry,'default reset');
  await call('test.finished',{passed:true,settings:true,persistence:true,geometry:true,themeAndLanguage:true,modifierOnlyCaptureIgnored:true,hotkeys:['Ctrl+Shift+K','Ctrl+K','F24','Ctrl+;'],mainAndFrameInput:true,focusRelease:true,hiddenResume:true,hotReloadDocuments:reports,clipboardAndSystemImeValidated:false});
 }catch(error){await call('test.finished',{passed:false,error:String(error.stack||error),reports});}
})().catch(error=>window.reffNativeCall('test.finished',{passed:false,error:String(error.stack||error)}));
