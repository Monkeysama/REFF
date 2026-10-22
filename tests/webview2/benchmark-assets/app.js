// 独立宿主使用固定动画负载；不连接业务桥、不访问网络或游戏对象。
let count = 0;
function tick() {
  document.querySelector('#live').textContent = `${innerWidth} × ${innerHeight} · frame ${++count}`;
  requestAnimationFrame(tick);
}
tick();
