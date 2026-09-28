/* ============================================================================
   图像引擎简明版（交互）

   设计原则：**一步只说一件事**。5 步、3 组控件、右侧只有 3 块面板。
   深挖内容（暂存盘换页 / 缓存级别 / 位深 / 边缘瓦片 / 区域撤销）→ ../02-image-engine-deep/

   底下跑的是真实像素数组：
     · 文档 = 由"格子"组成的稀疏网格（没写过的格子 = 0 字节）
     · 每个图层各有一套自己的格子
     · 你看到的效果 = 第三份数据（合成缓存），自底向上算出来
     · 改动只重算"脏格子"
   ========================================================================== */

'use strict';

const DOC_W = 512, DOC_H = 384;
const SCREEN_W = 280, SCREEN_H = 210;

const state = {
  doc: { w: DOC_W, h: DOC_H, tile: 128, layers: [] },
  layerSeq: 0,
  tiles: new Map(),      // `${layerId}:${tx},${ty}` → Float32Array（预乘 RGBA）
  proj: new Map(),       // `${tx},${ty}` → 合成缓存
  dirty: new Set(),
  lastDirty: new Set(),
  mode: 'dirty',
  tool: 'brush',
  radius: 16,
  color: [0.95, 0.78, 0.22],
  view: { x: 70, y: 50 },
  step: 0,
  useSeq: 0,
  stats: { lastStroke: 0, lastRecomp: 0, painted: 0 },
  strokes: { touched: null },
};

// ---------------------------------------------------------------- 格子（瓦片）
const tilePx = () => state.doc.tile;
const gridCols = () => Math.ceil(state.doc.w / tilePx());
const gridRows = () => Math.ceil(state.doc.h / tilePx());
const totalCells = () => gridCols() * gridRows();
const cellBytes = () => tilePx() * tilePx() * 4;   // 8 位/通道：4 B/px
const tkey = (id, tx, ty) => `${id}:${tx},${ty}`;
const pkey = (tx, ty) => `${tx},${ty}`;
const getTile = (id, tx, ty) => state.tiles.get(tkey(id, tx, ty)) || null;

/** 惰性分配：只有真的往这块写像素，才为它申请内存。 */
function ensureTile(id, tx, ty) {
  const key = tkey(id, tx, ty);
  let t = state.tiles.get(key);
  if (t) { t.lastUse = ++state.useSeq; return t; }
  t = { id, tx, ty, data: new Float32Array(tilePx() * tilePx() * 4), lastUse: ++state.useSeq };
  state.tiles.set(key, t);
  return t;
}

// ---------------------------------------------------------------- 文档 / 图层
function resetDoc(opts) {
  const d = state.doc;
  if (opts) Object.assign(d, opts);
  d.layers = [];
  state.tiles.clear();
  state.proj.clear();
  state.dirty.clear();
  state.lastDirty.clear();
  state.layerSeq = 0;
  state.stats = { lastStroke: 0, lastRecomp: 0, painted: 0 };
  state.view = { x: Math.round(d.w * 0.12), y: Math.round(d.h * 0.12) };
}

function addLayer(name) {
  const d = state.doc;
  const layer = { id: ++state.layerSeq, name: name || ('图层 ' + state.layerSeq), visible: true };
  d.layers.push(layer);            // 顺序表：index 0 = 最底
  d.active = d.layers.length - 1;
  invalidateAll();
  return layer;
}
const activeLayer = () => state.doc.layers[state.doc.active] || null;

// ---------------------------------------------------------------- 画一笔（只碰活动层）
/** 返回本次触及的格子集合；只有活动层的格子被写。 */
function paintSegment(layer, x0, y0, x1, y1, radius, rgb, erase) {
  const d = state.doc, n = tilePx(), touched = new Set();
  const minX = Math.max(0, Math.floor(Math.min(x0, x1) - radius));
  const maxX = Math.min(d.w - 1, Math.ceil(Math.max(x0, x1) + radius));
  const minY = Math.max(0, Math.floor(Math.min(y0, y1) - radius));
  const maxY = Math.min(d.h - 1, Math.ceil(Math.max(y0, y1) + radius));
  if (maxX < minX || maxY < minY) return { touched, px: 0 };

  const steps = Math.max(1, Math.ceil(Math.hypot(x1 - x0, y1 - y0) / Math.max(1, radius / 2)));
  let px = 0;
  for (let s = 0; s <= steps; s++) {
    const t = s / steps;
    const cx = x0 + (x1 - x0) * t, cy = y0 + (y1 - y0) * t;
    for (let y = Math.max(minY, Math.floor(cy - radius)); y <= Math.min(maxY, Math.ceil(cy + radius)); y++) {
      for (let x = Math.max(minX, Math.floor(cx - radius)); x <= Math.min(maxX, Math.ceil(cx + radius)); x++) {
        const dist = Math.hypot(x + 0.5 - cx, y + 0.5 - cy);
        if (dist > radius) continue;
        const a = Math.max(0, Math.min(1, 1 - (dist / radius) * (dist / radius)));
        if (a <= 0) continue;
        const tx = Math.floor(x / n), ty = Math.floor(y / n);
        const tile = ensureTile(layer.id, tx, ty);
        touched.add(tkey(layer.id, tx, ty));
        const i = ((y % n) * n + (x % n)) * 4;
        const s0 = tile.data;
        if (erase) {
          const k = 1 - a;
          s0[i] *= k; s0[i + 1] *= k; s0[i + 2] *= k; s0[i + 3] *= k;
        } else {
          s0[i] = rgb[0] * a + s0[i] * (1 - a);
          s0[i + 1] = rgb[1] * a + s0[i + 1] * (1 - a);
          s0[i + 2] = rgb[2] * a + s0[i + 2] * (1 - a);
          s0[i + 3] = a + s0[i + 3] * (1 - a);
        }
        px++;
      }
    }
  }
  touched.forEach(k => state.dirty.add(pkey(...k.split(':')[1].split(',').map(Number))));
  state.stats.painted = px;
  return { touched, px };
}

// ---------------------------------------------------------------- 合成（第三份数据）
/** 自底向上 source-over（预乘）：out = src + dst × (1 − srcA) */
function compositeTile(tx, ty, upto) {
  const d = state.doc, n = tilePx(), out = new Float32Array(n * n * 4);
  const top = (upto === undefined || upto < 0) ? d.layers.length - 1 : Math.min(upto, d.layers.length - 1);
  for (let ly = 0; ly < n; ly++) {
    const y = ty * n + ly;
    if (y >= d.h) break;
    for (let lx = 0; lx < n; lx++) {
      const x = tx * n + lx;
      if (x >= d.w) break;
      const o = (ly * n + lx) * 4;
      let r = 0, g = 0, b = 0, a = 0;
      for (let li = 0; li <= top; li++) {
        const L = d.layers[li];
        if (!L || !L.visible) continue;
        const t = getTile(L.id, tx, ty);
        if (!t) continue;                       // 未分配 = 全透明
        const i = ((y % n) * n + (x % n)) * 4;
        const sa = t.data[i + 3], inv = 1 - sa;
        r = t.data[i] + r * inv;
        g = t.data[i + 1] + g * inv;
        b = t.data[i + 2] + b * inv;
        a = sa + a * inv;
      }
      out[o] = r; out[o + 1] = g; out[o + 2] = b; out[o + 3] = a;
    }
  }
  state.proj.set(pkey(tx, ty), out);
}

/** 只重算脏格子（或整幅，用于对照）。 */
function recompute() {
  let list;
  if (state.mode === 'full') {
    list = [];
    for (let ty = 0; ty < gridRows(); ty++)
      for (let tx = 0; tx < gridCols(); tx++) list.push(pkey(tx, ty));
  } else {
    list = [...state.dirty];
  }
  list.forEach(k => { const [tx, ty] = k.split(',').map(Number); compositeTile(tx, ty); });
  state.lastDirty = new Set(list);
  state.stats.lastRecomp = list.length;
  state.dirty.clear();
}

function invalidateAll() {
  for (let ty = 0; ty < gridRows(); ty++)
    for (let tx = 0; tx < gridCols(); tx++) state.dirty.add(pkey(tx, ty));
  recompute();
}

const projPixel = (x, y) => {
  const n = tilePx(), t = state.proj.get(pkey(Math.floor(x / n), Math.floor(y / n)));
  if (!t) return [0, 0, 0, 0];
  const i = ((y % n) * n + (x % n)) * 4;
  return [t[i], t[i + 1], t[i + 2], t[i + 3]];
};

// ---------------------------------------------------------------- 画到 canvas
const docCanvas = document.getElementById('docCanvas');
const docCtx = docCanvas.getContext('2d');
const screenCanvas = document.getElementById('screenCanvas');
const screenCtx = screenCanvas.getContext('2d');
const off = document.createElement('canvas');

function docTransform() {
  const d = state.doc, pad = 16;
  const scale = Math.min((docCanvas.width - pad * 2) / d.w, (docCanvas.height - pad * 2) / d.h);
  return { scale, ox: (docCanvas.width - d.w * scale) / 2, oy: (docCanvas.height - d.h * scale) / 2 };
}
const imgToCanvas = (x, y) => { const t = docTransform(); return [t.ox + x * t.scale, t.oy + y * t.scale]; };
const canvasToImg = (x, y) => { const t = docTransform(); return [(x - t.ox) / t.scale, (y - t.oy) / t.scale]; };
const checker = (x, y) => ((Math.floor(x / 8) + Math.floor(y / 8)) % 2 === 0) ? [200, 200, 200] : [138, 138, 138];

/** 预乘 → 直通，再叠到棋盘格上（显示"透明"就是这么做的） */
function putPixel(r, g, b, a, x, y, out, o) {
  const [cr, cg, cb] = checker(x, y);
  out[o] = Math.round((r + (cr / 255) * (1 - a)) * 255);
  out[o + 1] = Math.round((g + (cg / 255) * (1 - a)) * 255);
  out[o + 2] = Math.round((b + (cb / 255) * (1 - a)) * 255);
  out[o + 3] = 255;
}

function paintDoc() {
  const d = state.doc, t = docTransform();
  docCtx.imageSmoothingEnabled = false;
  docCtx.fillStyle = '#1e1e1e';
  docCtx.fillRect(0, 0, docCanvas.width, docCanvas.height);

  const img = new ImageData(d.w, d.h);
  for (let y = 0; y < d.h; y++)
    for (let x = 0; x < d.w; x++) {
      const [r, g, b, a] = projPixel(x, y);
      putPixel(r, g, b, a, x, y, img.data, (y * d.w + x) * 4);
    }
  off.width = d.w; off.height = d.h;
  off.getContext('2d').putImageData(img, 0, 0);
  docCtx.drawImage(off, t.ox, t.oy, d.w * t.scale, d.h * t.scale);

  // 格子：已分配（实色）/ 未分配（虚线）/ 刚改过（红框）/ 视口要读的（青框）
  // 【要点】格子锚在图像坐标上，不随视口移动；视口与哪些格子相交，就"要读"哪些格子。
  const n = tilePx(), layer = activeLayer(), v = viewRect();
  const needCols = [Math.floor(v.x0 / n), Math.floor(v.x1 / n)];
  const needRows = [Math.floor(v.y0 / n), Math.floor(v.y1 / n)];
  for (let ty = 0; ty < gridRows(); ty++) {
    for (let tx = 0; tx < gridCols(); tx++) {
      const [cx, cy] = imgToCanvas(tx * n, ty * n);
      const w = Math.min(n, d.w - tx * n) * t.scale, h = Math.min(n, d.h - ty * n) * t.scale;
      const tile = layer ? getTile(layer.id, tx, ty) : null;
      if (tile) {
        docCtx.fillStyle = 'rgba(74,144,217,.14)';
        docCtx.fillRect(cx, cy, w, h);
        docCtx.strokeStyle = 'rgba(130,170,210,.6)';
        docCtx.setLineDash([]);
      } else {
        docCtx.strokeStyle = 'rgba(150,150,150,.35)';
        docCtx.setLineDash([5, 5]);
      }
      docCtx.lineWidth = 1;
      docCtx.strokeRect(cx + .5, cy + .5, w - 1, h - 1);
      docCtx.setLineDash([]);

      // 视口要读的格子（青色虚线）
      const inNeed = tx >= needCols[0] && tx <= needCols[1] && ty >= needRows[0] && ty <= needRows[1];
      if (inNeed) {
        docCtx.strokeStyle = 'rgba(46,230,214,.85)';
        docCtx.setLineDash([3, 3]);
        docCtx.lineWidth = 2;
        docCtx.strokeRect(cx + 2, cy + 2, w - 4, h - 4);
        docCtx.setLineDash([]);
      }
      if (state.lastDirty.has(pkey(tx, ty))) {
        docCtx.strokeStyle = '#ff5c5c';
        docCtx.lineWidth = 2;
        docCtx.strokeRect(cx + 1, cy + 1, w - 2, h - 2);
      }
    }
  }

  // 视口（复用上面已经取好的 v）
  const [vx, vy] = imgToCanvas(v.x0, v.y0);
  const [vx2, vy2] = imgToCanvas(v.x1 + 1, v.y1 + 1);
  docCtx.strokeStyle = '#4a90d9';
  docCtx.lineWidth = 2;
  docCtx.strokeRect(vx + 1, vy + 1, vx2 - vx - 2, vy2 - vy - 2);

  docCtx.strokeStyle = '#000';
  docCtx.lineWidth = 1;
  docCtx.strokeRect(t.ox - .5, t.oy - .5, d.w * t.scale + 1, d.h * t.scale + 1);
}

/** 视口读数：把"格子锚在图像坐标、与视口无关"这件事用数字说清。 */
function updateViewInfo() {
  const el = document.getElementById('viewInfo');
  if (!el) return;
  const v = viewRect(), n = tilePx();
  const cols = Math.floor(v.x1 / n) - Math.floor(v.x0 / n) + 1;
  const rows = Math.floor(v.y1 / n) - Math.floor(v.y0 / n) + 1;
  const aligned = (v.x0 % n === 0 && v.y0 % n === 0);
  el.innerHTML = `视口左上角 = 图像 <b>(${v.x0}, ${v.y0})</b> → 落在格子 `
    + `(${Math.floor(v.x0 / n)}, ${Math.floor(v.y0 / n)}) 内部，块内偏移 <b>(${v.x0 % n}, ${v.y0 % n})</b>`
    + ` · 这一屏要读 <b>${cols} × ${rows} = ${cols * rows}</b> 个格子（青色虚线，含边缘只露出一部分的格子）`
    + (aligned
      ? ' · <span style="color:#8fe0b0">此刻视口边刚好压在格子线上（因为坐标是粒度的整数倍）</span>'
      : ' · <span style="color:#ffd479">此刻视口边落在格子中间，与格子线不重合</span>');
}

function viewRect() {
  const d = state.doc;
  const vw = Math.min(d.w, SCREEN_W), vh = Math.min(d.h, SCREEN_H);
  const x0 = Math.max(0, Math.min(d.w - vw, Math.round(state.view.x)));
  const y0 = Math.max(0, Math.min(d.h - vh, Math.round(state.view.y)));
  return { x0, y0, x1: x0 + vw - 1, y1: y0 + vh - 1 };
}

function paintScreen() {
  const d = state.doc, v = viewRect();
  const w = SCREEN_W, h = SCREEN_H, img = new ImageData(w, h);
  for (let sy = 0; sy < h; sy++)
    for (let sx = 0; sx < w; sx++) {
      const x = Math.min(d.w - 1, v.x0 + sx), y = Math.min(d.h - 1, v.y0 + sy);
      const [r, g, b, a] = projPixel(x, y);
      putPixel(r, g, b, a, x, y, img.data, (sy * w + sx) * 4);
    }
  off.width = w; off.height = h;
  off.getContext('2d').putImageData(img, 0, 0);
  screenCtx.imageSmoothingEnabled = false;
  screenCtx.drawImage(off, 0, 0, screenCanvas.width, screenCanvas.height);
}

// ---------------------------------------------------------------- 图层 / 数字
function layerThumb(layer, canvas) {
  canvas.width = state.doc.w; canvas.height = state.doc.h;
  const img = new ImageData(state.doc.w, state.doc.h), n = tilePx();
  for (let ty = 0; ty < gridRows(); ty++)
    for (let tx = 0; tx < gridCols(); tx++) {
      const tile = getTile(layer.id, tx, ty);
      for (let y = ty * n; y < Math.min(state.doc.h, (ty + 1) * n); y++)
        for (let x = tx * n; x < Math.min(state.doc.w, (tx + 1) * n); x++) {
          const o = (y * state.doc.w + x) * 4;
          if (!tile) { const [cr, cg, cb] = checker(x, y); img.data[o] = cr; img.data[o + 1] = cg; img.data[o + 2] = cb; img.data[o + 3] = 255; continue; }
          const i = ((y % n) * n + (x % n)) * 4;
          putPixel(tile.data[i], tile.data[i + 1], tile.data[i + 2], tile.data[i + 3], x, y, img.data, o);
        }
    }
  canvas.getContext('2d').putImageData(img, 0, 0);
}

function renderLayers() {
  const d = state.doc, host = document.getElementById('layerList'), all = [...state.tiles.values()];
  host.innerHTML = '';
  for (let i = d.layers.length - 1; i >= 0; i--) {
    const L = d.layers[i];
    const count = all.filter(t => t.id === L.id).length;
    const row = document.createElement('div');
    row.className = 'layer' + (i === d.active ? ' on' : '');
    row.innerHTML = `<canvas style="width:40px;height:30px"></canvas>
      <div><div class="nm">${L.name}</div>
      <div class="mt">格子 ${count} / ${totalCells()} · ${(count * cellBytes() / 1024).toFixed(0)} KB</div></div>`;
    layerThumb(L, row.querySelector('canvas'));
    row.onclick = () => { d.active = i; renderAll(); };
    host.appendChild(row);
  }
}

function renderStats() {
  const d = state.doc, alloc = state.tiles.size, total = totalCells();
  const lines = [
    ['文档', `${d.w} × ${d.h} px · 格子 ${tilePx()}×${tilePx()} → ${gridCols()} × ${gridRows()} = ${total} 格`],
    ['已分配格子', `${alloc} / ${total}（${(alloc * cellBytes() / 1024).toFixed(0)} KB；整幅全分配要 ${(d.w * d.h * 4 / 1024).toFixed(0)} KB）`],
    ['上次重算', `${state.stats.lastRecomp} 格（整幅是 ${total} 格）· 这一笔改了 ${state.stats.painted.toLocaleString()} 像素`],
  ];
  document.getElementById('stats').innerHTML =
    lines.map(([k, v]) => `<div class="stat"><span class="k">${k}</span><span class="v">${v}</span></div>`).join('');
}

// ---------------------------------------------------------------- 5 个步骤
const STEPS = [
  {
    t: '切成格子',
    title: '① 一张图不是一整块，是切成格子',
    run() {
      resetDoc({ tile: state.doc.tile });
      addLayer('图层 1');
      recompute(); renderAll();
    },
    actions: [
      ['画一笔示范', () => drawDemoStrokes(1)],
      ['视口对齐格子角 (128,128)', () => { state.view = { x: 128, y: 128 }; paintAll(); }],
      ['错位 1px 看边缘格子', () => { state.view = { x: 129, y: 129 }; paintAll(); }],
      ['视口挪到 2×2 格区', () => { state.view = { x: 300, y: 200 }; paintAll(); }],
    ],
    html: `<h3>切成固定大小的格子</h3>
      <ul>
        <li>粒度 <span class="k">128×128</span>[共识]；格子数 = <span class="mono">⌈宽/128⌉ × ⌈高/128⌉</span>
          —— 本页 512×384，**默认粒度下是 4 × 3 = 12 格**。</li>
        <li><b>格子锚在图像原点 (0,0)</b>：第 (0,0) 格永远是图像 [0,128) × [0,128)，
          <b>不随视口移动</b>。视口（蓝框）只是从图像上开的一扇窗。</li>
        <li>所以视口左上角通常<b>落在某个格子内部</b>（看蓝框下方的"块内偏移"读数）；
          只有滚到 128 的整数倍，窗边才和格子线重合 —— 点下面两个按钮对比。</li>
        <li>一屏<b>要读</b>的格子 = 与视口相交的那些（青框），含左右/上下只露出一部分的边缘格子；
          视口外有数据的格子依然存在（否则一滚动就得重新分配）。</li>
      </ul>
      <p>对照现实：粒度改成 64 或 256 试试，同一笔会碰到更多/更少的格子。</p>`
  },
  {
    t: '只画过才有',
    title: '② 只画过的地方才有格子（稀疏）',
    run() { drawDemoStrokes(2); },
    actions: [['再画两笔', () => drawDemoStrokes(2)]],
    html: `<h3>没写过 = 0 字节</h3>
      <ul>
        <li>刚才两笔只让少数格子变成"已分配"，其余格子<b>一个字节都不占</b>。</li>
        <li>所以超小图层、超大画布都能存在 —— 这也是"为什么不是一次 malloc 整幅"的答案。</li>
        <li>数字面板第二行直接在数：<b>已分配 X / ${totalCells()} 格</b>。</li>
      </ul>
      <p>对照现实：新建透明文档时是 <b>0 格</b>；选"白底"新建会把整层写满 → 全部格子被分配。</p>`
  },
  {
    t: '每层一套',
    title: '③ 每个图层各自一套格子',
    run() {
      if (state.doc.layers.length < 2) {
        addLayer('图层 2');
        const L = activeLayer();
        paintSegment(L, 320, 90, 430, 250, 34, [0.95, 0.35, 0.35], false);
        state.stats.lastStroke = 0;
        recompute();
      }
      renderAll();
    },
    actions: [['在新图层上画', () => {
      addLayer(null);
      const L = activeLayer();
      paintSegment(L, 120, 250, 360, 300, 26, [0.35, 0.95, 0.55], false);
      recompute(); renderAll();
    }]],
    html: `<h3>图层是"并排的几套格子"，不是"叠起来的数组"</h3>
      <ul>
        <li>新建图层只多一条图层记录，格子<b>从零开始</b>（看右栏每层的"格子 x / 12"）。</li>
        <li>点某层 = 设为活动层，画笔只改<b>它自己</b>的格子；其他层一个字节都不动。</li>
        <li>隐藏图层（02 深入版里有开关）<b>照样占存储</b>，只是不参与合成。</li>
      </ul>
      <p>对照现实：图层可以比画布小、可以带偏移；图层组才是树形。</p>`
  },
  {
    t: '叠起来算',
    title: '④ 你看到的 = 叠起来后算出的第三份数据',
    run() { animateComposite(); },
    actions: [['重播叠加动画', () => animateComposite()]],
    html: `<h3>合成：自底向上逐格算</h3>
      <ul>
        <li>把可见图层按顺序叠加，写进<b>第三份数据</b>（合成缓存）：<br>
          <span class="mono">out = src + dst × (1 − srcA)</span></li>
        <li>它既不是图层像素，也不是屏幕 —— 而是一份<b>缓存</b>，随时能重算出来。</li>
        <li>所以改不透明度要整幅重画，而画一笔只影响一小块。</li>
      </ul>
      <p>对照现实：[源码] GIMP 里这份东西叫 <code>GimpProjection</code>，按格子惰性渲染。</p>`
  },
  {
    t: '只重算一小块',
    title: '⑤ 改一小块，只重算那一小块',
    run() {
      state.mode = 'dirty'; syncButtons();
      const L = activeLayer();
      paintSegment(L, 60, 60, 200, 90, 20, [0.6, 0.8, 1.0], false);
      state.stats.lastStroke = 0;
      recompute(); renderAll();
    },
    actions: [['整幅重算一次（看差别）', () => {
      state.mode = 'full'; syncButtons(); recompute(); renderAll();
    }]],
    html: `<h3>脏格子</h3>
      <ul>
        <li>画笔只让笔迹覆盖的格子变"脏"→ 只重算这几格（红框就是刚重算的）。</li>
        <li>切到"整幅重算"再画一笔对比：数字面板第三行会从个位数跳到 <b>全部格子数</b>（默认粒度下 12）。</li>
        <li>真实产品还会按粒度取整 + 只算可见的格子（02 深入版有视口与缓存级别）。</li>
      </ul>
      <p>对照现实：这就是"拖动很流畅"的根本原因 —— 一帧只算几格。</p>`
  },
];

// ---------------------------------------------------------------- 演示动作
function drawDemoStrokes(n) {
  const L = activeLayer();
  if (!L) return;
  const spots = [
    [90, 110, 230, 130], [340, 150, 430, 260],
    [120, 260, 300, 300], [200, 80, 260, 220],
  ];
  for (let i = 0; i < n; i++) {
    const [x0, y0, x1, y1] = spots[i % spots.length];
    const r = paintSegment(L, x0, y0, x1, y1, state.radius,
                           [0.95 - i * 0.15, 0.7 + i * 0.1, 0.25 + i * 0.2], false);
    state.stats.lastStroke = r.touched.size;
  }
  recompute();
  renderAll();
}

let compositeTimer = null;
function animateComposite() {
  if (compositeTimer) clearInterval(compositeTimer);
  const d = state.doc;
  if (d.layers.length === 0) return;
  let upto = -1;
  const hint = document.getElementById('docHint');
  compositeTimer = setInterval(() => {
    upto++;
    if (upto >= d.layers.length) {
      clearInterval(compositeTimer);
      compositeTimer = null;
      recompute();
      hint.textContent = '合成完成：这就是"叠起来"的结果（第三份数据）。';
      paintAll();
      return;
    }
    for (let ty = 0; ty < gridRows(); ty++)
      for (let tx = 0; tx < gridCols(); tx++) compositeTile(tx, ty, upto);
    hint.textContent = `正在自底向上叠加：已叠到第 ${upto + 1} / ${d.layers.length} 层（${d.layers[upto].name}）`;
    paintAll();
  }, 550);
}

// ---------------------------------------------------------------- 交互
let dragging = null;
const canvasPos = (ev, c) => {
  const r = c.getBoundingClientRect();
  return [(ev.clientX - r.left) * (c.width / r.width), (ev.clientY - r.top) * (c.height / r.height)];
};

docCanvas.addEventListener('pointerdown', (ev) => {
  const [cx, cy] = canvasPos(ev, docCanvas);
  const [ix, iy] = canvasToImg(cx, cy);
  docCanvas.setPointerCapture(ev.pointerId);
  if (ev.shiftKey) { dragging = { view: true, dx: ix - state.view.x, dy: iy - state.view.y }; return; }
  const L = activeLayer();
  if (!L) return;
  dragging = { last: [ix, iy] };
  paintSegment(L, ix, iy, ix, iy, state.radius, state.color, state.tool === 'eraser');
  recompute(); renderAll();
});

docCanvas.addEventListener('pointermove', (ev) => {
  if (!dragging) return;
  const [cx, cy] = canvasPos(ev, docCanvas);
  const [ix, iy] = canvasToImg(cx, cy);
  if (dragging.view) {
    state.view = { x: ix - dragging.dx, y: iy - dragging.dy };
    paintDoc(); paintScreen();
    return;
  }
  const L = activeLayer();
  if (!L) return;
  paintSegment(L, dragging.last[0], dragging.last[1], ix, iy, state.radius, state.color,
               state.tool === 'eraser');
  dragging.last = [ix, iy];
  recompute(); renderAll();
});

docCanvas.addEventListener('pointerup', () => { dragging = null; renderAll(); });

function syncButtons() {
  document.querySelectorAll('#toolRow button').forEach(b => b.classList.toggle('on', b.dataset.tool === state.tool));
  document.querySelectorAll('#tileRow button').forEach(b => b.classList.toggle('on', +b.dataset.tile === state.doc.tile));
  document.querySelectorAll('#modeRow button').forEach(b => b.classList.toggle('on', b.dataset.mode === state.mode));
}
document.getElementById('toolRow').addEventListener('click', e => {
  const b = e.target.closest('button'); if (!b) return;
  state.tool = b.dataset.tool; syncButtons();
});
document.getElementById('tileRow').addEventListener('click', e => {
  const b = e.target.closest('button'); if (!b) return;
  resetDoc({ tile: +b.dataset.tile });
  addLayer('图层 1');
  drawDemoStrokes(1);
  syncButtons(); renderAll();
});
document.getElementById('modeRow').addEventListener('click', e => {
  const b = e.target.closest('button'); if (!b) return;
  state.mode = b.dataset.mode; syncButtons(); invalidateAll(); renderAll();
});
document.getElementById('btnAddLayer').onclick = () => { addLayer(null); renderAll(); };
document.getElementById('btnDelLayer').onclick = () => {
  const d = state.doc;
  if (d.layers.length <= 1) return;
  const L = d.layers[d.active];
  [...state.tiles.keys()].forEach(k => { if (k.startsWith(L.id + ':')) state.tiles.delete(k); });
  d.layers.splice(d.active, 1);
  d.active = Math.max(0, Math.min(d.active, d.layers.length - 1));
  invalidateAll(); renderAll();
};
document.getElementById('btnPrev').onclick = () => goStep(state.step - 1);
document.getElementById('btnNext').onclick = () => goStep(state.step + 1);
window.addEventListener('keydown', e => {
  if (e.key === 'ArrowRight') goStep(state.step + 1);
  if (e.key === 'ArrowLeft') goStep(state.step - 1);
});

// ---------------------------------------------------------------- 渲染与步骤
function paintAll() { paintDoc(); paintScreen(); updateViewInfo(); }
function renderAll() { renderLayers(); renderStats(); paintAll(); }

function renderStepStrip() {
  const host = document.getElementById('stepStrip');
  host.innerHTML = STEPS.map((s, i) =>
    `<div class="step ${i === state.step ? 'on' : ''}" data-i="${i}">
       <span class="n">${i + 1}</span>${s.t}</div>`).join('');
  host.querySelectorAll('.step').forEach(el => { el.onclick = () => goStep(+el.dataset.i); });
}

function goStep(i) {
  if (i < 0 || i >= STEPS.length) return;
  state.step = i;
  renderStepStrip();
  const s = STEPS[i];
  document.getElementById('stepTitle').textContent = s.title;
  document.getElementById('explain').innerHTML = s.html;
  const acts = document.getElementById('stepActions');
  acts.innerHTML = '';
  (s.actions || []).forEach(([label, fn]) => {
    const b = document.createElement('button');
    b.textContent = label;
    b.onclick = fn;
    acts.appendChild(b);
  });
  s.run();
  renderAll();
}

// 启动
resetDoc({});
addLayer('图层 1');
drawDemoStrokes(1);
syncButtons();
goStep(0);
