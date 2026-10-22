// 仅复制到独立夹具的浏览器脚本：操作本测试宿主的 DOM，不读取或操作游戏窗口。
(async()=>{
 const call=(method,params={})=>window.reffNativeCall(method,params);
 const check=(yes,message)=>{if(!yes)throw Error(message);};
 const pause=ms=>new Promise(resolve=>setTimeout(resolve,ms));
 const wait=async predicate=>{for(let i=0;i<200;i++){const value=predicate();if(value)return value;await pause(25);}throw Error('DOM/bridge wait timeout');};
 const denied=async(method,params={})=>{try{await call(method,params);}catch(e){check(e.code==='FORBIDDEN'||e.code==='SUBSCRIPTION_NOT_OWNED',method+': '+e.code);return;}throw Error('permission bypass: '+method);};
 const identity=await call('ui.identity');
 // 网页伪造外层类型或票据应被忽略，不能使原生回调异常或取得其他文档权限。
 chrome.webview.postMessage({kind:4,ticket:[],id:{},request:{}});
 chrome.webview.postMessage({kind:'request',ticket:'forged',id:'forged',request:{method:'reff.settings.reset',params:{}}});
 if(window!==parent){
  try{
   const id=identity.pluginId;
   check(location.origin==='reff://'+id+'.plugin','plugin origin mismatch: '+location.origin);
   check(identity.kind==='plugin'&&identity.pageId.startsWith('f'),'native plugin identity missing');
   const before=await call(id+'.get');check(before.gameName==='mhwilds','real Lua snapshot missing');
   await call(id+'.refresh');
   await denied('reff.settings.get');await denied('ui.plugins');
   await denied(id==='example.html'?'example.react.get':'example.html.get');
   await denied('ui.unsubscribe',{subscriptionId:'another-document'});
   await denied('ui.input.focus',{active:false});
   parent.postMessage({type:'reff:request',id:'forged-parent',method:'reff.settings.reset',params:{}},'reff://shell');
   let parentBlocked=false;try{void parent.document.body;}catch{parentBlocked=true;}check(parentBlocked,'plugin read Shell DOM');
   const shared=await wait(()=>document.querySelector('button:not([disabled]),.el-button:not(.is-disabled)'));
   check(shared&&document.body.textContent.includes('mhwilds'),'original frontend failed SDK/Lua rendering');
   let eventReceived=false;
   if(id==='example.vue'){
    const sid='browser-test';await call('ui.subscribe',{eventName:'example.vue.refreshed',subscriptionId:sid});
    const listener=()=>eventReceived=true;window.addEventListener('reff:'+sid,listener);
    await call('example.vue.refresh');await wait(()=>eventReceived);
    await call('ui.unsubscribe',{subscriptionId:sid});window.removeEventListener('reff:'+sid,listener);
    // 留下一项订阅供父文档移除 iframe 时验证原生清理，重复重载不会冲突。
    await call('ui.subscribe',{eventName:'example.vue.refreshed',subscriptionId:'unload-probe'});
   }
   parent.postMessage({type:'p1-child',passed:true,id,pageId:identity.pageId,eventReceived},'reff://shell');
  }catch(error){parent.postMessage({type:'p1-child',passed:false,error:String(error.stack||error)},'reff://shell');}
  return;
 }
 const results=[];
 try{
  check(identity.kind==='shell'&&identity.pluginId===null,'Shell identity mismatch');
  await wait(()=>document.querySelectorAll('.el-menu-item').length===4);
  const plugins=(await call('ui.plugins')).plugins;
  for(const id of ['example.vue','example.react','example.html','example.vue']){
   const plugin=plugins.find(p=>p.id===id);check(plugin,'missing manifest '+id);
   const done=new Promise((resolve,reject)=>{
    const timer=setTimeout(()=>{removeEventListener('message',listener);reject(Error('child timeout '+id));},8000);
    function listener(event){const frame=document.querySelector('iframe');
     if(event.source!==frame?.contentWindow||event.data?.type!=='p1-child')return;
     clearTimeout(timer);removeEventListener('message',listener);resolve(event.data);}
    addEventListener('message',listener);
   });
   const button=[...document.querySelectorAll('.el-menu-item')].find(el=>el.textContent.includes(plugin.localizedName?.['zh-CN']||plugin.name));
   check(button,'missing plugin menu');button.click();
   const result=await done;check(result.passed,result.error);results.push(result);
  }
  check(results[0].pageId!==results[3].pageId,'document identity reused after unload');
  await call('test.finished',{checkpoint:'reset'});
  const recovered=new Promise((resolve,reject)=>{
   const timer=setTimeout(()=>{removeEventListener('message',listener);reject(Error('reset timeout'));},8000);
   function listener(event){if(event.source!==document.querySelector('iframe')?.contentWindow||event.data?.type!=='p1-child')return;
    clearTimeout(timer);removeEventListener('message',listener);resolve(event.data);}
   addEventListener('message',listener);
  });
  let invalidated=false;
  try{await call('example.vue.refresh');}catch(error){invalidated=error.code==='SCRIPT_RESET';}
  check(invalidated,'in-flight write survived reset');
  const reset=await recovered;check(reset.passed,reset.error);
  check(reset.pageId!==results[3].pageId,'Shell reused pre-reset plugin document');
  check((await call('ui.status')).epoch===2,'new Lua epoch missing');
  document.querySelector('.el-menu-item').click();await wait(()=>!document.querySelector('iframe'));
  // 原生 frame 销毁及取消订阅异步抵达，让驱动核对 Lua 注册表归零。
  await pause(200);
  await call('test.finished',{passed:true,frameworks:results,sourceIsolation:true,unload:true,scriptReset:{passed:true,newDocument:reset.pageId,invalidatedWrite:invalidated,epoch:2}});
 }catch(error){await call('test.finished',{passed:false,frameworks:results,error:String(error.stack||error)});}
})().catch(error=>window.reffNativeCall('test.finished',{passed:false,error:String(error.stack||error)}));
