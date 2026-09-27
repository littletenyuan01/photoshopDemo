/* 演示稿的引擎自检（可选，删掉不影响页面运行）
 *
 * 目的：把 app.js 在 Node 里**真跑一遍**（用最小 DOM 桩件），从而验证：
 *   1) 没有运行时错误：9 个步骤的 run() + 所有 render 都执行一遍；
 *   2) 「惰性分配」成立：透明文档 0 瓦片，画一笔只分配几块；
 *   3) 「只重算脏瓦片」成立：脏瓦片模式下重算块数远小于整幅；
 *   4) 「瓦片越小碰到的块越多」成立；
 *   5) 合成公式正确：白底 + 50% 红 → 预乘结果 (1, 0.5, 0.5, 1)；
 *   6) 历史按区域存，且撤销真的能还原；
 *   7) 暂存盘换页：LRU 换出后内存占用 ≤ 额度。
 *
 * 跑法：node _verify/run_in_node.js
 *      （在本文件所在目录的上级，即 presentations/01-photoshop-image-engine 下执行）
 */
'use strict';
const fs = require('fs');
const path = require('path');
const vm = require('vm');

// ---------------------------------------------------------------- DOM 桩件
function makeCtx() {
  const noop = () => {};
  return new Proxy({
    imageSmoothingEnabled: true,
    putImageData: noop, drawImage: noop, fillRect: noop, strokeRect: noop,
    beginPath: noop, moveTo: noop, lineTo: noop, stroke: noop, fill: noop,
    setLineDash: noop, clearRect: noop,
  }, {
    get(t, k) { return k in t ? t[k] : noop; },
    set(t, k, v) { t[k] = v; return true; },
  });
}

function makeElement(tag) {
  const el = {
    tagName: (tag || 'div').toUpperCase(),
    width: 100, height: 100,
    style: {}, dataset: {},
    value: '', textContent: '', innerHTML: '', title: '',
    onclick: null, oninput: null,
    classList: { toggle: () => {}, add: () => {}, remove: () => {}, contains: () => false },
    getContext: () => makeCtx(),
    appendChild: (c) => c,
    append: () => {},
    addEventListener: () => {},
    removeEventListener: () => {},
    setPointerCapture: () => {},
    getBoundingClientRect: () => ({ left: 0, top: 0, width: 520, height: 380 }),
    querySelector: () => makeElement('input'),
    querySelectorAll: () => [],
    closest: () => null,
  };
  return el;
}

const elementCache = new Map();
global.document = {
  getElementById(id) {
    if (!elementCache.has(id)) elementCache.set(id, makeElement(id.endsWith('Canvas') ? 'canvas' : 'div'));
    return elementCache.get(id);
  },
  createElement: (tag) => makeElement(tag),
  querySelectorAll: () => [],
  addEventListener: () => {},
};
global.window = { addEventListener: () => {} };
global.ImageData = class ImageData {
  constructor(w, h) { this.width = w; this.height = h; this.data = new Uint8ClampedArray(w * h * 4); }
};

// ---------------------------------------------------------------- 跑 app.js + 断言
const appPath = path.join(__dirname, '..', 'app.js');
const app = fs.readFileSync(appPath, 'utf8');

const driver = `
// ================= 验证驱动 =================
let fails = 0;
function check(ok, what) {
  if (ok) console.log('  ok  : ' + what);
  else { console.error('  FAIL: ' + what); fails++; }
}
const approx = (a, b, eps = 0.02) => Math.abs(a - b) <= eps;

console.log('-- 1) 9 个步骤全部执行（任何异常都会让本脚本崩掉）');
for (let i = 0; i < 9; i++) { goStep(i); }
check(true, '9 个步骤的 run() + render 都跑通了');

console.log('-- 2) 惰性分配：透明文档 0 瓦片，画一笔只分配几块');
resetDoc({ w: 512, h: 384, tile: 128, depth: 8 });
addLayer('测试层');
check(state.tiles.size === 0, '新建透明文档：已分配瓦片 = 0（像素内存 0 字节）');
let L = activeLayer();
let r = paintSegment(L, 100, 100, 140, 120, 10, [1, 0, 0], false);
check(r.touched.size > 0 && r.touched.size <= 4, '一笔只触碰 ' + r.touched.size + ' 块瓦片（≤4）');
captured.lazyTiles = r.touched.size;

console.log('-- 3) 只重算脏瓦片 vs 整幅');
state.mode = 'tiles';
state.dirty.clear(); recompute();
const beforeTiles = state.stats.lastRecompTiles;
r = paintSegment(L, 300, 200, 320, 210, 8, [0, 1, 0], false);
recompute();
const dirtyRecomp = state.stats.lastRecompTiles;
state.mode = 'full';
state.dirty.clear(); recompute();
const fullRecomp = state.stats.lastRecompTiles;
state.mode = 'tiles';
check(dirtyRecomp > 0 && dirtyRecomp < fullRecomp,
  '脏瓦片模式重算 ' + dirtyRecomp + ' 块 / 整幅 ' + fullRecomp + ' 块');
captured.dirtyRecomp = dirtyRecomp; captured.fullRecomp = fullRecomp;
void beforeTiles;

console.log('-- 4) 瓦片越小，同一笔碰到的块越多');
resetDoc({ w: 512, h: 384, tile: 256, depth: 8 }); addLayer('t');
const touched256 = paintSegment(activeLayer(), 150, 120, 300, 220, 40, [1, 0, 0], false).touched.size;
resetDoc({ w: 512, h: 384, tile: 64, depth: 8 }); addLayer('t');
const touched64 = paintSegment(activeLayer(), 150, 120, 300, 220, 40, [1, 0, 0], false).touched.size;
check(touched64 > touched256, '边长 64 触碰 ' + touched64 + ' 块 > 边长 256 触碰 ' + touched256 + ' 块');
captured.t64 = touched64; captured.t256 = touched256;

console.log('-- 5) 合成公式（预乘 source-over）');
resetDoc({ w: 256, h: 256, tile: 128, depth: 8 });
const bottom = addLayer('底');
fillLayer(bottom, [1, 1, 1, 1]);                 // 不透明白
const top = addLayer('顶');
fillLayer(top, [1, 0, 0, 0.5]);                  // 50% 红
recompute();
const px = projPixel(10, 10);
check(approx(px[0], 1) && approx(px[1], 0.5) && approx(px[2], 0.5) && approx(px[3], 1),
  '白底 + 50% 红 = [' + px.map(v => v.toFixed(2)).join(', ') + ']（期望 1, 0.5, 0.5, 1）');

console.log('-- 6) 隐藏图层不参与合成，但数据仍在');
top.visible = false; invalidateAllTiles();
const px2 = projPixel(10, 10);
const topTiles = [...state.tiles.values()].filter(t => t.layerId === top.id).length;
check(approx(px2[1], 1) && topTiles > 0,
  '隐藏后合成结果是纯白 [' + px2.map(v => v.toFixed(2)).join(', ') + ']，但该层仍有 ' + topTiles + ' 块瓦片');
top.visible = true; invalidateAllTiles();

console.log('-- 7) 历史记录按区域存（不是整幅），且撤销真的能还原');
state.history = [];
resetDoc({ w: 512, h: 384, tile: 128, depth: 8 }); addLayer('h');
const hLayer = activeLayer();
beginOp();
const hr = paintSegment(hLayer, 60, 60, 90, 80, 12, [0, 0, 1], false);
recompute();
const undoBefore = projPixel(70, 70)[3];
const entry = endOp('测试笔', hLayer);
void entry;
const histEntry = state.history[state.history.length - 1];
check(histEntry.bytes === hr.touched.size * tileBytes(),
  '一步历史 = ' + hr.touched.size + ' 块 × ' + (tileBytes() / 1024) + ' KB = ' + (histEntry.bytes / 1024) + ' KB');
check(histEntry.bytes < state.doc.w * state.doc.h * 4, '远小于整幅快照 ' + (state.doc.w * state.doc.h * 4 / 1024) + ' KB');
undo();
const undoAfter = projPixel(70, 70)[3];
check(undoBefore > 0 && undoAfter < undoBefore, '撤销把该区域还原（alpha ' + undoBefore.toFixed(2) + ' → ' + undoAfter.toFixed(2) + '）');

console.log('-- 8) 暂存盘换页（LRU），内存占用回到额度内');
resetDoc({ w: 1024, h: 768, tile: 128, depth: 8 }); addLayer('大');
state.memBudgetMB = 1;                       // 故意给很小的额度（1 MB）
for (let i = 0; i < 8; i++) paintSegment(activeLayer(), 40 + i * 110, 40, 90 + i * 110, 700, 30, [1, 1, 0], false);
enforceMemoryBudget();
const ramBytes = inRamTiles() * tileBytes();
check(state.stats.pagedOut > 0 && ramBytes <= state.memBudgetMB * 1024 * 1024,
  '换出 ' + state.stats.pagedOut + ' 块后，内存占用 ' + (ramBytes / 1024).toFixed(0) + ' KB ≤ 额度 1 MB（磁盘 ' + diskTiles() + ' 块）');
recompute();
check(state.stats.pagedIn > 0, '重算时把换出的瓦片换入 ' + state.stats.pagedIn + ' 次');

console.log('-- 9) 关键数字（用于写文档）');
captured.allocWhite = state.tiles.size;
console.log('全部断言' + (fails === 0 ? '通过' : ('失败 ' + fails + ' 项')));
if (fails > 0) throw new Error('有断言失败');
`;

global.captured = {};
vm.runInThisContext(app + '\n' + driver, { filename: 'app.js+driver' });
console.log('\n捕获的数字:', JSON.stringify(global.captured, null, 2));
