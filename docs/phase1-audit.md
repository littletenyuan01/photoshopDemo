# 一期代码审计：bug / 逻辑错误候选清单

> 状态：**候选清单；高危 A1–A7/A10 已复核并修复（其余待续）**　·　2026-10
> 方法：按 8 个子系统（domain / app / io / engine·op / engine·graph / backend·cache / tools / ui）并行深审，
> 覆盖 **344 个真实源文件 / 47.8k 行**（已排除 `debug/`、`release/`、`build*/`、生成的 `moc_*`/`ui_*`）
> ⚠️ **诚实声明**：本清单由并行审计产出，**其余条目尚未逐条人工复核**。`确定` 级也可能存在误报
> （审计者可能读错了调用方）。**复核通过后才进 [pending-dev.md](pending-dev.md) 的缺陷台账并编号。**
> 相关：[architecture.md](architecture.md)、[tech-notes.md](tech-notes.md)、[pending-dev.md](pending-dev.md)
>
> **已修**：A1、A2、A4（预览包围盒∩文档）、A5、A6、A7、A10（`boundLayer`）；回归 `phase1a` / `filterpanel`

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

### A3 —— 区域滤镜缓存是文档坐标，但平移图层不失效它（图层"跳回原位"）`确定`
- **位置**：`domain/layer.cpp:250`（命中判定与取子图）、`layer.cpp:92-99`（`Layer::translate`）、
  `domain/imagedocument.cpp:1257-1265`（`translateLayer`，注释明说"平移不改像素，勿走 `markDirty`"）
- **问题**：`m_regionFilterRect/m_regionFilterImage`（`layer.h:336-337` 注释写明**文档坐标**）由 `LayerFilterJob`
  按**快照时刻的 offset** 写入；但改 offset 的两条路径**都不失效它**，`contentGeneration` 也只在
  `invalidateCompositeRaster` 里自增 ⇒ 平移既不清缓存、也不作废在途任务。
  注释给的理由只对**层局部**的 `m_compositeRaster` 成立，对**文档坐标**的区域缓存不成立。
- **触发**：对某层跑过重滤镜（异步定稿把区域缓存写成整层矩形）→ 移动工具拖动该层（live 路径 `finishDrag` 不发 `markDirty`、
  还清掉 dirtyRect，`tools/movetool.cpp:316-326`）→ 此后任何整幅 dirty（Ctrl+Z、增删层、切显隐）让投影重算该块
  → 命中旧缓存 → **图层回到移动前的位置**。
- **验证**：在 `t_filterasync` 区域缓存段后加：先记 `contentGeneration()`，`translateLayer(1,10,0,false)` 后断言世代号前进
  且 `regionFilterCovers(bounds)==false`（**当前两条都红**）。

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

### A8 —— 撤销图像/画布大小、裁剪后选区 mask 与文档尺寸失配 `确定`
- **位置**：`app/undoitem.cpp:342`（`DocumentGeomUndo::pop`）、`domain/selection.cpp:64-77`（`replaceFromImage` 会缩放成**当前** mask 尺寸）、
  `domain/imagedocument.cpp:1508/1563/1649`（三个正向入口）
- **问题**：pop 先把文档尺寸写回旧值，再用 `replaceFromImage` 恢复选区 —— 而它**不按文档尺寸重建 mask**，
  把旧 mask 缩放/裁剪成**操作后新尺寸**。于是撤销后文档回到旧尺寸、选区仍是新尺寸（**违反 `selection.h:58/125` 的"与文档同尺寸"不变量**）。
- **后果**：① 选区是被最近邻重采样的错内容且只覆盖左上角；`Selection::value` 越界返回 0 ⇒
  填充/描边/反选/变换 clip 被静默裁掉。② 任何按**文档尺寸**造 shapeMask 的路径会被 `selection.cpp:231-232` 的尺寸守卫直接 return ——
  `imagedocument.cpp:255` `pathToSelection` 撤销后**静默什么都不做**，却已 push 一条 undo 并 `return true`。
- **验证**：`createBlank(400,400)` → `selectRectangle(QRect(0,0,400,400))` → `cropTo(QRect(0,0,200,200))` → `undo()`
  → 断言 `selection().mask().size() == QSize(doc->width(), doc->height())`（**现在 200×200 ≠ 400×400**）。

### A9 —— 调整层「面积算子」分支重算 `below` 时丢掉 dst 中已有内容 `确定`
- **位置**：`engine/compositor.cpp:133`（关联 `:131` 点算子分支用 `dst.copy(area)`、`lo=qMax(0,layerBegin)`）、
  调用侧 `tools/movetool.cpp:297`、`tools/transformtool.cpp:492`
- **问题**：调整层两条分支**互不等价**：`srcWant==area`（点算子）时直接用 `dst.copy(area)` 当滤镜输入（dst 里是"下方全部层 + 正在拖动的图章"）；
  否则（**面积算子**，本项目明确支持的"调整层 + 面积算子"特性）在私有子缓冲里用 `blendLayerRangeImpl(below, ..., lo, i, ...)`
  从图层栈 `[lo,i)` **从零重搭** `below`，**完全不看 dst 里 `layerBegin` 之前已合成好的内容**。
  `lo=qMax(0,layerBegin)` ⇒ 只要 `layerBegin>0` 两条分支输入就不同。
- **后果**：调整层滤的是"被移动层之上、调整层之下那几层"，既看不到下方层也看不到被移动的图章；
  不透明度通常 1.0 ⇒ 走整像素替换 ⇒ 补丁区被替换成近乎空白。**更糟**：移动松手时 `MoveTool::finishDrag`
  会 `emit liveProjectionCommitted(m_live)`，`CanvasView` 执行 `Projection::adoptImage` 并清脏区 ⇒
  **这份错像素被当成全图真相写进投影且不再重算**。
- **旁证**：违反工具自己的注释（`movetool.cpp:295`「调整层依赖移动后的下方结果」、`pending-dev.md:126`「调整吃到 below+图章」）。
- **验证**：3 层文档（L0 像素、L1 像素、L2=`addAdjustmentLayer(BoxBlur, r=6)`），
  先 `blendLayerRange(live, doc, area, 0, 1, -1)` 再 `blendLayerRange(live, doc, area, 1, 3, -1)`，
  断言 `live(area) == Compositor::composite(doc)(area)`（**现在 diffs>0**）。
  把 L2 换成点算子后同一断言通过 ⇒ 证明是**分支不对称**而非算子问题。

### A10 —— 非模态滤镜对话框用可变成员定位目标层，会写进另一图层 `已修`
- **位置**：`ui/filterstackwidget.cpp`（`onEdit`）
- **问题**：非模态期间 `setContext` 改 `m_layer` ⇒ Cancel 把 A 的 `original` 写进 B。
- **修复**：捕获 `boundLayer`（对照 `MainWindow::runFilterDialog`）。回归：`filterpanel` A10。

## 2. 中危（明显逻辑错）

| ID | 位置 | 问题 | 置信 |
|---|---|---|---|
| **A11** | `domain/imagedocument.cpp:1537` | `scaleImage` 把**每层自身栅格**拉伸到整幅新画布（`src.scaled(newWidth,newHeight)`），而 offset 用**文档**系数 `:1542-1543` ⇒ 两个系数不一致：200×200 文档里 100×100、offset(50,50) 的层（`placeImageAsLayer` 置入的就是这种）在 `scaleImage(400,400)` 后变成 400×400、内容被**拉伸 4 倍**（应 2 倍）并超出画布被裁。凡尺寸≠画布的层都会在「图像大小」后失真 | 确定 |
| **A12** | `domain/imagedocument.cpp:1438` | `removeLayer` 只修活动层下标、**不清 `m_editingLayerMask`**（该标志的清理散落在 `:502-507`/`:1009-1012`/`:1082-1085`）⇒ 删掉正在编辑蒙版的层后：新活动层无蒙版 ⇒ 画笔在 `painttool.cpp:78-81/123-126` **直接 return false（"没反应"）**；新活动层有蒙版 ⇒ 笔画**静默写进另一层的蒙版**。`LayerStructureUndo::pop`（`undoitem.cpp:200-236`）同样漏了 | 确定 |
| **A13** | `domain/imagedocument.cpp:1598` | `resizeCanvas` 用扩展色 `neu.fill(扩展色)` 填**整张**新画布再 SourceOver 贴旧像素 ⇒ (a) 底层原本**透明**的内部像素被涂成扩展色（UI 选项名是"扩展颜色"，语义只该作用于**新增区域**）；(b) 原本**半透明**的像素与不透明扩展色混合后变成不透明 ⇒ **图层 alpha 被永久烘焙掉**。选"透明"时走 else 分支 ⇒ 正常（所以没被测出来） | 确定 |
| **A14** | `engine/op/selectfloodop.cpp:119` | 魔棒 BFS 把"**探测过但颜色不匹配**"的邻居在**同一张 mask** 上标为 `1`（255 才是命中，见 `:80-85`），随后整张 mask 交给 `combineShapeMask`（`:135`）；而 `Selection::isSelected` 定义为 `value > 0` ⇒ 这些像素**被判为已选**。落在返回 area 内的（如白底上黑色物体轮廓那一圈）会被真正拷进选区 ⇒ 填充/删除/绘制连带改到它们。`FloodFillOp` 在同一张 mask 上用的是 `== 255`（`floodfillop.cpp:179`）⇒ **口径不一致，本算子漏了**。仅影响连续模式 | 确定 |
| **A15** | `io/projectio.cpp:377` | 读端把 `count > 64` 当损坏直接失败（整份工程打不开），**写端 `:343` 对滤镜数完全无检查**，`FilterStack::append` 与 `ImageDocument::addLayerFilter` 也无上限 ⇒「连续加 65 个滤镜 → 保存成功 → 再也打不开」= **用户数据事实丢失**，保存时还没有任何提示。图层数/路径数（读端 ≤4096 `:467`/`:604`）同一模式 | 确定 |
| **A16** | `io/psdio.cpp:847` | 画布尺寸在 `:705` 校验（≤30000），但**图层矩形**（`:760-765` 只做截断检查）从不与画布或任何上限比较就参与 `planeBytes = lw*lh`；`readChannelPlane` 第一件事是 `out->resize(expectedBytes)`（`:173`），且 a/r/g/b **四个平面同时存活**（`:849-898`）⇒ 一条 `30000×30000` 的层记录就要 **~3.6GB**；若 `lw*lh > 2^31` 则 signed int **溢出**（UB）。触发条件是用户能打开的任意 `.psd`，后果是 `bad_alloc` 未捕获 ⇒ 进程终止 | 可疑 |
| **A17** | `tools/croptool.cpp:141` | 注释与 `croptool.h:5` 都声明"双击确认"，但 Qt 双击序列是 press→release→**dblclick-press**→release，第一次 press **不置** `doubleClick`，会落到 `:140-141` 把 `m_hasFrame` 清掉 ⇒ dblclick 分支里 `m_hasFrame` **恒为 false**，裁剪框被静默丢弃（Enter 路径 `:209-211` 正常）。**反向更糟**：双击时鼠标抖动 ≥1 文档像素（100% 缩放下 1 控件像素=1 文档像素）⇒ 第一次 click 的 release 把 1~2px 小框置为 `m_hasFrame=true` ⇒ dblclick 确认**裁出 1~2px 的画布** | 确定 |
| **A18** | `tools/pentool.cpp:243` | Ctrl+点锚点会 `PathEditOps::deleteAnchorAt → Path::deleteAnchor → sp.anchors.removeAt(...)`（`domain/path.cpp:164`），但 `m_activeIdx` **既不重选也不做边界检查**；`drawOverlay:517-519` 只判 `m_activeIdx >= 0` 就索引 `anchors[m_activeIdx]`（`mouseMove:355` 同）⇒ 删最后一个锚点后**越界读**（UB）。删中间点时下标整体前移，后续拖拽/橡皮带静默作用到**另一个锚点** | 确定 |
| **A19** | `tools/toolmanager.cpp:146` | `beginTemporaryTool` 直接改写 `m_activeTool` 并重连信号，**既不 deactivate 旧工具也不 activate 新工具**（对比 `setActiveTool:185-186`）。而 `CanvasView::mouseReleaseEvent`（`ui/canvasview.cpp:852`）只把 release 派发给"当时的活动工具"⇒ 选框拖拽中按空格（`:906`）→ 松左键（release 落到 Hand）→ 松空格恢复选框，此时选框 `m_dragging` 仍为 true、`hasOverlay()` 为 true ⇒ **橡皮筋冻结**、本次选区不提交。画笔/仿制图章/磁性套索同类残留 | 可疑 |
| **A20** | `ui/canvassizedialog.cpp:123` | `absolutePixelSize()` 只读 `widthUnitCombo` 得到 `unit`，然后**同时**用于宽和高（`:132-133`）；构造函数只连了 `widthUnitCombo→syncHeightUnitFromWidth`（`:56-57`）。`heightUnitCombo` 全工程**只被写、从未被读** ⇒ **死控件**：宽度单位=像素、高度单位=英寸、高度填 2、PPI 72 ⇒ 期望 144px，**实际得到 2px** | 确定 |
| **A21** | `ui/canvasview.cpp:1400` | `flushAdjustmentPreview` 用 `m_adjPreviewBusy` 做单飞（`:1195-1198`），但主路径回调**先在 `:1400` 把 busy 置 false，之后才在 `:1401` 判世代**（同文件另三处 `:1279-1282`/`:1294-1303`/`:1326-1334` 都是先判后置）⇒ 过期回调提前解锁 ⇒ 每个 32ms tick 都能再起一个任务，且都走 `haveStacks=false` 分支 ⇒ 在主线程之外分配**两张文档全幅 QImage**（`:1287-1290`，2480×3508 ≈ 35MB/张）并**并发调 `Compositor::blendLayerRange`** | 可疑 |
| **A22** | `ui/propertiespanel.cpp:171` | `refreshFromDocument` 开头用 `m_boundAdjLayer` 直接索引**当前** `m_document` 提交未决预览，只用 `prev->isAdjustmentLayer()` 校验、**不校验身份**；而 `onSessionDocumentChanged` 在调用它**之前**就把 `m_document` 换成新文档（`:87-90 → :114`）⇒ 打开/新建文档时，上一文档的调整参数被写进**新文档**同下标的调整层并压一条 undo（触发：点调整层滑条后用方向键改值，只发 previewChanged，`m_pendingPreview` 一直为 true）。文档内结构变化（`moveLayer` 发 `structureChanged`）同理会写错层 | 可疑 |
| **A23** | `app/undoitem.cpp:154` | `LayerPropUndo::pop` 通过 `applyLayerProps` 把快照写回，快照 `hasMask=false` 时会 `layer.setMask(nullptr)`（`:52-54`）即**蒙版消失**，但全程**不碰 `doc.m_editingLayerMask` 也不发 `editingTargetChanged`**（正向路径都会同步：`imagedocument.cpp:985-987`/`:1009-1011`/`:1082-1084`/`:502-507`）⇒「加蒙版 → Ctrl+Z」后文档一直声称在编辑蒙版，而活动层已无蒙版 ⇒ 画笔**静默失效**、面板仍画编辑目标（`ui/layertreepanel.cpp:908-911`） | 确定 |
| **A24** | `engine/op/windfilterop.cpp:55` | 风-交错用 `(y & 1)` 选方向，而 `y` 是传入图**内部**的行号（`engine/graph/pointfilterop.h:45-47` 把几何整体平移掉，算子拿不到层内绝对行号）；Wind **不在** `FilterStack::hasPositionalOp` 名单（`domain/filterstack.cpp:62-65` 只有 AddNoise/Ripple/Mosaic/RadialBlur/Tiles）⇒ `LayerFilterJob` 的"位置相关 ⇒ strips=1"护栏（`layerfilterjob.cpp:136-141`）对它无效 ⇒ 条带求值（`:143-154`，条带高为 base/base+1，二者之一是奇数）时**每条带第 0 行相位重新起算** ⇒ 同一层行在不同条带里**方向相反**，且与同步的 64 块路径（`layer.cpp:242-269`）不一致。触发：交错风 + 层高 ≥128 | 可疑 |

## 3. 低危 / 设计隐患

| ID | 位置 | 问题 |
|---|---|---|
| A25 | `app/undoitem.cpp:311` | `DocumentGeomUndo::byteSize` 每个 `LayerState` 都持有 `props.maskGray`（整幅 Grayscale8，`imagedocument.cpp:347` 填充、`applyLayerProps` 会真正恢复），但 byteSize **只算 selectionMask + pixels**；同文件 `LayerPropUndo::byteSize:150` 却显式加了 `m_snap.maskGray.sizeInBytes()` ⇒ **同一份快照两种口径，这里是漏算**。`LayerStructureUndo::byteSize:194-197` 同理（被删层的蒙版完全不计）⇒ 256MB 上限与 `PSLITE_MEM` 的 `undoBytes`（`engine/cache/memstats.cpp:42`）**少报**（4000×3000 文档每层少计 12MB） |
| A26 | `app/historystack.cpp:39` | `m_undoBytes` 只在 push 时 `+=`（`:39`），`undo()` 搬去 `m_redo` 前先减（`:55-58`）⇒ 每按一次 Ctrl+Z，就有一份完整快照（可达整幅 `w*h*4`）滞留在**完全不计账、也没有 trim** 的 `m_redo` 里，只被下一次 push 的 `m_redo.clear()` 释放 ⇒ `historystack.h:32/78` 宣称的"约 256MB 上限"**实际可达约 2×**，而 `PSLITE_MEM` 把它当"撤销内存"上报，会低估真实占用 |
| A27 | `tools/transformtool.cpp:577` | 会话只缓存 `m_layerIndex`（`:352`），而会话从 Ctrl+T 起一直存活到 Enter/Esc，期间图层面板全程可用；`cancelSession`/`commitSession` 每次都重新 `layerAt(m_layerIndex)` **不校验是否还是进会话时那一层**。触发：图层 2 进入自由变换 → 删除/重排图层 → Esc ⇒ `:581-582` `layer->replaceFromImage(m_preSessionPixels)` 把**另一个图层**的整层像素+偏移盖上去；下标越界时 `commitSession` 在 `:612` 直接 `return false`，会话仍 active 而**源像素只存在于 `m_srcPixels`**，界面停在半途 |
| A28 | `io/rasterio.cpp:194` | `RasterIo::writeEncoded` 用 `QFile + QIODevice::Truncate` **原地截断**目标文件（"导出为 PNG/JPEG"与"存储为 Web"都走它，`:227`/`saveforwebdialog.cpp:268`）⇒ 中途失败（磁盘满/断电/被杀）会**毁掉用户原有的同名图片**且不回滚不删残留，只返回 false（`:199-202`）。同 io 层另两条落盘路径都用 `QSaveFile + commit`（`projectio.cpp:632-637/727`、`psdio.cpp:522-527/557`）⇒ **三处写法不一致** |
| A29 | `io/projectio.cpp:497` | 保存时若无瓦片就写长度 0 的 PNG（`:660-667`），文件里**没有该层宽高**；读取时空 PNG 走 `:496-497` 用**文档**宽高建层。但层 extent 本可≠画布（`LayerStack` 允许"有 extent 但没瓦片"：`TileBuffer::setFromImage` 遇整块全透明不分配瓦片，`domain/tilebuffer.cpp:264-275`，而 `m_width/m_height` 保留）⇒ 500×500 文档里"置入"一张 100×100 **全透明** PNG（层 100×100 无瓦片）→ 保存再打开 → 该层变成 500×500，`boundsInDocument`/变换框/后续蒙版尺寸全变 |
| A30 | `domain/imagedocument.cpp:1175` | `markDirty`/`markDirtyQuiet` **只对 `activeLayer()`** 调 `invalidateContentBounds`/`invalidateCompositeRaster`。凡"写 A 层、活动层是 B"的路径都会留过期缓存：`tools/transformtool.cpp` 会话开始记 `m_layerIndex`（`:352`），若期间切走活动层，提交时 `markDocumentDirty`（`:665`）失效的是 B；A 若带样式或位置相关算子会走 `ensureCompositeRaster` 的整层缓存（`layer.cpp:214-238`）⇒ **继续显示变换前的像素**。把"谁失效谁"绑在 activeLayer 上是隐式约定 |
| A31 | `engine/projection.cpp:276` | 串行分支会检查 `if (!Compositor::compositeRegion(...)) continue;`（`:283-285`）后才 `markChunksValid`，**并行分支在 `blockingMap` 里完全丢弃返回值**、事后无条件把每个 piece 标有效（`:275-278`）。`compositeRegion` 只在 dst 尺寸/格式与文档不一致时返回 false（`compositor.cpp:289-294`）⇒ 一旦出现"chunk 网格基于旧缓冲、文档尺寸已变"的窗口，会把**根本没被合成**的块标成有效，`hasPendingChunks()` 立刻变 false（`canvasview.cpp:1503-1504` 停掉 idle 定时器）⇒ 画布**永久保留旧/空块**。窗口存在的原因：网格只由 `ensureBuffer()`/`adoptImage()` 按**缓冲尺寸**建立（`:95-100`/`:85-86`），而 `recomposeInvalidChunks` 的 `full` 按**文档尺寸**现算（`:181`），且 `processPendingChunks()`/`flushPriority()`（`:358-372`）**都不调 `ensureBuffer()`** |
| A32 | `engine/projection.cpp:85` | `adoptImage` 只校验"尺寸等于文档"，随后 `resetChunkGrid + m_chunkValid.fill(true)` 并返回 true，等于宣布"整幅投影就是这张图"，而调用方拿到 true 后立刻 `clearDirtyRect()`（`canvasview.cpp:64-65`）⇒ 没有"这张图是否真覆盖整幅文档当前状态"的校验也不留脏区。一旦传入的图只在子矩形内有效（`movetool.cpp:166-171` 的 `m_below/m_live` 正是只 `fillRect(work, transparent)`），**错像素被当成全图真相且不再重算**。现有唯一调用方需 `snapOk==false` 才走到，故不常触发，但契约极易被下一个调用方踩 |
| A33 | `engine/graph/compositegraph.cpp:213` | `CompositeGraph::build` 的栅格层分支只用 `ensureCompositeRaster()` 的整层烘焙结果，**从不看 `layer->hasCompositePreview()`**；而生产路径 `compositor.cpp:226-243` 在 hasPreview 时会先叠瓦片再叠预览 ⇒ 只要某层存在活动预览（自由变换拖拽期间），`compositeViaGraph` 与 `composite` 就**不同**，违反 `compositor.h:79-84`/`docs/graph-engine.md:155` 的"逐像素一致"承诺。今天 `compositeViaGraph` 无生产调用方（仅自检），影响面是**护栏失效** |
| A34 | `engine/op/freetransformop.cpp:113` | 采样先对每个 tap 解预乘（`:113`/`:152`），权重只取 `wx*wy`（`:114`，**不含 alpha**），最后再由 `premulFromFloat` 乘上插值出的 alpha（`:121→:78`）⇒ 算出的是 `(Σw·c)·(Σw·a)` 而正确口径是 `Σw·(c·a)`。核内 alpha 有变化就**偏小**：相邻"不透明白"与"a=0"各 w=0.5 时，正确预乘红=128，当前得 64（相当于边缘**被压暗一半**）。硬边不透明图不受影响，故 `t_transform` 用例照常绿 |
| A35 | `tools/movetool.cpp:409` | press 中**无条件** `pushLayerOffsetUndo(index)`，而"是否真移动过"要到 mouseMove 才知道（`:428-429` `dx==dy==0` 直接 return）⇒ 纯单击（尤其 `:382-385` `pickLayerAt` 自动切活动层的单击）也压一条内容全等的 `LayerPropUndo`：Ctrl+Z 看起来"没反应"却消耗一步、**清空 redo 栈**、历史列表被空操作灌满。`paintbuckettool.cpp:45` 同一形状（先 push，`:70-74` 无脏区时直接返回） |
| A36 | `tools/pathanchortool.cpp:42` | `m_path` 是活动路径的**副本**，只在 `mousePress` 里经 `loadActive` 刷新（`:60`），此后**无任何失效途径**（`hasOverlay` 只看 `m_hasPath`，`drawOverlay` 直接画 `m_path` 且忽略 `Path::visible`）⇒ 路径面板删除该路径、或 Ctrl+Z 撤销路径创建（`PathUndo::pop`）后，画布重绘仍画出**幽灵路径**的轮廓与锚点方框，直到用户在画布上再点一次；隐藏的路径同样照画 |

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
