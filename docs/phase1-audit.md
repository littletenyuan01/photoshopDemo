# 一期代码审计：bug / 逻辑错误候选清单

> 状态：**候选清单；A1–A36 已修，余 GPU #22**　·　2026-10
> 方法：按 8 个子系统（domain / app / io / engine·op / engine·graph / backend·cache / tools / ui）并行深审，
> 覆盖 **344 个真实源文件 / 47.8k 行**（已排除 `debug/`、`release/`、`build*/`、生成的 `moc_*`/`ui_*`）
> ⚠️ **诚实声明**：本清单由并行审计产出，**其余条目尚未逐条人工复核**。`确定` 级也可能存在误报
> （审计者可能读错了调用方）。**复核通过后才进 [pending-dev.md](pending-dev.md) 的缺陷台账并编号。**
> 相关：[architecture.md](architecture.md)、[tech-notes.md](tech-notes.md)、[pending-dev.md](pending-dev.md)
>
> **已修**：A1–A36。余下：GPU #22。

## 0. 总体判断

八个子系统的**主路径质量偏高**：瓦片边界算术、`QImage` 写时复制、每线程算子实例、
`prepare→process→finish` 三段式、直方图类算子的进出样本守恒、投影 chunk 有效位、
ROI 反向传播、`.pslite` append-only 参数块都经得起逐行核对。

**问题集中在四类，而且都是"同一模式重复出现"**：

| 模式 | 表现 | 代表 |
|---|---|---|
| **① 失效点不收口** | 缓存/状态的真值源分裂；改了一处忘了另一处 | A3、A8、A23、A30 |
| **② 跨线程碰主线程数据** | 并行合成时 worker 直接读 `Layer`/`TileBuffer`（瓦片读会写共享状态） | A5 |
| **③ 用可变下标/成员当对象身份** | 会话期间文档结构变化 ⇒ 写错层 | A10、A22、A26、A27 |
| **④ 失败路径/边界不设限** | 来自文件的计数无上限、分配不与画布求交 | A4、A7、A16、A20 |

## 1. 高危（会造成内容丢失或崩溃）

### A1 —— 自由变换抹掉选区外接框内的像素（不可逆内容丢失）`已修`
- **位置**：`engine/op/freetransformop.cpp:283`（关联 `:213-225` 求脏区、`:249-250` 清 patch、`:263-277` 写入）
- **问题**：脏区 `dirty` 只是**选区的外接矩形**，而 patch 被整块清零后只有「选区内 **且** 目标四边形内」的像素被写回，
  最后 `TilePatch::blit(tiles, dirty, patch)` 把 patch **整块 memcpy** 回去（`tilepatch.h:57` 不做掩码）
  ⇒ dirty 内所有未写到的像素被写成 0。
- **触发**：① 非矩形选区（椭圆/套索/魔棒）——外接框四角被抹成透明；② 目标四边形被旋转/斜切——包围盒内四边形外被抹掉。
- **旁证**：其余绘制类算子（`stampdabop`/`focusdabop`/`tonedabop`/`clonestampdabop`/`shapefillop`）
  都用 `OpPaintClip::restoreOutsideSelection` 回滚选区外像素 ⇒ 这里是**漏了**，不是有意语义。
- **验证**：64×64 全不透明层 + `selectEllipse(QRect(8,8,32,32))`，取源快照后 `PaintEngine::freeTransform(..., 整体右移10px, clearSource=true, clip)`；
  断言外接框内、椭圆外的像素（如 `(9,9)`）仍等于原层颜色 —— **已修**：`restoreOutsideSelection` + `phase1a` 绿。

### A2 —— 撤销"扩层类"像素操作时蒙版被重置为全白且不可恢复 `已修`
- **位置**：`domain/layer.cpp:392`（重置点）、`app/undoitem.cpp:112`（`LayerPixelsUndo::pop`）、
  `domain/imagedocument.cpp:128-129`（4 参构造 ⇒ `m_trackMask=false`）
- **问题**：`replaceFromImage` 在**尺寸变化**时执行
  `m_mask = std::make_unique<LayerMask>(width(), height(), 255)`（注释写"undo 会整栈恢复"，**对这条路径不成立**）；
  而 `pushLayerPixelsUndo` 用 4 参构造，**撤销条目根本不带蒙版**。
- **触发**：带蒙版的层 → 自由变换向外拖角（`transformtool.cpp:632` push 像素 undo、`:642` `expandToIncludeLocal` 同时扩层与蒙版）
  → Ctrl+Z：`m_pixels` 是变换前的**小尺寸**栅格 ⇒ 判定尺寸变了 ⇒ 用户蒙版灰度被换成全白，画卷重新显示，**redo 也回不来**。
- **验证**：64×64 单层 + `addLayerMask` 并把左半涂黑 → 记 mask 快照 → 模拟变换提交（push undo + expand + `replaceFromImage(74×74)`）
  → `undo()` → 断言 mask 与快照逐字节相同 —— **已修**：`pushLayerPixelsUndo` 始终跟踪蒙版。

### A3 —— 区域滤镜缓存是文档坐标，但平移图层不失效它（图层"跳回原位"）`已修`
- **位置**：`domain/layer.cpp`（`translate` / `setOffsetSilent`）
- **问题**：区域滤镜缓存是文档坐标；改 offset 不清缓存 ⇒ 合成命中旧位置。
- **修复**：`translate`/`setOffsetSilent` 调用 `clearRegionFilterResult` 并 `++contentGeneration`（复合栅格仍保留）。回归：`phase1a` A3。

### A4 —— 自由变换预览按未求交的包围盒分配工作缓冲（可 OOM）`已修`
- **位置**：`tools/transformtool.cpp`（`updateLayerPreview`）
- **问题**：`previewDestDoc` 只取四角包围盒，从不与文档求交 ⇒ 拖出视图可分配数亿像素工作缓冲。
- **修复**：`destDoc = previewDestDoc().intersected(文档矩形)`；空则清预览返回。

### A5 —— 分块并行合成让 worker 线程并发读瓦片，而瓦片读写无锁共享状态（数据竞争）`已修`
- **位置**：`engine/projection.cpp`（`blockingMap`）、`engine/compositor.cpp`（`blendLayerRangeParallel`）、
  `domain/tilebuffer.cpp`（`makeResident`）、`engine/cache/tilecache.*`（时钟 / swap I/O）
- **问题**：瓦片**读不是只读**。worker 并发 ⇒ `m_clock`/`m_usedBytes` 记账丢失（预算失控）；
  **开启换页（`PSLITE_TILE_CACHE_MB>0`）时并发 seek/read 可能读到交错数据 → 静默错像素**，
  读失败还会走降级路径把该瓦片换成全透明 → 静默丢像素。
- **修复**：`TileSharedReadGuard` / `TileCache::beginSharedRead` —— 主线程钉住文档全部层瓦片并
  `forEachAllocatedTile` 预加载；shared-read 期间 `touch` 空操作、`trim` 跳过；
  `makeResident` 在非驻留时 `Q_ASSERT`；`m_clock` 改为 `atomic`。接入 `Projection` 分块路径与
  `Compositor::blendLayerRangeParallel`。回归：`t_phase1a` A5（换页 + 并行/串行 bit-exact）。

### A6 —— 直线工具向左/向上画线不绘制 `已修`
- **位置**：`engine/op/shapefillop.cpp`
- **问题**：非 normalized 的 `QRectF` 被用来算包围盒 ⇒ 向左/上拖时 `bounds` 为空，不绘制。
- **修复**：包围盒一律 `m_rect.normalized()`；端点仍用原始 `left/top/right/bottom`。回归：`phase1a` A6。

### A7 —— `.pslite` 路径锚点数无上限 ⇒ 坏文件把 load 拖成近无限循环直到 OOM `已修`
- **位置**：`domain/path.cpp`（`Path::read`）
- **问题**：`nSub/nA` 无上限、不查流状态 ⇒ 声明 `0x7FFFFFFF` 锚点可 OOM。
- **修复**：`nSub≤4096`、`nA≤65536`，循环内查 `in.status()`，超限/`ReadCorruptData` 立即返回空路径。回归：`phase1a` A7。

### A8 —— 撤销图像/画布大小、裁剪后选区 mask 与文档尺寸失配 `已修`
- **位置**：`app/undoitem.cpp`（`DocumentGeomUndo::pop`）
- **问题**：pop 恢复文档尺寸后 `replaceFromImage` 仍缩放到旧（裁剪后）mask 尺寸。
- **修复**：先 `selection.reset(m_width, m_height)` 再 `replaceFromImage`。回归：`phase1a` A8。

### A9 —— 调整层「面积算子」分支重算 `below` 时丢掉 dst 中已有内容 `已修`
- **位置**：`engine/compositor.cpp`（面积算子分支）
- **问题**：`blendLayerRangeImpl(below, ..., lo, i)` 从 `layerBegin` 起算 ⇒ 丢掉更下层与 live 图章。
- **修复**：私有缓冲从图层 **0** 重搭到 `i`。回归：`phase1a` A9。

### A10 —— 非模态滤镜对话框用可变成员定位目标层，会写进另一图层 `已修`
- **位置**：`ui/filterstackwidget.cpp`（`onEdit`）
- **问题**：非模态期间 `setContext` 改 `m_layer` ⇒ Cancel 把 A 的 `original` 写进 B。
- **修复**：捕获 `boundLayer`（对照 `MainWindow::runFilterDialog`）。回归：`filterpanel` A10。

## 2. 中危（明显逻辑错）

| ID | 位置 | 问题 | 置信 |
|---|---|---|---|
| **A11** | `imagedocument.cpp` `scaleImage` | **已修**：按文档 sx/sy 缩放层 extent+offset+mask（`phase1a`） | 确定 |
| **A12** | `removeLayer` / `LayerStructureUndo` | **已修**：删层清 `m_editingLayerMask` 并发 `editingTargetChanged` | 确定 |
| **A13** | `resizeCanvas` | **已修**：扩展色作底 + Source 贴回旧像素（`phase1a`） | 确定 |
| **A14** | `selectfloodop.cpp` | **已修**：合并选区前把非 255 清 0（`phase1a`） | 确定 |
| **A15** | `FilterStack::kMaxFilters` | **已修**：append/addLayerFilter/写端均 ≤64（`phase1a`） | 确定 |
| **A16** | `psdio.cpp` | **已修**：层矩形 ≤30000 且 w*h 不溢出 int，否则拒载 | 可疑→已修 |
| **A17** | `croptool` | **已修**：有框时 press 不改框角；拖≥3px 才替换；双击可确认（`phase1a`） | 确定 |
| **A18** | `pentool` | **已修**：删锚后 `clampActiveAnchor` + 读写下标守卫（`pentool`） | 确定 |
| **A19** | `toolmanager` | **已修**：临时工具 begin/end 会 deactivate/activate（清拖拽态） | 可疑→已修 |
| **A20** | `canvassizedialog.cpp` | **已修**：高/宽单位分别换算（`phase1a`） | 确定 |
| **A21** | `canvasview` | **已修**：adj preview 回调先判世代再清 busy | 可疑→已修 |
| **A22** | `propertiespanel` | **已修**：换文档清 pending；仅切走活动层时 commit | 可疑→已修 |
| **A23** | `LayerPropUndo` / `LayerPixelsUndo` | **已修**：去掉蒙版时清 `m_editingLayerMask`（`phase1a`） | 确定 |
| **A24** | `FilterStack::hasPositionalOp` | **已修**：Wind 列入位置相关（`phase1a`） | 可疑→已修 |

## 3. 低危 / 设计隐患

| ID | 位置 | 问题 |
|---|---|---|
| A25 | `DocumentGeomUndo` / `LayerStructureUndo` | **已修**：byteSize 计入 maskGray / 层蒙版（`phase1a`） |
| A26 | `HistoryStack` | **已修**：`m_redoBytes` + `historyBytes()`；memstats 用合计（`phase1a`） |
| A27 | `tools/transformtool.cpp` | **已修**：会话存 `m_sessionLayer*`，`resolveSessionLayer` 校验/重定位；删层后 cancel/commit 不写错层（`phase1a`） |
| A28 | `RasterIo::writeEncoded` | **已修**：改 `QSaveFile + commit`（`phase1a`） |
| A29 | `io/projectio.cpp` | **已修**：`ProjectFormat::Current=5`，空 PNG 另存/读 `extentW/H`（`phase1a`） |
| A30 | `domain/imagedocument.cpp` | **已修**：`markDirty`/`Quiet` 失效与脏区相交的层缓存（`phase1a`） |
| A31 | `engine/projection.cpp` | **已修**：并行分支检查 `compositeRegion` 返回值；`processPendingChunks`/`flushPriority` 先 `ensureBuffer` |
| A32 | `engine/projection.cpp` | **已修**：`adoptImage(image, coveredDoc)` 仅标 covered 内块有效；Move/Transform 传入覆盖区 |
| A33 | `engine/graph/compositegraph.cpp` | **已修**：有 `compositePreview` 时与 Compositor 同口径叠瓦片+预览 |
| A34 | `engine/op/freetransformop.cpp` | **已修**：双线性/双三次在预乘空间加权（`phase1a` 中点≈128） |
| A35 | `tools/movetool.cpp` / `paintbuckettool.cpp` | **已修**：首次真实位移/有脏填充再压撤销（`phase1a`） |
| A36 | `tools/pathanchortool.cpp` | **已修**：overlay 读文档真值并尊重 `visible`；已删/隐藏不画幽灵 |

## 4. 已知且仍未修（不重复编号）

- **GPU r≥5 越界**（缺陷台账 **#22**）：`median.comp:106` 的 `chR[81]` 只支持 **r ≤ 4**，
  而 host `qbound(1,radius,8)` 与 UI 1..8 滑块都允许到 8 ⇒ **r=5..8 静默错像素**。
  详见 [gpu-plan.md](gpu-plan.md) §17.1。**至今未修。**

## 5. 建议的复核与修复顺序

**第一步：先复核 4 条"用户能撞到的数据丢失/崩溃"**（每条 5 分钟内可判真伪）

| 顺序 | ID | 为什么先它 | 复核方式 |
|---|---|---|---|
| 1 | **A1** | ~~椭圆/旋转变换不可逆抹像素~~ | **已修**（`phase1a`） |
| 2 | **A2** | ~~蒙版永久丢失~~ | **已修**（`phase1a`） |
| 3 | **A5** | ~~并行读瓦片数据竞争~~ | **已修**（`TileSharedReadGuard` + `phase1a`） |
| 4 | **A10** | ~~非模态对话框写错层~~ | **已修**（`filterpanel`） |

**第二步：改动小、收益明确的稳定性修补**
~~A4 / A6 / A7~~ **已修**；下一项：**A20**（死控件）、以及 A3/A8/A9 等

**第三步**：其余按"用户能否撞到"排序，**不要一次全修**。

> **纪律提醒**：本项目要求「**先测再改**」「被数据推翻的假设也要记」。
> 上述每条都给了 `验证` 方式 —— **复核为真**的才进 [pending-dev.md](pending-dev.md) 台账并编号（从 **#23** 起），
> **证伪的也要在本文件记一笔**（写明为什么是误报），避免下一个人重复审。
