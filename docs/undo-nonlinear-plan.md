# 非线性撤销（历史面板 + 任意跳转 + 命名书签）开发计划书

> 状态：已完成（T0–T6）
> 更新：2026-10-15 ｜ 本文件是交给实施方（Cursor）的唯一契约，不依赖任何对话上下文。
> 前置：0+1 档（瓦片引用快照 / 区域重投影）已落地并于 `a31be4a` 通过评估，见 [undo-region-plan.md](undo-region-plan.md)、[undo-region-review.md](undo-region-review.md)。
> 相关：[pending-dev.md](pending-dev.md)、[tests.md](tests.md)、[architecture.md](architecture.md)

## 0. 进度总览

| 阶段 | 状态 | 关键数字 / 证据 |
|---|---|---|
| T0 记账按"触碰瓦片"（1 档收尾） | ✅ 2026-10-15 | `.build-clean\release\psdemo_tests.exe undoregion undocow undodirty` 退出码 0；T5①b TileRefs **16512**（前 16777344）；100 笔 `historyTrimmed=0` `undoDepth=100` `undoBytes=3682816` |
| T1 `ChangeBatch`：跳转期信号批量 | ✅ 2026-10-15 | `.build-clean\release\psdemo_tests.exe undobatch phase1a filterundo` 退出码 0；批量内 10 dirty+2 增层 → content/structure/pixels 各 **1**；并集=10,20 75×51；批量外 3 次仍各发 3 次 |
| T2 跳转 API（`stepCount/currentStep/jumpToStep`） | ✅ 2026-10-15 | `.build-clean\release\psdemo_tests.exe undojump undoregion phase1a` 退出码 0；k∈[0,10] 与 20 次随机跳 `diffCount==0`；每次跳转 `contentChanged==1`；clamp/空栈绿 |
| T3 蒙版引用快照（跳转性能前置；**产品代码只有 2 行**） | ✅ 2026-10-15 | `.build-clean\release\psdemo_tests.exe undomaskcow phase1a` 退出码 0；stampMaskDab/fill COW 绿；undo diffs=0；A2 绿；带蒙版 512²×200 步 jump **1 ms** |
| T4 历史面板 `HistoryPanel` | ✅ 2026-10-15 | `.build-clean\release\psdemo_tests.exe undopanel` 退出码 0；5 笔后 count=6；点第 3 行 currentStep=3 且 diffCount=0；trim 后 count=stepCount+1 |
| T5 命名书签（快照=位置标记） | ✅ 2026-10-15 | `.build-clean\release\psdemo_tests.exe undobookmark undopanel` 退出码 0；jump 到书签 A 后 diffCount=0；trim dropped=6 后书签 A 删除；push 清 redo 删 Tip 留 Mid；上限 64 拒第 65 |
| T6 回归与实测记账 | ✅ 2026-10-15 | ① Δ1/10/100/200=**0ms** 且 content=1；② 200→0 historyBytes 峰值 **4531200**；③ 满幅一笔 **16777280→16512**；④ 带蒙版 200 步 **1ms**。全量连跑 51/52（`tilecache` P13 计时偶发；单独绿）；本计划用例全绿 |

---

## 1. 目标与边界

**目标（一句话）**：在**不改变现有线性交换语义**的前提下，让用户能从历史列表**跳到任意一步**、并给任意一步**起名字**（书签），一次跳转只重投影一次。

**完成标准（可验收的现象）**：

1. 图层面板同一容器（`QTabWidget#panelTabs`）里出现「历史」页：列出每一步（含"初始状态"行），当前步高亮；点击任意行，画面与图层结构变成该步状态。
2. 跳转 N 步（N ≥ 50）时 `ImageDocument::contentChanged` **只发一次**（用计数器断言），跳转总耗时 ≤ 现有单次 `sync` 预算量级（实测数字写入 `pending-dev.md`）。
3. 跳转后 `Ctrl+Z`/`Ctrl+Y` 继续按"当前游标"工作（不是回到跳转前的位置），菜单文案同步。
4. 命名书签在 `historyTrimmed` 之后**指向的步号仍然正确**；指向已被丢弃步的书签被删除并通知面板（不静默错位）。
5. 全量用例退出码 0；`phase1a`、`undocow`、`undotile`、`undoregion`、`undodirty`、`tilecache`、`filterundo`、`projectio` 保持绿。

**不做什么（Out of scope）**：

- **分支树**（撤销后编辑产生新分支、树形历史、分支 GC）、**历史画笔**。理由：PS 自身的历史面板不分支；树要配分支 GC + 树形 UI + 按分支算上限，成本 ≥3 批次且演示价值低。
- **真·状态快照克隆**（把当前文档整份克隆成一个独立状态长期保留）。本计划的"快照"= **位置书签**，0 内存。
- **层稳定 id / 撤销条目下标重映射**。⚠️ 这是对 [undo-region-review.md](undo-region-review.md) §7 末尾判断的**更正**：跳转用"重复 pop/redo"实现时，条目的绝对层下标在 LIFO 下**始终自洽**，因此本计划**不需要**层 id。层 id 降级为独立的健壮性任务，另立计划（详见 §3"被证伪的判断"）。
- 撤销条目的磁盘持久化 / 跨会话历史。
- **蒙版成本的两笔"外账"**（本计划点到为止，不在范围内，另立小立项）：① `LayerPropSnapshot::maskGray` 的整张深拷贝（`undoitem.cpp:104-109`，每次属性撤销/蒙版笔画 4 MB @2048²）；② 蒙版与像素一样存在"记账按整张、物理按触碰"的口径差（T0 只覆盖像素条目）。**T3 只做像素撤销路径上的 2 行蒙版引用改造，不得顺手扩散到这两项**（扩散点见 §4 T3 的"不扩散"约束）。
- 把撤销重构成 Command 总线或重放机制（理由见 §3）。
- 面板的拖拽排序、搜索过滤、多选、时间戳、缩略图列。

## 2. 现状事实

> 每条都在代码里查过，带 `文件:行`。

- **双栈 + 交换**：`m_undo`/`m_redo` 两个 `std::vector<unique_ptr<UndoItem>>`（`historystack.h:79-83`）；`push` 清空 redo 并清 `m_redoBytes`（`historystack.cpp:34-46`）；`undo`/`redo` 以 `m_applying` 做重入守卫，取栈顶 → `item->pop(doc)` → 移到另一栈（`historystack.cpp:48-85`）。**没有显式游标**，"当前步"就是 `m_undo.size()`。
- **记账时刻**：`byteSize()` 在 `push`（`:40`）、`undo`（`:55`，pop 前）、`redo`（`:74`）各求值一次；换栈时把同一数值搬来搬去。
- **trim 从队首丢**且始终保留 ≥1 条，丢弃后发 `historyTrimmed(dropped)`（`historystack.cpp:96-113`）。
- **`contentChanged` 是同步重投影**：`CanvasView` 在收到 `contentChanged` 时**直接调用** `syncProjection()`（`canvasview.cpp:132-153`），预算 8 ms（`canvasview.cpp:964`）⇒ 一次跳转若逐步触发，代价会被放大 N 倍。
- **`pixelsChanged(rect)` 的消费者忽略 rect**：`layertreepanel.cpp:205`、`channeltreepanel.cpp:88` 的槽参数都是匿名的 ⇒ 批量结束时用**并集矩形**补发是安全的（不会有人按"精确小矩形"做增量）。
- **`HistorySuppress` 只压制 push**（`imagedocument.cpp:80-94`），**不压制信号** ⇒ 不能拿它当跳转的批量机制。
- **结构/激活/编辑目标信号由撤销条目直接 emit**：`LayerStructureUndo::pop` 发 `structureChanged/activeLayerChanged/[editingTargetChanged]/contentChanged`（`undoitem.cpp:239-243`）；`LayerPropUndo::pop` 发 `layerPropertiesChanged/[editingTargetChanged]`（`undoitem.cpp:176-178`）；`LayerPixelsUndo::pop` 发 `layerPropertiesChanged`（`undoitem.cpp` 尾部）。批量机制若只合并 `contentChanged`，跳转期间**图层面板会漏掉增删层**。
- **蒙版快照是深拷贝**：`LayerPixelsUndo` 与 `undoitem.cpp` 的 `swapLayerMask` 用 `layer->mask()->image().copy()`（`undoitem.cpp` 匿名命名空间内），蒙版 1 字节/像素；`LayerMask::image()` 的非 const 重载会清 opaque 缓存（`layermask.h:36-38`）⇒ 写路径走它就能触发 QImage detach（与 `t_undocow` 钉住的机制同源）。
- **UI 面板范式**：面板 = `.ui` + `Ui::` 生成类 + 源文件三处登记在 `app.pri`（`app.pri:129` SOURCES、`:291` HEADERS、`:322` FORMS）；容器是 `ui/layerpanel.ui:61` 的 `QTabWidget name="panelTabs"`，其中一页是 `PathTreePanel`（`layerpanel.ui:78`，`native="true"`）；面板类的取文档方式是 `setSession(Ps::AppSession*)` + `documentChanged` 连接（`propertiespanel.cpp:79-95`），刷新方式是 `refreshFromDocument()`（`pathtreepanel.cpp:37-77`）。
- **测试入口**：用例源文件登记在 `psDemo/tests.pro:61-62`，用例表在 `psDemo/tests/testmain.cpp:106-107`；断言用 `PsTests::Reporter::check/info`，退出码 = 失败用例数；无 GUI（测试自动设 `QT_QPA_PLATFORM=offscreen`，`docs/tests.md:71-78`）。面板可在测试里手工构造（`tests/t_filterpanel.cpp` 是范例）。
- **当前历史深度瓶颈**：`TileRefSnapshot::bytes()` 按快照内**每块** `sizeInBytes` 累加（`domain/tilebuffer.cpp` `TileRefSnapshot::bytes()`），满幅 2048² 层每笔仍记 ≈16.78 MB，256 MB 上限下深度只有约 15 步（实测见 [undo-region-review.md](undo-region-review.md) §7 R6）。
- **蒙版还有第二处整张深拷贝（本计划不修，见 §1）**：`captureLayerProps` 在每个 `LayerPropUndo` 条目里 `s.maskGray = layer.mask()->image().copy()`（`undoitem.cpp:104-109`），且 `LayerPropUndo::byteSize()` 把它计入（`undoitem.cpp:156`）⇒ 2048² 蒙版层上**每改一次属性（显隐/不透明度/改名…）或每画一笔蒙版**都要压一张 4 MB 的整张蒙版。这是 T3 之外的另一笔账，也是"带蒙版文档历史深度差"的主因。

## 3. 方案

**选型（唯一答案）**：**跳转 = 在既有双栈上重复 pop/redo**（复用已被 bit-exact 验证的交换语义），配 **`ImageDocument::ChangeBatch` 合并信号**（一次跳转 = 一次重投影、一次面板刷新），书签 = **游标位置标记**，面板 = `panelTabs` 新增一页。另加两项前置/配套：**T0 记账按触碰瓦片**（否则面板会把"历史被 trim"摊在用户面前）、**T3 蒙版引用快照**（否则长跳转会被逐步的蒙版深拷贝拖慢）。

**理由**：

1. **零契约改动**：`UndoItem::pop()` 的交换语义与 7 个条目实现一行不改；跳转只是在它外面加一层循环 + 批量。风险最低、回归面最小（既有的 20 轮交替 undo/redo 用例已经证明交换是自洽的）。
2. **内存不增加**：条目本来就各持一份状态（交换式），跳转不需要额外的状态副本 —— 这与"改成 `apply(snapshot)` 契约"相比，省掉了重写 7 个条目类的风险，收益（少几次 QImage 引用交换）可以忽略。
3. **书签=位置**省掉文档级 clone/restore（那是另一个量级的工程），且与"线性历史 + 游标"模型天然一致。
4. **信号批量是必需的而非优化**：`contentChanged` 的处理是**同步** `syncProjection()`（`canvasview.cpp:144`），不批量的话 200 步跳转最坏 ≈200 × 8 ms。

**不采用的做法及原因（各一句）**：

- 把 `pop()` 改成 `capture()/apply(state)` 契约：要重写全部 7 个条目类与两处批量语义，收益仅是省几次引用交换。
- 分支树 / 历史画笔：见 §1 Out of scope。
- 真·快照克隆（文档级 clone/restore）：需要重建整个 `LayerStack`（含 owner 回指、`LayerStack` 改栈方法 private、pslite 无关），成本与风险都远超收益。
- 复用 `HistorySuppress` 做批量：它只压制 push，不压制信号（`imagedocument.cpp:80-94`）。

**被证伪的判断（记档，勿静默）**：

- 原 [undo-region-review.md](undo-region-review.md) §6/§7 判断"2 档需要层稳定 id"。**该判断错误**：跳转用重复 pop/redo 实现，结构条目（增/删/重排）与像素条目在 LIFO 下顺序一致，绝对下标始终指向同一层。⇒ 本计划不含 id 改造。

## 4. 任务分解

> 依赖顺序：**T0 → T1 → T2**；T3 可与 T2 并行；T4 依赖 T2（依赖 T1 的批量否则体验差）；T5 依赖 T4（书签要靠面板展示）；T6 最后。

### T0 记账按"触碰瓦片"（1 档收尾，建议先做）

- **改动的文件**：`psDemo/domain/imagedocument.h`、`psDemo/domain/imagedocument.cpp`、`psDemo/app/undoitem.h`、`psDemo/app/undoitem.cpp`、`psDemo/app/historystack.h`、`psDemo/app/historystack.cpp`、`psDemo/tests/t_undoregion.cpp`（追加断言）
- **依赖**：无（0+1 档已落地）
- **规模**：约 6 文件 / +110 行
- **要点**：`markDirty(rect)` 里，若当前正在记录历史（`shouldRecordHistory()`）且**栈顶条目**是 TileRefs 像素条目、并且该条目所属图层就是被改图层，则把 `rect` 转成层内坐标累加进条目（只累加矩形并集，不必逐块枚举）；`HistoryStack` 提供一个 `refreshTopBytes()` 把 `delta = byteSize() - lastAccounted` 同步进 `m_undoBytes`。整层操作（清除/填充/变换）会标整层 ⇒ 自然退化为按整层计。
- **风险点**：累加条件写错会把"别的图层属性变化"记进像素条目（只会**高估**，安全）；`undo()`/`redo()` 换栈时 `byteSize()` 会重新求值（`historystack.cpp:55/74`），必须保证 `lastAccounted` 与所在栈同步，否则账面漂移。
- **验收**：
  ```text
  <build-dir>\release\psdemo_tests.exe undoregion undocow undodirty
  ```
  期望结果：`T5①b` 满幅白底一笔的 `TileRefs` 增量从 16 777 344 降到"触碰瓦片字节"量级（1 块 ≈16 KB）；新增断言：2048² 满幅层连续 100 笔后 `historyTrimmed` 槽被调用次数 == 0。退出码 0。

### T1 `ChangeBatch`：跳转期间合并信号

- **改动的文件**：`psDemo/domain/imagedocument.h`、`psDemo/domain/imagedocument.cpp`、`psDemo/app/undoitem.cpp`（把直接 `emit doc.xxx()` 改成走新的通知方法）、`psDemo/tests/t_undobatch.cpp`（新增）、`psDemo/tests.pro`、`psDemo/tests/testmain.cpp`
- **依赖**：T0
- **规模**：约 5 文件 / +140 行
- **要点**：新增 RAII `ImageDocument::ChangeBatch`；`markDirty()`、`structureChanged`、`activeLayerChanged`、`editingTargetChanged`、`contentChanged`、`layerPropertiesChanged`、`pixelsChanged` 在批量深度 > 0 时只**累计**（脏区并集 + 各类信号的需求位），最外层析构时按固定顺序补发一次：`structureChanged` → `activeLayerChanged` → `editingTargetChanged` → `layerPropertiesChanged`（若有多层则对每层各发一次）→ `pixelsChanged(union)` → `contentChanged`。
- **风险点**：补发顺序错会让面板读到中间态（先 `contentChanged` 再 `structureChanged` ⇒ 图层面板用旧结构刷）；漏发某一类 ⇒ 跳转后 UI 静默不同步。**累计期间不得跳过 `invalidateLayersIntersecting`**（它必须逐次执行，否则缓存失效会漏）。
- **验收**：
  ```text
  <build-dir>\release\psdemo_tests.exe undobatch phase1a filterundo
  ```
  期望结果：`undobatch` 断言——在一个 `ChangeBatch` 内做 10 次 `markDirty` + 2 次增删层，`contentChanged` 计数 == 1、`structureChanged` 计数 == 1、`pixelsChanged` 收到的是并集矩形（等于 10 个矩形的并）；批量外的行为与改造前完全一致（同用例内对比）。退出码 0。

### T2 跳转 API

- **改动的文件**：`psDemo/app/historystack.h`、`psDemo/app/historystack.cpp`、`psDemo/tests/t_undojump.cpp`（新增）、`psDemo/tests.pro`、`psDemo/tests/testmain.cpp`
- **依赖**：T1
- **规模**：约 4 文件 / +150 行
- **要点**：见 §5 的 API 与步号定义；`undo()`/`redo()` 改为薄封装（调用新的私有 `undoOne()`/`redoOne()`，保持对外行为与 `changed()` 发射不变）；`jumpToStep()` 内部循环调用 `undoOne/redoOne`，整体包在 `ChangeBatch` 里，末尾只发一次 `changed()`。
- **风险点**：`jumpToStep` 与 `m_applying` 的重入（必须自己设 `m_applying` 并跳过 `undoOne/redoOne` 的守卫）；跳转目标 clamp（`[0, stepCount()]`）；`stepCount()==0` 时不得崩。
- **验收**：
  ```text
  <build-dir>\release\psdemo_tests.exe undojump undoregion phase1a
  ```
  期望结果：`undojump` 断言——建 10 笔历史后，对每个 k ∈ [0,10] 执行 `jumpToStep(k)`，图层 `materialize()` 与"从头做 k 笔"的参照状态逐像素相同（`diffCount == 0`）；来回跳 20 次（随机 k）仍一致；`jumpToStep` 期间 `contentChanged` 计数 == 1/次；跳转到超界值被 clamp。退出码 0。

### T3 蒙版引用快照（跳转性能前置，可与 T2 并行；**产品代码只有 2 行**）

- **改动的文件**：`psDemo/app/undoitem.cpp`、`psDemo/tests/t_undomaskcow.cpp`（新增）、`psDemo/tests.pro`、`psDemo/tests/testmain.cpp`
- **依赖**：无
- **规模**：约 3 文件 / +70 行（**产品代码 2 行，其余是钉住 COW 的用例**）
- **要点**：现状是**每步两次整张蒙版深拷贝**——`swapLayerMask` 里 `layer->mask()->image().copy()`（捕获），以及 `LayerMask::setFromImage()` 内部 `m_gray = gray.copy()`（`layermask.cpp:112-113` 已核实，是深拷贝）。2048² 蒙版层一次 pop = 8 MB memcpy，200 步跳转 ≈1.6 GB ⇒ UI 冻结数秒。改法：
  1. 捕获侧去掉 `.copy()`：`QImage curMaskGray = layer->mask()->image();`（浅拷贝，引用计数）。
  2. 恢复侧不走 `setFromImage`，改 `mask->image() = *m_maskGray;`（`LayerMask::image()` 非 const 重载会清 `isFullyOpaque` 缓存，`layermask.h:36-38`）。
  写入侧靠 `LayerMask::image()` 非 const + `scanLine` 触发 detach（与 `t_undocow` 钉住的瓦片机制同源）。
- **风险点**：若某处写入蒙版不走 `image()` 非 const（例如先 `const` 取图再 `const_cast`），共享缓冲会被就地改写 ⇒ 撤销后蒙版内容错。**必须先写测试钉住**（`PaintEngine::stampMaskDab` + `LayerMask::fill` 两条真实写路径）。已核实的写路径都安全：`stampMaskDab` 走 `scanLine`、`LayerMask::fill` 走 `QImage::fill`（`layermask.cpp:57`）、`expand` 整对象替换（`layermask.cpp:75`）——三条都会 detach。
- **不扩散（硬约束）**：本任务**只改 `undoitem.cpp` 里 `swapLayerMask` 的那两行**。不得修改 `captureLayerProps`/`LayerPropSnapshot`/`LayerPropUndo::byteSize`，也不得改记账口径——那属于 §1 列出的"蒙版成本外账"，另立小立项。发现必须一起改才能通过验收时，停下来告诉用户，不要自己扩范围。
- **验收**：
  ```text
  <build-dir>\release\psdemo_tests.exe undomaskcow phase1a
  ```
  期望结果：`undomaskcow` 断言——共享蒙版快照后，用 `PaintEngine::stampMaskDab` 与 `LayerMask::fill` 各写一次，快照像素不变；撤销后蒙版与原快照逐字节相同（复用 A2 的断言方式）。`phase1a` 的 A2 保持绿。退出码 0。
- **本任务的产品改动固定为上述 2 行**（决策已定，无备选路径）。

### T4 历史面板 `HistoryPanel`

- **改动的文件**：`psDemo/ui/historypanel.h`、`psDemo/ui/historypanel.cpp`、`psDemo/ui/historypanel.ui`（新增三者）、`psDemo/ui/layerpanel.ui`（`panelTabs` 新增一页）、`psDemo/app.pri`（SOURCES/HEADERS/FORMS 三处登记）、`psDemo/ui_layerpanel.h`（重新生成或手改）、`psDemo/tests/t_undopanel.cpp`（新增）、`psDemo/tests.pro`、`psDemo/tests/testmain.cpp`
- **依赖**：T2（+T1 的批量）
- **规模**：约 8 文件 / +330 行
- **要点**：照 `PathTreePanel` 范式（`.ui` + `Ui::` 生成 + `setSession` + `documentChanged` + `refreshFromDocument`）。列表第一行是"初始状态"（游标 0），其后每步一行；点击第 k 行 → `jumpToStep(k)`；当前游标行高亮；程序化 `setCurrentRow` 期间用 `m_updating` 标志屏蔽 `currentRowChanged`（否则点击 → 跳转 → 刷新 → 递归）。工具按钮：`后退一步`/`前进一步`/`新建快照`（T5 接线）。**跳转期间 UI 保护**：`|Δstep| ≥ 10` 时套 `QApplication::setOverrideCursor(Qt::WaitCursor)` 并禁用列表，跳转返回后恢复（`jumpToStep` 是同步调用，无需线程）。
- **风险点**：`refreshFromDocument` 与 `currentRowChanged` 相互触发（必须屏蔽）；`trim` 后行号整体前移（重刷而非增量改）；`historyTrimmed` 未连接 ⇒ 列表显示已被丢弃的步。
- **验收**：
  ```text
  <build-dir>\release\psdemo_tests.exe undopanel
  ```
  期望结果：用例在 offscreen 下构造 `HistoryPanel` + 文档，做 5 笔后断言 `itemList->count() == stepCount+1`；点第 3 行后断言 `currentStep()==3` 且 `materialize()` 与参照一致；再 `trim`（把上限调到很小）后断言列表被重建、行数与 `stepCount+1` 一致。退出码 0。
  手工：跑 GUI，画 3 笔 → 打开「历史」页 → 点第 1 行，画面回到 1 笔后状态；再点回最后一行恢复。

### T5 命名书签（快照 = 位置标记）

- **改动的文件**：`psDemo/app/historystack.h`、`psDemo/app/historystack.cpp`、`psDemo/ui/historypanel.cpp`（显示/新建/删除）、`psDemo/tests/t_undobookmark.cpp`（新增）、`psDemo/tests.pro`、`psDemo/tests/testmain.cpp`
- **依赖**：T4
- **规模**：约 5 文件 / +160 行
- **要点**：见 §5 的书签数据结构与**重基规则**；`trimUndo()` 丢弃 `dropped` 条后，对每个书签的步号 `-= dropped`，步号 < 0 的书签**删除**；`push` 清空 redo 时，步号 > `currentStep()` 的书签删除（它们指向已不存在的状态）。书签变动发 `bookmarksChanged()`。
- **风险点**：重基漏做 ⇒ 书签指向错误的步（静默错位，比不显示更糟）；`push` 清 redo 时的书签清理要按"位置"而不是"条目指针"判断。
- **验收**：
  ```text
  <build-dir>\release\psdemo_tests.exe undobookmark undopanel
  ```
  期望结果：`undobookmark` 断言——在第 3 步建书签 `A`；继续做 5 笔；`jumpToStep(bookmarkStep("A"))` 后状态 == 第 3 步状态；把 `byteLimit` 调到只留 2 条并 push 触发 trim，断言书签步号按 `dropped` 重基（或已被删除）、且 `currentStep()` 语义未变。退出码 0。

### T6 回归与实测记账

- **改动的文件**：`docs/pending-dev.md`（新增一节）、本文件（§0）、`docs/tests.md`（用例表）、`docs/architecture.md` 或 `docs/tech-notes.md`（各一段，说明"跳转 = 重复交换 + 批量信号"）
- **依赖**：T0–T5
- **规模**：文档 3–4 处 / ±80 行
- **要点**：跑全量；打印并记录：① 跳转 1/10/100/200 步的耗时（`QElapsedTimer`）与 `contentChanged` 次数；② 200 步跳转期间 `historyBytes` 峰值；③ T0 前后的满幅层 `undoBytes` 对照；④ **带蒙版图层 200 步跳转耗时**（这是"要不要回头补 T3"的唯一判据，必须实测）。
- **风险点**：只写"更快"不给数字 = 这轮白做。
- **验收**：
  ```text
  <build-dir>\release\psdemo_tests.exe
  ```
  期望结果：退出码 0；三组数字写入 `pending-dev.md`。

## 5. 精确实施规格

**新增/修改的接口与签名**：

```cpp
// 文件：psDemo/app/historystack.h
public:
    /** 历史总步数（已发生 + 可重做）。 */
    int stepCount() const { return int(m_undo.size() + m_redo.size()); }
    /** 游标：已发生的步数。0 = 全撤到底，stepCount() = 全重做到顶。 */
    int currentStep() const { return int(m_undo.size()); }
    /** 第 step 步（0-based，从最老开始）的条目名；越界返回空串。 */
    QString stepName(int step) const;
    /** 第 step 步是否是书签；是则同时输出书签名。 */
    bool bookmarkAt(int step, QString *label = nullptr) const;
    /** 跳转到第 step 步（clamp 到 [0, stepCount()]）。返回是否真的发生了跳转。 */
    bool jumpToStep(int step, ImageDocument &doc);

    /** 书签（= 位置标记）。step 越界或不落在 [0, stepCount()] 返回 false。 */
    bool addBookmark(int step, const QString &label);
    bool removeBookmark(int step);
    QVector<QPair<int, QString>> bookmarks() const { return m_bookmarks; }

    /** 栈顶条目字节数变化后同步账面（T0；由 ImageDocument::markDirty 调用）。 */
    void refreshTopBytes();

signals:
    void bookmarksChanged();

private:
    void undoOne(ImageDocument &doc);   // 无重入守卫、不发 changed()、不发 trim
    void redoOne(ImageDocument &doc);
    QVector<QPair<int, QString>> m_bookmarks;   // 按 step 升序；label 非空
    quint64 m_topAccounted = 0;                 // T0：栈顶条目的已记账字节
```

```cpp
// 文件：psDemo/domain/imagedocument.h
public:
    /**
     * 批量变更守卫：期间所有"变更类"信号只累计，最外层析构时按固定顺序补发一次。
     * 可嵌套（深度计数）。用于历史跳转等"多步一次生效"的场景。
     */
    class ChangeBatch {
    public:
        explicit ChangeBatch(ImageDocument &doc);
        ~ChangeBatch();
        ChangeBatch(const ChangeBatch &) = delete;
        ChangeBatch &operator=(const ChangeBatch &) = delete;
    private:
        ImageDocument &m_doc;
    };

private:
    /** 通知出口：批量深度 > 0 时只登记需求位，否则立刻 emit。 */
    void notifyStructureChanged();
    void notifyActiveLayerChanged(int index);
    void notifyContentChanged();
    void notifyPixelsChanged(const QRect &rect);
    void notifyLayerPropertiesChanged(int index);
    void notifyEditingTargetChanged();
    /** 最外层 ChangeBatch 析构时调用：按 §4 T1 的顺序补发。 */
    void flushPendingNotifications();
```

```cpp
// 文件：psDemo/app/undoitem.h（T0 用；TileRefs 模式新增"已触碰区域"）
    /** 累加"本条目生效期间被写到的层内矩形"（由 ImageDocument::markDirty 调用）。 */
    void noteTouchedLocalRect(const QRect &localRect);
    /** 该条目是否正在跟踪某个图层（供 markDirty 判定）。 */
    bool touchesLayer(int layerIndex) const;
```

```cpp
// 文件：psDemo/ui/historypanel.h（T4）
class HistoryPanel : public QWidget
{
    Q_OBJECT
public:
    explicit HistoryPanel(QWidget *parent = nullptr);
    ~HistoryPanel() override;
    /** 与其它面板同法：接 AppSession，跟随 documentChanged。 */
    void setSession(Ps::AppSession *session);
private slots:
    void refreshFromDocument();
    void onListSelectionChanged();
    void onNewBookmark();
    void onDeleteBookmark();
private:
    Ui::HistoryPanel *ui = nullptr;
    Ps::AppSession *m_session = nullptr;
    Ps::ImageDocument *m_document = nullptr;
    bool m_updating = false;   ///< 程序化刷新期间屏蔽 currentRowChanged
};
```

**步号与游标的定义（必须按此实现，面板与书签都依赖它）**：

- 总步数 `n = m_undo.size() + m_redo.size()`；游标 `c = m_undo.size()`，取值 `[0, n]`。
- 第 `i` 步（`0 ≤ i < n`，时间顺序）的名称：
  - `i < c` → `m_undo[i]->name()`
  - `i ≥ c` → `m_redo[n - 1 - i]->name()`（因为 redo 栈顶是最早被撤销的那一步）
- 状态定义：**游标 = c 表示"第 0..c-1 步已生效"**。`c == 0` 是"初始状态"。
- `jumpToStep(t)`：`t = qBound(0, t, n)`；`while (currentStep() > t) undoOne(doc); while (currentStep() < t) redoOne(doc);`

**接口契约**：

- **所有权**：书签只存 `(int step, QString label)`，不持有任何状态；`UndoItem` 仍由双栈独占。
- **原地修改**：`jumpToStep` 原地改文档状态（与 `undo()`/`redo()` 同语义），不返回新对象。
- **错误路径**：`jumpToStep` 在 `m_applying` 已为真时返回 `false` 且零改动；`step` 越界 clamp；`n == 0` 时直接返回 `false`。
- **信号**：`jumpToStep` 期间**不发** `changed()`；结束时发**一次**。所有文档级"变更类"信号在 `ChangeBatch` 内合并（顺序见 T1）。
- **`markDirty` 在批量内的行为**：照常累计 `m_dirtyRect`、照常执行 `invalidateLayersIntersecting`（**不可跳过**），只跳过 `emit`。

**默认值与开关**：

- 无环境变量开关。面板默认随 `panelTabs` 显示（与图层/通道/路径页同级）。
- 书签数量上限 **64**（超出时 `addBookmark` 返回 false）；理由：书签只占几十字节，但面板行高亮与"新建快照"按钮需要一个界限，避免误操作刷屏。

**数值与坐标约定**：

- 步号 0-based；"初始状态"行使用步号 0（不是 -1）。
- 批量补发的 `pixelsChanged` 使用**文档坐标**的并集矩形；矩形遵循 `QRect` 闭区间语义（`right()/bottom()` 含端点）。
- `noteTouchedLocalRect` 收**层内坐标**（调用方在 `markDirty` 里用 `rect.translated(-layer->offsetX(), -layer->offsetY())` 换算），与 `TileBuffer` 的 `forEachTileInRect` 同一坐标系。
- 瓦片边长 64（`TileBuffer::kTileSize`）；触碰瓦片字节 = Σ 相交瓦片的实际尺寸 × 4（含边缘块）。

**可复用的既有工具函数**：

- `TileBuffer::tileBounds/forEachTileInRect/allocatedTileCount`、`TileRefSnapshot::bytes()`（`domain/tilebuffer.*`）。
- `HistoryStack::push/undo/redo/trimUndo/undoText/redoText/undoDepth`、信号 `changed/historyTrimmed`（`app/historystack.h`）。
- `ImageDocument::markDirty(rect)`、`HistorySuppress`（**只可用于压制 push**）、`layers()`、`history()`。
- UI：`PathTreePanel`（面板范式）、`propertiespanel.cpp:79-95`（`setSession` 接法）、`app.pri` 三处登记。
- 测试：`PsTests::Reporter::check/info`、`PsTests::diffCount`、`PsTests::makeLayerImage`、`tests/testutil.h`。

## 6. 验收口径

- **功能验收**：
  - 任意 k ∈ [0, n] 跳转后，文档状态与"从头重做 k 步"逐像素一致（`diffCount == 0`）。
  - 一次跳转：`contentChanged` 恰好 1 次、`structureChanged` 至多 1 次（有结构变化时）。
  - 跳转后 `undoText()/redoText()` 与游标一致（菜单显示正确的下一步）。
  - 书签在 trim 后步号正确或已被删除，绝不指向错误步。
- **回归项（必须保持不变）**：
  - 既有 `undo/redo/clear/push` 的对外行为与信号次数（`undobatch` 用例内做改造前后对比）。
  - `phase1a`（A2/A27/A34/A35）、`undocow`、`undotile`、`undoregion`、`undodirty`、`tilecache`、`filterundo`、`filterpanel`、`transformlive`、`projectio`、`psdio` 全绿。
  - `.pslite` / PSD 文件格式**不变**（本计划不改任何序列化；`t_projectio`、`t_psdio` 必须保持绿）。
- **测试命令**（构建方式见 `docs/tests.md:13-18`，用你现有构建目录）：
  ```bat
  <build-dir>\release\psdemo_tests.exe undobatch undojump undomaskcow undopanel undobookmark
  <build-dir>\release\psdemo_tests.exe
  ```
- **手工验收步骤**：
  1. 打开 GUI，新建 1024×1024 文档，画 3 笔（不同位置/颜色），新建一层再画 1 笔，删掉中间那层。
  2. 打开「历史」页：列表应有"初始状态"+ 每步一名；当前行是最末一行且高亮。
  3. 点第 2 行：画面与该步一致；图层面板同时回到该步的结构（层数/名称正确）。
  4. 点回最后一行：与步骤 1 结束时的画面逐像素一致（可用取色器抽查两处像素）。
  5. 在当前步点「新建快照」，命名 `S1`；再画 2 笔；点 `S1` 行回到该步；`Ctrl+Z`/`Ctrl+Y` 仍按当前游标工作。
  6. 用 `PSLITE_MEM=1` 观察跳转期间 `historyBytes` 不出现阶梯式暴涨（蒙版层专项：给图层加蒙版后再重复 3–5）。

## 7. 风险与回退

| # | 风险 | 现象 | 回退办法 |
|---|---|---|---|
| R1 | 批量信号漏发/顺序错 | 跳转后图层面板显示旧结构、画布显示旧像素 | `undobatch` 用例钉住计数与顺序；出问题时可把 `jumpToStep` 内部改成"逐步 emit"（去掉 `ChangeBatch`，功能仍正确、只是慢） |
| R2 | 跳转中 `m_applying` 重入 | 跳转被静默忽略 / 递归 | `jumpToStep` 自己设守卫，`undoOne/redoOne` 不带守卫；用例断言连续两次 `jumpToStep` 都生效 |
| R3 | 书签重基漏做 | 书签指向错误的步（静默错位） | `undobookmark` 用"故意 trim"用例钉住；发布前若发现异常，宁可**全部删除书签**也不能错位 |
| R4 | 蒙版浅拷贝被就地改写 | 撤销后蒙版内容错/花屏 | `undomaskcow` 两条写路径钉住；不成立则回退为 `.copy()`（只损失跳转性能，不影响正确性），并在 §0 记一条 |
| R5 | 长跳转耗时（N 步 × 每步开销） | 面板点一步卡住几百 ms | T3 已把蒙版那部分降为引用交换；仍慢时给面板加"跳转中禁用输入 + 进度提示"，或把 N 步跳转改成"从最近的目标侧出发"（本计划不实现） |
| R6 | T0 记账条件写错 | `undoBytes` 高估/低估（低估会让上限失效） | 只在"栈顶是 TileRefs 条目且图层匹配"时累加；`undoregion` 的护栏断言（稀疏一笔 = 128）必须保持通过，防止把像素条目算成全层 |
| R7 | 面板与 `currentRowChanged` 递归 | 点一下跳多次 / 卡死 | `m_updating` 标志屏蔽程序化刷新 |
| R8 | 面板行数随历史增长（几千行） | 刷新变慢 | 书签上限 64、`trim` 本身限制条目数；若实测慢，加"只显示最近 500 步"（本计划不实现，先实测） |

**整体回退**：本计划的改动集中在 `app/historystack.*`、`domain/imagedocument.*`（通知出口 + ChangeBatch）、`app/undoitem.*`（T0/T3 各一小块）与新增 UI 面板。不想要面板时，删掉 `layerpanel.ui` 里那一页即可回到"只有菜单撤销"的现状；`jumpToStep` 是纯增量 API。

## 8. 实施方记录要求

- 每完成一个任务，在本文件 §0 登记：任务号、完成日期、验收证据（命令 + 关键输出行 + 实测数字）。
- 同步更新：`docs/pending-dev.md` 新增一节「非线性撤销：历史面板 + 跳转 + 书签（2026-10）」，写现象/改法/被否掉的方案（分支树、真快照克隆、层 id 前置）/三组实测数字/仍欠。
- 同步 `docs/tests.md` 的用例表（新增 5 个用例），以及 `docs/architecture.md` 或 `docs/tech-notes.md` 中撤销一节的"跳转 = 重复交换 + 批量信号"说明。
- 若发现本计划与代码现状冲突（例如某个信号无法批量而不破坏语义），**停下并在 §0 记一条**，不要静默改设计。
- 提交信息沿用工程习惯：`feat:` / `fix:` / `test:` / `docs:` + 中文摘要。
- 未在计划内的额外改动，完成后单独列出并说明原因。

## 附：给实施方的执行提示词（可直接整段粘贴）

```text
你是本工程的实施方（Cursor）。先完整阅读 photoshopDemo/docs/undo-nonlinear-plan.md——它是唯一契约，不依赖任何对话上下文——然后按 §4 的顺序实施：T0 → T1 → T2 → T3（可与 T2 并行）→ T4 → T5 → T6。

【纪律】
1. 先测再改：每个任务先写它"改动的文件"里列的用例，再改产品代码。T3 尤其必须先钉住蒙版 COW（两条写路径），再动那两行。
2. 一个任务一次提交；提交信息用 feat: / fix: / test: / docs: + 中文摘要（沿用 git log 现状）。只 add 本任务涉及的文件——工作区里另有无关改动（如 docs/学习路径/*.md），不要一起提交。
3. 每个任务完成后在本计划 §0 进度总览登记：任务号、完成日期、验收证据（命令 + 关键输出行 + 实测数字）。不接受"应该没问题"。
4. 每完成一个任务就停下汇报，等我确认再继续下一个（除非我一次性授权连做）。
5. 不做计划外改动。硬约束：
   - 不引入图层稳定 id / 撤销条目下标重映射（计划 §1、§3 已说明不需要）。
   - 不改 .pslite / PSD 文件格式（t_projectio、t_psdio 必须保持绿）。
   - T3 只准改 app/undoitem.cpp 里 swapLayerMask 的那两行；不得顺手改 captureLayerProps / LayerPropSnapshot / LayerPropUndo::byteSize / 记账口径（见 §1 Out of scope 与 §4 T3 的"不扩散"条款）。
   - 不实现分支树、历史画笔、真·状态快照克隆。
6. 计划与代码现状冲突时（例如某类信号无法批量而不破坏语义、某条写路径不 detach）：停下，不要自己改设计；在 §0 记一条，并告诉我现象、证据、你建议的方案。

【每个任务的上报格式】
  任务：T<n> <名称>
  改动：<文件:行 列表 + 净增删行数>
  命令：<build-dir>\release\psdemo_tests.exe <用例名...>
  输出：<关键 PASS/数字行原文，贴 3–8 行>
  实测：<本任务要求的数字：T0 的满幅一笔 undoBytes、T1 的 contentChanged 次数、T2 的跳转后逐像素一致、T3 的带蒙版 200 步耗时、T4 的面板行数、T5 的书签重基>
  未做/偏离：<无则写"无">
  阻塞：<无则写"无">

【构建与测试】
- 构建方式见 docs/tests.md 第 13–18 行（qmake + mingw32-make，用你现有的构建目录）。
- 全量 psdemo_tests.exe 的退出码 = 失败用例数，必须为 0。
- 新增用例要同时登记 psDemo/tests.pro 与 psDemo/tests/testmain.cpp（照 t_undocow / t_undotile 的写法）。

【T6 必交的四组数字】写进 docs/pending-dev.md 新增节「非线性撤销：历史面板 + 跳转 + 书签（2026-10）」
  ① 跳转 1/10/100/200 步的耗时，以及"每次跳转 contentChanged 只发一次"的实测计数
  ② 200 步跳转期间 historyBytes 峰值
  ③ T0 前后对照：2048² 满幅层一笔的 undoBytes 增量（应从 ≈16.78 MB 降到触碰瓦片量级）
  ④ 带蒙版图层 200 步跳转耗时（T3 生效前后对照）

【收尾】更新 docs/tests.md 用例表、docs/architecture.md 或 docs/tech-notes.md 的撤销一节，并在 pending-dev 写"仍欠"：蒙版成本外账（LayerPropSnapshot 整张深拷贝 + 蒙版记账口径）、空脏区仍压条目、分支树/历史画笔。
最后回一条总汇报：每个任务一行（判定 + 证据摘要）+ 四组数字 + 未做项。
```
