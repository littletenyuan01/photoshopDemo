# 开发路线

## Phase 0 — 工程与文档

- [x] Qt 空窗口工程
- [x] `.gitignore`
- [x] 项目 Wiki
- [x] `docs/` 技术文档骨架
- [x] Cursor 工程规则（文档同步 + commit 文案）
## Phase 1 — 能看见图

- [x] Document / Layer 数据模型（基础）
- [x] Canvas 显示合成结果
- [x] 新建 / 打开图像
- [x] 图层面板（列表、显隐、透明度、增删排序）

## Phase 2 — 能编辑

- [x] 画笔、橡皮（圆形 dab + 线段插值，写活动层）
- [x] 活动层绘制
- [x] **结构收口（留缝）**：信号分级 + 语义化 setter + 累计脏区（见 [docs/ui/ui-review.md](../docs/ui/ui-review.md)）
- [x] **附带收口**：抽出 `tools/` 工具层与 `app/AppSession` 广播（越晚做越贵的结构债）
- [x] 撤销 / 重做（`HistoryStack` + 四类 UndoItem；Ctrl+Z / Ctrl+Y）

## Phase 3 — 链路闭合

- [x] 导出 PNG / JPEG（`RasterIo`；文件→导出 / 导出为…）
- [x] 菜单与快捷键整理（缩放、选区、撤销、清除/填充、空格平移、X/D、[ ] 笔刷…）
- [x] 按 Wiki 自测整条演示路径（见 [Feature-Pipeline.md](Feature-Pipeline.md)「自测脚本」）

## Phase 4 — 简历打磨（可选）

- [x] 截图 / 简短演示说明（`wiki/Demo.md`；静态图见 `docs/images/`；整窗 `workspace.png` 可自行补拍）
- [x] README 与 Wiki 对齐（能力表、结构、演示入口、简历话术）
- [x] 控制总代码量：盘点无孤立实验源文件（`psDemo.pro` 与目录一致）；未做破坏性删减
  - 通道/路径面板等 UI 占位**保留**（对齐 PS 壳，tooltip 已标未接入）

## Phase 6.5 — 轻量算子壳（engine/op，无 GEGL）

> 对照 GEGL/`gimp_operations_init`：**注册表 + pad 依赖 + Runner 调度**，不引入 GEGL 库。  
> Buffer：`OpRunner` 显式 prepare→process→finish。Point：`PointOpRegistry`（合成热路径）。滤镜节点栈：Phase 8 首片已落地（BrightnessContrast）。

- [x] `Operation` / `PointOp` / `BufferOp` / `OpContext` / `OpPad`
- [x] `OpRegistry` + `opsInit` + `OpRunner`（缓冲算子）
- [x] `PointOpRegistry` + `layermodecatalog`（`BlendMode` → `OpName::LayerMode`）
- [x] `LayerModeOp` — Compositor 经注册表创建
- [x] `FloodFillOp` / `GradientOp` / `StampDabOp` / `SolidFillOp` — PaintEngine → OpRunner
- [x] 图层滤镜节点栈（→ Phase 8 首片：FilterStack + BrightnessContrast）

## Phase 6 — 撤销与命令层（架构核心 ①）

> **为什么不在最后做**：撤销是「改动前先推快照」的语义。若先做各按钮功能、事后补撤销，**每个改文档状态的入口都要回头改一遍**，且漏一处即静默不可撤销。故本相位必须**前置于按钮功能批量实现**。

> 【对照 GIMP】`app/core/gimpimage-undo-push.c`（46 KB）与 `gimpimage-undo-push.h` 的 **50+ 个 `gimp_image_undo_push_*` 入口**；每类对象一个 undo 子类（`gimpdrawableundo` / `gimplayerundo` / `gimpitemundo` / `gimpmaskundo` / `gimplayerpropundo` / `gimpundo.c`）。本项目取其**推入式 + 每对象一类**的语义，裁到最小集合；**不搬** GIMP 的 GObject undo 类层次规模。

> 【前置已就位】`ImageDocument` 的语义化 setter（`setLayerVisible/Opacity/Name/BlendMode`）已是唯一的属性变更入口，push 只需加在这里；不透明度滑条已改为「松手才提交」，保证一次操作 = 一次状态变更。见 [docs/ui/ui-review.md](../docs/ui/ui-review.md)。

- [x] `push` 入口最小集合（先做这 4 类即可闭环）：
  - [x] `pushDrawablePixels` — 像素改动（画笔/橡皮/滤镜），整层快照（脏矩形优化后置）
    - [x] **后置项已于 2026-10 落地**：像素撤销改为**瓦片引用快照（TileRefs）**——`TileBuffer::takeRefSnapshot()` 只存 `QImage` 共享引用（Qt 隐式共享/写时复制），出栈 `swapWithRefSnapshot()` 双向交换，标脏按 `cacheKey` **差分**只重投影受影响矩形；记账按**触碰瓦片**（`HistoryStack::refreshTopBytes`）；会改层 extent 的两处（更新链接 / 自由变换）显式 `PixelUndoMode::WholeLayer`。用例 `undocow`/`undotile`/`undoregion`/`undodirty`；计划 [docs/undo-region-plan.md](../docs/undo-region-plan.md)、评估 [docs/undo-region-review.md](../docs/undo-region-review.md)
  - [x] `pushLayerProp` — 显隐 / 不透明度 / 名称 / 混合模式 / 偏移
  - [x] `pushLayerStructure` — 新建 / 删除 / 复制
  - [x] `pushDocumentProp` — 图像大小 / 画布大小（`DocumentGeomUndo`）
- [x] `HistoryStack`：undo/redo 双栈 + 内存上限（超限丢最老）
- [x] **撤销覆盖收口（GIMP 式，非 Command 总线）**：改文档走 `ImageDocument` 语义化 API，API 内自动 push
  - 属性 undo 含图层样式 / 滤镜栈；新增 `SelectionUndo`；像素 undo 含 offset（变换扩层可逆）
  - **不做**独立 commands 层（对照 GIMP：core mutate + `push_undo`，无 GoF Command）
- [x] 菜单/快捷键接线：Ctrl+Z / Ctrl+Y，`编辑` 撤销/重做/清除/填充已解除灰色
- [x] 【约束】代码评审口径：**改文档状态而不 push = bug**

## Phase 7 — 投影与脏区分块（架构核心 ②，v1 后加强）

> 【对照 GIMP】`app/core/gimpprojection.c`（`update_region` / `priority_rect` / `iter` / `idle_id`）、`app/gegl/gimptilehandlervalidate.c`（603 行，脏区核心）、`app/core/gimpchunkiterator.c`，以及 GIMP 3.x 把 tile 实现下沉给 GEGL 后 GIMP 侧仅剩 91 行适配壳（`gimptilehandlerprojectable.c`）。

> 【本项目简化】不分块稀疏存储、不做优先级调度线程。只做「**脏矩形集合 + 分块缓存 + 按需重算**」。
>
> **复核（2026-10）**：两条简化**仍维持**——理由与"什么条件下才值得做"如下。
>
> | 简化项 | 现状 | 今天为什么仍不做 | 什么条件下才值得做 |
> |---|---|---|---|
> | 不分块稀疏存储（= 投影缓冲稀疏化） | **成立**：图层侧**已稀疏**（`TileBuffer` = `QHash` + 64×64 懒分配）；**投影侧稠密**（`m_buffer` = 整幅 `QImage` + `QBitArray` 有效位） | 投影内存 = 文档面积 × 4B（2048²→16MB、4000²→64MB），而**图层瓦片 / 撤销历史**才是内存大头（已降到触碰瓦片量级）；改造要动 `compositeRegion` 的连续写回、`CanvasView` 改按块取图、`adoptImage`、TileCache 记账，并**重验全部投影等价用例**（约 3–5 批次）——`pending-dev` 亦判定"风险大于页缓存" | 当"实际会打开的文档 > 4000²"**或**"投影缓冲字节 > 图层瓦片总字节"时 |
> | 不做优先级调度线程 | **部分成立**：优先级**排序**后来做了（`setPriorityRect` + `ScanMode::PriorityOnly / NonPriorityOnly`，对齐 GIMP `priority_rect`）；**调度线程**始终没做 | 现状是"块数 + 时间双预算 + idle 定时器"分帧（每帧 48 块 / 8ms；实测视口级脏区 **1.6ms**、整幅 2048² 排空 **74ms**、2048²×200 步跳转 **5–40ms**），已够交互；上调度线程要把**并发一致性**重新引入渲染主路径，而本工程在 A5 / #1 / #15 / #21 已踩过同类事故 | 当"视口铺满需要 > 10 帧"**或**"主线程被合成阻塞 > 50ms/帧"（用 `dirtybudget` 量化）时 |

- [x] **模型与投影严格分离**：`engine/Projection` 持有只读合成缓存；`CanvasView` 只 `sync` + 绘制
- [x] 文档级脏区信令：`markDirty(QRect)` / `dirtyRect` / `clearDirtyRect`（既有）
- [x] `Compositor::compositeRegion` + `Projection::sync` 按脏矩形就地重算（脏区对齐 64 块网格）
- [x] 投影分块有效位图（`QBitArray`）；图层属性脏区收窄到内容包围盒；通道缩略图增量合成缓存
- [x] 【成本约束】不引入后台渲染线程；保持同步，只减计算量
  - **复核（2026-10）**：仍成立，但要区分两件事——**渲染/合成全程主线程同步**（`Projection::sync`；后来加的并行是 `QtConcurrent::blockingMap`，**阻塞式**并行，不是异步）；**异步只给了"计算"**：滤镜预览/定稿走 `LayerFilterJob`（`filterasync` / `filterpreview` / `jobteardown`），GPU 探测走 `RasterScheduler` 的单线程探测池。⇒ **"滤镜异步" ≠ "后台渲染线程"**。
  - **什么条件下才值得做**：当"合成单帧 > 16ms 且已排除滤镜因素"时。当前不满足（视口级脏区 1.6ms；贵的从来是重滤镜，那个已经异步化了）。
  - **想更跟手时的更省替代**：调常量 `kProjectionBudgetMs` 8→12ms、`kSyncPriorityBudget` 48→96（2 行，用 `dirtybudget` 做前后实测对比）。

## Phase 8 — 节点化非破坏编辑（架构核心 ③）

> 【对照 GIMP】`gimpapplicator.c`、`gimpdrawablefilter.c`（94 KB）、`gimpfilterstack.c`、`gimpfilteredcontainer.c`，配合 `app/operations/layer-modes/`（17 个 GEGL op）——**滤镜 = 图层上的可重排、可带蒙版、可开关的节点**，而非一次栅格化。

> 【本项目简化】**不引入 GEGL**，只取其「滤镜是节点、可重排可开关」的语义，做成**只读滤镜节点栈 + 扁平的 `Layer`**。

- [x] `Layer` **允许无可编辑像素**（调整层无自有像素；滤镜节点只声明参数与输入）
- [x] 滤镜节点栈：增 / 删 / 开关 / 参数（`FilterNode` + `FilterStack`；重排后置）
- [x] 调整层 MVP：`LayerKind::Adjustment` + `Compositor` 对下方阶段性合成（dst）应用滤镜；首种亮度/对比度（色阶/曲线后置）
- [x] 投影管线支持节点求值：`Compositor` 对启用滤镜层 `materialize`→`filters.apply`→混合；【前置】Phase 7 已在位
- [x] 【硬约束】节点栈只读，不得就地改写 `Layer` 瓦片（`FilterEval` 只改临时图）
- [x] 首个滤镜：`OpName::BrightnessContrast` + 菜单「图像→调整→亮度/对比度」（默认参数，无对话框）
- [x] `.pslite`：层 `kind` + 滤镜栈；菜单/图层面板「新建调整图层」

> **Phase 8 止于「能演示非破坏节点」**。产品级滤镜库（实时拖参、长链、大图不卡）见 **Phase 9**。

## Phase 9 — 迷你图引擎（仿 GEGL 语义）+ 滤镜库

> 【结论】**主路径已落地，不另起炉灶**——在 `engine/op` + `FilterStack` + `engine/graph` 上演进成「迷你 GEGL」。  
> 【目标】能力对齐 GEGL 的 ROI / 异步 / 多线程图求值；**仍不捆绑 GEGL 库**。  
> 【前置】Phase 7 脏区 + Phase 8 节点栈。专文：[docs/graph-engine.md](../docs/graph-engine.md)、[docs/engine/operators.md](../docs/engine/operators.md)、[docs/engine/roi.md](../docs/engine/roi.md)、[docs/filter-roadmap.md](../docs/filter-roadmap.md)。

> **进度同步（2026-10）**：下列能力**已在代码与常驻用例中**，本页勾选与之对齐；勾空项 = 真未做或刻意不做。
>
> | 能力 | 状态 | 落点 |
> |------|------|------|
> | `engine/graph/`（`PsGraph` / ROI 反向 / 节点缓存 / 脏传播） | ✅ | `psgraph.*`、`graphselfcheck`、`t_graphcheck` |
> | 滤镜链入图 + 就地求值 | ✅ | `FilterStack::apply`、`PointFilterOp` |
> | 合成入图（校验路径） | ✅ | `CompositeGraph`；**生产合成仍走** `Compositor::compositeRegion` |
> | 层复合栅格区域化 + pad | ✅ | `Layer::compositeRasterRegion`（有启用样式仍整层） |
> | 投影分块并行 | ✅ | `Projection` + `QtConcurrent`（阻塞式并行，非图内调度器） |
> | `FilterOp` 族（三段式 + **每线程实例**） | ✅ | 邻域/几何滤镜已迁入；点滤镜仍直接分发（无资源） |
> | ROI ①②（`srcRect`/`outRect`、位置相关整层） | ✅ | `FilterContext`、`roictx` / `posasync`；**ROI ③ 不做** |
> | 调整层 + 面积算子 | ✅ | 子缓冲外扩；`adjarea` |
> | 异步预览/定稿 + 可取消/接替 | ✅ | `LayerFilterJob`；`filterasync` / `filterpreview` / `jobteardown` |
> | 区域滤镜缓存（世代号失效） | ✅ | 按层 + 区域；**非**逐节点参数指纹表 |
> | 滤镜产品面（对话框/面板/撤销） | ✅ MVP | `FilterDialog`、滤镜栈面板、`pushLayerPropUndo` |
> | 滤镜库扩面 | ✅ 至 C1 | 见 `filter-roadmap.md`；D/F 可选尾巴 |
> | 通用 `GraphScheduler` / 生产路径改走整图 DAG | ❌ 不做 | 现路径已够演示；图路径作校验 |
> | 逐节点参数指纹缓存、滤镜蒙版、独立滤镜浏览器 | ❌ 未做 | 后置 |

### 9.0 映射表（相对 GEGL）

| GEGL 概念 | 本项目现状 | 备注 |
|-----------|------------|------|
| Operation | `BufferOp` / `FilterOp` / `GraphOp` + `OpName` | 编辑命令 / 滤镜临时图 / 图求值三条壳，职责分离 |
| 注册表 | `OpRegistry` + `PointOpRegistry` + `FilterOp` 表 | 菜单已分类接线；无独立「滤镜浏览器」控件 |
| prepare→process | `OpRunner`（绘制）；`FilterOp` 每线程实例（滤镜） | 绘制仍 UI 线程短路径 |
| 节点链 / 图 | `FilterStack` + `PsGraph`（链图 / 合成图） | **线性栈为主**；合成图未挂 Projection |
| 缓冲 | `TileBuffer` 64×64；滤镜区域 `QImage` | 区域化已落地；非全稀疏图调度 |
| 脏区 → ROI | `Projection` + `requiredInputs` / pad | 面积算子外扩已接 |
| 异步 / 并行 | `LayerFilterJob`；投影块 `blockingMap` | 无独立图内调度器 |

生产链路（现状）：

```text
菜单加滤镜 → FilterStack.append(FilterNode)
合成 → compositeRasterRegion（区域+缓存）→ FilterStack::apply / FilterEval
重滤镜 → LayerFilterJob（后台条带）→ 写区域缓存 → Projection 命中
交互绘制 → PaintEngine → OpRunner → BufferOp（UI 线程）
```

### 9.0.1 改造步骤（历史顺序；多数已完成）

1. ~~统一求值入口~~ → **已做**：面积/几何走 `FilterOp`；点滤镜薄分发。  
2. ~~栈 → 图骨架~~ → **已做**：`engine/graph/`；生产合成未切换为整图调度。  
3. ~~分块 + ROI~~ → **已做**：区域栅格 + pad；缓存按区域/世代，非逐节点指纹。  
4. ~~异步~~ → **已做**：`LayerFilterJob`（非 `GraphScheduler` 名）。  
5. ~~并行线程模型~~ → **已做**：`FilterOp` 每线程实例；投影块并行。  
6. ~~产品面 MVP~~ → **已做**：对话框预览、栈面板、撤销；无滤镜蒙版 / 独立浏览器。

- [x] 文档化映射（本页 + [graph-engine.md](../docs/graph-engine.md) / [operators.md](../docs/engine/operators.md) / [roi.md](../docs/engine/roi.md)）
- [x] 落地 `engine/graph/` 骨架（单链等价 + 合成图校验）
- [x] `FilterEval` → `FilterOp` 收口（邻域/几何；点算子保持直接分发）

### 9.1 完整 ROI / 节点缓存

- [x] 滤镜求值带 **输入/输出 ROI**（`FilterContext` + `inputPad` / `requiredInputs`）
- [x] 区域输出可缓存（层区域滤镜缓存 + `contentGeneration` 失效）；图节点缓存用于图路径
- [x] 合成侧按脏块取 `compositeRasterRegion`，避免整层重跑全栈（有样式层除外）
- [x] 对照语义见 [roi.md](../docs/engine/roi.md)（自研等价物，非 GEGL）
- [ ] （可选后置）逐节点参数指纹表；有样式层的区域化

### 9.2 异步求值（不卡 UI）

- [x] 重滤镜 / 拖参：**后台**预览与定稿（`LayerFilterJob`）
- [x] 请求可取消 / **接替**（`runId` 世代）
- [x] 完成回调经 Qt 信号回主线程 → 缓存 / 投影刷新
- [x] 投影 idle 仍可主线程；滤镜求值允许线程池（与 Phase 7 约束一致）

### 9.3 多线程算子图

- [x] 滤镜/合成分块可并行（投影 `blockingMap`；滤镜条带并行）
- [x] `FilterOp`：**每线程一份实例**（写进 [operators.md](../docs/engine/operators.md)）
- [x] 画笔类仍走 UI 线程短路径，与滤镜后台隔离
- [ ] （明确不做）通用可调度算子图 + 图内依赖调度器替代现生产路径

### 9.4 滤镜库产品面（与上三项配套）

- [x] 分类菜单接线（消费 `opNameTitle` 等）；无独立滤镜浏览器窗
- [x] 常用滤镜扩面至 **C1**（对话框实时预览）；色阶/曲线等调整层对话框已有
- [x] 节点重排 / 开关 / 删除（滤镜栈面板）+ 参数/栈结构撤销
- [ ] （后置）滤镜蒙版；批次 D/F（见 [filter-roadmap.md](../docs/filter-roadmap.md)）

## Phase 5 — 有余力再做（可选 stretch，非 v1）

> 优先级低于 v1 闭环与简历打磨；**不作为完成标准**。体量大、兼容面广，仅在链路稳、代码量仍有余量时再开。

- [x] **智能对象（链接层 MVP）**：置入链接对象 + 更新链接 + 栅格化；无完整 Smart Object / 文件监视
- [x] **PSD 导入 / 导出**：✅ 子集——导出原有；导入 `PsdIo::load`（RGB8 像素层名/显隐/不透明度/混合/位置；Raw+RLE）。不追求完整 PS 特性兼容
- [ ] **插件库**：极简扩展点（如滤镜/导出钩子），进程内动态库或脚本均可；**不做** GIMP PDB / 完整插件宿主
  - 与 Phase 9 关系：插件可注册新 `OpName` 进滤镜图；**不替代** Phase 9 的 ROI/异步/多线程基建
- [x] **存储为 Web / 多格式压缩导出**（对照 PS「存储为 Web 所用格式」精简版）✅ MVP
  - **目标**：在现有「导出 PNG/JPEG」之上，提供面向交付的扁平图导出对话框：选格式、调压缩/质量、看预览与体积，再写入文件。
  - **已落地格式**：PNG（压缩级别 + 保留 alpha）、JPEG（质量、白底）、WebP（有损质量 / 无损；**依赖本机 Qt `imageformats` 插件**，无插件时菜单不列出）。
  - **UI**：文件→导出→「存储为 Web 所用格式…」（`Ctrl+Alt+Shift+S`）；左侧 2-up 原稿/优化、右侧参数、估计大小。
  - **实现落点**：`RasterIo`（`WebExportOptions` / `encodeBytes` / `exportWithOptions`）+ `ui/saveforwebdialog.*`；输入合成 `QImage`，**不改** `.pslite`。
  - **明确不做（本 Phase）**：切片、HTML、CSS、多设备套件、ICC 完整印刷管线、调色板 PNG、去元数据细控。
  - **对照**：PS Save for Web；GIMP「导出图像」+ 编码器参数。
