# 算子调用链与示例

> 【对照 GIMP】`app/operations/gimp-operations.c` 的 `gimp_operations_init`、
> `app/operations/layer-modes/gimp-layer-modes.c` 的 `gimp_layer_mode_get_operation`（per-mode op 缓存）、
> GEGL `GeglOperationClass::prepare/process`。
>
> 本页记录**本项目实际怎么调**（已实现），不是设想。代码引用一律给**文件 + 符号**，
> 不给行号——行号会随改动漂移。

---

## 0. 一句话

**注册表只存工厂；调度时按 `OpName` 取一个常驻实例，把本次参数写进去，跑
`prepare → process → finish`。绘制算子读写 `TileBuffer` 并返回层内脏矩形；
选区算子（`SelectPolygonOp`）写 `Selection` mask，返回文档坐标影响区。**

---

## 1. 两条调用路径

进程里同时存在两种"算子"，它们的执行方式完全不同，不要混为一谈：

| | 路径 A：缓冲算子 | 路径 B：点算子 |
|---|---|---|
| 成员 | `StampDabOp` / … / **`CloneStampDabOp`** / **`FocusDabOp`** / **`ToneDabOp`** | `LayerModeOp` |
| 基类 | `BufferOp`（有 `prepare`/`process`/`finish` 虚函数） | `PointOp`（**无任何虚函数**） |
| 触发者 | **用户输入**（鼠标 / 菜单） | **重绘**（`contentChanged` → 重投影） |
| 调度器 | `OpRunner::run(OpName, OpContext&, Configure)` | `PointOpRegistry::instance(OpName)` 后直接调 |
| 生命周期 | `configure → prepare → process → finish` | **无**，就是一个参数化函数对象 |
| 入参 | `OpContext`（`tiles` 和/或 `selection` + `clip` + `roi`） | 函数实参 `backdrop[3]` / `source[3]` / `comp[3]` |
| 出参 | `QRect`：绘制→层内脏矩形；选区→文档坐标影响区 | 就地写 `comp[3]`，无返回 |
| 上下文 / pad / 脏区 | 有 | 全都没有 |

> 路径 B 之所以这么轻，是因为 `pointop.h` 里的 `PointOp` 是**空壳**（连 `process` 虚函数都没有）。
> 它借了算子的名字与注册表，但**不是可调度算子**——参见 §10「仍欠」。

两条路径在时间上的关系：

```
用户绘制 ──► 路径 A（改像素）──► markDirty ──► contentChanged ──► 路径 B 重投影
用户套索 ──► 路径 A（SelectPolygon 改 mask）──► selectionChanged ──► 蚂蚁线（不重投影）
```

---

## 2. 启动期：只注册，不构造任何算子

```cpp
// main.cpp —— 对照 gimp_operations_init，在 MainWindow 之前
Ps::opsInit();
```

`opsInit()`（`opsinit.cpp`）做多次 `add`：

```cpp
OpRegistry::add({
    OpName::FloodFill,
    {OpPad::Tiles},
    [] { return std::unique_ptr<BufferOp>(new FloodFillOp); },   // ← 工厂 lambda，不是对象
});
...
OpRegistry::add({
    OpName::SelectPolygon,
    {OpPad::Selection},
    [] { return std::unique_ptr<BufferOp>(new SelectPolygonOp); },
});
PointOpRegistry::add({ OpName::LayerMode,
                       [] { return std::unique_ptr<PointOp>(new LayerModeOp); } });
```

`OpRegistry::add` 只是往一个函数内静态 `QHash<int, OpRegistration>` 里插一条。

**此刻进程里一个算子对象都没有**，只有工厂 lambda + pad 声明。构造推迟到第一次调度。

| 注册项 | pad | 工厂 |
|--------|-----|------|
| `OpName::StampDab` | `{Tiles}` | `new StampDabOp` |
| `OpName::FloodFill` | `{Tiles}` | `new FloodFillOp` |
| `OpName::Gradient` | `{Tiles}` | `new GradientOp` |
| `OpName::SolidFill` | `{Tiles}` | `new SolidFillOp` |
| `OpName::SelectPolygon` | `{Selection}` | `new SelectPolygonOp` |
| `OpName::SelectFlood` | `{Selection}` | `new SelectFloodOp` |
| `OpName::CloneStampDab` | `{Tiles}` | `new CloneStampDabOp` |
| `OpName::FocusDab` | `{Tiles}` | `new FocusDabOp` |
| `OpName::ToneDab` | `{Tiles}` | `new ToneDabOp` |
| `OpName::LayerMode` | —（点算子无 pad） | `new LayerModeOp` |

---

## 3. 路径 A：缓冲算子（输入驱动）

### 3.1 入口一览

| 操作 | 入口 | 最终调用 |
|------|------|----------|
| 画笔 / 橡皮 | `PaintTool::mousePress` / `mouseMove` | `PaintEngine::stampDab` / `strokeSegment` |
| 油漆桶 | `PaintBucketTool::mousePress` | `PaintEngine::floodFill` |
| 渐变 | `GradientTool::mouseRelease` | `PaintEngine::fillGradient` |
| 编辑→填充 / 清除 | `MainWindow::onFill` / `onClear` → `ImageDocument::…` | `PaintEngine::solidFill` |
| **自由套索** | `LassoTool::mouseRelease` → `ImageDocument::selectPolygon` | `PaintEngine::selectPolygon` |
| **磁性套索** | `MagneticLassoTool::mouseRelease` → 同上 | `PaintEngine::selectPolygon` |
| **魔棒** | `MagicWandTool::mousePress` → `ImageDocument::selectFlood` | `PaintEngine::selectFlood` |
| **快速选择** | `QuickSelectTool` 拖拽采样 → 同上 | `PaintEngine::selectFlood` |
| **仿制图章** | `CloneStampTool` → 同上 | `PaintEngine::cloneStampDab` / `cloneStrokeSegment` |
| **模糊/锐化/涂抹** | `FocusTool` | `PaintEngine::focusDab` / `focusStrokeSegment` |
| **减淡/海绵** | `ToneTool` | `PaintEngine::toneDab` / `toneStrokeSegment` |

`PaintEngine` 是**门面**：绘制入口组装 `OpContext::fromTiles`；选区入口组装
`OpContext::fromSelection`，再交给 `OpRunner` 一个 `Configure` lambda：

```cpp
QRect PaintEngine::floodFill(TileBuffer &tiles, const QPoint &seed, const QColor &fillColor,
                             int tolerance, bool contiguous, PaintSelectionClip clip)
{
    OpContext ctx = OpContext::fromTiles(tiles, clip);          // ① 上下文（roi 留空 = 整层）
    return OpRunner::run(OpName::FloodFill, ctx, [&](BufferOp &base) {   // ② 调度
        auto &op = static_cast<FloodFillOp &>(base);            // ③ 下转型拿具体类型
        op.setSeed(seed);                                       // ④ 写本次参数
        op.setFillColor(fillColor);
        op.setTolerance(tolerance);
        op.setContiguous(contiguous);
    });
}
```

套索对照（`gimp_channel_select_polygon`）：

```cpp
QRect PaintEngine::selectPolygon(Selection &selection, const QPolygonF &points, ChannelOp op)
{
    OpContext ctx = OpContext::fromSelection(selection);
    return OpRunner::run(OpName::SelectPolygon, ctx, [&](BufferOp &base) {
        auto &selOp = static_cast<SelectPolygonOp &>(base);
        selOp.setPoints(points);
        selOp.setChannelOp(op);
    });
}
```

> 第 ③ 步的下转型之所以安全，是因为实例就是这个 `OpName` 注册的工厂造出来的。
> 之所以**必须**下转型，是因为 `Operation` 只声明了 `id()/name()`，没有把参数放上接口——
> 参数靠具体类型的 setter 传递。详见 §10。

### 3.2 完整时序（以油漆桶为例）

```
用户点击画布
└─ CanvasView 鼠标事件 → 归一化成 ToolEvent（坐标已换算到图像坐标系）
   └─ ToolManager → PaintBucketTool::mousePress
      │
      ├─ ctx.document->pushLayerPixelsUndo(...)      ← 改像素前推快照（对照 gimp_image_undo_push_*）
      ├─ 组装 PaintSelectionClip（选区指针 + 层偏移）
      │
      └─ PaintEngine::floodFill(tiles, seed, fill, tol, contiguous, clip)
         │
         ├─ OpContext ctx = OpContext::fromTiles(tiles, clip)
         │
         └─ OpRunner::run(OpName::FloodFill, ctx, configure)
            │
            ├─ OpRegistry::find(OpName::FloodFill)         ← QHash O(1)
            ├─ resolveDependencies(reg, ctx)               ← requiredPads={Tiles} ⇒ 查 ctx.tiles
            │                                                 返回 false 就退出（连实例都不取）
            ├─ instance(OpName::FloodFill)
            │  ├─ 常驻实例表命中 → 直接返回裸指针            ← 稳态走这里：零堆分配
            │  └─ 未命中（仅首次）→ reg->create() → new FloodFillOp → 存入常驻表
            │
            ├─ configure(*op)                              ← 把本次 4 个参数写进那个常驻实例
            │
            ├─ op->prepare(ctx)                            ── 返回 false 则跳过 process
            │  ├─ ctx.tiles 非空、w/h > 0
            │  ├─ seed 在层内
            │  ├─ seed 在选区内（layerPixelSelected：层坐标 + 偏移 → 文档坐标查 mask）
            │  ├─ m_window = OpPaintClip::roiWindow(ctx.roi, w, h)   ← 消费 roi（空 = 整层）
            │  ├─ seed 必须落在窗口内
            │  ├─ m_work = materializeWindow(tiles, m_window)        ← 只物化窗口覆盖的瓦片
            │  ├─ seed 像素 == 填充色 → return false                 ← 短路：本来就没变化
            │  └─ m_region = 三态掩码（Grayscale8，窗口尺寸），清零
            │
            ├─ dirty = op->process(ctx)
            │  ├─ 连续：BFS 四邻；非连续：全窗口扫色
            │  ├─ 掩码三态：0 = 未到达、1 = 已入队、255 = 命中
            │  ├─ 按选区裁掩码（命中但未选 → 清 0）
            │  ├─ 求命中 bbox（窗口局部坐标）→ 平移到层内坐标
            │  ├─ tiles.forEachTileInRect(dirty, /*allocateMissing=*/true, …)
            │  │     → ensureTile 缺格就分配 → 逐像素写 m_fillPx
            │  └─ return dirty（**层内坐标**）
            │
            └─ op->finish(ctx)                             ← m_work / m_region = QImage()
                                                              （实例要复用，临时图必须在这里放掉）
         │
         ├─ dirty 为空 → return true（已消费点击，不改像素、不 emit）
         ├─ markDocumentDirty(ctx, dirty.translated(offsetX, offsetY))   ← 层内 → 文档坐标
         │  └─ Tool::markDocumentDirty → ImageDocument::markDirty(rect)
         │     ├─ m_dirtyRect = m_dirtyRect.united(rect)   ← 累计
         │     ├─ emit pixelsChanged(rect)
         │     └─ emit contentChanged()                    ← 触发重投影（路径 B）
         └─ 有选区则 clearSelection()
```

### 3.3 注册表与实例表是两件事

两个表都在函数内静态变量里（无全局构造顺序问题），都是 `QHash<int, …>`，键 = `static_cast<int>(OpName)`：

| 表 | 位置 | 存什么 | 谁写 | 谁读 |
|----|------|--------|------|------|
| `OpRegistry` | `opregistry.cpp` | `{OpName, requiredPads, 工厂}` | `opsInit()`（启动期一次） | `OpRunner::run` / `OpRunner::instance` |
| `OpRunner` 常驻实例表 | `oprunner.cpp` | `unique_ptr<BufferOp>` | `OpRunner::instance`（首次调度时懒建） | 同上 |

所以**稳态下每次 `run()` 是 2 次 QHash 查找 + 0 次堆分配**；只有该 `OpName` 的第一次调用会
`new`。改造前是每次调用都 `new`——画笔一笔要跑几十个 dab、合成更是逐瓦片调用。

### 3.4 三段式契约

`BufferOp` 声明三个虚函数，`OpRunner::run` 严格按序驱动：

| 阶段 | 语义 | 返回 false 的后果 |
|------|------|-------------------|
| `prepare(OpContext&)` | 校验 + 申请临时资源 | **跳过 `process`**，但 `finish` 仍会调用 |
| `process(OpContext&)` | 改像素 | — |
| `finish(OpContext&)` | 释放 `prepare` 申请的资源 | — |

两条必须记住的推论：

1. **`prepare` 是"这次到底要不要干活"的唯一判据。** 所有「参数非法 / 无事可做 / 已达成目标」
   的分支都收敛到 `return false`，`process` 就不必再写一堆防御判断。
2. **`finish` 不能假设 `prepare` 里的状态已经建立**（因为 `prepare` 可能提前 false）。
   例如 `FloodFillOp::finish` 对空 `QImage` 赋值，是安全的。

### 3.5 `process` 内部：工作窗口 + 瓦片遍历

缓冲算子统一走这个形状（`SolidFillOp` / `GradientOp` 最干净）：

```cpp
// prepare：把窗口算好存下来
m_window = OpPaintClip::operationWindow(ctx.clip, ctx.roi, tiles.width(), tiles.height());
return !m_window.isEmpty();

// process：只碰窗口覆盖的瓦片，逐像素直接写
tiles.forEachTileInRect(m_window, /*allocateMissing=*/true,
                        [&](int, int, QImage &tile, const QRect &bounds) {
    const QRect area = m_window.intersected(bounds);
    for (int ly = area.top(); ly <= area.bottom(); ++ly) {
        QRgb *line = reinterpret_cast<QRgb *>(tile.scanLine(ly - bounds.y()));
        for (int lx = area.left(); lx <= area.right(); ++lx) {
            if (!OpPaintClip::layerPixelSelected(clip, lx, ly))
                continue;
            line[lx - bounds.x()] = /* 新像素 */;
        }
    }
});
```

`OpPaintClip`（`op/paintclip.h`）提供三个窗口/选区助手：

| 助手 | 作用 |
|------|------|
| `roiWindow(roi, w, h)` | `roi ∩ 层范围`；`roi` 空 = 整层 |
| `operationWindow(clip, roi, w, h)` | `roiWindow ∩ 选区外接框`；**逐像素独立求值**的算子用这个 |
| `selectionRectInLayer(clip, w, h)` | 选区在层内坐标下的外接矩形 |
| `restoreOutsideSelection(...)` | 把子区域内、选区外的像素回滚（dab 用） |

**为什么要窗口**：`TileBuffer` 的设计是「透明 = 未分配瓦片」的稀疏存储
（见 [tiles-and-memory.md](../layers/tiles-and-memory.md)）。若算子走
`materialize()` 整层 + `setFromImage()` 整层回写，等于把稀疏设计整块废掉——
`setFromImage` 内部的 `reset()` 会**释放全部瓦片再整层重新切块**。

**一个刻意的例外**：`FloodFillOp` 的窗口只取 `roiWindow`，**不**按选区外接框截断。
因为洪泛是**传播**类算子：必须在整层上蔓延、之后再按选区裁，否则「绕过障碍连通两块不相邻选区」
的路径会被外接框切断。这与 GIMP 一致（先算 contiguous region，再 `apply_buffer` 加 mask）。

### 3.6 脏区上行：算子返回值现在是**载荷**，不是装饰

```
算子 process() 返回 QRect（层内坐标）
  → PaintEngine 原样返回
  → 工具层 .translated(offsetX, offsetY)   ← 层内 → 文档坐标
  → Tool::markDocumentDirty → ImageDocument::markDirty(rect)
      · m_dirtyRect = m_dirtyRect.united(rect)      （空矩形直接 return，不入账）
      · emit pixelsChanged(rect) / emit contentChanged()
  → CanvasView 订阅 contentChanged → Projection::sync()
      · dirty = doc.dirtyRect() ∩ 全图
      · 对齐到 64 chunk 网格
      · 若脏区 < 全图：Compositor::compositeRegion(buffer, doc, dirty)   ← 只重算这一块
        否则：Compositor::composite(doc)                                 ← 全量重合成
      · doc.clearDirtyRect()
  → update() 重绘
```

**这条链意味着算子返回的脏矩形是"投影重算多少"的直接输入**：

- 报小了 → 该刷新的像素不刷新（表现为笔迹拖尾 / 残影）
- 报大了 → 白算，Phase 7 的增量收益被吃掉
- 报空了 → 画布完全不更新

同理，`PaintEngine::stampDab` / `strokeSegment` 把 `OpRunner::run` 的返回值**往上冒泡**
（`strokeSegment` 用 `unitedDirty` 把一段里所有 dab 的脏区并起来，且用 `isEmpty()` 过滤，
避免 QRect 的 null/empty 语义把并集边界撑大），工具层不再自己推算脏区。

---

## 4. 路径 B：点算子（重绘驱动）

这条路径**完全不走 `OpRunner`**，没有 `OpContext`、没有 pad、没有三段式、没有脏区返回。

```
CanvasView 收到 contentChanged
└─ CanvasView::syncProjection()                    （只订阅 contentChanged，不重要到就整图重合成）
   └─ Projection::sync()
      └─ Compositor::composite(doc)  或  Compositor::compositeRegion(buffer, doc, dirty)
         └─ 自底向顶 for 每个可见图层
            ├─ const BlendMode mode = layer->blendMode();
            ├─ auto *modeOp = static_cast<LayerModeOp *>(
            │        PointOpRegistry::instance(layerModeOperation(mode)));
            │     ↑ 每「层」解析一次（不是每瓦片、更不是每像素）
            ├─ modeOp->setMode(mode)                  ← 唯一的参数设置
            └─ layer->tiles().forEachAllocatedTile(…)  ← 合成是只读路径：只走已分配瓦片
               └─ blendTileOnto(dst, tile, ox, oy, opacity, modeOp, area)
                  ├─ dissolving = (modeOp->mode() == BlendMode::Dissolve)
                  └─ 逐像素：
                     ├─ Premul::unpremultiplyRgb(源) / (目标)
                     ├─ modeOp->blendPixel(backdrop, source, comp)     ← 算 B(Cb, Cs)
                     │  └─ LayerModeOp::blendPixel → Blend::pixel(m_mode, …)
                     ├─ ratio = la / ar01 …（合成公式）
                     └─ 就地写 dst
```

**分工是刻意的**（对照 GIMP 的文件切分）：

| 职责 | 本项目 | GIMP |
|------|--------|------|
| 颜色混合 `B(Cb, Cs)` | `engine/blend.cpp` + `LayerModeOp` | `gimpoperationlayermode-blend.c` |
| Alpha 合成 `composite_union` | `Compositor::blendTileOnto` | `gimpoperationlayermode-composite.c`（`gimp_operation_layer_mode_composite_union`） |

注意 `forEachAllocatedTile`（`allocateMissing = false` 语义）：**合成不会为了混合而分配瓦片**。
未分配的瓦片 = 全透明，跳过即可。

> 【Phase 8，进行中】图层挂有**已启用滤镜节点**时，合成改走
> `layer->filters().apply(layer->materialize())`——对整层临时图求值，不再走瓦片循环。
> 这条路径恰好与上面相反（整层物化 + 深拷贝），脏区很小时会把增量重算的收益吃掉。
> 当前 `ImageDocument::addBrightnessContrastFilter()` / `setLayerFilterEnabled()` /
> `removeLayerFilter()` **没有任何 UI 调用点**，所以该分支暂时进不去（没有任何入口能加节点）。

---

## 5. 一次画笔笔画 = 嵌套循环

```cpp
// PaintTool::mouseMove
const QRect dirtyLocal = PaintEngine::strokeSegment(tiles, fromLocal, toLocal,
                                                   radius, fg, mode, 0.85, 0.25, clip);
```

展开成实际执行次数：

```
mouseMove 事件                                        1 次
└─ strokeSegment()                                    1 次
   ├─ step = max(0.5, radius × 2 × 0.25) = radius × 0.5
   └─ while (d <= len)                                N 次（N ≈ 段长 / step）
      └─ stampDab()                                   每次都是一次完整的 OpRunner::run
         ├─ OpRegistry::find()        QHash O(1)      ┐
         ├─ resolveDependencies()     pad 检查         │ 每次都执行
         ├─ 常驻实例查找              QHash O(1)      │ 但**零堆分配**
         ├─ configure()               写 5 个参数      │（改造前这里每次都有 new/delete）
         ├─ prepare()                 校验 + 算 dabRect│
         └─ process()                 逐瓦片逐像素画 dab
      └─ unitedDirty()                                把 N 个脏区并起来（空的不参与）
└─ markDocumentDirty()                                1 次（整段只 emit 一次）
```

`radius = 10` 时 `step = 5px`，拖 500px ≈ **100 次算子调度**。

再看 dab 的选区处理（`StampDabOp::process`）：

```
if (选区有效)
    workRect = dabRect ∩ 选区外接框          ← 选区外的瓦片整块跳过，不去画
forEachTileInRect(workRect, allocateMissing = true, …)
    ├─ 无选区：直接画
    └─ 有选区：
       area   = dabRect ∩ bounds             ← 只备份 dab 真正覆盖到的子区
       before = tile.copy(area)              ← 不是整块 64×64
       画 dab
       restoreOutsideSelection(tile, before, area 在瓦片内的位置, area 在层内的位置, clip)
```

`restoreOutsideSelection` 收**两个坐标**（`regionInImage` / `regionInLayer`）：备份图是从瓦片上
裁下来的，所以「往哪写」用瓦片内坐标，「选区判哪个像素」用层内坐标——这两个值不相等，
签名拆开就是为了不让人写错。

---

## 6. 坐标系流转

算子边界上最容易错的就是坐标。全链只有三套坐标：

| 阶段 | 坐标系 | 谁负责换算 |
|------|--------|------------|
| `ToolEvent.imagePos` | **文档** | 画布把控件坐标归一化时算好 |
| `layer->toLayerLocal(p)` | **层内** | 工具层（`PaintTool` / `GradientTool` / `PaintBucketTool`） |
| `OpContext.tiles` | **层内** | 天然如此（`TileBuffer` 的固有坐标系） |
| `OpContext.roi` | **层内** | 注入方（目前无人注入，默认整层） |
| `PaintSelectionClip.layerOffsetX/Y` | 层→文档的偏移 | 工具层组装时填 |
| 算子返回值（脏矩形） | **层内** | 算子自己保证已与层范围求交 |
| `markDirty(rect)` | **文档** | 工具层 `.translated(offsetX, offsetY)` |
| `Selection` 的 mask | **文档** | `layerPixelSelected` 内部加回 `layerOffsetX/Y` |
| `Layer::offsetX/Y` | 图层在文档中的位置 | `Layer` 持有 |

---

## 7. 实例复用带来的四条硬约束

`OpName → 单例对象`是当前调度模型的核心，代价是四条必须遵守的纪律：

| # | 约束 | 违反的后果 | 现状 |
|---|------|------------|------|
| 1 | `configure` 必须把该算子的**全部**参数写一遍 | 静默沿用上一次的值（不报错、不崩溃） | 4 个算子都写全了（dab 5 个 / 洪泛 4 个 / 渐变 9 个 / 填充 1 个） |
| 2 | `finish` 必须释放临时资源 | 临时整图跟着实例常驻到进程结束 | `FloodFillOp::finish` 放掉 `m_work` / `m_region` |
| 3 | `finish` 不能假设 `prepare` 已成功 | 崩在空指针 / 空图上 | 均按「对空值安全」写 |
| 4 | 实例表无锁，**只能单线程访问** | 数据竞争 | 目前全部在 UI 线程；引入渲染线程前必须先改（Roadmap 明确不做线程） |

> 新增算子时最容易踩的是第 1 条。它属于 `no-latent-bugs` 说的「不报错、不崩溃，但就是不工作」。

---

## 8. 失败 / 短路路径

| 情况 | 在哪短路 | 结果 |
|------|----------|------|
| `OpName` 未注册 | `OpRunner::run` 查表 | 返回空 `QRect` |
| pad 不满足（`ctx.tiles == nullptr`） | `resolveDependencies` | 同上；**连实例都不取** |
| 工厂返回空 | `OpRunner::instance` | 同上 |
| 窗口为空（选区/roi 与层不相交） | 各算子 `prepare` | 跳过 `process`，`finish` 仍执行 |
| 洪泛：seed 越界 / 不在选区内 / 不在 roi 内 / 已是填充色 | `FloodFillOp::prepare` | 同上 |
| 渐变：不透明度 ≤ 0 | `GradientOp::prepare` | 同上 |
| dab：半径 ≤ 0 | `StampDabOp::prepare` | 同上 |
| 算子返回空脏区 | 工具层判 `isEmpty()` | 不 `markDirty`、不 emit、不重投影 |

**注意**：`prepare` 短路与「真的改了像素」是两回事。工具层只看**返回的脏区是否为空**，
所以 `prepare` 里所有「无事可做」的分支都必须让 `process` 不产生脏区，否则会出现
「报了个脏区但什么都没改」的空刷新。

---

## 9. 与 GIMP 对照

| 本项目 | GIMP / GEGL |
|--------|-------------|
| `Ps::opsInit()` | `gimp_operations_init()`（`app/operations/gimp-operations.c`） |
| `OpName` 枚举 + `OpRegistry` | GEGL 类型注册表；层模式另有 `GimpLayerModeInfo.op_name`（`gimp-layer-modes.c`） |
| `layerModecatalog.h` 的 `BlendMode → OpName` | `gimp_layer_mode_get_operation_name()`（多数模式都映射到同一个 `"gimp:layer-mode"`，只有 10 个有专属 op） |
| `OpRunner::instance()` 常驻实例 | `gimp_layer_mode_get_operation()` 的 `if (!ops[mode])` per-mode 缓存 |
| `BufferOp::prepare / process / finish` | `GeglOperationClass::prepare` / `process`（GEGL 无独立 `finish`，对应 `dispose/finalize`） |
| `PointOp` + `LayerModeOp::blendPixel` | `GeglOperationPointComposer3Class::process` |
| `OpPad` / `requiredPads` | GEGL 的具名 pad（`"input"` / `"aux"` / `"output"`）——**本项目只是枚举标记，未绑定 buffer** |
| `Compositor::blendTileOnto` | `gimp_operation_layer_mode_composite_union()` |
| `Projection::sync()` + `Compositor::compositeRegion` | `GimpProjection` / `gimp_projection_update_priority_rect` |
| 工具层调 `pushLayerPixelsUndo` 后才跑算子 | `gimp_image_undo_push_*`（改之前先推快照） |

---

## 10. 已知取舍与仍欠

### 10.1 刻意的取舍

| 取舍 | 理由 |
|------|------|
| 洪泛保留一份窗口副本（不逐瓦片随机读） | 洪泛要随机访问邻居；逐像素 `tileAt()` 会变成每像素一次 QHash 查找 |
| 洪泛窗口不按选区外接框截断 | 传播语义，见 §3.5 |
| `LayerModeOp` 是纯转发壳 | 颜色算法本体在 `blend.cpp`（27 种模式共用），算子壳只负责"可命名调度 + 带 mode 参数" |
| 渐变手写预乘 SourceOver | 逐像素独立求值，省掉「底图 + overlay」两张整层临时图（旧实现各占一个文档大小） |

### 10.2 仍欠（**不得视为已完成**）

| # | 欠账 | 影响 | 计划 |
|---|------|------|------|
| 1 | `PointOp` 无 `process` 虚函数 | 路径 B 必须 `static_cast` 到 `LayerModeOp`，多态完全用不上；`PointOpRegistry` 实际只起"带缓存的 switch"作用 | Phase 8 节点栈；或反过来承认"混合不是算子"删掉这层 |
| 2 | `OpPad` 是空转的抽象 | 4 个算子都只声明 `{Tiles}`，而 `BufferOp::prepare` 默认实现已在查同一件事；`OpPad::Selection` 的 `padSatisfied` 直接 `return true` | Phase 7/8 做成真 pad（具名 + buffer 绑定），或删掉 |
| 3 | `OpContext::roi` 无上层注入方 | 算子已会消费，但目前没人设它 → 实际总是整层 | Phase 7 脏区驱动重算落地时由上层注入 |
| 4 | `opname.*` 名字表 / 各算子 `id()/name()` 无调用点 | 约 100 行 + 5 处 override 目前不产生行为 | Phase 8 滤镜列表会消费（对照 GEGL 的 `name`/`title` 键） |
| 5 | 算子与撤销没有接缝 | `OpRunner` 不知道撤销；每个调用点各自记得 `pushLayerPixelsUndo`（漏了就是静默不可撤销） | Roadmap Phase 6「命令层收口」 |
| 6 | 油漆桶空脏区时不取消选区 | 同一个"什么都没变"的点击，选区命运与成功填充时不同（`PaintBucketTool::mousePress` 的早返回跳过了 `clearSelection`） | 待定；属分支顺序问题，非算子问题 |

### 10.3 图例

- `psDemo/engine/op/opsinit.cpp` —— 注册清单（新增算子从这里开始看）
- `psDemo/engine/op/oprunner.cpp` —— 调度与常驻实例
- `psDemo/engine/paintengine.cpp` —— 四个门面函数（照抄形状最快）
- `psDemo/engine/op/solidfillop.cpp` —— 最干净的"窗口 + 逐瓦片"样板
- `psDemo/engine/op/stampdabop.cpp` —— 选区子区备份/回滚样板
