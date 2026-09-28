/* 简明版自检（可选，删掉不影响页面）
 *
 * 用最小 DOM 桩件把 app.js 真跑一遍，验证页面声称的几条：
 *   1) 5 个步骤的 run() + 渲染都能跑通（有异常这里就崩）；
 *   2) 稀疏：新建透明文档 0 格，画一笔只分配几格；
 *   3) 只重算脏格子：脏模式下重算格数远小于整幅；
 *   4) 粒度越小，同一笔碰到的格子越多；
 *   5) 合成公式：白底 + 50% 红 = [1, 0.5, 0.5, 1]（预乘 source-over）；
 *   6) 图层各自一套格子：在 A 层画不影响 B 层。
 *
 * 跑法：node _verify/run_in_node.js
 */
'use strict';
const fs = require('fs');
const path = require('path');
const vm = require('vm');

// ---------------------------------------------------------------- DOM 桩件
const noop = () => {};
function makeCtx() {
  return new Proxy({ imageSmoothingEnabled: true }, {
    get(t, k) { return k in t ? t[k] : noop; },
    set(t, k, v) { t[k] = v; return true; },
  });
}
function makeEl(tag) {
  return {
    tagName: (tag || 'div').toUpperCase(), width: 100, height: 100,
    style: {}, dataset: {}, value: '', textContent: '', innerHTML: '',
    classList: { toggle: noop, add: noop, remove: noop, contains: () => false },
    getContext: () => makeCtx(), appendChild: c => c, append: noop,
    addEventListener: noop, setPointerCapture: noop,
    getBoundingClientRect: () => ({ left: 0, top: 0, width: 560, height: 420 }),
    querySelector: () => makeEl('canvas'), querySelectorAll: () => [],
    closest: () => null,
  };
}
const cache = new Map();
global.document = {
  getElementById(id) {
    if (!cache.has(id)) cache.set(id, makeEl(id.endsWith('Canvas') ? 'canvas' : 'div'));
    return cache.get(id);
  },
  createElement: t => makeEl(t),
  querySelectorAll: () => [],
  addEventListener: noop,
};
global.window = { addEventListener: noop };
global.ImageData = class ImageData {
  constructor(w, h) { this.width = w; this.height = h; this.data = new Uint8ClampedArray(w * h * 4); }
};
// 动画用的定时器会阻止 Node 退出，这里直接禁用（动画不是本自检的目标）
global.setInterval = () => 0;
global.clearInterval = noop;

// ---------------------------------------------------------------- 跑 + 断言
const app = fs.readFileSync(path.join(__dirname, '..', 'app.js'), 'utf8');
const driver = `
let fails = 0;
function check(ok, what) {
  if (ok) console.log('  ok  : ' + what);
  else { console.error('  FAIL: ' + what); fails++; }
}
const near = (a, b, eps = 0.02) => Math.abs(a - b) <= eps;

console.log('-- 1) 5 个步骤全部执行（异常会直接崩掉本脚本）');
for (let i = 0; i < 5; i++) goStep(i);
check(true, '5 个步骤的 run() + 渲染都跑通');

console.log('-- 2) 稀疏：透明文档 0 格，画一笔只分配几格');
resetDoc({ tile: 128 }); addLayer('t');
check(state.tiles.size === 0, '新建透明文档：已分配格子 = 0');
const r1 = paintSegment(activeLayer(), 100, 100, 140, 120, 10, [1, 0, 0], false);
check(r1.touched.size > 0 && r1.touched.size <= 4, '一笔只碰 ' + r1.touched.size + ' 格（≤4）');

console.log('-- 3) 只重算脏格子 vs 整幅');
state.mode = 'tiles'; state.dirty.clear(); recompute();
const r2 = paintSegment(activeLayer(), 300, 200, 320, 210, 8, [0, 1, 0], false);
recompute();
const dirtyN = state.stats.lastRecomp;
state.mode = 'full'; state.dirty.clear(); recompute();
const fullN = state.stats.lastRecomp;
state.mode = 'dirty';
check(dirtyN > 0 && dirtyN < fullN, '脏格子重算 ' + dirtyN + ' 格 / 整幅 ' + fullN + ' 格');

console.log('-- 4) 粒度越小，碰到的格子越多');
resetDoc({ tile: 256 }); addLayer('t');
const t256 = paintSegment(activeLayer(), 150, 120, 300, 220, 40, [1, 0, 0], false).touched.size;
resetDoc({ tile: 64 }); addLayer('t');
const t64 = paintSegment(activeLayer(), 150, 120, 300, 220, 40, [1, 0, 0], false).touched.size;
check(t64 > t256, '粒度 64 碰 ' + t64 + ' 格 > 粒度 256 碰 ' + t256 + ' 格');

console.log('-- 5) 合成公式（预乘 source-over）');
resetDoc({ tile: 128 });
const bottom = addLayer('底');
for (let ty = 0; ty < gridRows(); ty++) for (let tx = 0; tx < gridCols(); tx++) {
  const t = ensureTile(bottom.id, tx, ty); t.data.fill(0); 
  for (let i = 0; i < t.data.length; i += 4) { t.data[i] = 1; t.data[i+1] = 1; t.data[i+2] = 1; t.data[i+3] = 1; }
  state.dirty.add(pkey(tx, ty));
}
const top = addLayer('顶');
for (let ty = 0; ty < gridRows(); ty++) for (let tx = 0; tx < gridCols(); tx++) {
  const t = ensureTile(top.id, tx, ty); t.data.fill(0);
  for (let i = 0; i < t.data.length; i += 4) { t.data[i] = 0.5; t.data[i+3] = 0.5; }
  state.dirty.add(pkey(tx, ty));
}
recompute();
const px = projPixel(10, 10);
check(near(px[0], 1) && near(px[1], 0.5) && near(px[2], 0.5) && near(px[3], 1),
  '白底 + 50% 红 = [' + px.map(v => v.toFixed(2)).join(', ') + ']（期望 1, 0.5, 0.5, 1）');

console.log('-- 6) 图层各自一套格子：在 A 层画不影响 B 层');
resetDoc({ tile: 128 });
const A = addLayer('A'), B = addLayer('B');
paintSegment(A, 50, 50, 80, 80, 10, [1, 0, 0], false);
const tilesOfB = [...state.tiles.values()].filter(t => t.id === B.id).length;
const tilesOfA = [...state.tiles.values()].filter(t => t.id === A.id).length;
check(tilesOfA > 0 && tilesOfB === 0, 'A 层分配 ' + tilesOfA + ' 格，B 层仍是 ' + tilesOfB + ' 格');

console.log('-- 7) 格子锚在图像坐标，与视口无关');
resetDoc({ tile: 128 }); addLayer('t');
state.view = { x: 70, y: 50 };
let v = viewRect();
check(v.x0 === 70 && v.x0 % 128 === 70,
  '视口 (70,50) 落在格子 (0,0) 内部：块内偏移 ' + (v.x0 % 128) + '（不与格子线重合）');
const cols = Math.floor(v.x1 / 128) - Math.floor(v.x0 / 128) + 1;
const rows = Math.floor(v.y1 / 128) - Math.floor(v.y0 / 128) + 1;
check(cols === 3 && rows === 3, '这一屏要读 ' + cols + ' × ' + rows + ' = ' + (cols * rows) + ' 个格子（含边缘局部格子）');
paintSegment(activeLayer(), 400, 300, 430, 330, 10, [1, 0, 0], false);
const vt = { c: Math.floor(v.x1 / 128), r: Math.floor(v.y1 / 128) };
const allOutside = [...state.tiles.keys()].every(k => {
  const [tx, ty] = k.split(':')[1].split(',').map(Number);
  return tx > vt.c || ty > vt.r;
});
check(allOutside, '在视口外画的格子照样被分配 —— 格子不随视口存在');
state.view = { x: 128, y: 128 };
v = viewRect();
check(v.x0 % 128 === 0 && v.y0 % 128 === 0, '滚到 (128,128) 时视口边才与格子线重合');

console.log(fails === 0 ? '全部断言通过' : ('失败 ' + fails + ' 项'));
if (fails > 0) throw new Error('有断言失败');
`;
vm.runInThisContext(app + '\n' + driver, { filename: 'app.js+driver' });
