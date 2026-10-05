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

- [x] **模型与投影严格分离**：`engine/Projection` 持有只读合成缓存；`CanvasView` 只 `sync` + 绘制
- [x] 文档级脏区信令：`markDirty(QRect)` / `dirtyRect` / `clearDirtyRect`（既有）
- [x] `Compositor::compositeRegion` + `Projection::sync` 按脏矩形就地重算（脏区对齐 64 块网格）
- [x] 投影分块有效位图（`QBitArray`）；图层属性脏区收窄到内容包围盒；通道缩略图增量合成缓存
- [x] 【成本约束】不引入后台渲染线程；保持同步，只减计算量

## Phase 8 — 节点化非破坏编辑（架构核心 ③）

> 【对照 GIMP】`gimpapplicator.c`、`gimpdrawablefilter.c`（94 KB）、`gimpfilterstack.c`、`gimpfilteredcontainer.c`，配合 `app/operations/layer-modes/`（17 个 GEGL op）——**滤镜 = 图层上的可重排、可带蒙版、可开关的节点**，而非一次栅格化。

> 【本项目简化】**不引入 GEGL**，只取其「滤镜是节点、可重排可开关」的语义，做成**只读滤镜节点栈 + 扁平的 `Layer`**。

- [ ] `Layer` **允许无可编辑像素**（调整层无自有像素；滤镜节点只声明参数与输入）
- [x] 滤镜节点栈：增 / 删 / 开关 / 参数（`FilterNode` + `FilterStack`；重排后置）
- [ ] 调整层：对「已合成的下方结果」应用（色阶 / 曲线起步）
- [x] 投影管线支持节点求值：`Compositor` 对启用滤镜层 `materialize`→`filters.apply`→混合；【前置】Phase 7 已在位
- [x] 【硬约束】节点栈只读，不得就地改写 `Layer` 瓦片（`FilterEval` 只改临时图）
- [x] 首个滤镜：`OpName::BrightnessContrast` + 菜单「图像→调整→亮度/对比度」（默认参数，无对话框）

> **Phase 8 止于「能演示非破坏节点」**。产品级滤镜库（实时拖参、长链、大图不卡）见 **Phase 9**。

## Phase 9 — 迷你图引擎（仿 GEGL 语义）+ 滤镜库

> 【结论】**已有雏形，不另起炉灶**——在现有 `engine/op` + `FilterStack` 上改造成「迷你 GEGL」。  
> 【目标】能力对齐 GEGL 的 ROI / 异步 / 多线程图求值；**仍不捆绑 GEGL 库**。  
> 【前置】Phase 7 脏区 + Phase 8 节点栈；`OpRunner` 须先可线程安全（见 `docs/engine/operators.md` §7）。

### 9.0 现有雏形（已实现，改造起点）

| GEGL 概念 | 本项目现状 | 缺口 |
|-----------|------------|------|
| Operation | `BufferOp` / `OpName` / `opsInit` | 滤镜求值多走 `FilterEval`，未统一进 `OpRunner` |
| 注册表 | `OpRegistry` + `PointOpRegistry` | 滤镜目录 UI 尚未消费名字表 |
| prepare→process | `OpRunner` | 单线程常驻实例，无图调度 |
| 节点链 | `FilterNode` + `FilterStack::apply` | **线性栈**，非整图；无边/依赖 |
| 缓冲 | `TileBuffer` 64×64 | 滤镜仍 `materialize` **整层临时图** |
| 脏区 | `Projection` / `dirtyRect` | 未传到滤镜节点级 ROI |
| 异步 / 并行 | 无 | Phase 9.2 / 9.3 |

对照链路（现状）：

```text
菜单加滤镜 → FilterStack.append(FilterNode)
合成 → materialize 整层 → FilterStack::apply → FilterEval::applyNode（同步、整图）
交互绘制 → PaintEngine → OpRunner → BufferOp（另一条路，已算子化）
```

改造目标链路：

```text
FilterStack（或升级为 FilterGraph）
  → GraphScheduler（ROI + 缓存 + 线程池）
    → OpRunner / BufferOp（与画笔共用注册表）
      → 分块写回节点缓存 → Compositor 只取输出
```

### 9.0.1 改造步骤（顺序固定，避免返工）

1. **统一求值入口**：`FilterEval` 改为薄封装，内部按 `node.op()` 调 `OpRunner`（或专用滤镜 `BufferOp`）；禁止滤镜与绘制两套无关算法壳。  
2. **栈 → 图**：引入 `engine/graph/`（建议名 `PsGraph`）：`GraphNode` / `GraphEdge` / `GraphScheduler`；初期图可仍是单链，接口按图设计。  
3. **分块 + ROI**：`FilterStack::apply(QImage)` 改为对 `TileBuffer`/分块 ROI 求值；节点输出缓存带参数指纹。  
4. **异步**：`GraphScheduler` 投递到线程池；UI 只收完成信号；请求可取消合并。  
5. **并行**：无依赖分块并行；先改 `OpRunner` 线程模型（加锁 **或** 每线程实例）。  
6. **产品面**：滤镜浏览器、对话框实时预览、重排/蒙版、undo——挂在图引擎之上。

- [ ] 文档化上述映射（本小节 + `docs/architecture.md` / `docs/engine/operators.md` 互链）
- [ ] 落地 `engine/graph/` 骨架（可先空转单链，行为与 `FilterStack` 等价）
- [ ] `FilterEval` → `OpRunner` 收口（BrightnessContrast 作样板）

### 9.1 完整 ROI / 节点缓存

- [ ] 滤镜节点带 **输入/输出 ROI**：参数或上游脏区变化时，只重算相交区域
- [ ] 节点输出可缓存（按层 + 节点 id + 参数指纹）；未脏节点跳过求值
- [ ] 合成侧消费「滤镜栈脏区 ∪ 图层脏区」，避免每次 `materialize` 整层重跑全栈
- [ ] 对照：GIMP `gimptilehandlervalidate` / GEGL 的 ROI 传播语义（自研等价物）

### 9.2 异步求值（不卡 UI）

- [ ] 重滤镜 / 拖动参数时：**后台算预览**，UI 线程只投递请求与贴图
- [ ] 请求可取消 / 合并（最新参数覆盖未完成任务，防滑条拖影排队）
- [ ] 完成回调经 Qt 信号回主线程更新 `Projection` / 对话框预览
- [ ] Phase 7「投影同步不算后台线程」约束**仅限投影 idle**；本阶段允许**滤镜求值线程池**

### 9.3 多线程算子图

- [ ] 滤镜栈升级为可调度的 **算子图**（节点 = `OpName` + 参数 + 可选蒙版 pad；边 = 数据依赖）
- [ ] 图内独立子树 / 分块可并行（线程池）；共享 `TileBuffer` 访问有明确读写协议
- [ ] `OpRunner`：常驻实例加锁，或改为「每线程一份实例 / 无状态算子」——二选一写进实现说明
- [ ] 画笔类交互算子仍可走 UI 线程短路径；滤镜图与交互路径隔离，避免抢同一实例

### 9.4 滤镜库产品面（与上三项配套）

- [ ] 滤镜浏览器 / 分类菜单（消费 `opNameId` / `opNameTitle`）
- [ ] 常用滤镜扩面（模糊、锐化、色阶/曲线对话框等）；参数面板可实时预览
- [ ] 节点重排、滤镜蒙版（可后置于 9.1–9.3 跑通之后）
- [ ] 撤销：滤镜参数/栈结构变更走专用 undo（对照 `gimpdrawablefilterundo` 精简）

## Phase 5 — 有余力再做（可选 stretch，非 v1）

> 优先级低于 v1 闭环与简历打磨；**不作为完成标准**。体量大、兼容面广，仅在链路稳、代码量仍有余量时再开。

- [x] **智能对象（链接层 MVP）**：置入链接对象 + 更新链接 + 栅格化；无完整 Smart Object / 文件监视
- [ ] **PSD 导入 / 导出**：能读常见分层 PSD（至少图层像素 + 显隐/透明度）；导出尽量保留图层。不追求完整 PS 特性兼容
- [ ] **插件库**：极简扩展点（如滤镜/导出钩子），进程内动态库或脚本均可；**不做** GIMP PDB / 完整插件宿主
  - 与 Phase 9 关系：插件可注册新 `OpName` 进滤镜图；**不替代** Phase 9 的 ROI/异步/多线程基建
