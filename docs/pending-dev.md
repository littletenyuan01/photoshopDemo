# 待开发 / 已知问题与完善方向

记录**已经有的功能**里，已知或可预见的问题，以及出现问题后建议怎么完善。  
不是功能清单（见 [features.md](features.md)）；这里只收「已实现但有债 / 有风险」项。

状态约定：

| 标记 | 含义 |
|------|------|
| 已实现 | 主路径可用 |
| 已知风险 | 特定条件下会暴露 |
| 完善方向 | 出问题或主动优化时的改法 |

---

## 移动工具拖图层 — 已对齐 PS 跟手 + GIMP 语义（2026-10）

**对照 GIMP**（`gimpmovetool` → `gimpeditselectiontool`）

- motion：真改 `offset`（`gimp_image_item_list_translate`）
- `preview_freeze` / `thaw`：**只冻缩略图**（`pixelsChanged`），不冻画布
- release：`gimp_image_flush` + thaw

**对照 PS 观感**

- 拖中：图层像素**连续跟着鼠标**移动（不等投影分块追上来）
- 松手：位置**立刻固定**，无「先消失再出现」

---

### 曾卡顿 / 松手闪：逻辑原因（改前）

移动卡顿不是单一 bug，而是**三条错误路径叠加**（按时间顺序）：

#### 路径 A — 每帧全量投影 sync（最初 GIMP 对齐版）

| 环节 | 做了什么 | 为何卡 |
|------|----------|--------|
| `mouseMove` | `translateLayer(..., emitContent=true)` | 每像素步进都改 offset |
| 文档 | 旧∪新 `styleBounds` 累计进 `dirtyRect`，发 `contentChanged` | 脏区沿拖拽轨迹**不断膨胀** |
| `CanvasView` | `contentChanged` → `Projection::sync()` | 对脏区内每个 64×64 块调用 `Compositor::compositeRegion` |
| 代价 | 大文档 × 多层 × 每 mouseMove 一次 | 4000×3000 级文档轻松 **<10 FPS**，图层「跟不上鼠标」 |

本质：**把「交互预览」当成「正式合成刷新」**，每帧重算多层混合，而不是只平移已算好的层像素。

#### 路径 B — `preview_freeze` 冻住画布 + 自建 live 但每帧整层 blend

| 环节 | 做了什么 | 为何仍卡 |
|------|----------|----------|
| `beginPreviewFreeze` | 拖中不发 `contentChanged` | 投影不更新，工具自建 `liveProjection` |
| `buildLive` | press 时全文档合成 below + above（**两次** `blendLayerRange`） | 按下就卡一下 |
| `mouseMove` | 每帧对脏区调用 `blendLayerRange(..., layerIndex, layerIndex+1)` | 仍走 Compositor 单层混合；有滤镜/样式/蒙版时每帧 `ensureCompositeRaster` |
| 图章 | 用裸 `materialize()`，无样式/滤镜 | 预览与最终画面不一致，且无法走快路径 |

本质：live 有了，但 **motion 仍在做合成器工作**，只是从「全栈 sync」换成「单层反复 blend」。

#### 路径 C — 投影分块预算 + live 松手无 adopt

| 环节 | 做了什么 | 现象 |
|------|----------|------|
| `Projection::sync` | 视口内限额 `kSyncPriorityBudget` 块/帧，其余 idle | 拖中若仍走 sync：视口内**一块块补洞**，图层位置滞后 |
| 松手 | 清 `liveProjection` → 再 `markDirty` + `sync` | 中间一帧画**旧投影**（层还在旧位置）→ 再重算 → **消失再出现** |

本质：**预览与投影缓冲没有无缝交接**；松手瞬间 live 没了、投影还没算完。

---

### 现不卡顿：逻辑说明（改后，2026-10）

核心思路：**拖中用「合成一次 + 图章平移」做预览；松手把 live 直接交给投影，不再重算。**

#### 逻辑优化对照

| 维度 | 改前（卡 / 闪） | 改后（跟手） |
|------|-----------------|--------------|
| **拖中谁负责画面** | `Projection::sync` 分块重合成，或 live 内每帧 `blendLayerRange` | `MoveTool::liveProjection()` 整幅 live 缓冲；CanvasView **跳过 sync** |
| **press 成本** | 两次全文档 `blendLayerRange`（below + above） | **无调整层上方**：复用 `projectionSnapshot`，只在层占位区重刷下方栈，above 烘焙一次。<br>**有调整层上方**：整幅建纯 below（不用快照），above **不**烘焙 |
| **motion 成本** | 脏区走 Compositor 叠当前层 | **图章快路径**：below 脏区 + `drawImage(stamp)`；无调整层上方时贴烘焙 above；**有调整层上方**时对补丁 `blendLayerRange` 重跑上方栈 |
| **层像素来源** | `materialize()` 裸瓦片 | `ensureCompositeRaster()`（含滤镜 + 图层样式 + origin 偏移） |
| **offset 变更** | `emitContent=true` 驱动投影 | `translateLayer(..., emitContent=false)`：只改 offset + 记脏，**不**触发投影 sync |
| **preview_freeze** | 曾误冻画布 | 只抑制 `pixelsChanged`（缩略图）；`endPreviewFreeze` 在 adopt **之前**调用，且 adopt 前 live 仍在 |
| **松手** | 清 live → sync 旧投影 → 重算 | `liveProjectionCommitted` → `Projection::adoptImage(live)`（块全标有效）→ 清 live；**零等待、无闪断** |

#### 路径 D — 上方有调整层时挖空区发白 / 未调整（曾 bug，2026-10 修）

| 改前错误逻辑 | 现象 | 改后 |
|--------------|------|------|
| `m_below = projectionSnapshot.copy()`（已含调整结果）+ 只在旧层矩形重刷下方 | 挖空区外仍是「旧投影」；挖空区内是未调整 below | 有调整层上方时 **禁用快照**，below = 纯下方栈 |
| `m_above` 在透明底上 `blendLayerRange` 烘焙，再 `SourceOver` 贴回 | 调整层滤的是透明，贴回**不会**对真实内容重跑调整 → 挖空处发白/未调 | `m_liveApplyAbove`：每帧对补丁 `blendLayerRange(live, layerIndex+1…)`，调整吃到 below+图章 |

#### 运行时序（改后）

```
mousePress
  ├─ beginPreviewFreeze()
  ├─ 若上方无调整层：below ← projectionSnapshot（层区重刷）+ above ← 烘焙
  │  若上方有调整层：below ← 全幅纯下方栈；above 空；m_liveApplyAbove=true
  ├─ stamp ← ensureCompositeRaster()（Normal 时预烘焙蒙版）
  └─ rebuildLive() → m_useLive = true

mouseMove（每步 dx,dy）
  ├─ translateLayer(index, dx, dy, emitContent=false)   // 真改 offset，不 sync
  ├─ patch = oldStamp ∪ newStamp ∪ oldBounds ∪ newBounds
  └─ rebuildLive(patch)
       ├─ 贴 below + 图章
       └─ 有调整层上方？ → blendLayerRange 上方栈（含调整）
                        ： → SourceOver 烘焙 above

mouseRelease
  ├─ endPreviewFreeze()
  ├─ emit liveProjectionCommitted(m_live)
  │    └─ CanvasView: m_projection.adoptImage(live); clearDirtyRect(); update()
  └─ clearLive()
```

#### 图章快路径适用条件

- 像素层（非调整层）
- 混合模式 = **Normal**
- 有蒙版时：press 时一次性 `applyMaskToStamp`，motion 不再走 Compositor
- **不满足**时回退：`blendLayerRange` 只重算脏区内的当前层（仍比全栈 sync 轻，但不如图章丝滑）
- **上方有启用调整层**：图章仍可用，但 above 必须每帧重跑（见路径 D）

#### 相关代码

| 文件 | 职责 |
|------|------|
| `tools/movetool.*` | below/above/stamp/live；`m_liveApplyAbove`；`liveProjection` / `liveProjectionCommitted` |
| `tools/toolcontext.h` | `projectionSnapshot`：press 时传入当前投影 |
| `engine/projection.cpp` | `adoptImage()`：松手无缝写入投影缓冲 |
| `ui/canvasview.cpp` | 拖中画 live 并跳过 sync；连接 `liveProjectionCommitted` |
| `domain/imagedocument.cpp` | `translateLayer(emitContent)`；`preview_freeze` 只冻缩略图 |

---

## 调整层拖参 / 显隐卡顿 — 改前 vs 改后对照（2026-10）

### GIMP 怎么做（对照）

GIMP **没有** PS 式常驻「调整图层」；等价物是 **drawable filter**（GEGL 算子图挂在 drawable 上）：

| GIMP | 含义 | 本项目对应 |
|------|------|------------|
| GEGL 节点缓存 | 参数变时只重算该算子，输入 buffer 复用 | 拖参时缓存 `below`/`above`，只重跑 `FilterEval` |
| `gimp_projection_flush` | 脏区 + 视口优先分块 | `Projection::sync` + idle |
| `gimp_projection_flush_now` | 同步刷完关心区域 | `Projection::flushPriority()` |
| 滤镜对话框预览 | ROI / 降采样，不全文档每 tick 全栈 | `adjustmentPreviewChanged` → CanvasView live |

本 Demo 按 **PS 调整层** 语义（独立层种 + 白蒙版 + 属性页），性能策略对齐「缓存输入、只重跑滤镜、主线程不堵滑条」。

---

### 曾卡顿：逻辑原因（改前）

用户体感常见两句：

1. **「滑条卡一会才动，动完画布才变」** → 主线程被合成堵住，事件循环转不动  
2. **「只有中间一小块先变，松手整图才变」** → 曾把预览裁成视口中心 ≤512 块（已废）

| # | 错误路径 | 做了什么 | 为何卡 / 错 |
|---|----------|----------|-------------|
| A | **拖参整幅 markDirty** | `setLayerFilterNode` → 调整层 extent≈画布 → 全文档块失效 | 每 tick 等价「整图重投影」 |
| B | **每 64×64 块重算滤镜** | `compositeRegion`：`dst.copy` + `FilterStack::apply`，无 input 缓存 | 同一 below 被反复滤 |
| C | **每块重混下方全部像素层** | 滑条一动 = 预算内全栈合成；idle 被打断 | 滑条与画布抢主线程 |
| D | **白蒙版慢路径** | `opacity≈1 && mask` 强制逐像素 `valueAt` | QPainter 整区快路径进不去 |
| E | **主线程 `ensureAdjustmentStacks`** | 定时器回调里同步 `blendLayerRange` 建 below/above（还曾分配**整文档**两张图） | **滑条 valueChanged 之后事件循环被堵** → 「滑条卡住再跳」 |
| F | **中心裁 512 预览**（中间态） | 为减负把 patch 裁成视口中心小块 | 只有一小块先变，松手才全图 sync |
| G | **显隐眼睛** | 大脏区 + 分块预算未 flush 视口就 paint | 先画旧块再一块块补洞 |

本质对比 GIMP：**把「改滤镜参数」当成「整图投影刷新」在 UI 线程做完**；正确做法是 UI 只改参数，重算进后台且复用 input。

---

### 现不卡顿：逻辑说明（改后）

核心思路：**主线程只改节点 + 合并定时器；建栈与滤镜全在 `QtConcurrent` 单飞；拖中缓存 below/above；覆盖整视口（可降采样算滤镜）。**

#### 总对照表

| 维度 | 改前（卡） | 改后（跟手） |
|------|------------|--------------|
| **滑条回调** | 间接触发主线程重合成 / 建栈 | 只 `setLayerFilterNode(..., pushUndo=false)` → `adjustmentPreviewChanged` → 启 32ms 单次定时器 |
| **谁建 below/above** | 主线程 `ensureAdjustmentStacks`（堵滑条） | **后台线程** `blendLayerRange`；主线程零合成 |
| **input 缓存** | 无，或每 tick 因 priority 微变重建 | 首次后台建好后拖参期间固定（`m_adjLiveLayer`）；视口变才丢 |
| **预览覆盖范围** | 曾裁中心 ≤512 → 小块先变 | **整视口** `priorityRect`；滤镜可在 ≤512 边长上算再放大回贴 |
| **滤镜算力** | 全分辨率点运算 / 无 LUT | `FilterEval` 模板内环 + 亮度/色阶 **LUT**；大图先缩小再滤 |
| **并发策略** | 无，或主线程串行 | **单飞**：busy 只记 `dirty`，结束再跑最新参数（不排队） |
| **松手** | markDirty + 分块 sync / 曾主线程全幅 rebuild | 清 ROI 缓存 → `syncProjection`（大脏区 `flushPriority` 视口） |
| **默认白蒙版** | 逐像素 mask | `isFullyOpaque` → 整区 Source 替换 |
| **显隐** | 视口分块未完就 paint | 脏区 ≥ 半幅文档 → `flushPriority()` 后再画 |

#### 拖参运行时序（改后）

```
QSlider::valueChanged
  └─ AdjustmentPropsHost::emitPreview
       └─ ImageDocument::setLayerFilterNode(..., pushUndo=false)
            └─ emit adjustmentPreviewChanged(layer)     // 不 markDirty、不 contentChanged

CanvasView::onAdjustmentPreview
  └─ 单次 QTimer(32ms) → flushAdjustmentPreview

flushAdjustmentPreview（主线程，必须轻）
  ├─ busy？ → m_adjPreviewDirty=true; return
  ├─ 有缓存？用 m_adjStackRect : 用整视口 priorityRect
  ├─ 浅拷贝 below/above 引用 + FilterStack 快照
  └─ QtConcurrent::run
       ├─ 无缓存：后台 blendLayerRange 建 below/above（ROI）
       ├─ 可选降采样 → FilterStack::apply → 放大回视口尺寸
       ├─ 叠 opacity/蒙版 + above
       └─ Queued：写 m_adjLive、缓存 ROI、update()；若 dirty 再 flush 一帧

sliderReleased → setLayerFilterNode(..., pushUndo=true)
  └─ adjustmentPreviewCommit → 清 live 缓存 → syncProjection（视口优先）
```

#### 为何滑条不再卡

| 改前堵点 | 改后 |
|----------|------|
| 定时器里主线程建两张全文档图 + 合成 | 建栈在 worker；主线程只投递任务 |
| 每 tick 全投影 sync | 拖参不发 `contentChanged`，不走 Projection::sync |
| 滤镜与滑条同线程 | 滤镜在线程池；滑条事件照常派发 |

#### 白蒙版 / 显隐（仍适用）

- `LayerMask::isFullyOpaque()` + Compositor 跳过逐像素蒙版  
- 大脏区 `flushPriority()`：视口立刻出图，避免旧块闪一下  

#### 相关代码

| 文件 | 职责 |
|------|------|
| `ui/adjustmentpropshost.cpp` | 滑条 → `previewChanged` / 松手 `commitChanged` |
| `ui/propertiespanel.cpp` | `setLayerFilterNode(false/true)` |
| `domain/imagedocument.cpp` | 调整层预览只发 `adjustmentPreviewChanged`；提交才脏区 + commit |
| `ui/canvasview.cpp` | `flushAdjustmentPreview` / 单飞 / ROI 缓存 / commit→sync |
| `engine/filtereval.cpp` | LUT + 模板点滤镜 |
| `domain/layermask.*`、`engine/compositor.cpp` | 白蒙版快路径 |

**已知限制**：首次建栈（大文档+多层）后台仍需时间，画布可能晚一两帧；已涂黑的蒙版走慢路径；平移/缩放会丢 below 缓存（下次预览重建）。

---

## 自由变换（Ctrl+T）— 已实现

**现状（已实现）**

- 入口：编辑→自由变换 / Ctrl+T；选项栏；右键模式（自由/缩放/旋转/斜切/扭曲/透视）
- 交互态维护四角；**拖中预览写入 `Layer::compositePreview` 工作缓冲**（不写瓦片、不扩层）；确认再一次 expand + 栅格化
- 确认走 `PaintEngine::freeTransform` → `FreeTransformOp`（`quadToQuad` 逆映射采样）
- 插值：邻近 / 两次线性 / 两次立方；水平/垂直翻转；会话内逐步撤销
- Compositor：有预览覆盖时叠「挖空后的瓦片 + 预览图」，z 序不变

**对照**：交互偏 PS Ctrl+T；预览语义对齐 GIMP composited preview 精简版（独立缓冲参与合成，确认再写 drawable）。未做：预览透明度选项、同步/异步开关、约束键等。

### 已缓解：大层拖拽扩层卡顿（2026-10，第 1 档）

**曾有问题**：预览每帧写回本层 → `expandToIncludeLocal` → 整层 `materialize`/`setFromImage`，大图拖角卡顿。

#### 逻辑优化对照（改前 → 改后）

| 维度 | 改前（写回本层预览） | 改后（会话工作缓冲） |
|------|----------------------|----------------------|
| **预览落点** | 每帧 `freeTransform` 直接写 `layer->tiles()` | 写临时 `TileBuffer`，结果挂 `Layer::compositePreview` |
| **层 extent** | 每帧 `ensureLayerFitsCorners` → 可能 `expandToIncludeLocal`（整层重分配） | 拖中**从不扩层**；仅 `commitSession` 扩一次 |
| **擦除旧预览** | 每帧 `clearLayerRect` 清旧脏∪源∪新目标，再重写瓦片 | 替换预览 QImage 即可；层瓦片在会话内保持「挖空后的真相」 |
| **文档真相** | 拖中瓦片已被改写；取消依赖整层快照回滚（含多次扩层后的尺寸） | 拖中瓦片不动（除进会话时一次挖空）；预览是合成覆盖 |
| **合成路径** | 与平常一样读瓦片（预览已在瓦片里） | Compositor：有预览时叠「瓦片（含挖空）+ 预览图」，z / 不透明度 / 混合不变 |
| **确认** | 再 `ensureLayerFitsCorners` + 正式 `freeTransform`（瓦片上已有预览痕迹，需先还原快照） | 清预览 → 还原进会话快照 → undo → **一次** expand → `freeTransform` |
| **取消** | 整层 `replaceFromImage` 回滚（可能已多次扩层） | `clearCompositePreview` + 还原进会话快照（extent 未被动过） |
| **代价模型** | O(扩层 materialize) × 拖拽帧数，大图拖出边界时爆炸 | O(目标包围盒栅格化 + 挂一张预览图) × 帧数；扩层成本摊到确认一次 |

**核心语义变化**：拖动阶段从「改 drawable 再靠 undo/快照假装可逆」改为「drawable 冻结 + composited preview」，对齐 GIMP「独立缓冲参与合成、确认再写 drawable」的精简版。

**当前做法（落地步骤）**

1. **会话工作缓冲**：`updateLayerPreview` 在临时 `TileBuffer` 上栅格化，结果挂 `setCompositePreview`；拖中不碰层 extent。
2. **确认一次定稿**：还原进会话快照 → undo → `expandToIncludeLocal` 一次 → `freeTransform` 写瓦片。
3. **取消**：`clearCompositePreview` + 整层还原进会话快照。

**仍可后置（收益递减）**：轻量扩层 API、拖拽节流、预览选项 UI。

**相关代码**

- `tools/transformtool.cpp`：`updateLayerPreview` / `commitSession` / `cancelSession`（已删拖中 `ensureLayerFitsCorners`）
- `domain/layer.*`：`setCompositePreview` / `clearCompositePreview`
- `engine/compositor.cpp`：预览覆盖叠层
- `engine/op/freetransformop.cpp`：栅格化算子

---

## 未排期：进一步对齐 GIMP 时可按模块补

若要以 GIMP Unified Transform 为齐，建议分轮，不必与上面绑在一起：

1. **约束** — Shift/修饰键与手柄约束（等比、沿边斜切、透视约束等）
2. **预览选项** — 是否显示预览、合成预览、同步、透明度等
3. **变换对象** — 选区 / 路径等，不只活动层像素
4. **clip** — 变换结果裁到画布 / 按结果调整层等策略 UI

---

## 维护

- 新发现「已有功能的坑」：在本文件追加一节（已实现 / 已知风险 / 完善方向 / 相关代码）
- 某项完善落地后：把该节改为「已缓解」并链到对应实现说明，或删除风险段并在 [features.md](features.md) 更新口径
