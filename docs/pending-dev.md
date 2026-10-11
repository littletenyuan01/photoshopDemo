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

### 已缓解：拖中每帧投影重合成（第 2 档）

**曾有问题**：拖中 `updateLayerPreview` 每帧 `markDirty` → `contentChanged` → `Projection::sync()`
（分块重合成整个预览脏区），叠加栅格化后实测约 **2~5 FPS**。

#### 改后（照搬 MoveTool 的 live 图章思路）

- `TransformTool` 新增 `liveProjection()`：会话开始时用 `ctx.projectionSnapshot` 建
  「本层之下全文档合成」（先清空本层占位再叠下方栈）；拖中每帧把 patch 还原到 below，
  再从**本层到顶** `blendLayerRange`（含 `compositePreview`）。
- 画布 `contentChanged` 见 `activeLiveProjection()` 非空即**跳过** `syncProjection`
  （与移动工具同一短路，[`ui/canvasview.cpp`](../psDemo/ui/canvasview.cpp)）。
- 提交：先 `emit liveProjectionCommitted(m_live)`（→ `Projection::adoptImage`，避免回落到旧投影再重算），
  再 `clearLive()` + 正式 `freeTransform` + `markDirty`。
- 取消：`clearLive()` 后还原 —— 取消回到会话前状态，旧投影本就正确，无闪断。

**验证（无 GUI）**：断言 identity 与 scale-50 下 `liveProjection() == 提交后合成`，**diffs=0**。

#### 仍欠（实测已定位）

栅格化仍是最大单项：1400×1100 目标、2000×1500 源下实测
**Nearest 212ms / 双线性 418ms / 双三次 1035ms**。即 **>50% 的成本不在插值**，
而在 `TileBuffer` → `patch` → `TilePatch::blit` → `TilePatch::extract` 的往返与逐像素记账。

已试「仿射增量映射 + 去掉逐像素 `destPoly.containsPoint` + 不透明像素跳过除法」：
**A/B 交替实测无收益（差异全在机器噪声内），已回退**。
真正的下一步是**去掉中间缓冲直接写目标**，或并行化（不是改像素映射数学）。

---

## 投影刷新的时间预算（第 2 档）

`Projection::sync` / `flushPriority` / `processPendingChunks` 新增 `maxMs` 时间预算
（`sync` 的两阶段**共用同一时钟**，故总耗时 ≤ maxMs）；`CanvasView` 每帧给
`sync` 与 `flushPriority` 各 8ms，idle 每 tick 8ms。

**为什么**：旧行为里大面积脏（新建调整层、层级显隐）走 `flushPriority()` **无上限**
同步刷完整个视口 —— 实测单帧 **471ms**（2000×1500 / 6 层 / 视口 1200×800）。

| | 旧（无预算） | 新（8+8ms 预算） |
|---|---|---|
| 单帧最坏 | **471 ms** | **18.5 ms** |
| 稳态每帧 | — | **16.7 ms** |
| 收尾 | 同一帧内完成 | idle 分帧（163 帧 ≈ 1.5s 后台补完） |

代价：视口分 2~3 帧渐进填满，而非一次到位；换来 UI 不冻结。

---

## 合成器热路径优化（第 2 档）

两处**逐 chunk 固定开销**（受控交替 A/B：分块路径稳定 **15~25%**）：

1. `Compositor::compositeRegion` 的清屏：旧写法每 chunk 建一次 `QPainter` + `fillRect`；
   改为逐行 `memset`（预乘透明 = 全 0，等价）。
2. 栅格层瓦片遍历：旧写法 `forEachAllocatedTile` —— 遍历该层**全部**瓦片再逐个判交，
   分块重算时是 O(块数 × 层数 × 总瓦片数)；改用新增的**只读** `TileBuffer::forEachTileInRect(rect, fn)`，
   只走「与矩形相交且已分配」的瓦片格。

**行为等价验证**：`graphSelfCheck()` 的 `checkCompositeGraph` 用**独立的图合成路径**
交叉校验分块合成结果 —— 改动后仍 PASSED（逐像素一致）。

> ⚠️ **度量纪律**：本轮曾据两次非受控运行误判「memset 带来 2.3~2.6×」，
> 交替 A/B 后修正为 **15~25%**。本机单次运行噪声可达 ±2×，
> **凡性能结论必须来自交替 A/B（或多轮 min）**，不能拿前后两次运行直接相减。

---

## 分块合成并行化（第 2 档）

`Projection::recomposeInvalidChunks` 改为「收集候选 chunk → 分批 `QtConcurrent::blockingMap`
并行合成」，各 worker 只写投影缓冲的**不相交区域**。

### 三处线程安全前置（不做就是数据竞争）

| 前置 | 处理 |
|---|---|
| `Layer::ensureCompositeRaster()` 是 `mutable` 缓存 | 并行前在**主线程** `warmLayerCachesForParallel()` 预热；只预热**有启用样式**的层（纯滤镜层后来改为按区域求值、不碰该缓存，见下节），调整层不走这条 |
| `LayerMask::isFullyOpaque()` 是 `mutable` 缓存 | 同上预热 |
| `PointOpRegistry` 常驻 `LayerModeOp` 实例是**共享可变**（`setMode` 写 `m_mode`，两线程不同图层会互踩） | 合成器不再取实例：`blendTileOnto` 改收 **`BlendMode` 按值**；注册表只保留只读的 `find()` 校验 |
| 同一 `QImage` 被多线程 `scanLine` 触发 detach | 并行前 `(void)m_buffer.bits()` 确保独占 |
| `QPainter` 对同一 paint device 不可同时绘制 | 清屏改 `memset`；调整层「整区替换」改逐行 `memcpy` |

### 自适应批大小

一次 `blockingMap` 有数 ms 固定开销（唤醒 + 汇总），故按
`(时间预算 − 批次固定开销) / 单块耗时估计` 折算批量。单块耗时用 EMA，
且**只由「足够大的批次」更新** —— 否则小批次被固定开销主导，会把估计越推越高（正反馈）。

### 受控交替 A/B（16 逻辑核）

| 测量 | 串行 | 并行 | 提升 |
|---|---|---|---|
| 整幅脏 sync + 全部 pending | 482~800 ms | **105~121 ms** | ~4.6~7× |
| 摊帧到完成（含 idle） | 103~105 帧 / 1171~1200 ms | **15 帧 / 154 ms** | ~7.8× |
| 无预算到完成 | 564~609 ms | **60~64 ms** | ~9× |
| 预算单帧（8+8ms） | 16.7~24 ms | 17~20 ms | 持平 |

**行为等价验证**：`partest`（2000×1500 / 6 层 / 含 Multiply·Screen·蒙版）断言
「分块并行合成 == 单次整体合成」，**5 轮 diffs=0**。

> ⚠️ **本项由测试抓到真 bug**：并行后 `bench` 的调整层用例**访问冲突崩溃**，
> 报 `QPainter::begin: A paint device can only be painted by one painter at a time` ——
> `blendLayerRange` 的调整层分支还残留 `QPainter`。
> 而 `partest` / `transformtest` 因**不含调整层**全程未暴露。
> 教训：等价性测试的**分支覆盖不足会漏掉整条路径**；性能用例（含调整层）反而充当了崩溃探测器。

---

## 逐像素原语：Normal 快路径（第 2 档）

**先量后改**（`micropix` 微基准，102 万像素；ns/px）：

| 步骤 | 改前 | 改后 |
|---|---|---|
| 基线（纯读 src） | 0.7~0.9 | — |
| `unpremul` 不透明 / 半透明 | 1.8 / 6.4 | 未改 |
| `Blend::pixel` Normal（**跨 TU 调用**） | 7.5~9.6 | 未改 |
| **`compositePixelOnto` Normal 不透明** | **30~36** | **19.1** |
| `compositePixelOnto` Normal 蒙版 128 | 33.7 | 19.3 |
| `compositePixelOnto` Multiply 不透明 | 34.5 | 30.8 |

**根因**：`Blend::pixel` 定义在 `blend.cpp` —— 没有 LTO 时**跨 TU 不内联**；
而 Normal 下 `Blend::pixel` 恒返回 source，使合成公式里的 `(comp/255 − cs)` 项**恒为 0**，
编译器看不见、白算一遍。

**改法**：`compositePixelOnto` 为 `BlendMode::Normal` 加内联快路径，公式化简为
`ratio*(cs − cb) + cb`。

**精确性前提**（已实测校验）：`(n/255)*255 == n` 对 n∈[0,255] 全部成立，
且 `comp/255 == cs` —— 故化简是**位等价**，不是近似。

**等价性验证**：`pixeq` 穷举 **27 模式 × 56×56 像素对 × 3 不透明度 × 4 蒙版 × 2 坐标
= 2,032,128 组**，与「改动前逐字复制的旧实现」对比 → **0 处不一致**。

**端到端**（同机，与前面几轮可比）：

| 测量 | 改前 | 改后 |
|---|---|---|
| 全幅合成（5 层） | ~470~565 ms | **~324 ms** |
| 整幅脏 sync + 全部 pending | 105~121 ms | **73 ms** |
| 摊帧到完成 | 15 帧 / 154 ms | **8 帧 / 89 ms** |
| 无预算到完成 | 60~64 ms | **36 ms** |

**仍欠**：非 Normal 模式（Multiply 等）仍付 `Blend::pixel` 的跨 TU 调用（30.8 ns/px）。
可选后续：把 `blendPerChannel` / `pixel` 移入 `blend.h` 内联，或开 LTO ——
两者都会放大编译面与构建时间，需先量收益再决定。

---

## 图引擎就地链路（第 2 档）

**问题**：`FilterStack::apply` 走图求值时，**每个节点各持一份输出缓冲** ——
`ImageSourceOp::process` 的 `copy()` 加每级 `PointFilterOp` 的 `applyNode` 触发 detach 拷贝，
线性 3 节点链共 **4 次整图拷贝**（直接循环只需 1 次）。

**改法**：`GraphOp` 增加 `canProcessInPlace()` / `processInPlace()`；
`PsGraph::evaluate` 在「算子声明可就地 **且** 该输入只被本节点消费（`consumerCount()==1`）」时，
先放掉上游缓存持有的引用（使缓冲独占），再把栅格交给 `processInPlace` **就地改写**。
放缓存只损失一次可重算的缓存，不改变正确性；有分叉（多个消费者）时自动退回拷贝路径。

**受控 A/B（2000×1500，3 节点滤镜链，各 3 轮）**：

| | 走图耗时 |
|---|---|
| 无就地 | 475.8 / 493.7 / 473.3 ms（均 481） |
| **有就地** | 484.1 / 452.3 / 453.3 ms（均 **463**） |

→ 省下约 **18 ms**（即 3 次 12 MB 拷贝），与预估一致；每轮 `diffs=0`。

### 「图持久化」经测量判定为**不必要**

只建图不求值（4 节点 + 3 连接）实测 **0.0017 ms/次（1.7 µs）**，占 475 ms 的 **0.0004%**。
持久化不会带来可测收益，反而要为「源图随每次调用变化」引入额外状态（或被迫加一层
`setSource` 协议）。故**不做**，理由与数据记录于此。

> 这条是「先量后改」的正面样本：若按直觉去「持久化图」，会写一堆状态而收益约等于 0。

---

## 层复合栅格区域化（修掉一条 **447ms** 的无界停顿）

**问题（实测）**：在带滤镜的**像素层**上画一笔（64×64 脏区），UI 线程停 **447 ms**：

| 场景（2000×1500，某层带 3 个点滤镜） | 改前 | 改后 |
|---|---|---|
| 首次 `sync(预算 8ms)` | 416.6 ms | **12.4 ms** |
| 整幅脏（栅格已缓存） | 421.6 ms | **8.9 ms** |
| **64×64 小脏区（≈一笔）** | **447.2 ms** | **8.9 ms** |
| 全量排空（整幅重投影） | ~500 ms | **93.5 ms** |

链路：`painttool` → `Tool::markDocumentDirty` → `ImageDocument::markDirty` →
**清掉活动层的整层复合栅格** → `contentChanged` → 投影里 `ensureCompositeRaster()`
缓存未命中 → **整层 materialize + 整条滤镜链 + 样式**，全幅且**不受时间预算约束**。

**改法**：新增 `Layer::compositeRasterRegion(regionDoc)` —— 只 `TilePatch::extract`
请求区、只对那块跑滤镜；合成器改为按块（`area`）请求。点滤镜逐像素，故
「按区域滤镜」与「整层滤镜后裁剪」**逐像素等价**。同时把
`warmLayerCachesForParallel()` 收窄为「只预热**有样式**的层」，否则它会把
「整层一次建完」重新引回来。

**为什么区域化该排在异步之前**：异步只是把 447 ms 挪到后台 —— UI 不冻了，但该层
每帧仍滞后 ~0.45 s，画笔照样跟不上（吞吐没变）。区域化把工作量本身降到与脏区成正比，
并让工作可分块 → 时间预算才真正生效、并行才吃得上（全量排空 500 → 93.5 ms）。

**等价性验证**：`regionfeq` 构造「某层带 3 点滤镜 + Multiply」的文档，断言
「分块投影（区域化滤镜）」== 「`Compositor::compositeViaGraph`（整层栅格）」→ **diffs=0**。

**仍欠**：**有启用样式**的层仍走整层缓存（样式要外扩 halo，区域化须带 pad 再裁回），
故这类层仍可能有同类停顿（**未实测**）。可选解法：带 pad 的区域化样式求值，
或异步 + 双缓冲投影。

---

## 移动工具逐帧路径并行化（真实文档实测）

**问题**：`演示2.pslite`（2480×3508，8.7 Mpx）里 tempGirl 层上方有「图层 4」+ 两个调整层。
关掉调整层时移动很跟手；**打开后每帧卡**。

**根因**（`tools/movetool.cpp` 的 `rebuildLive`）：上方有调整层时 `m_liveApplyAbove == true`，
每帧必须重跑**上方全部层**（调整层依赖移动后的下方结果），而这条调用是**单次串行**
`Compositor::blendLayerRange` —— 并行只加在 `Projection` 里，**工具层绕过了它**。

**改法**：新增 `Compositor::blendLayerRangeParallel()` —— 把 rect 切 64×64 块交
`QtConcurrent::blockingMap`；各块只读写自己那块 dst 区域（栅格层读层瓦片、调整层
`dst.copy(area)` 后写回同区），互不相交。`MoveTool` 的逐帧与按下路径改用它。
顺带把预热逻辑上提为 `Compositor::warmLayerCaches()`（原 `Projection::warmLayerCachesForParallel`）。

**实测（同一文档、同进程交替 A/B）**：

| | 串行 | 并行 | 提升 |
|---|---|---|---|
| 每帧上方层合成 | 30.2 ms (33 fps) | **3.5 ms (283 fps)** | **8.5×** |
| 端到端每帧（贴 below 补丁 + 贴 stamp + 上方层） | 71.2 ms (14.0 fps) | **13.7 ms (73.1 fps)** | **5.2×** |

**等价性**：串行 vs 并行整幅合成 **diffs=0**。

**仍欠**：端到端剩下的 ~10 ms 是两处 `QPainter::drawImage`（贴 below 补丁 / 贴 stamp）；
要再快就得把它们换成分块 `memcpy` 并与上面一起并行。

---

## 调整层参数持久化：核对结论是「本来就对」，但补掉一个真实缺口

用户报告「重新打开文档后滑动条回到 0」。逐环节核对（用其真实文件 `演示2.pslite`）：

| 环节 | 结论 | 证据（本轮新增的验证程序） |
|---|---|---|
| 文件里有没有参数 | **有** | `dumppslite`：亮度/对比度 `bri=0.08 con=0.10`；色阶 `levels=137/168/4.79` |
| 存读往返 | **OK** | `roundtrip`：kind + 全部参数逐项一致 → `ROUNDTRIP OK` |
| 回填到滑动条 | **OK** | `adjui`（离屏实例化面板）→ 8 / 10 / 137 / 168 / 479 |
| 重开后是否作用到画面 | **是** | `adjapply`：关掉两个调整层前后，视口 600×600 有 **99.17%** 像素不同 |

所以「参数丢失」在当前代码里**不复现**。但核对中发现一个**真实缺口**并已修：
`AdjustmentPropsHost` 的提交信号只挂在 `QSlider::sliderReleased` —— 在**数值框里输入/回车/
滚轮**改值时只走预览，**不产生 undo、也不把文档标记为已修改**，关窗时可能不提示保存，
用户的改动就丢了。

**改法**：加 `m_pendingPreview` 标记「有未提交的预览」；`sliderReleased` 与数值框
`editingFinished` 都调 `emitCommit`，无待提交改动时不提交（避免重复 undo 条目）。

**验证**（`adjui` 离屏）：改数值框 → `preview+1 commit+0`；回车 → `preview+2 commit+1`。

> 两条测试自身的坑（记下来免得再犯）：
> ① `host.findChild<QSlider*>("slider_gamma")` 会**全树**搜索 —— 色阶页与曝光页同名，
> 必须按「当前页」查找，否则测出假值；
> ② 信号 lambda 若每轮重新 `connect` 且按引用捕获**局部**计数器，旧 lambda 会写悬垂引用 →
> 计数翻倍。要么只连一次，要么按增量断言。

---

## 面积算子 BoxBlur + ROI 外扩（滤镜库起步）

**背景**：此前所有滤镜/调整都是点算子（ROI 恒等）；`GraphOp::requiredInputs` 的外扩分支设计好了却从没被用过 —— 模糊/锐化/杂色这类「邻域算子」一个都没有。

**落地（首片）**：

- `OpName::BoxBlur` + `FilterNode::blurRadius`（含 `inputPad()`）+ `FilterStack::totalPad()`。
- `FilterEval::applyBoxBlur`：**直通 alpha** 的可分离盒模糊（两趟滑动窗口），abyss = 透明（层外即透明）。
- 图路径：`PointFilterOp::requiredInputs` 按 `inputPad()` 外扩、`process` 求值后裁回 `out`（面积算子 `canProcessInPlace()==false`，走常规拷贝路径）。
- 像素层滤镜（生产路径）：`Layer::compositeRasterRegion` 取「请求区外扩 totalPad」再求值、裁回。
- `.pslite` 增存 `blurRadius`（仍在 512B 预留块内）。

**验证（常驻用例 `areafilter`，12/12 通过）**：

- 图路径 BoxBlur == 直接 FilterEval（diffs=0）—— ROI 外扩/裁回真走通；
- **像素层 BoxBlur 分块投影 == 整幅合成（diffs=0）—— 跨 64×64 块无接缝**（面积算子最易错处）；
- 半径变化改变结果；BoxBlur 非调整层算子（护栏）；往返保住 radius。

**关键结论（为什么先做面积算子而非菜单）**：这是结构上真正缺的一块；且 `compositeRasterRegion` 读的是**层自己的瓦片**、不碰共享投影缓冲，pad 天然无接缝 —— 像素层面积算子这一步是**安全且正确**的。

### BoxBlur 内环与临时缓冲（**先量后改**）

**基线（改前）**：

| 场景 | applyNode | 两次触页分配 | 分配占比 |
|---|---|---|---|
| 64×64 r=2（每块） | 0.084 ms | 0.019 ms | 22.4% |
| 256×256 r=4 | 1.165 ms | 0.015 ms | 1.3% |
| 1024×1024 r=8（整层级规模） | 42.237 ms | 2.228 ms | **5.3%** |

**量出来推翻了我的假设**：我原打算按「三段式 + 常驻实例复用临时缓冲」省分配，但**在最吃时间的大块场景，分配只占 5.3%** —— 真正的开销在卷积内环本身：

1. 每像素 **4 次整数除法**，除数 `n = 2r+1` 是运行时值（约 20~40 周期/次）；
2. 垂直趟每样本调一次 `QImage::pixel()`（逐样本格式分发 + 边界检查），半径 8 时约 19 次/像素。

**改法**：

- 除法 → **倒数乘法**（`((sum + n/2) * inv) >> 32`）。在本项目取值范围内（sum ≤ 65k、n ≤ 257）该近似**精确**，已由语义 oracle 证实与整数除法逐像素一致。
- 垂直趟 → 预先取**行首指针**（`constScanLine` / `scanLine`），消除逐样本 `pixel()` 与 `scanLine()` 的 detach 检查。
- 临时缓冲 → **线程本地池**（`thread_local AreaBufferPool`，只缓存最近尺寸、上限 1 Mpx ≈ 4 MB/块；超限退回「用后即弃」）。用 `thread_local` 是因为并行分块下同一算子会被多个工作线程同时调用，全局静态会数据竞争。

**结果（累计）**：

| 场景 | 基线 | 现在 | 提升 |
|---|---|---|---|
| 64×64 r=2（每块） | 0.084 ms | **0.039 ms** | **2.15×** |
| 256×256 r=4 | 1.165 ms | **0.629 ms** | **1.85×** |
| 1024×1024 r=8 | 42.237 ms | **23.914 ms** | **1.77×** |

**仍欠（已量出方向）**：1024² 现在 23.9 ms / 2.1 M 像素趟 ≈ **11.4 ns/像素趟**，对滑动窗口盒模糊仍偏慢，疑为**垂直趟的访存局部性**（按列扫描 = 每像素跨 17 行、每行跨 4 KB，等于每像素多次 cache miss）。标准解法是**按列块**（如 64 列一组）同时维护 running sums，使每行读取连续、sums 常驻 L1 —— 未做。

**仍欠（下一轮）**：

- **调整层 + 面积算子**：调整层滤镜的是「下方合成结果」= 共享投影缓冲，其 ROI 外扩要解决「外扩区可能落在未合成的块上」→ 需要合成器支持**子缓冲/偏移**（在临时区算、只拷回请求区），或按层反向扫描每层 `need_i`。本轮用 `isAdjustmentOp(BoxBlur)==false` 作护栏，避免静默错像素。
- 更多面积算子：中值、锐化（USM）、Sobel、位移（参考 GIMP `gimpoperationoffset.c`）。
- abyss 策略固定为「透明」；GEGL 有 CLIP / LOOP / BLACK / WHITE / TRANSPARENT，后续可参数化。

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
