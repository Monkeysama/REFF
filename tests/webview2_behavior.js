// 独立隐藏浏览器夹具：验证生产设置组件、SDK 与 IPC 输入，不操作游戏或系统剪贴板。
(async()=>{
 const call=(method,params={})=>window.reffNativeCall(method,params),pause=ms=>new Promise(r=>setTimeout(r,ms));
 const check=(ok,why)=>{if(!ok)throw Error(why);};
 const wait=async(fn,label)=>{for(let i=0;i<320;i++){const v=fn();if(v)return v;await pause(25);}throw Error('wait: '+label);};
 const batch=messages=>call('test.finished',{checkpoint:'input',messages});
 const key=(action,key,modifiers=0)=>({type:'key',action,key,modifiers});
 let pointerOffset={x:0,y:0};
 // 用生产 IPC 驱动真实浏览器选择/拖动；两份文档各自运行，不启动 OLE 或访问系统剪贴板。
 const pagePolicy=async()=>{
  const fixture=document.createElement('section');fixture.style.cssText='position:fixed;left:20px;top:140px;z-index:10001;width:260px;font:14px/24px monospace;user-select:text';
  fixture.innerHTML='<p>copyable plain text</p><div draggable="true">draggable item</div><div data-reff-no-select>nonselectable label</div><input value="input copy text"><textarea>textarea copy text</textarea><div contenteditable="true">editable copy text</div>';
  for(const node of fixture.children)node.style.cssText='display:block;box-sizing:border-box;height:24px;width:260px;margin:0;padding:0;border:0';
  document.body.append(fixture);
  const [plain,draggable,optOut,textInput,textArea,editable]=fixture.children;
  const point=(node,dx=4)=>{const r=node.getBoundingClientRect();return {type:'mouse',button:0,x:Math.round(r.left+dx+pointerOffset.x),y:Math.round(r.top+r.height/2+pointerOffset.y)};};
  // RPC 回执只说明驱动接受批次，不代表浏览器已派发输入；等待真实 mouseup 后才验证默认行为。
  const drag=async(node,dx=4)=>{const start=point(node,dx);let released=false;const release=()=>released=true;node.addEventListener('mouseup',release);
   try{await batch([{...start,action:'move'},{...start,action:'down',modifiers:16}]);await batch([{...start,action:'move',x:start.x+100,modifiers:16},{...start,action:'up',x:start.x+100}]);await wait(()=>released,'real mouse release after fixture drag');}
   finally{node.removeEventListener('mouseup',release);}
  };
  const originalWarn=console.warn;let warnings=0;const inputTrace=[];
  const trace=event=>inputTrace.push({type:event.type,target:event.target?.parentElement===fixture?[...fixture.children].indexOf(event.target):event.target?.tagName,canceled:event.defaultPrevented,buttons:event.buttons});
  const tracedEvents=['pointerdown','pointermove','pointerup','mousedown','mouseup','dragstart','dragend','selectstart'];for(const type of tracedEvents)addEventListener(type,trace,true);
  try{
   let started=0,prevented=false,trusted=false;
   draggable.addEventListener('dragstart',event=>{started++;prevented=event.defaultPrevented;trusted=event.isTrusted;});
   await drag(draggable);await wait(()=>started>0,'real draggable dragstart');
   check(prevented&&trusted,'document capture must cancel trusted dragstart before native OLE');
   check((await call('ui.identity')).pageId,'bridge remains responsive after canceled native drag');
   getSelection().removeAllRanges();await drag(plain);await wait(()=>getSelection().toString().length>0,'ordinary text selection preserved');
   let selectedTextDrag=false;plain.addEventListener('dragstart',event=>selectedTextDrag=event.defaultPrevented&&event.isTrusted);
   await drag(plain,12);await wait(()=>selectedTextDrag,'selected plain text cannot enter native OLE drag');
   let selectionCanceled=false,pointerMoves=0,pointerCanceled=false;const selectionEvents=[];
   optOut.addEventListener('selectstart',event=>{selectionCanceled=event.defaultPrevented;selectionEvents.push({type:event.type,canceled:event.defaultPrevented,cancelable:event.cancelable,trusted:event.isTrusted,path:event.composedPath().filter(node=>node instanceof Element).map(node=>node.tagName)});});
   optOut.addEventListener('mousedown',event=>selectionEvents.push({type:event.type,canceled:event.defaultPrevented,detail:event.detail,button:event.button}));
   optOut.addEventListener('pointerdown',event=>pointerCanceled||=event.defaultPrevented);
   optOut.addEventListener('pointermove',event=>{pointerMoves++;pointerCanceled||=event.defaultPrevented;});
   const optPoint=point(optOut),hit=document.elementFromPoint(optPoint.x-pointerOffset.x,optPoint.y-pointerOffset.y);
   getSelection().removeAllRanges();await drag(optOut);
   check(selectionCanceled&&getSelection().toString()==='','explicit non-input selection opt-out: '+JSON.stringify({selectionCanceled,text:getSelection().toString(),rangeCount:getSelection().rangeCount,anchor:getSelection().anchorNode?.parentElement?.tagName,active:document.activeElement?.tagName,pointerMoves,pointerCanceled,events:selectionEvents,point:optPoint,hit:hit?.tagName,hitIndex:[...fixture.children].indexOf(hit),trace:inputTrace.slice(-30)}));
   check(pointerMoves>0&&!pointerCanceled,'selection opt-out must preserve pointer-based/native-preview interactions');
   fixture.setAttribute('data-reff-no-select','');
   for(const control of [textInput,textArea,editable]){
    control.focus();await batch([key('down',65,4),key('up',65,4)]);
    await wait(()=>control===editable?getSelection().toString()==='editable copy text':control.selectionEnd-control.selectionStart===control.value.length,'editable selection exception');
    const selectionEvent=new Event('selectstart',{bubbles:true,cancelable:true});control.dispatchEvent(selectionEvent);
    const copyEvent=new Event('copy',{bubbles:true,cancelable:true});control.dispatchEvent(copyEvent);
    check(!selectionEvent.defaultPrevented&&!copyEvent.defaultPrevented,'input/contenteditable select/copy must not be canceled');
    await batch([key('down',90),key('char',122),key('up',90)]);
    await wait(()=>(control===editable?control.textContent:control.value)==='z','editable replacement after selection');
   }
   // 开发告警在每份隔离文档自己的 MutationObserver 中处理动态 select，不依赖父页面跨源扫描。
   console.warn=(...args)=>{if(String(args[0]).startsWith('[REFF] Native <select>'))warnings++;originalWarn.apply(console,args);};
   const select=document.createElement('select');select.innerHTML='<option>choice</option>';fixture.append(select);
   await wait(()=>warnings===1,'dynamic native select warning');select.focus();await pause(30);check(warnings===1,'select warning is once per element');
   const container=document.createElement('div');container.innerHTML='<select><option>dynamic</option></select>';fixture.append(container);
   await wait(()=>warnings===2,'dynamic subtree select warning');
  }finally{for(const type of tracedEvents)removeEventListener(type,trace,true);console.warn=originalWarn;getSelection().removeAllRanges();fixture.remove();}
 };
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
  await pagePolicy();field.focus();
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
  const sliders=[...document.querySelectorAll('.appearance-section .el-slider__button-wrapper')];check(sliders.length===4,'appearance sliders');
  for(const slider of sliders){slider.focus();await batch([key('down',35),key('up',35)]);await pause(120);}
  await wait(()=>getComputedStyle(document.documentElement).getPropertyValue('--reff-text-scale').trim()==='1.4','text scaling');
  const switches=[document.querySelector('.general-section .el-switch'),...document.querySelectorAll('.input-section .el-switch')];check(switches.every(Boolean),'settings switches missing');switches.forEach(e=>e.click());await pause(400);
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
  document.querySelector('.el-menu-item').click();await wait(()=>!document.querySelector('iframe'),'iframe cleanup');await wait(()=>!document.querySelector('.settings-panel footer button')?.disabled,'navigation save completed');document.querySelector('.settings-panel footer button').click();
  await wait(()=>document.querySelector('kbd')?.textContent==='Home','restore defaults');const defaults=await call('reff.settings.get');check(defaults.hotkey.key===36&&defaults.hotkey.modifiers===0&&defaults.language==='zh-CN'&&defaults.startup.preload&&!defaults.input.keyboardPassthrough&&!defaults.window.geometry,'default reset');
  await call('test.finished',{passed:true,settings:true,persistence:true,geometry:true,themeAndLanguage:true,modifierOnlyCaptureIgnored:true,hotkeys:['Ctrl+Shift+K','Ctrl+K','F24','Ctrl+;'],mainAndFrameInput:true,nativeDragSuppressed:true,textSelectionPreserved:true,editableSelectionAndCopyPreserved:true,dynamicSelectWarnings:true,focusRelease:true,hiddenResume:true,hotReloadDocuments:reports,clipboardAndSystemImeValidated:false});
 }catch(error){await call('test.finished',{passed:false,error:String(error.stack||error),reports});}
})().catch(error=>window.reffNativeCall('test.finished',{passed:false,error:String(error.stack||error)}));
