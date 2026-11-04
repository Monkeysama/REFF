// 只运行在测试拥有的隐藏 WebView；实际 Shell/SDK/身份与 WARP 回读都走生产路径。
(async()=>{
 const call=(method,params={})=>window.reffNativeCall(method,params),pause=ms=>new Promise(r=>setTimeout(r,ms));
 const check=(v,label)=>{if(!v)throw Error(label);};
 const wait=async(fn,label)=>{for(let i=0;i<240;i++){if(fn())return;await pause(25);}throw Error('preview wait '+label);};
 const box=r=>[r.x,r.y,r.width,r.height];
 if(window!==parent){
  let slot;
  addEventListener('message',async event=>{
   if(event.source!==parent||event.data?.type!=='preview-test')return;
   try{
    const args=event.data;
    if(args.action==='init'){
     document.body.innerHTML='<div id="scroll" style="position:absolute;left:20px;top:60px;width:240px;height:180px;overflow:hidden"><div style="height:600px;position:relative"><div id="slot" style="position:absolute;left:0;top:30px;width:240px;height:240px"></div></div></div><div style="position:fixed;left:70px;top:110px;width:30px;height:30px;background:rgb(255,0,0)"></div><div style="position:fixed;left:130px;top:110px;width:30px;height:30px;background:rgba(0,0,255,.5)"></div>';
     document.documentElement.style.background='transparent';document.body.style.cssText='margin:0;background:transparent';
     const module=await import('reff://shell/shared/reff-ui.mjs');
     slot=await module.registerNativePreviewSlot({call},document.getElementById('slot'),'portrait');
     await call('ui.preview.present',{pageId:'forged',slots:[]}).then(()=>{throw Error('iframe projected itself');},e=>check(e.code==='FORBIDDEN','iframe projection blocked'));
     await call('ui.preview.register',{slotId:'../bad'}).then(()=>{throw Error('unsafe slot registered');},e=>check(e.code==='INVALID_ARGUMENT','unsafe slot rejected'));
    }
    if(args.action==='scroll')document.getElementById('scroll').scrollTop=40;
    if(args.action==='corner'){document.getElementById('scroll').style.cssText='position:fixed;right:0;bottom:0;width:240px;height:180px;overflow:hidden';document.getElementById('slot').style.top='0';}
    if(args.action==='uncorner'){document.getElementById('scroll').style.cssText='position:absolute;left:20px;top:60px;width:240px;height:180px;overflow:hidden';document.getElementById('slot').style.top='30px';}
    if(args.action==='zoom')document.body.style.zoom='1.25';
    if(args.action==='hide')document.getElementById('slot').style.display='none';
    if(args.action==='show')document.getElementById('slot').style.display='block';
    if(args.action==='opacity')document.getElementById('slot').style.opacity='.5';
    if(args.action==='rotate')document.getElementById('slot').style.rotate='10deg';
    if(args.action==='round')document.getElementById('scroll').style.borderRadius='16px';
    if(args.action==='restore'){document.getElementById('slot').style.opacity='1';document.getElementById('slot').style.rotate='none';document.getElementById('scroll').style.borderRadius='0';}
    if(args.action==='remove')slot.dispose();
    await pause(240);
    const target=document.getElementById('slot'),r=target?.getBoundingClientRect(),sc=document.getElementById('scroll')?.getBoundingClientRect();
    const clip=r&&sc?[Math.max(r.left,sc.left),Math.max(r.top,sc.top),Math.min(r.right,sc.right)-Math.max(r.left,sc.left),Math.min(r.bottom,sc.bottom)-Math.max(r.top,sc.top)]:[];
    parent.postMessage({type:'preview-test-reply',id:args.id,pageId:(await call('ui.identity')).pageId,rect:r?box(r):[],clip,viewportWidth:innerWidth,viewportHeight:innerHeight},'*');
   }catch(error){parent.postMessage({type:'preview-test-reply',id:event.data.id,error:String(error)},'*');}
  });
  parent.postMessage({type:'preview-test-ready',pageId:(await call('ui.identity')).pageId},'*');return;
 }
 try{
  await wait(()=>document.querySelector('[data-plugin-id="example.vue"]'),'menu');
  let childReady=null;addEventListener('message',e=>{if(e.source===document.querySelector('iframe')?.contentWindow&&e.data?.type==='preview-test-ready')childReady=e.data;});
  const select=async id=>{childReady=null;document.querySelector(`[data-plugin-id="${id}"]`).click();if(id!=='reff.settings')await wait(()=>childReady,'child navigation');await pause(120);};
  await select('example.vue');await wait(()=>document.querySelector('iframe'),'iframe');
  let sequence=0;
  const child=async action=>{const frame=document.querySelector('iframe'),id=++sequence;return new Promise((resolve,reject)=>{
   const listener=e=>{if(e.source!==frame.contentWindow||e.data?.type!=='preview-test-reply'||e.data.id!==id)return;clearTimeout(timeout);removeEventListener('message',listener);e.data.error?reject(Error(e.data.error)):resolve(e.data);};
   const timeout=setTimeout(()=>{removeEventListener('message',listener);reject(Error('preview child timeout '+action));},4500);addEventListener('message',listener);frame.contentWindow.postMessage({type:'preview-test',action,id},'*');});};
  const capture=async(data,options={})=>{
   await pause(500);const frame=document.querySelector('iframe'),r=frame?.getBoundingClientRect();
   const project=b=>[r.left+b[0]*r.width/data.viewportWidth,r.top+b[1]*r.height/data.viewportHeight,b[2]*r.width/data.viewportWidth,b[3]*r.height/data.viewportHeight];
   const rect=data?project(data.rect):[],clip=data?project(data.clip):[];
   const point=(x,y,rgb)=>({x:Math.floor(r.left+x*r.width/data.viewportWidth),y:Math.floor(r.top+y*r.height/data.viewportHeight),rgb});
   const samples=data&&options.count!==0?[point(230,180,options.provider===false?[0,0,0]:[0,255,0])]:[];
   if(data&&options.overlay){samples.push(point(80,120,[255,0,0]),point(140,120,[0,127,128]));}
   const captureId=++sequence;await new Promise((resolve,reject)=>{
    const listener=e=>{if(e.data?.kind==='test-ack'&&e.data.id===captureId){clearTimeout(timer);chrome.webview.removeEventListener('message',listener);resolve();}};
    const timer=setTimeout(()=>{chrome.webview.removeEventListener('message',listener);reject(Error('GPU capture acknowledgement timeout'));},5000);chrome.webview.addEventListener('message',listener);
    call('test.finished',{checkpoint:'preview-capture',rootRadius:getComputedStyle(document.querySelector('.reff-shell')).borderRadius,rootBox:box(document.querySelector('.reff-shell').getBoundingClientRect()),frameBox:r?box(r):null,captureId,count:data?1:0,rect,clip,samples,...options}).catch(reject);
   });
  };
  let data=await child('init');await capture(data,{overlay:true});
  const duplicate={slotId:'portrait',rect:[100,180,100,100],clip:[100,180,100,100]};
  await call('ui.preview.present',{pageId:data.pageId,viewportWidth:innerWidth,viewportHeight:innerHeight,slots:[duplicate,duplicate]}).then(()=>{throw Error('overlapping slots accepted');},e=>check(e.code==='OVERLAPPING_SLOTS','overlap rejected'));
  for(const unsupported of ['opacity','rotate','round']){await child(unsupported);await capture(null);data=await child('restore');await capture(data);}
  // 全局圆角可保留；只有实际进入 Shell 曲线角区的槽位才停画，不能把 iframe 整体当作槽位。
  await child('corner');await capture(null);
  data=await child('uncorner');await capture(data);
  data=await child('scroll');await capture(data,{overlay:true});
  data=await child('zoom');await capture(data);
  await capture(data,{provider:false});await capture(data,{provider:true,rebuild:true});
  // 模拟 Core 在 F8 重开时确认同尺寸新代次，并在另一轮改变视口；只操作自有宿主 IPC。
  await call('test.finished',{checkpoint:'input',messages:[{type:'visible',value:false},{type:'viewport',width:1458,height:818,generation:3},{type:'visible',value:true}]});
  await pause(400);await capture(data);
  await call('test.finished',{checkpoint:'input',messages:[{type:'viewport',width:1280,height:720,generation:4}]});
  await pause(400);data=await child('inspect');await capture(data);
  await child('hide');await capture(null);data=await child('show');await capture(data);
  // 普通页面不需要提供者；切换/返回后旧文档不能重新注册或复用其投影。
  const stale=data;await select('reff.settings');await capture(null);
  await call('ui.preview.present',{pageId:stale.pageId,viewportWidth:innerWidth,viewportHeight:innerHeight,slots:[]}).then(()=>{throw Error('old page accepted');},e=>check(e.code==='PAGE_UNLOADED','old page revoked'));
  await select('example.vue');data=await child('init');check(data.pageId!==stale.pageId,'new document');await capture(data);
  childReady=null;await call('test.finished',{checkpoint:'reset'});
  await call('example.vue.refresh').then(()=>{throw Error('reset write replayed');},e=>check(e.code==='SCRIPT_RESET','reset invalidates in-flight write'));
  await wait(()=>childReady&&childReady.pageId!==data.pageId,'reset new page');data=await child('init');await capture(data);
  await child('remove');await capture(null);
  await call('test.finished',{passed:true,preview:true,metadata:true,clipping:true,occlusion:true,zoom:true,unregister:true,reset:true,reopen:true});
 }catch(error){await call('test.finished',{passed:false,error:String(error),stack:error?.stack});}
})()
