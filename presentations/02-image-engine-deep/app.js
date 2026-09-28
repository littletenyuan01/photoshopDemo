/* ============================================================================
   Photoshop 图像引擎演示（交互稿）

   这个页面把 Photoshop 的**分块（瓦片）架构**用一个真实可跑的简化模型重现出来：
     · 文档 = 元数据 + 由瓦片组成的稀疏二维网格（未写过的瓦片 = 0 字节）
     · 每个图层**各有一套自己的瓦片网格**（互不嵌套）
     · 屏幕合成（composite）也是一套**瓦片缓存**，只重算脏瓦片
     · 历史记录只保存"被改动的瓦片"的旧内容（不是整幅）
     · 内存额度用完 → 瓦片换出到暂存盘（Scratch Disk），访问时再换入

   【诚实说明】
     1) 颜色运算内部统一用 float32（便于看清数值）；页面上算的"存储字节"才按
        位深计：8 位/通道 = 4 B/px，16 位/通道 = 8 B/px。
     2) PS 内部实现未公开文档化，本页按"官方概念 + 业界共识"重现，逐条标了
        [官方]/[共识]/[源码 GIMP]/[模拟]。数字都是当场算的，不是抄来的。
   ========================================================================== */

'use strict';

const SCREEN_W = 260, SCREEN_H = 195;
const DOC_W = 512, DOC_H = 384;

const state = {
  doc: { w: DOC_W, h: DOC_H, tile: 128, depth: 8, layers: [] },
  layerSeq: 0,
  tiles: new Map(),        // `${layerId}:${tx},${ty}` → 瓦片记录
  proj: new Map(),         // `${tx},${ty}` → 屏幕合成缓存（L0）
  dirty: new Set(),        // 待重算的投影瓦片
  lastDirty: new Set(),    // 上一次重算过的（高亮用）
  history: [],
  stats: {
    alloc: 0, pagedOut: 0, pagedIn: 0,
    lastRecompTiles: 0, lastUndoBytes: 0, lastUndoFullBytes: 0,
    lastPaintedPx: 0, lastTouchedLayers: 0,
  },
  memBudgetMB: 120,
  mode: 'tiles',
  zoom: 1,
  view: { x: 60, y: 40 },
  tool: 'brush',
  radius: 14,
  color: [0.91, 0.76, 0.23],
  useSeq: 0,
  inspector: null,
  step: 0,
  lastStrokeTiles: 0,
  strokes: { touched: null },
};

/** 当前"一次操作"（一笔 / 一次脚本动作）的现场：被触碰瓦片的**旧内容**快照。
    历史记录必须用这份旧内容 —— 否则存的是"画完以后"的数据，撤销等于没撤。 */
state.op = null;

/** 开始一次操作：之后被写到的瓦片会先把旧内容存进 saved。 */
function beginOp() { state.op = { saved: new Map() }; }

/** 结束一次操作并记进历史（存的是**操作前**的瓦片内容）。 */
function endOp(label, layer) {
  if (state.op && state.op.saved.size) pushHistory(label, layer, state.op.saved);
  state.op = null;
}

// ------------------------------------------------------------------ 瓦片基础
const tilePx = () => state.doc.tile;
/** 画布要切成 网格列数 × 网格行数 块瓦片：⌈宽/边长⌉ × ⌈高/边长⌉ */
const gridCols = () => Math.ceil(state.doc.w / tilePx());
const gridRows = () => Math.ceil(state.doc.h / tilePx());
const totalTiles = () => gridCols() * gridRows();
/** 一块瓦片的字节数：8 位/通道 = 4 B/px；16 位/通道 = 8 B/px。[官方] 位深影响存储 */
const tileBytes = () => tilePx() * tilePx() * 4 * (state.doc.depth === 16 ? 2 : 1);
const tkey = (layerId, tx, ty) => `${layerId}:${tx},${ty}`;
const pkey = (tx, ty) => `${tx},${ty}`;

function getTile(layerId, tx, ty) {
  return state.tiles.get(tkey(layerId, tx, ty)) || null;
}

/** 惰性分配：只有真的写到这块像素时，才为它分配内存（稀疏存储）。 */
function ensureTile(layerId, tx, ty) {
  const key = tkey(layerId, tx, ty);
  let t = state.tiles.get(key);
  if (t) { touch(t); return t; }
  t = {
    layerId, tx, ty,
    data: new Float32Array(tilePx() * tilePx() * 4),   // 预乘 RGBA
    inRam: true, lastUse: ++state.useSeq, dirty: false,
  };
  state.tiles.set(key, t);
  state.stats.alloc++;
  return t;
}

/** 某块瓦片的**有效**尺寸：边缘瓦片会小于粒度（例如 400 宽、粒度 128 → 第 4 列只有 16px 有效）。 */
const tileValidW = tx => Math.min(tilePx(), state.doc.w - tx * tilePx());
const tileValidH = ty => Math.min(tilePx(), state.doc.h - ty * tilePx());
/** 网格覆盖的总面积（每块按整格算）与文档面积之差 = 边缘用不到的部分。 */
function edgeWaste() {
  const covered = gridCols() * gridRows() * tilePx() * tilePx();
  const doc = state.doc.w * state.doc.h;
  return { covered, doc, waste: covered - doc };
}

/** 触碰 = 记 LRU 时间；若已换出则换入（真实产品这里会读磁盘）。 */
function touch(t) {
  t.lastUse = ++state.useSeq;
  if (!t.inRam) { t.inRam = true; state.stats.pagedIn++; }
}

/** 内存额度检查：超了就把最久未用的瓦片换出到暂存盘。[官方] Scratch Disks */
function enforceMemoryBudget() {
  const budget = state.memBudgetMB * 1024 * 1024;
  const allocated = [...state.tiles.values()].filter(t => t.inRam);
  let used = allocated.length * tileBytes();
  if (used <= budget) return;
  allocated.sort((a, b) => a.lastUse - b.lastUse);   // 最久未用优先换出（LRU）
  for (const t of allocated) {
    if (used <= budget) break;
    t.inRam = false;
    used -= tileBytes();
    state.stats.pagedOut++;
  }
}
const inRamTiles = () => [...state.tiles.values()].filter(t => t.inRam).length;
const diskTiles = () => [...state.tiles.values()].filter(t => !t.inRam).length;

// ------------------------------------------------------------------ 文档 / 图层
function resetDoc(opts) {
  const d = state.doc;
  if (opts) Object.assign(d, opts);
  d.layers = [];
  state.tiles.clear();
  state.proj.clear();
  state.dirty.clear();
  state.lastDirty.clear();
  state.history = [];
  state.layerSeq = 0;
  state.stats = {
    alloc: 0, pagedOut: 0, pagedIn: 0,
    lastRecompTiles: 0, lastUndoBytes: 0, lastUndoFullBytes: 0,
    lastPaintedPx: 0, lastTouchedLayers: 0,
  };
  state.inspector = null;
  state.lastStrokeTiles = 0;
  state.view = { x: Math.max(0, Math.round(d.w * 0.1)), y: Math.max(0, Math.round(d.h * 0.1)) };
}

function addLayer(name, opts = {}) {
  const d = state.doc;
  const layer = {
    id: ++state.layerSeq,
    name: name || ('图层 ' + state.layerSeq),
    visible: true,
    opacity: 1,
    offset: opts.offset || { x: 0, y: 0 },   // [官方] 图层可以小于画布/带偏移
    w: opts.w || d.w, h: opts.h || d.h,
  };
  d.layers.push(layer);                        // 顺序表：index 0 = 最底
  d.active = d.layers.length - 1;
  invalidateAllTiles();
  return layer;
}

const activeLayer = () => state.doc.layers[state.doc.active] || null;

/** 用某个颜色填满整层（用于「白底背景」新建）：会真的分配全部瓦片。 */
function fillLayer(layer, rgba) {
  const [r, g, b, a = 1] = rgba;
  for (let ty = 0; ty < gridRows(); ty++) {
    for (let tx = 0; tx < gridCols(); tx++) {
      const t = ensureTile(layer.id, tx, ty);
      for (let i = 0; i < t.data.length; i += 4) {
        t.data[i] = r * a; t.data[i + 1] = g * a; t.data[i + 2] = b * a; t.data[i + 3] = a;
      }
      state.dirty.add(pkey(tx, ty));
    }
  }
  enforceMemoryBudget();
}

// ------------------------------------------------------------------ 绘制（只碰活动层）
/**
 * 在活动图层上画一段（含插值），返回：
 *   touched: 触及的瓦片集合（这些瓦片会被"惰性分配"）
 *   rect:    改动矩形（像素坐标）
 * 关键：只有活动层的瓦片被写。
 */
function paintSegment(layer, x0, y0, x1, y1, radius, rgb, erase) {
  const d = state.doc;
  const touched = new Set();
  const minX = Math.max(0, Math.floor(Math.min(x0, x1) - radius));
  const maxX = Math.min(d.w - 1, Math.ceil(Math.max(x0, x1) + radius));
  const minY = Math.max(0, Math.floor(Math.min(y0, y1) - radius));
  const maxY = Math.min(d.h - 1, Math.ceil(Math.max(y0, y1) + radius));
  if (maxX < minX || maxY < minY) return { touched, rect: null };

  const n = tilePx();
  const steps = Math.max(1, Math.ceil(Math.hypot(x1 - x0, y1 - y0) / Math.max(1, radius / 2)));
  let pxCount = 0;
  for (let s = 0; s <= steps; s++) {
    const t = s / steps;
    const cx = x0 + (x1 - x0) * t, cy = y0 + (y1 - y0) * t;
    const bx0 = Math.max(minX, Math.floor(cx - radius)), bx1 = Math.min(maxX, Math.ceil(cx + radius));
    const by0 = Math.max(minY, Math.floor(cy - radius)), by1 = Math.min(maxY, Math.ceil(cy + radius));
    for (let y = by0; y <= by1; y++) {
      for (let x = bx0; x <= bx1; x++) {
        const dx = x + 0.5 - cx, dy = y + 0.5 - cy;
        const dist = Math.hypot(dx, dy);
        if (dist > radius) continue;
        const a = Math.max(0, Math.min(1, 1 - (dist / radius) * (dist / radius)));
        if (a <= 0) continue;

        // 图层偏移：图像坐标 → 图层本地坐标
        const lx = x - layer.offset.x, ly = y - layer.offset.y;
        if (lx < 0 || ly < 0 || lx >= layer.w || ly >= layer.h) continue;

        const tx = Math.floor(x / n), ty = Math.floor(y / n);
        const tile = ensureTile(layer.id, tx, ty);      // 惰性分配
        const key = tkey(layer.id, tx, ty);
        touched.add(key);
        // 本次操作第一次写到这块瓦片 → 先留一份旧内容，供历史记录用
        if (state.op && !state.op.saved.has(key)) state.op.saved.set(key, tile.data.slice());
        const i = ((ly % n) * n + (lx % n)) * 4;
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
        pxCount++;
      }
    }
  }
  touched.forEach(k => {
    const coord = k.split(':')[1];
    const [tx, ty] = coord.split(',').map(Number);
    state.dirty.add(pkey(tx, ty));                     // 屏幕合成缓存对应瓦片失效
    const t = state.tiles.get(k);
    if (t) t.dirty = true;
  });
  state.stats.lastPaintedPx = pxCount;
  state.stats.lastTouchedLayers = 1;
  enforceMemoryBudget();
  return { touched, rect: { x0: minX, y0: minY, x1: maxX, y1: maxY } };
}

// ------------------------------------------------------------------ 屏幕合成（投影）
/**
 * 合成一块瓦片到屏幕合成缓存。
 * 【公式】自底向上 source-over（预乘）：out = src + dst × (1 − srcA)
 */
function compositeTile(tx, ty) {
  const d = state.doc;
  const n = tilePx();
  const out = new Float32Array(n * n * 4);
  const baseX = tx * n, baseY = ty * n;
  for (let ly = 0; ly < n; ly++) {
    const y = baseY + ly;
    if (y >= d.h) break;
    for (let lx = 0; lx < n; lx++) {
      const x = baseX + lx;
      if (x >= d.w) break;
      const o = (ly * n + lx) * 4;
      let r = 0, g = 0, b = 0, a = 0;
      for (const L of d.layers) {                       // 顺序表自底向上
        if (!L.visible || L.opacity <= 0) continue;
        const gx = x - L.offset.x, gy = y - L.offset.y;
        if (gx < 0 || gy < 0 || gx >= L.w || gy >= L.h) continue;
        const t = getTile(L.id, tx, ty);
        if (!t) continue;                               // 未分配 = 全透明
        if (!t.inRam) { t.inRam = true; state.stats.pagedIn++; }   // 换入才算得出
        t.lastUse = ++state.useSeq;
        const i = ((gy % n) * n + (gx % n)) * 4;
        const o2 = L.opacity;
        const sa = t.data[i + 3] * o2;
        const inv = 1 - sa;
        r = t.data[i] * o2 + r * inv;
        g = t.data[i + 1] * o2 + g * inv;
        b = t.data[i + 2] * o2 + b * inv;
        a = sa + a * inv;
      }
      out[o] = r; out[o + 1] = g; out[o + 2] = b; out[o + 3] = a;
    }
  }
  state.proj.set(pkey(tx, ty), out);
}

/** 把待重算的瓦片算掉。策略决定"重算多少块"——这是整套架构的收益所在。 */
function recompute() {
  let list;
  if (state.mode === 'full') {
    list = [];
    for (let ty = 0; ty < gridRows(); ty++)
      for (let tx = 0; tx < gridCols(); tx++) list.push(pkey(tx, ty));
  } else {
    list = [...state.dirty];
  }
  list.forEach(k => {
    const [tx, ty] = k.split(',').map(Number);
    compositeTile(tx, ty);
  });
  state.lastDirty = new Set(list);
  state.stats.lastRecompTiles = list.length;
  state.dirty.clear();
}

function invalidateAllTiles() {
  for (let ty = 0; ty < gridRows(); ty++)
    for (let tx = 0; tx < gridCols(); tx++) state.dirty.add(pkey(tx, ty));
  recompute();
}

const projPixel = (x, y) => {
  const n = tilePx();
  const t = state.proj.get(pkey(Math.floor(x / n), Math.floor(y / n)));
  if (!t) return [0, 0, 0, 0];
  const i = ((y % n) * n + (x % n)) * 4;
  return [t[i], t[i + 1], t[i + 2], t[i + 3]];
};

// ------------------------------------------------------------------ 画到 canvas
const docCanvas = document.getElementById('docCanvas');
const docCtx = docCanvas.getContext('2d');
const screenCanvas = document.getElementById('screenCanvas');
const screenCtx = screenCanvas.getContext('2d');
const off = document.createElement('canvas');

function docTransform() {
  const d = state.doc, pad = 14;
  const scale = Math.min((docCanvas.width - pad * 2) / d.w, (docCanvas.height - pad * 2) / d.h);
  return { scale, ox: (docCanvas.width - d.w * scale) / 2, oy: (docCanvas.height - d.h * scale) / 2 };
}
const imgToCanvas = (x, y) => { const t = docTransform(); return [t.ox + x * t.scale, t.oy + y * t.scale]; };
const canvasToImg = (x, y) => { const t = docTransform(); return [(x - t.ox) / t.scale, (y - t.oy) / t.scale]; };

function checker(x, y) {
  return ((Math.floor(x / 8) + Math.floor(y / 8)) % 2 === 0) ? [200, 200, 200] : [138, 138, 138];
}
/** 预乘 → 直通，再与棋盘格做 source-over（显示透明区就是这么做的） */
function unpremulOverChecker(r, g, b, a, x, y, out, o) {
  const [cr, cg, cb] = checker(x, y);
  const oa = a + (1 - a);
  const rr = (r + (cr / 255) * (1 - a)) / oa;
  const gg = (g + (cg / 255) * (1 - a)) / oa;
  const bb = (b + (cb / 255) * (1 - a)) / oa;
  out[o] = Math.max(0, Math.min(255, Math.round(rr * 255)));
  out[o + 1] = Math.max(0, Math.min(255, Math.round(gg * 255)));
  out[o + 2] = Math.max(0, Math.min(255, Math.round(bb * 255)));
  out[o + 3] = 255;
}

function paintDocCanvas() {
  const d = state.doc, t = docTransform();
  docCtx.imageSmoothingEnabled = false;
  docCtx.fillStyle = '#1e1e1e';
  docCtx.fillRect(0, 0, docCanvas.width, docCanvas.height);

  // 整幅（读的是屏幕合成缓存，不是各层像素）
  const img = new ImageData(d.w, d.h);
  for (let y = 0; y < d.h; y++) {
    for (let x = 0; x < d.w; x++) {
      const [r, g, b, a] = projPixel(x, y);
      unpremulOverChecker(r, g, b, a, x, y, img.data, (y * d.w + x) * 4);
    }
  }
  off.width = d.w; off.height = d.h;
  off.getContext('2d').putImageData(img, 0, 0);
  docCtx.drawImage(off, t.ox, t.oy, d.w * t.scale, d.h * t.scale);

  // 瓦片网格：未分配（虚线）/ 已分配（实色）/ 脏（红）/ 换出（橙）
  const n = tilePx();
  const layer = activeLayer();
  for (let ty = 0; ty < gridRows(); ty++) {
    for (let tx = 0; tx < gridCols(); tx++) {
      const x0 = tx * n, y0 = ty * n;
      const [cx, cy] = imgToCanvas(x0, y0);
      const w = Math.min(n, d.w - x0) * t.scale, h = Math.min(n, d.h - y0) * t.scale;
      const tile = layer ? getTile(layer.id, tx, ty) : null;
      const key = pkey(tx, ty);
      if (tile) {
        docCtx.fillStyle = tile.inRam ? 'rgba(74,144,217,.13)' : 'rgba(217,138,43,.22)';
        docCtx.fillRect(cx, cy, w, h);
        docCtx.strokeStyle = tile.inRam ? 'rgba(120,160,200,.55)' : '#d98a2b';
        docCtx.setLineDash([]);
        docCtx.lineWidth = 1;
        docCtx.strokeRect(cx + .5, cy + .5, w - 1, h - 1);
      } else {
        docCtx.strokeStyle = 'rgba(150,150,150,.30)';
        docCtx.setLineDash([4, 4]);
        docCtx.lineWidth = 1;
        docCtx.strokeRect(cx + .5, cy + .5, w - 1, h - 1);
        docCtx.setLineDash([]);
      }
      if (state.lastDirty.has(key)) {
        docCtx.strokeStyle = '#ff5c5c';
        docCtx.lineWidth = 2;
        docCtx.strokeRect(cx + 1, cy + 1, w - 2, h - 2);
      }
      if (state.dirty.has(key)) {
        docCtx.strokeStyle = '#ffd479';
        docCtx.lineWidth = 2;
        docCtx.strokeRect(cx + 1, cy + 1, w - 2, h - 2);
      }
    }
  }

  // 视口
  const v = viewRect();
  const [vx, vy] = imgToCanvas(v.x0, v.y0);
  const [vx2, vy2] = imgToCanvas(v.x1 + 1, v.y1 + 1);
  docCtx.strokeStyle = '#4a90d9';
  docCtx.lineWidth = 2;
  docCtx.strokeRect(vx + 1, vy + 1, vx2 - vx - 2, vy2 - vy - 2);

  // 文档边框
  docCtx.strokeStyle = '#000';
  docCtx.lineWidth = 1;
  docCtx.strokeRect(t.ox - .5, t.oy - .5, d.w * t.scale + 1, d.h * t.scale + 1);

  // 边缘瓦片：把"这块只有多少像素落在画布内"写在格子左上角
  // （网格只覆盖画布范围 —— 画布外没有任何瓦片；不满一格的行/列照样占**一整块**）
  docCtx.font = '10px Consolas, monospace';
  docCtx.textBaseline = 'top';
  for (let ty = 0; ty < gridRows(); ty++) {
    for (let tx = 0; tx < gridCols(); tx++) {
      const vw = tileValidW(tx), vh = tileValidH(ty);
      if (vw === n && vh === n) continue;
      const [cx, cy] = imgToCanvas(tx * n, ty * n);
      docCtx.fillStyle = 'rgba(255,212,121,.92)';
      docCtx.fillText(`有效 ${vw}×${vh}`, cx + 4, cy + 4);
    }
  }
}

/** 视口：图像坐标（闭区间）。缩放越大，看到的图像范围越小。 */
function viewRect() {
  const d = state.doc;
  const vw = Math.max(1, Math.ceil(SCREEN_W * state.zoom));
  const vh = Math.max(1, Math.ceil(SCREEN_H * state.zoom));
  const x0 = Math.max(0, Math.min(Math.max(0, d.w - vw), Math.round(state.view.x)));
  const y0 = Math.max(0, Math.min(Math.max(0, d.h - vh), Math.round(state.view.y)));
  return { x0, y0, x1: Math.min(d.w - 1, x0 + vw - 1), y1: Math.min(d.h - 1, y0 + vh - 1) };
}

/** 屏幕：只光栅化「视口 ∩ 图像」，按缓存级别降采样。[官方] Cache Levels / [模拟] 金字塔 */
function paintScreenCanvas() {
  const d = state.doc, v = viewRect();
  const level = state.zoom;                 // 1=100%, 2=50%, 4=25%
  const w = SCREEN_W, h = SCREEN_H;
  const img = new ImageData(w, h);
  const step = level;                       // 每 level×level 个像素取一次（金字塔近似）
  for (let sy = 0; sy < h; sy++) {
    for (let sx = 0; sx < w; sx++) {
      const x = Math.min(d.w - 1, v.x0 + sx * step);
      const y = Math.min(d.h - 1, v.y0 + sy * step);
      let r = 0, g = 0, b = 0, a = 0, cnt = 0;
      for (let dy = 0; dy < step; dy++) {
        for (let dx = 0; dx < step; dx++) {
          const px = x + dx, py = y + dy;
          if (px >= d.w || py >= d.h) continue;
          const [pr, pg, pb, pa] = projPixel(px, py);
          r += pr; g += pg; b += pb; a += pa; cnt++;
        }
      }
      cnt = Math.max(1, cnt);
      unpremulOverChecker(r / cnt, g / cnt, b / cnt, a / cnt, x, y, img.data, (sy * w + sx) * 4);
    }
  }
  off.width = w; off.height = h;
  off.getContext('2d').putImageData(img, 0, 0);
  screenCtx.imageSmoothingEnabled = false;
  screenCtx.drawImage(off, 0, 0, screenCanvas.width, screenCanvas.height);

  // 屏幕用的瓦片数：级别越高，每块缓存瓦片覆盖的图像范围越大
  const effectiveTile = tilePx() * level;
  const used = Math.ceil((v.x1 - v.x0 + 1) / effectiveTile) * Math.ceil((v.y1 - v.y0 + 1) / effectiveTile);
  const l0 = Math.ceil((v.x1 - v.x0 + 1) / tilePx()) * Math.ceil((v.y1 - v.y0 + 1) / tilePx());
  document.getElementById('cacheHint').textContent =
    `缓存级别 L${Math.log2(level)}（${100 / level}%）· 本次用到 ${used} 块 L${Math.log2(level)} 瓦片（若按 L0 需 ${l0} 块）`;
}

// ------------------------------------------------------------------ 历史记录（区域快照）
/**
 * 记一步历史。
 * @param saved 该次操作**之前**的瓦片内容：Map<tileKey, Float32Array>
 *              —— 只存被触碰的那几块，不存整幅（这正是 PS/GIMP 的做法）。
 */
function pushHistory(label, layer, saved) {
  const tiles = [];
  saved.forEach((data, key) => tiles.push({ key, data }));
  const entry = { label, layerId: layer.id, tiles, bytes: tiles.length * tileBytes() };
  state.history.push(entry);
  state.stats.lastUndoBytes = entry.bytes;
  state.stats.lastUndoFullBytes = state.doc.w * state.doc.h * 4 * (state.doc.depth === 16 ? 2 : 1);
  return entry;
}

function undo() {
  const e = state.history.pop();
  if (!e) { flash('没有可撤销的步骤'); return; }
  e.tiles.forEach(({ key, data }) => {
    const t = state.tiles.get(key);
    if (t) { t.data.set(data); t.inRam = true; state.dirty.add(pkey(t.tx, t.ty)); }
  });
  state.stats.lastUndoBytes = 0;
  recompute();
  renderAll();
}

// ------------------------------------------------------------------ 图层列表 / 检查器
function renderLayerList() {
  const d = state.doc;
  const host = document.getElementById('layerList');
  host.innerHTML = '';
  const all = [...state.tiles.values()];
  for (let i = d.layers.length - 1; i >= 0; i--) {       // 面板顶行 = 最靠前的图层
    const L = d.layers[i];
    const count = all.filter(t => t.layerId === L.id).length;
    const disk = all.filter(t => t.layerId === L.id && !t.inRam).length;
    const row = document.createElement('div');
    row.className = 'layer-row' + (i === d.active ? ' active' : '');
    row.innerHTML = `
      <div class="eye" title="显隐（隐藏不释放存储）">${L.visible ? '👁' : '—'}</div>
      <div>
        <div class="name">${L.name}</div>
        <div class="meta">栈下标 [${i}] · 瓦片 ${count}/${totalTiles()}${disk ? ` · 暂存盘 ${disk}` : ''} ·
          偏移 ${L.offset.x},${L.offset.y} · ${(count * tileBytes() / 1024).toFixed(0)} KB</div>
      </div>
      <input type="range" min="0" max="100" value="${Math.round(L.opacity * 100)}" title="不透明度">
    `;
    row.onclick = (ev) => {
      if (ev.target.classList.contains('eye')) {
        L.visible = !L.visible; invalidateAllTiles(); renderAll(); return;
      }
      if (ev.target.tagName === 'INPUT') return;
      d.active = i; renderAll();
    };
    const slider = row.querySelector('input');
    slider.oninput = () => { L.opacity = slider.value / 100; invalidateAllTiles(); renderAll(); };
    host.appendChild(row);
  }
}

function renderInspector() {
  const el = document.getElementById('tileInspector');
  const ins = state.inspector;
  if (!ins) { el.textContent = '选「检查瓦片」工具后在画布上点一下，这里会显示该瓦片在各图层里的状态。'; return; }
  const perLayer = state.doc.layers.map(L => {
    const t = getTile(L.id, ins.tx, ins.ty);
    if (!t) return `<tr><td>${L.name}</td><td>未分配</td><td>0 B</td></tr>`;
    return `<tr><td>${L.name}</td><td>${t.inRam ? '内存' : '暂存盘（已换出）'}</td><td>${(tileBytes() / 1024).toFixed(0)} KB</td></tr>`;
  }).join('');
  el.innerHTML = `
    <div class="ins-head">瓦片 (${ins.tx}, ${ins.ty}) · 覆盖 x ${ins.tx * tilePx()}–${Math.min(state.doc.w - 1, ins.tx * tilePx() + tilePx() - 1)},
      y ${ins.ty * tilePx()}–${Math.min(state.doc.h - 1, ins.ty * tilePx() + tilePx() - 1)}</div>
    <table><tr><th>图层</th><th>状态</th><th>占用</th></tr>${perLayer}</table>
    <div class="tiny">换算：tx = ⌊x / ${tilePx()}⌋ = ${ins.tx}；ty = ⌊y / ${tilePx()}⌋ = ${ins.ty}</div>`;
}

// ------------------------------------------------------------------ 数字面板
/** 边缘瓦片那一行：说清"右/下零头仍占一整块，只是有一部分用不到"。 */
function edgeStatLine() {
  const e = edgeWaste();
  const lastW = tileValidW(gridCols() - 1), lastH = tileValidH(gridRows() - 1);
  const partial = (lastW < tilePx() ? `右列每块只用 ${lastW}/${tilePx()} 宽` : '')
    + (lastW < tilePx() && lastH < tilePx() ? '，' : '')
    + (lastH < tilePx() ? `下行每块只用 ${lastH}/${tilePx()} 高` : '');
  if (e.waste === 0) {
    return `无（${state.doc.w}×${state.doc.h} 正好是 ${tilePx()} 的整数倍，网格完全贴合画布）`;
  }
  return `${partial}；网格覆盖 ${e.covered.toLocaleString()} px − 文档 ${e.doc.toLocaleString()} px `
    + `= <b>${e.waste.toLocaleString()} px（占 ${(e.waste / e.covered * 100).toFixed(0)}%）</b>用不到，`
    + `但每块仍按整格占内存`;
}

function renderStats() {
  const d = state.doc;
  const alloc = state.tiles.size;
  const ram = inRamTiles(), disk = diskTiles();
  const usedBytes = alloc * tileBytes();
  const fullBytes = d.w * d.h * 4 * (d.depth === 16 ? 2 : 1);
  const bigTiles = Math.ceil(4000 / tilePx()) * Math.ceil(3000 / tilePx());
  const histTotal = state.history.reduce((s, e) => s + e.bytes, 0);

  const lines = [
    ['画布', `${d.w} × ${d.h} px · 粒度 ${tilePx()}×${tilePx()} · <b>寻址空间</b> ${gridCols()} × ${gridRows()} = ${totalTiles()} 个地址 · ${d.depth} 位/通道`],
    ['已分配数据块', `${alloc} / ${totalTiles()} 个地址有数据（内存 ${ram}，暂存盘 ${disk}）`],
    ['边缘瓦片（不满一格）', edgeStatLine()],
    ['已分配占用', `${(usedBytes / 1024).toFixed(0)} KB（整幅全分配要 ${(fullBytes / 1024).toFixed(0)} KB）`],
    ['上一笔触碰瓦片', `${state.lastStrokeTiles} 块 → 只分配/只重算这几块；改动 ${state.stats.lastPaintedPx.toLocaleString()} 像素，涉及图层 ${state.stats.lastTouchedLayers} 个`],
    ['上次屏幕重算', `${state.stats.lastRecompTiles} 块瓦片（整幅是 ${totalTiles()} 块）`],
    ['历史记录', `共 ${state.history.length} 步 · 合计 ${(histTotal / 1024).toFixed(0)} KB（最近一步 ${(state.stats.lastUndoBytes / 1024).toFixed(0)} KB；整幅快照要 ${(state.stats.lastUndoFullBytes / 1024).toFixed(0)} KB）`],
    ['换页', `换出 ${state.stats.pagedOut} 次 · 换入 ${state.stats.pagedIn} 次（额度 ${state.memBudgetMB} MB）`],
    ['换算 4000×3000', `瓦片网格 ${bigTiles} 块；整幅 ${(4000 * 3000 * 4 / 1048576).toFixed(0)} MB（8 位）/ ${(4000 * 3000 * 8 / 1048576).toFixed(0)} MB（16 位）；一笔碰 4 块 = ${(4 * tileBytes() / 1024).toFixed(0)} KB`],
  ];
  document.getElementById('stats').innerHTML =
    lines.map(([k, v]) => `<div class="stat"><span class="k">${k}</span><span class="v">${v}</span></div>`).join('');
}

function renderHistory() {
  const el = document.getElementById('history');
  if (!state.history.length) { el.innerHTML = '<div class="tiny">还没有历史步骤。画几笔看看。</div>'; return; }
  const full = state.doc.w * state.doc.h * 4 * (state.doc.depth === 16 ? 2 : 1);
  let run = 0;
  el.innerHTML = state.history.map((e, i) => {
    run += e.bytes;
    return `<div class="hrow"><span>${i + 1}. ${e.label}</span>
      <span class="tiny">${e.tiles.length} 块瓦片 = ${(e.bytes / 1024).toFixed(0)} KB（累计 ${(run / 1024).toFixed(0)} KB）</span></div>`;
  }).join('') +
  `<div class="tiny">对照：若每步都存整幅快照，${state.history.length} 步 = ${((full * state.history.length) / 1048576).toFixed(1)} MB。</div>`;
}

// ------------------------------------------------------------------ 步骤讲解
const STEPS = [
  {
    t: '① 新建文档：先有元数据；瓦片是"写像素时"才出现的',
    run() {
      resetDoc({ w: DOC_W, h: DOC_H, transparentBg: true });
      addLayer('图层 1');       // 透明底：一个瓦片数据块都不分配
      recompute(); renderAll();
    },
    html: `<h3>先把三个词分清楚（这里最容易混）</h3>
      <table>
        <tr><th>词</th><th>是什么</th><th>什么时候出现</th></tr>
        <tr><td>文档元数据</td><td>宽、高、分辨率、颜色模式、位深 + 一条空的图层记录</td>
            <td><b>新建文档时</b>就有了</td></tr>
        <tr><td>瓦片<b>划分</b>（寻址空间）</td>
            <td>按粒度把画布算成 ⌈宽/边长⌉ × ⌈高/边长⌉ 个<b>地址</b></td>
            <td>尺寸一确定就<b>推导得出</b>，不是被"建"出来的对象</td></tr>
        <tr><td>瓦片<b>数据块</b>（分配）</td>
            <td>真正装像素的那 边长×边长×4 字节</td>
            <td><b>第一次往这块写像素</b>时才分配</td></tr>
      </table>
      <p>所以"新建文档时建立了一张空瓦片网格"这种说法<b>不准确</b>：
      网格不是被创建出来的实体，它只是 <span class="mono">tx = ⌊x/边长⌋, ty = ⌊y/边长⌋</span>
      这套<b>寻址</b>算出来的结果。新建时真正被建立的只有<b>元数据与图层记录</b>。</p>
      <ul>
        <li>本页 512×384、粒度 128 → 寻址空间 <b>4 × 3 = 12 个地址</b>（画布上那些虚线格）。</li>
        <li>当前是<b>透明底</b>文档：一个像素都没写 → <b>已分配 0 / 12 块</b>，像素内存 <b>0 字节</b>。</li>
        <li>若在"新建"对话框选白底/背景色，PS 会把整层像素写满 → 12 个地址才都拿到数据块。
        <b>"分配"是被"写"触发的，不是被"建文档"触发的。</b></li>
      </ul>
      <p>[共识] 瓦片边长常引用 <b>128×128</b>（Adobe 未文档化）；[源码] GIMP 的粒度是<b>可配属性</b>
      （历史默认 64×64）。点第 ② 步的 64 / 128 / 256 按钮看差别。</p>
      <p class="bad">诚实标注：PS 内部是否会预建一张"瓦片指针表/目录"没有官方文档；
      能确定的是<b>像素内存按需分配</b>（这正是超大画布、超小图层能存在的原因）。
      [源码] GIMP 侧可核对：GeglBuffer 只管外框，tile 由 tile handler
      在被访问时才校验/渲染/取内存。</p>`
  },
  {
    t: '② 瓦片是什么：先"划分"（寻址），再"分配"',
    run() {
      resetDoc({ w: DOC_W, h: DOC_H, transparentBg: true });
      addLayer('图层 1');
      const L = activeLayer();
      beginOp();
      const r = paintSegment(L, 150, 120, 300, 220, 40, [0.30, 0.65, 1.0], false);
      recompute();
      state.lastStrokeTiles = r.touched.size;
      endOp('演示笔画', L);
      renderAll();
    },
    html: `<h3>瓦片 = 把画布按固定粒度切格子，作为<b>寻址与存储</b>的单位</h3>
      <p><b>划分别等于分配</b>：划分是"给每个像素算一个格子地址"，尺寸一确定就能算；
      分配是"为某个格子真的申请 边长×边长×4 字节"，只有写像素时才发生。
      所以画布上虚线格 = 地址有了、数据还没有。</p>
      <p>坐标换算（记住这一条就够用）：</p>
      <p class="mono">tx = ⌊x / 边长⌋ &nbsp; ty = ⌊y / 边长⌋ &nbsp; 块内偏移 = (x mod 边长, y mod 边长)</p>
      <p>同一笔（半径 40、横向一段）在不同粒度下碰到的格子数完全不同 —— 点上面的 64 / 128 / 256 试：</p>
      <ul>
        <li>粒度越小：边缘浪费少，但<b>格子数暴涨</b>（索引与管理开销大）；</li>
        <li>粒度越大：管理简单，但一次要处理/换页/撤销的数据多，小笔画浪费大。</li>
        <li>所以真实产品把它定在 64–128 这个量级，而不是 16 或 1024。</li>
      </ul>
      <h3>边缘瓦片：右/下"不满一格"怎么办</h3>
      <p>划分只覆盖<b>文档范围</b>，所以行数是 <span class="mono">⌈高/粒度⌉</span>、列数是
      <span class="mono">⌈宽/粒度⌉</span> —— <b>上取整，所以最后一行/列可能只用得到一部分</b>。
      点上面的「400×300（有零头）」看：右列每块只有 16px 宽有效、下行每块只有 44px 高有效，
      画布上会把「有效 w×h」写在格子角上，数字面板里给出用不到的像素比例。</p>
      <ul>
        <li><b>画布外没有任何瓦片</b>：网格 = ⌈宽/粒度⌉ × ⌈高/粒度⌉，最右边/最下边的格子也属于画布内。</li>
        <li>不满一格的行/列，<b>照样按一整块占内存</b>（内存按整格申请）；
        [诚实标注] 是否对边缘做特殊处理没有官方文档，[源码] GIMP 的 tile 就是整块缓冲。</li>
        <li>这就是"粒度"的取舍另一面：粒度越大，边缘浪费越明显（400×300 用 128 粒度时，
        网格覆盖 512×384 = 196,608 px，其中 <b>39%</b> 落在画布外用不到）。</li>
      </ul>
      <h3>为什么非要用瓦片（7 个理由）</h3>
      <ol>
        <li><b>稀疏分配</b>：没写过的格子 0 字节 —— 超大画布、超小图层才敢开。</li>
        <li><b>增量重算</b>：改一格只重算相关格子的合成结果，不必整幅。</li>
        <li><b>撤销的天然单位</b>：历史按"被改动的格子"存旧内容（第 ⑥ 步）。</li>
        <li><b>可换页</b>：内存不够就把格子写进暂存盘 [官方 Scratch Disks]。</li>
        <li><b>显示金字塔</b>：缓存级别（Cache Levels）[官方] 缩小时用更粗的格子。</li>
        <li><b>异步/并行</b>：只渲染可见格子、后台补算其余。[源码] GIMP 的 validate handler 就是按 tile 惰性渲染。</li>
        <li><b>缓存局部性</b>：一次只在 128×128 小块里遍历，CPU 缓存友好。</li>
      </ol>`
  },
  {
    t: '③ 新建图层：又一套空的寻址空间（+ 偏移）',
    run() {
      addLayer('图层 2');
      const L = activeLayer();
      beginOp();
      const r = paintSegment(L, 330, 90, 430, 250, 30, [0.95, 0.35, 0.35], false);
      recompute();
      state.lastStrokeTiles = r.touched.size;
      endOp('图层 2 上画一笔', L);
      renderAll();
    },
    html: `<h3>每个图层各有一套自己的格子</h3>
      <ul>
        <li>新建图层只多了一条<b>图层记录</b>：它和画布共用同一套寻址公式，
        但<b>数据块要从零开始</b>——空白层永远是 0 块（面板里"瓦片 0/12"）。</li>
        <li>图层不是"叠起来的数组"，而是<b>并排的若干套稀疏格子集合</b>。</li>
        <li>[官方] 图层可以<b>比画布小</b>、可以带偏移（本页图层记录里的 <code>offset</code> 对应 PS 的图层位置），
        这样"小贴图"图层不会白占整幅内存。</li>
        <li>隐藏图层（点眼睛）<b>照样占存储</b>，只是不参与合成 —— 与"删除图层"完全不同。</li>
        <li>图层顺序是<b>栈/顺序表</b>（本页 <code>layers[]</code>，index 0 = 最底），不是链表；
        只有<b>图层组</b>才是树形。[源码] GIMP：<code>GimpContainer</code> + <code>GimpGroupLayer</code>。</li>
      </ul>
      <p>连点几次「新建图层」：面板里新层的"瓦片 0/12"始终不变 —— 这就是稀疏分配。</p>`
  },
  {
    t: '④ 操作图层：写瓦片 → 标脏 → 合成失效',
    run() {
      const L = activeLayer();
      beginOp();
      const r = paintSegment(L, 120, 260, 380, 300, 22, [0.35, 0.95, 0.55], false);
      recompute();
      state.lastStrokeTiles = r.touched.size;
      endOp('画笔（活动层）', L);
      renderAll();
    },
    html: `<h3>在某一层上画一笔，内部依次发生了什么</h3>
      <ol>
        <li>工具拿到<b>活动图层</b>（O(1) 的图层引用/ID，不是遍历查找）；</li>
        <li>笔迹覆盖的每块瓦片：<b>没分配就惰性分配</b>（画布上虚线 → 实色）→ 写入像素；</li>
        <li>这些瓦片标记 <b>dirty</b>（红框）；<b>别的图层一个字节都不动</b>（看各层瓦片数变化）；</li>
        <li>屏幕合成缓存的<b>对应瓦片失效</b> → 只重算这几块（数字面板"上次屏幕重算"）；</li>
        <li>系统只把屏幕上受影响的矩形标脏重绘（Windows 上是 InvalidateRect）；</li>
        <li>历史记录：<b>把这几次触碰的瓦片旧内容存一份</b>（第 ⑥ 步细看）。</li>
      </ol>
      <p class="bad">"只碰活动层"这条不变量很重要：本页数字里「涉及图层」永远是 1。
      真实 PS 也一样，所以"画错层"是常见事故 —— 也因此才有锁定图层/锁定透明像素这些功能。</p>`
  },
  {
    t: '⑤ 你看到的画面：合成缓存 + 只画可见瓦片',
    run() {
      state.view = { x: 80, y: 60 };
      state.zoom = 1; syncButtons(); recompute(); renderAll();
    },
    html: `<h3>从"图层数据"到"屏幕像素"要过三道关</h3>
      <ol>
        <li><b>屏幕合成（composite）</b>：把可见图层自底向上叠加成一幅"当前效果"。公式（预乘 alpha）：
        <span class="mono">out = src + dst × (1 − srcA)</span></li>
        <li><b>缓存级别（Cache Levels）</b>[官方]：缩放显示时用更粗的缓存（100% / 50% / 25%…），
        所以缩小后拖动不卡。点 100% / 50% / 25%，看"本次用到几块瓦片"。</li>
        <li><b>只画视口</b>：屏幕只光栅化「视口 ∩ 图像」。蓝框就是视口 —— 拖它，
        视口外的像素<b>根本不会被算、也不会被画</b>。</li>
      </ol>
      <p>所以"叠起来的效果"是<b>第三份数据</b>（合成结果），既不是图层数据，也不是屏幕本身：
      它是一份<b>缓存</b>，随时能由图层重算 —— 这就是「改一下不透明度会立刻全图重画、
      而画一笔只重画一小块」的原因。</p>
      <p>[源码] GIMP 里这份东西叫 <code>GimpProjection</code>，配 <code>GimpTileHandlerValidate</code>
      按 tile <b>惰性/异步</b>渲染；<code>gimp_projection_add_update_area()</code> 只标脏一块区域。</p>`
  },
  {
    t: '⑥ 撤销 / 历史：按区域（瓦片）存，不是整幅',
    run() {
      const L = activeLayer();
      for (let i = 0; i < 3; i++) {
        beginOp();
        const r = paintSegment(L, 100 + i * 90, 330, 150 + i * 90, 360, 12,
                               [0.9, 0.5 + i * 0.15, 0.2], false);
        recompute();
        state.lastStrokeTiles = r.touched.size;
        endOp('第 ' + (i + 1) + ' 笔', L);
      }
      renderAll();
    },
    html: `<h3>为什么历史记录既"很能记"又"很吃内存"</h3>
      <p>因为每一步只保存<b>被改动区域的旧内容</b>，不是整幅快照：</p>
      <ul>
        <li>本页历史面板里每步都是"几块瓦片 = 几十 KB"；整幅快照是 <b>768 KB</b>（512×384×4B）。</li>
        <li>[源码] GIMP 的像素撤销类就是 <code>GeglBuffer *buffer + x, y, width, height</code>
        （<code>app/core/gimpdrawableundo.h</code>）—— <b>一块区域</b>，不是整层。</li>
        <li>[官方] 但"步数 × 改动面积"一累积就很吓人：4000×3000（8 位）整幅 48 MB，
        若某步真动了全图，50 步就是 <b>2.4 GB</b>；16 位管线翻倍。</li>
        <li>所以首选项里的"历史记录状态数"要克制；历史不够用时 PS 会把旧状态写到暂存盘。</li>
      </ul>
      <p>顺带一提：PS 的<b>历史记录画笔</b>就是拿这些区域快照做"局部回退"——
      架构决定了功能怎么长出来。</p>`
  },
  {
    t: '⑦ 暂存盘：内存不够时瓦片换到磁盘',
    run() {
      addLayer('大图层');
      const L = activeLayer();
      for (let i = 0; i < 6; i++)
        paintSegment(L, 40 + i * 70, 40, 80 + i * 70, 340, 26, [0.5, 0.4 + i * 0.08, 0.9], false);
      enforceMemoryBudget();
      recompute();
      state.lastStrokeTiles = [...state.tiles.values()].filter(t => t.layerId === L.id).length;
      renderAll();
    },
    html: `<h3>内存不够时：瓦片换到磁盘</h3>
      <p>[官方] Photoshop 首选项里的 <b>Scratch Disks（暂存盘）</b>就是干这个的：
      RAM 装不下的瓦片会被写到磁盘，需要时再读回来。</p>
      <ul>
        <li>把"内存额度"滑杆拉小（比如 8 MB），画布上的瓦片会变成<b>橙色</b> = 已换出到暂存盘；</li>
        <li>再去画/去合成那块时，它会被<b>换入</b>（数字面板"换入"次数增加）。真实机器上这是磁盘 IO，
        所以会卡；</li>
        <li>换出策略是 <b>LRU</b>（最久没用过的先走）—— 本页也是这么实现的。</li>
        <li>"PS 卡"的两个常见原因：<b>暂存盘太小/在慢盘上</b>，以及<b>历史步数太多</b>。</li>
      </ul>
      <p>这也是"为什么 PS 不一次性 malloc 整幅"最硬的理由：4000×3000 的 16 位文档是 96 MB/层，
      十层就接近 1 GB —— 只有分块 + 换页才撑得住。</p>`
  },
  {
    t: '⑧ PS 与 GIMP 对照：哪些是官方、哪些是共识',
    run() { renderAll(); },
    html: `<h3>资料分级对照表</h3>
      <table>
        <tr><th>机制</th><th>Photoshop</th><th>GIMP（可查源码）</th><th>来源</th></tr>
        <tr><td>分块存储</td><td>瓦片，常引用 128×128</td><td>瓦片边长可配（默认 64×64）</td><td><b>共识</b> / <b>源码</b></td></tr>
        <tr><td>投影（合成）</td><td>屏幕合成缓存，按脏区更新</td><td><code>GimpProjection</code> + <code>GimpTileHandlerValidate</code></td><td><b>源码</b> + 行为可观测</td></tr>
        <tr><td>缩放缓存</td><td>Cache Levels（首选项 1–8 层）</td><td>按需缩放渲染（无同款金字塔）</td><td><b>官方</b></td></tr>
        <tr><td>暂存盘</td><td>Scratch Disks（首选项）</td><td>无同款（靠系统 swap / 内存映射）</td><td><b>官方</b></td></tr>
        <tr><td>撤销粒度</td><td>按改动区域/瓦片</td><td><code>GimpDrawableUndo</code>：buffer + x/y/w/h</td><td><b>源码</b> + 行为可观测</td></tr>
        <tr><td>位深管线</td><td>8/16/32 位；多数运算在更高精度进行</td><td>8/16/32 位浮点 GeglBuffer</td><td><b>官方</b>（部分）</td></tr>
        <tr><td>图层</td><td>可小于画布、带偏移、可编组</td><td><code>GimpLayer</code> / <code>GimpGroupLayer</code></td><td><b>官方</b> / <b>源码</b></td></tr>
      </table>
      <p class="bad">诚实标注：PS 的内部实现（瓦片边长、合成缓存细节）<b>没有官方文档</b>，
      表中 PS 列是"官方可见概念 + 业界长期共识"的组合；GIMP 列可以逐条在源码里核对。
      两者架构思想一致，具体数字不必当成 PS 的定论。</p>`
  },
  {
    t: '⑨ 总结：一次"画一笔"的完整链路',
    run() { renderAll(); },
    html: `<h3>从鼠标到屏幕，PS 内部的一条链</h3>
      <ol>
        <li><b>命中活动图层</b>：工具持有图层引用/ID，O(1)，不遍历；</li>
        <li><b>写瓦片</b>：笔迹覆盖的瓦片若未分配 → 惰性分配；只写这一层；</li>
        <li><b>标脏</b>：这些瓦片 = 脏瓦片（属于该层）；</li>
        <li><b>合成失效</b>：屏幕合成缓存里对应瓦片失效；</li>
        <li><b>历史</b>：把这些瓦片的旧内容存一份（区域快照，不是整幅）；</li>
        <li><b>重算</b>：只重算脏瓦片（按缓存级别可能只算可见部分）；</li>
        <li><b>上屏</b>：只把视口内受影响的矩形交给系统/GPU 合成；</li>
        <li><b>换页</b>：内存额度不够时，LRU 把最少用的瓦片换到暂存盘。</li>
      </ol>
      <h3>三句话记住整套设计</h3>
      <ul>
        <li><b>图层 = 并排的多套瓦片网格</b>（不是数组套数组，也不是链表）；</li>
        <li><b>你看到的效果 = 第三份数据（合成缓存）</b>，由图层按顺序算出来，可随时重算；</li>
        <li><b>一切粒度都是瓦片</b>：分配、重算、撤销、换页、缩放缓存都用它。</li>
      </ul>
      <p>想看"真实开源实现怎么写"，读 GIMP 的
      <code>app/gegl/gimptilehandlervalidate.c</code>（按 tile 惰性校验/渲染）、
      <code>app/core/gimpprojection.c</code>（脏区 → 瓦片失效）、
      <code>app/core/gimpdrawableundo.h</code>（区域撤销）。</p>`
  },
];

// ------------------------------------------------------------------ 交互
let dragging = null;

function canvasPos(ev, canvas) {
  const r = canvas.getBoundingClientRect();
  return [(ev.clientX - r.left) * (canvas.width / r.width), (ev.clientY - r.top) * (canvas.height / r.height)];
}

docCanvas.addEventListener('pointerdown', (ev) => {
  const [cx, cy] = canvasPos(ev, docCanvas);
  const [ix, iy] = canvasToImg(cx, cy);
  docCanvas.setPointerCapture(ev.pointerId);

  if (state.tool === 'inspect') { inspectTile(ix, iy); return; }
  if (state.tool === 'viewport' || ev.shiftKey) {
    dragging = { kind: 'viewport', dx: ix - state.view.x, dy: iy - state.view.y };
    return;
  }
  if (state.tool === 'picker') {
    const x = Math.floor(ix), y = Math.floor(iy);
    if (x >= 0 && y >= 0 && x < state.doc.w && y < state.doc.h) {
      const [r, g, b, a] = projPixel(x, y);
      if (a > 0) {
        state.color = [r / a, g / a, b / a];
        document.getElementById('colorPick').value = rgbToHex(state.color);
      }
    }
    return;
  }
  const L = activeLayer();
  if (!L) return;
  state.strokes.touched = new Set();
  dragging = { kind: 'paint', last: [ix, iy] };
  beginOp();                                  // 笔开始：准备记录被写瓦片的旧内容
  const r = paintSegment(L, ix, iy, ix, iy, state.radius, state.color, state.tool === 'eraser');
  r.touched.forEach(k => state.strokes.touched.add(k));
  recompute();
  renderAll();
});

docCanvas.addEventListener('pointermove', (ev) => {
  if (!dragging) return;
  const [cx, cy] = canvasPos(ev, docCanvas);
  const [ix, iy] = canvasToImg(cx, cy);
  if (dragging.kind === 'viewport') {
    state.view = { x: ix - dragging.dx, y: iy - dragging.dy };
    paintDocCanvas(); paintScreenCanvas();
    return;
  }
  const L = activeLayer();
  if (!L) return;
  const r = paintSegment(L, dragging.last[0], dragging.last[1], ix, iy,
                         state.radius, state.color, state.tool === 'eraser');
  r.touched.forEach(k => state.strokes.touched.add(k));
  dragging.last = [ix, iy];
  recompute();
  renderAll();
});

docCanvas.addEventListener('pointerup', () => {
  if (dragging && dragging.kind === 'paint' && state.strokes.touched && state.strokes.touched.size) {
    const L = activeLayer();
    state.lastStrokeTiles = state.strokes.touched.size;
    // 记历史：用的是这一笔**开始前**保存的瓦片内容
    endOp((state.tool === 'eraser' ? '橡皮' : '画笔') + '（' + L.name + '）', L);
  } else {
    state.op = null;                          // 没画到东西就别留现场
  }
  dragging = null;
  state.strokes.touched = null;
  renderAll();
});

function inspectTile(ix, iy) {
  const x = Math.floor(ix), y = Math.floor(iy);
  if (x < 0 || y < 0 || x >= state.doc.w || y >= state.doc.h) state.inspector = null;
  else state.inspector = { tx: Math.floor(x / tilePx()), ty: Math.floor(y / tilePx()) };
  renderInspector();
  paintDocCanvas();
}

function rgbToHex(rgb) {
  return '#' + rgb.map(v => Math.round(Math.max(0, Math.min(1, v)) * 255).toString(16).padStart(2, '0')).join('');
}
function flash(msg) {
  const el = document.getElementById('docHint');
  const old = el.innerHTML;
  el.innerHTML = `<b style="color:#ffd479">${msg}</b>`;
  setTimeout(() => { el.innerHTML = old; }, 1500);
}

function syncButtons() {
  document.querySelectorAll('#toolRow button').forEach(b => b.classList.toggle('on', b.dataset.tool === state.tool));
  document.querySelectorAll('#modeRow button').forEach(b => b.classList.toggle('on', b.dataset.mode === state.mode));
  document.querySelectorAll('#tileRow button').forEach(b => b.classList.toggle('on', +b.dataset.tile === state.doc.tile));
  document.querySelectorAll('#depthRow button').forEach(b => b.classList.toggle('on', +b.dataset.depth === state.doc.depth));
  document.querySelectorAll('#zoomRow button').forEach(b => b.classList.toggle('on', +b.dataset.zoom === state.zoom));
  document.querySelectorAll('#sizeRow button').forEach(b =>
    b.classList.toggle('on', b.dataset.size === `${state.doc.w}x${state.doc.h}`));
}
document.getElementById('toolRow').addEventListener('click', e => {
  const b = e.target.closest('button'); if (!b) return;
  state.tool = b.dataset.tool; syncButtons();
});
document.getElementById('modeRow').addEventListener('click', e => {
  const b = e.target.closest('button'); if (!b) return;
  state.mode = b.dataset.mode; syncButtons(); invalidateAllTiles(); renderAll();
});
document.getElementById('tileRow').addEventListener('click', e => {
  const b = e.target.closest('button'); if (!b) return;
  // 换瓦片边长 = 重建文档（真实产品里瓦片尺寸一般固定，不会中途改）
  resetDoc({ tile: +b.dataset.tile });
  addLayer('图层 1');
  const L = activeLayer();
  beginOp();
  const r = paintSegment(L, 150, 120, 300, 220, 40, [0.30, 0.65, 1.0], false);
  recompute();
  state.lastStrokeTiles = r.touched.size;
  endOp('同一笔（边长 ' + b.dataset.tile + '）', L);
  syncButtons(); renderAll();
});
document.getElementById('depthRow').addEventListener('click', e => {
  const b = e.target.closest('button'); if (!b) return;
  state.doc.depth = +b.dataset.depth; syncButtons(); renderAll();
});
document.getElementById('zoomRow').addEventListener('click', e => {
  const b = e.target.closest('button'); if (!b) return;
  state.zoom = +b.dataset.zoom; syncButtons(); paintScreenCanvas(); renderStats();
});
document.getElementById('sizeRow').addEventListener('click', e => {
  const b = e.target.closest('button'); if (!b) return;
  const [w, h] = b.dataset.size.split('x').map(Number);
  resetDoc({ w, h });
  addLayer('图层 1');
  const L = activeLayer();
  beginOp();
  paintSegment(L, w * 0.15, h * 0.2, w * 0.8, h * 0.75, Math.round(tilePx() / 5),
               [0.30, 0.65, 1.0], false);
  recompute();
  endOp('换文档尺寸后画一笔', L);
  syncButtons(); renderAll();
});
document.getElementById('brushRange').addEventListener('input', e => {
  state.radius = +e.target.value;
  document.getElementById('brushLabel').textContent = state.radius;
});
document.getElementById('colorPick').addEventListener('input', e => {
  const h = e.target.value;
  state.color = [parseInt(h.slice(1, 3), 16) / 255, parseInt(h.slice(3, 5), 16) / 255,
                 parseInt(h.slice(5, 7), 16) / 255];
});
document.getElementById('memRange').addEventListener('input', e => {
  state.memBudgetMB = +e.target.value;
  document.getElementById('memLabel').textContent = state.memBudgetMB + ' MB';
  enforceMemoryBudget(); recompute(); renderAll();
});
document.getElementById('btnUndo').onclick = undo;
document.getElementById('btnRecompute').onclick = () => { invalidateAllTiles(); renderAll(); };
document.getElementById('btnAddLayer').onclick = () => {
  addLayer(null);
  state.lastStrokeTiles = 0;
  renderAll();
};
document.getElementById('btnDelLayer').onclick = () => {
  const d = state.doc;
  if (d.layers.length <= 1) { flash('至少留一层'); return; }
  const L = d.layers[d.active];
  [...state.tiles.keys()].forEach(k => { if (k.startsWith(L.id + ':')) state.tiles.delete(k); });
  d.layers.splice(d.active, 1);
  d.active = Math.max(0, Math.min(d.active, d.layers.length - 1));
  invalidateAllTiles(); renderAll();
};
document.getElementById('btnUp').onclick = () => moveLayer(+1);
document.getElementById('btnDown').onclick = () => moveLayer(-1);
function moveLayer(dir) {
  const d = state.doc, i = d.active, j = i + dir;
  if (i < 0 || j < 0 || j >= d.layers.length) return;
  [d.layers[i], d.layers[j]] = [d.layers[j], d.layers[i]];
  d.active = j;
  invalidateAllTiles(); renderAll();
}
document.getElementById('btnPrev').onclick = () => goStep(state.step - 1);
document.getElementById('btnNext').onclick = () => goStep(state.step + 1);
window.addEventListener('keydown', e => {
  if (e.key === 'ArrowRight') goStep(state.step + 1);
  if (e.key === 'ArrowLeft') goStep(state.step - 1);
});

// ------------------------------------------------------------------ 渲染与步骤
function paintAll() { paintDocCanvas(); paintScreenCanvas(); }
function renderAll() {
  renderLayerList(); renderInspector(); renderStats(); renderHistory(); paintAll();
}

function renderStepList() {
  const host = document.getElementById('stepList');
  host.innerHTML = STEPS.map((s, i) =>
    `<div class="step ${i === state.step ? 'active' : ''}" data-i="${i}">
       <div class="no">${i + 1}</div><div class="tt">${s.t}</div></div>`).join('');
  host.querySelectorAll('.step').forEach(el => { el.onclick = () => goStep(+el.dataset.i); });
  document.getElementById('stepBadge').textContent = `${state.step + 1} / ${STEPS.length}`;
}

function goStep(i) {
  if (i < 0 || i >= STEPS.length) return;
  state.step = i;
  renderStepList();
  document.getElementById('explain').innerHTML = STEPS[i].html;
  STEPS[i].run();
  renderAll();
}

function renderSources() {
  document.getElementById('sources').innerHTML = [
    ['[官方] 暂存盘 Scratch Disks', 'Photoshop 首选项 → 暂存盘'],
    ['[官方] 缓存级别 Cache Levels', '首选项 → 性能（1–8 层，用于缩放显示加速）'],
    ['[官方] 历史记录的内存占用', '官方帮助：历史状态会占用内存并可写入暂存盘'],
    ['[官方] 位深与精度', '8/16/32 位/通道；多数运算在更高精度进行'],
    ['[共识] 128×128 瓦片', '业界长期引用的内部实现数字，Adobe 未文档化'],
    ['[源码] 按 tile 惰性渲染', 'gimp-master/app/gegl/gimptilehandlervalidate.c'],
    ['[源码] 投影与脏区失效', 'gimp-master/app/core/gimpprojection.c'],
    ['[源码] 区域撤销', 'gimp-master/app/core/gimpdrawableundo.h'],
  ].map(([k, v]) => `<div>${k} → <span class="p">${v}</span></div>`).join('');
}

// 启动
renderSources();
resetDoc({});
addLayer('背景');
fillLayer(activeLayer(), [1, 1, 1, 1]);   // 白底：整层瓦片被分配
recompute();
syncButtons();
goStep(0);
