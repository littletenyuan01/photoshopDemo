# 撤销粒度改造（0+1 档）开发计划书

> 状态：**已落地（T1–T5）**
> 更新：2026-10-15 ｜ 本文件是交给实施方（Cursor）的唯一契约，不依赖任何对话上下文。
> 相关：[phase1-audit.md](phase1-audit.md)、[pending-dev.md](pending-dev.md)、[tests.md](tests.md)、[page-cache-plan.md](page-cache-plan.md)

## 0. 进度总览

| 阶段 | 状态 | 关键数字 / 证据 |
|---|---|---|
| T1 COW 写样式不变量 + `TileRefSnapshot` 定义 | ✅ 2026-10-15 | `psdemo_tests.exe undocow`：scanLine/fill/QPainter 均 PASS（QPainter 已 detach，未触 R1） |
| T2 `TileBuffer` 快照/交换 + 记账 + 换页读回 | ✅ 2026-10-15 | `undotile tilecache`：交换对称 + 换页 bit-exact + allocated/resident 回初值 |
| T3 `LayerPixelsUndo` tile-ref 模式 + 调用点声明 | ✅ 2026-10-15 | `undoregion phase1a filterundo transformlive`；稀疏一笔 undoBytes=128；链接/变换显式 WholeLayer |
| T4 出栈只标受影响矩形（0 档） | ✅ 2026-10-15 | `undodirty`：400 文档小笔 dirty=64×64≪整幅；样式 pad 外扩；画布外整幅回退 |
| T5 回归与实测记账 | ✅ 2026-10-15 | 全量 **47/47** 退出码 0；见 `pending-dev.md`「撤销粒度」节三组数字 |

---

## 1. 目标与边界

**目标（一句话）**：把"像素类撤销"的入栈成本从 **O(整层像素)** 降到 **O(真正被写到的瓦片)**（靠 QImage 隐式共享的写时复制），并让出栈只重投影**受影响矩形**；不改变撤销的线性语义、菜单文案与既有观感。

**完成标准（可验收的现象）**：

1. 4000×4000 图层上画一笔（触碰 ≤ 16 块瓦片），`history().undoBytes()` 的增量 ≤ `触碰瓦片字节 + 64KB`（当前约 **64 MB**）。
2. 同一深度（例如 200 笔）历史的峰值内存比改造前**低一个数量级**，并在 `pending-dev.md` 里写出前后实测数字（同机、同脚本）。
3. 一次像素撤销之后，`doc.dirtyRect()` 只覆盖"该层旧 extent ∪ 新 extent ∪ 样式外扩"，**不再等于整幅**（层 extent = 画布且改动很小时也必须小于整幅）。
4. 全量用例退出码 0；`phase1a`（含 A2 蒙版、A27 会话层、A34 预乘）、`tilecache`、`filterundo`、`transformlive`、`composite`、`projectio`、`psdio` 全部保持绿。

**不做什么（Out of scope）**：

- 非线性撤销（历史面板、任意跳转、命名快照、分支树、历史画笔）——属"2 档"，另立计划。
- 图层稳定 id（`Layer::id()`）与撤销条目的下标重映射——属"2 档前置"，本计划**不动**；因此"层增删后旧条目按下标定位"这一既有脆弱点**保持不变**（详见 §7 风险 R6）。
- `DocumentGeomUndo` / `LayerStructureUndo` / `SelectionUndo` / `PathUndo` 的粒度改造：它们本来就该是整幅/整栈快照，**不改**。
- 图层蒙版的粒度优化：本计划**保留**蒙版的整张深拷贝（理由见 §3）。
- 全透明瓦片回收（"未分配 = 透明"照旧；撤销后 `allocatedTileCount()` 可能变大，属**可接受副作用**）。
- 撤销条目的磁盘落盘、跨会话持久化。
- 任何 UI 改动（菜单文案、面板）。

## 2. 现状事实

> 下列每条都在代码里查过，带 `文件:行`。

- **双栈 + 交换语义**：`push` 存"改动之前"的状态；`undo`/`redo` 调**同一个** `pop()`，pop 内做对称交换（`historystack.h:22-23`、`40-52`、`79-83`）。默认上限 256 MB，超限丢最老（`historystack.h:83`）；`undoBytes + redoBytes` 都计入（`historystack.h:60-64`）。
- **像素入栈 = 整层深拷贝**：`ImageDocument::pushLayerPixelsUndo`（`imagedocument.cpp:121-142`）调 `layer->materialize()`（`imagedocument.cpp:140`）；`materialize()` 新建整幅 `QImage` 并逐块画进去（`tilebuffer.cpp:290-302`）——与"改了多少像素"无关。
- **出栈 = 整幅标脏**：`LayerPixelsUndo::pop` 调 `doc.markDirty()`（`undoitem.cpp:134`）。`markDirty(rect)` 会顺带失效相交层的内容包围盒与复合栅格缓存（`imagedocument.cpp:1198-1210`），**但 `rect` 为空时直接 `return`**（`imagedocument.cpp:1200-1201`）——回退分支必须用无参 `markDirty()`。
- **蒙版必须一起快照**：尺寸变化时 `replaceFromImage` 会把蒙版重置为全白（`layer.cpp:392`），故 `LayerPixelsUndo` 始终跟踪蒙版（`undoitem.cpp:99-129`，A2）。蒙版是**整层**灰度图（`layermask.h:70`，1 字节/像素）。
- **瓦片是 QImage 隐式共享，写时复制已被测试钉住**：`t_tilecache.cpp:207-220` 断言"复制图层后两侧同 `cacheKey`"→"写一侧后 `cacheKey` 不同且另一侧像素不变"。
- **写瓦片的三种样式**（决定 COW 是否成立）：
  - `scanLine()` 写：`floodfillop.cpp:177`、`gradientop.cpp:180`、`clonestampdabop.cpp:69`、`tonedabop.cpp:87`、`focusdabop.cpp:160`、`shapefillop.cpp:68`、`solidfillop.cpp:55`、`freetransformop.cpp:244`（写补丁）。
  - `QImage::fill`：`solidfillop.cpp:36` → `TileBuffer::fill`（`tilebuffer.cpp:248-253`）。
  - `QPainter`：`stampdabop.cpp:34`（`QPainter painter(&target)`，`target` 就是层内瓦片）。**「QPainter 画共享 QImage 会 detach」这一条本次未逐行核实**（`scanLine` 路径已由 `highpassfilterop.cpp:28` 的生产注释与 `t_tilecache.cpp:216-218` 的测试共同钉住）→ 由 T1 用测试钉死；不成立时的兜底见 §7 R1。
- **换页与 dirty 标志**：`evictOneTile` 只在 `slot.dirty || slot.swapOffset < 0` 时把瓦片回写 swap（`tilecache.cpp:351-354`）；`writeSlotToSwap` 会把 `dirty` 清成 false（`tilecache.cpp:301-311`）；`TileCache::markDirty(TileSlot&)` 是 **public**（`tilecache.h:73`）。换页默认关（`swapEnabled()` = 预算 >0 且开关开，`tilecache.h:102`；由 `PSLITE_TILE_CACHE_MB` 设定）。
- **"仅在 swap"的瓦片读回**：`tileAt`/`ensureTile` 经 `makeResident`/`makeResidentConst` 按需读回（`tilebuffer.cpp:191-236`）。
- **入栈时机永远在改动之前**：例如 `painttool.cpp:93` 在 `PaintEngine::stampDab`（`:96`）之前压栈；后续 `mouseMove` 每个 dab 继续写同一层，**一笔只压一条**。⇒ 压栈时**无法知道**本笔的脏区（脏区随拖动增长）。
- **13 个像素入栈点**：
  `imagedocument.cpp:298`（填充路径）、`:323`（描边路径）、`:477`（更新链接）、`:569`（清除）、`:597`（填充）；
  `tools/painttool.cpp:93`、`paintbuckettool.cpp:76`、`gradienttool.cpp:108`、`shapetool.cpp:224`、`tonetool.cpp:52`、`focustool.cpp:62`、`clonestamptool.cpp:138`、`transformtool.cpp:671`。
- **其中会改层 extent 的两处**：`imagedocument.cpp:476-481`（更新链接：`replaceFromImage`，注释写"尺寸可能变"）、`transformtool.cpp:671`（自由变换，随后 `expandToIncludeLocal`）。其余 11 处只写像素，不改 extent/offset。
- **空脏区仍会留下撤销条目**：`imagedocument.cpp:569-577`、`:597-604` 先压栈、算完 `dirtyLocal` 若为空就 `return false`，条目**不撤销**（既有行为，本计划不改，见 §7 R7）。
- **测试入口**：既有用例在 `psDemo/tests/`，源文件登记在 `psDemo/tests.pro:61-62`，用例表在 `psDemo/tests/testmain.cpp:106-107`；断言用 `PsTests::Reporter::check`，退出码 = 失败用例数（`docs/tests.md:13-20`）。

## 3. 方案

**选型（唯一答案）**：在 `TileBuffer` 上新增**瓦片引用快照**（`TileRefSnapshot`：key → `QImage` 的**共享引用**，不是像素拷贝）+ `swapWithRefSnapshot()` 双向交换；`LayerPixelsUndo` 增加 **tile-ref 模式**（extent 不变的像素操作用它，13 处里的 11 处），保留整层模式给改 extent 的两处；像素撤销出栈改为**只标受影响矩形**。

**理由**：

1. **入栈点零重排**。因为压栈时机是"改动之前"而脏区要等算子返回才有（见 §2 最后几条），"按脏区做矩形快照"必须引入"先压空条目、写前逐次补矩形"的新协议，并逐个算子核对"实际写域 == 声明脏区"。瓦片引用快照在**现有压栈点原地**就能拿到全部旧像素，不需要知道脏区。
2. **成本正比于真正被写的像素**。未触碰的瓦片只是引用计数 +1；被写的瓦片由 Qt 在首次非 const 访问时自动 detach，副本大小 = 那张瓦片。
3. **机制已被本项目测试覆盖**（`t_tilecache.cpp:207-220`），不是新引入的假设；且恢复用"交换"与现有 `pop()` 的 swap 语义一致，redo 对称性天然成立。
4. **不改上层**：`HistoryStack`、`UndoItem` 接口、菜单文案、UI 全部不动；失败可整体回退（见 §7）。

**不采用的做法及原因（各一句）**：

- **按脏区做矩形像素快照**（`TilePatch::extract/blit`）：需要新的"写前补快照"协议 + 逐算子写域审计，改动面与风险都更大（且不解决"压栈时不知脏区"这个根本矛盾）。
- **深拷贝整份瓦片哈希**：等价于现在的整层拷贝，失去全部收益。
- **命令模式 + 重放**：本工程有位置相关算子（杂色/风/位移）、异步定稿与 live 预览，重放等价性无法低成本保证。
- **顺带把蒙版也改引用快照**：蒙版只有 1 字节/像素且不参与瓦片记账，本期保留深拷贝，减少一个风险面（只有"编辑蒙版"路径会付这份成本）。

## 4. 任务分解

> 依赖顺序：**T4 与 T1 可并行**（T4 只动 `undoitem.cpp` 的 `pop`，不依赖新 API）；T2 依赖 T1；T3 依赖 T2；T5 依赖 T3、T4。

### T1 COW 写样式不变量 + `TileRefSnapshot` 定义

- **改动的文件**：`psDemo/domain/tilebuffer.h`（只加类型定义与注释）、`psDemo/tests/t_undocow.cpp`（新增）、`psDemo/tests.pro`、`psDemo/tests/testmain.cpp`
- **依赖**：无
- **规模**：约 3 文件 / +130 行
- **要点**：先写测试，把"共享的 `QImage` 被写入时另一持有者不受影响"钉死，覆盖三种写样式：① `scanLine()` 写；② `QImage::fill()`；③ `QPainter` 绘制。写法：从 `TileBuffer` 取一块瓦片 → 复制该 `QImage` 作为"快照" → 用三种样式之一写原瓦片 → 断言快照的像素与 `cacheKey` 语义（快照内容 == 写前内容）。
- **风险点**：③ 若不成立（`QPainter` 不 detach），**先不要改 Qt 用法**，按 §7 R1 的三行兜底处理后再继续。
- **验收**：
  ```text
  <build-dir>\release\psdemo_tests.exe undocow
  ```
  期望结果：3 种写样式各自 PASS；退出码 0。

### T2 `TileBuffer` 快照 / 交换 / 记账 / 换页读回

- **改动的文件**：`psDemo/domain/tilebuffer.h`、`psDemo/domain/tilebuffer.cpp`、`psDemo/tests/t_undotile.cpp`（新增）、`psDemo/tests.pro`、`psDemo/tests/testmain.cpp`
- **依赖**：T1
- **规模**：约 3 文件 / +200 行
- **要点**：实现 §5 的四个成员函数；`swapWithRefSnapshot` 必须处理三件事：① 两边都有的 key → 交换 `QImage`；② 只有快照有的 key（撤销时层里被删掉的格）→ 新建透明格再交换；③ 只有层里有的 key（操作中新分配的格）→ 移交给快照并把该 key 从层里删除（语义 = 透明）。删除/插入都要按 §5 的记账规则调用 `noteEvicted`/`noteResident`，并对装入的槽调用 `TileCache::markDirty(slot)`。
- **风险点**：换页开启时若快照持有 `img == null` 的槽，会**丢旧像素**（swap 文件稍后被覆盖）；删 key 时漏记账会让 `PSLITE_TILE_CACHE_MB` 的预算判断失真。
- **验收**：
  ```text
  <build-dir>\release\psdemo_tests.exe undotile tilecache
  ```
  期望结果：① 快照 → 写若干瓦片 → 交换 → `materialize()` 与快照前逐像素相同（`PsTests::diffCount == 0`）；② 再交换一次（模拟 redo）与写后状态逐像素相同；③ "操作新建了瓦片"的场景交换后 `hasPixelData()`/`allocatedTileCount()` 回到快照前；④ 换页开启（`cache.setBudgetBytes(小值)` + `setSwapStoreForTest`）时同样逐像素相同；⑤ `tilecache` 用例保持绿。退出码 0。

### T3 `LayerPixelsUndo` tile-ref 模式 + 调用点声明

- **改动的文件**：`psDemo/app/undoitem.h`、`psDemo/app/undoitem.cpp`、`psDemo/domain/imagedocument.h`、`psDemo/domain/imagedocument.cpp`、`psDemo/domain/imagedocument.cpp:477`（传 WholeLayer）、`psDemo/tools/transformtool.cpp:671`（传 WholeLayer）、`psDemo/tests/t_undoregion.cpp`（新增）、`psDemo/tests.pro`、`psDemo/tests/testmain.cpp`
- **依赖**：T2
- **规模**：约 6 文件 / +260 行
- **要点**：见 §5 的接口与 `pop()` 规格；`pushLayerPixelsUndo` 增加带模式参数的**默认参数重载**（`imagedocument.h:288` 那行保持可用，老调用点不改也能编译）。tile-ref 模式**不动 offset**（调试下断言 offset 未变），整层模式保持现有行为一字不改。
- **风险点**：pop 时若发现层 extent 与快照不符（说明调用点选错了模式），必须**走安全网**：整幅标脏 + `qWarning` + Debug 下 `Q_ASSERT`，绝不允许按错误的网格交换。
- **验收**：
  ```text
  <build-clean 或既有构建目录>\release\psdemo_tests.exe undoregion phase1a filterundo transformlive
  ```
  期望结果：① `phase1a` 全绿（含 A2 蒙版、A27、A34、A35）；② 新用例 `undoregion` 覆盖：画笔一笔 → undo → 与操作前 `materialize()` 逐像素相同；redo → 与撤销前逐像素相同；多笔交替 undo/redo 20 次仍逐像素一致；③ 内存断言：一笔后 `history().undoBytes()` 增量 ≤ `触碰瓦片数 × 6144 + 65536`（边缘瓦片小于 64×64，故用 6144 = 48×128 作宽松上限，实测值在 T5 记录）。退出码 0。

### T4 出栈只标受影响矩形（0 档）

- **改动的文件**：`psDemo/app/undoitem.cpp`（仅 `LayerPixelsUndo::pop`）、`psDemo/tests/t_undodirty.cpp`（新增）、`psDemo/tests.pro`、`psDemo/tests/testmain.cpp`
- **依赖**：无（与 T1–T3 并行）
- **规模**：约 2 文件 / +90 行
- **要点**：按 §5 的 `pop()` 规格计算矩形；**必须在调用 `replaceFromImage`/交换之前**先取活层的 `boundsInDocument()`（交换后活层就变成旧状态了）。矩形为空时回退到无参 `markDirty()`（保持既有"一定发 contentChanged"的行为）。
- **风险点**：矩形算小了会留下"旧像素残影"（因为投影像素没被标脏）；矩形算大了只是慢。因此**取并集时宁可保守**：老 bounds ∪ 新 bounds ∪ `styleBoundsInDocument()`（样式光晕会超出层 bounds）。
- **验收**：
  ```text
  <build-dir>\release\psdemo_tests.exe undodirty phase1a
  ```
  期望结果：① 400×400 文档上画一小笔 → 撤销 → 断言 `doc.dirtyRect()` 严格小于整幅且包含被改像素所在矩形；② 带投影样式的层：撤销后断言 `dirtyRect` 包含 `styleBoundsInDocument()`；③ 层在画布外（offset 远大于画布）时撤销不崩、且 `contentChanged` 已发（走整幅回退）；④ `phase1a` 全绿（尤其 A2 的扩层撤销）。退出码 0。

### T5 回归、实测与记账

- **改动的文件**：`docs/pending-dev.md`（新增一节）、本文件（进度总览）、`docs/tech-notes.md`（仅在确有需要时补一段撤销粒度说明）
- **依赖**：T3、T4
- **规模**：文档 2 处 / ±60 行
- **要点**：跑全量；用一次性程序或新用例打印三组数字：① 改造前后"一笔撤销的 `undoBytes` 增量"（同一文档、同一笔）；② 改造前后"200 笔历史的峰值内存"（`captureMemSnapshot().historyBytes` 或 `historyBytes()`）；③ 改造前后"撤销一次的重投影耗时"（`Projection::sync` 前后 `QElapsedTimer`，取 4000×4000 文档）。数字必须来自实跑，不接受估算。
- **风险点**：只写"更快"而不写数字 = 这一轮白做（用户要靠数字判断是否继续做 2 档）。
- **验收**：
  ```text
  <build-dir>\release\psdemo_tests.exe
  ```
  期望结果：退出码 0（当前基线：`phase1a` 在受限环境下曾因 `QTemporaryDir` 假红，见 `docs/phase1-audit-review.md`；若仍红，先确认是同一脚手架问题，再记录到 T5 报告里）；三组数字写入 `pending-dev.md`。

## 5. 精确实施规格

**新增/修改的接口与签名**：

```cpp
// 文件：psDemo/domain/tilebuffer.h（类 TileBuffer 内，public）
    /**
     * 瓦片引用快照：只保存 QImage 的**共享引用**（隐式共享），不做像素深拷贝。
     * images 里没有的 key = 当时该格「未分配」，语义为全透明。
     * tilesX/tilesY/width/height 用于交换前校验网格一致（不一致必须拒绝）。
     */
    struct TileRefSnapshot {
        int width = 0;
        int height = 0;
        int tilesX = 0;
        int tilesY = 0;
        QHash<qint64, QImage> images;
        /** 快照当前持有的像素字节（含边缘块实际尺寸），供撤销条目记账。 */
        qint64 bytes() const;
    };

    /**
     * 取引用快照。会把「仅在 swap」的瓦片读回内存（换页开启时才有代价，
     * 代价 = 该层换出的瓦片数 × 一次读盘；默认换页关闭 ⇒ 无代价）。
     */
    TileRefSnapshot takeRefSnapshot();

    /**
     * 与快照做双向交换（undo/redo 同一入口）：
     *  · 两边都有的 key：交换 QImage（O(1) 引用交换）
     *  · 只有快照有的 key：层内新建透明格后交换（= 恢复旧格）
     *  · 只有层有的 key：把该 QImage 交给快照并从层内删除（= 回到「未分配」）
     * 网格（width/height/tilesX/tilesY）不一致时**不做任何改动**并返回 false。
     * 成功返回 true，调用方负责 markDirty。
     */
    bool swapWithRefSnapshot(TileRefSnapshot &snap);

    /** 把当前持有像素的全透明格删除（本计划不使用；为后续瓦片回收预留，不得在其他任务里调用）。 */
    void eraseFullyTransparentTiles();
```

```cpp
// 文件：psDemo/app/undoitem.h（LayerPixelsUndo）
enum class PixelUndoMode {
    WholeLayer, ///< 现有行为：整层 materialize 快照，交换整幅图（extent 会变的操作必须用它）
    TileRefs,   ///< 新增：瓦片引用快照，只交换被写到的格
};

    /** 新增构造（tile-ref 模式）。pixels 不参与；蒙版语义与既有构造相同。 */
    LayerPixelsUndo(int layerIndex, TileBuffer::TileRefSnapshot snap, int extentW, int extentH,
                    int offsetX, int offsetY, bool hasMask, bool maskEnabled, bool maskLinked,
                    QImage maskGray, const QString &label,
                    PixelUndoMode mode = PixelUndoMode::TileRefs);
```

```cpp
// 文件：psDemo/domain/imagedocument.h（ImageDocument）
    /** 现有签名保持不变（= WholeLayer），仅供改 extent 的调用点使用。 */
    void pushLayerPixelsUndo(int layerIndex, const QString &label);
    /** 新增：默认 tile-ref（extent 不变的像素操作）。 */
    void pushLayerPixelsUndo(int layerIndex, const QString &label,
                             PixelUndoMode mode);
```

**新增数据结构与成员**：

- `TileBuffer::TileRefSnapshot`（上）。
- `LayerPixelsUndo` 新成员：`TileBuffer::TileRefSnapshot m_tileSnap;`、`int m_extentW/m_extentH;`、`PixelUndoMode m_mode = PixelUndoMode::WholeLayer;`。
- `LayerPixelsUndo::byteSize()`：`TileRefs` 模式返回 `m_tileSnap.bytes() + m_maskGray.sizeInBytes() + 128`；`WholeLayer` 模式保持 `m_pixels.sizeInBytes() + m_maskGray.sizeInBytes() + 64`（现 `undoitem.cpp:86-88`）。

**接口契约**：

- **所有权**：`TileRefSnapshot::images` 里的 `QImage` 由快照持有（引用计数），与层内槽共享缓冲直到任一侧写入；快照析构即释放。**不需要**在 `TileCache` 里登记这这份内存。
- **原地修改**：`takeRefSnapshot()` 会（仅当需要时）把 swap 中的瓦片读回内存（副作用：`m_usedBytes` 上升）；`swapWithRefSnapshot()` 原地改层内 `m_tiles` 与传入的快照。
- **错误路径**：网格不一致 → `swapWithRefSnapshot` 返回 false 且零改动；调用方（`pop`）走安全网（整幅标脏 + `qWarning`）。
- **坐标**：快照与交换全部在**层内坐标**；`pop` 里算脏区时才加 `offsetX/offsetY` 变成文档坐标。文档矩形一律 `∩ QRect(0,0,doc.m_width,doc.m_height)`。
- **alpha**：瓦片一律预乘 `Format_ARGB32_Premultiplied`（`tilebuffer.h:31`），本计划不做任何格式转换。

**`LayerPixelsUndo::pop()` 规格（按模式分叉）**：

```cpp
// 1) 取活层；不存在直接 return（保持现状）
// 2) 先记下「当前」几何与蒙版（交换前！）
const QRect curBoundsDoc = layer->boundsInDocument();
// 3) 分叉
if (m_mode == PixelUndoMode::WholeLayer) {
    // 现有逻辑一字不改（materialize + replaceFromImage + 蒙版交换）
} else {
    if (layer->tiles().width() != m_extentW || layer->tiles().height() != m_extentH) {
        qWarning("undo: extent 变化与 tile-ref 模式不符（调用点应用 WholeLayer）");
        Q_ASSERT(false);                 // Debug 下暴露
        // 安全网：按整层兜底交换，至少不写错网格
        QImage live = layer->materialize();
        layer->replaceFromImage(m_pixels_or_snapshotMaterialized());
        ...
    } else {
        layer->tiles().swapWithRefSnapshot(m_tileSnap);   // 像素回退
        // 蒙版交换（与现有逻辑相同：setMask / image() 赋值）
    }
}
// 4) 标脏（T4 的规则，两种模式共用）
const QRect docRect(0, 0, doc.m_width, doc.m_height);
QRect dirty = QRect(m_offsetX, m_offsetY, 旧宽, 旧高)          // WholeLayer: m_pixels 尺寸；TileRefs: m_extentW/H
                  .united(curBoundsDoc)
                  .united(layer->styleBoundsInDocument())
                  .intersected(docRect);
if (dirty.isEmpty()) doc.markDirty(); else doc.markDirty(dirty);
```

**默认值与开关**：

- 默认模式 = **tile-ref**；只有 `imagedocument.cpp:477`（更新链接）与 `transformtool.cpp:671`（自由变换）显式传 `PixelUndoMode::WholeLayer`。
- **不引入运行时开关**（不做环境变量/Kill switch）：回退靠 git 回滚，或把该调用点改成 `WholeLayer`（一行）。
- 新增像素入栈点的人必须判断"该操作是否会改层 extent"；会改就必须传 `WholeLayer`。这条规则要写进 `docs/cursor-role-prompt.md` 或 `.cursor/rules/*` 的既有约定里（由实施方在 T5 一并登记）。

**数值与坐标约定**：

- 瓦片边长 64（`TileBuffer::kTileSize`，`tilebuffer.h:39`）；key = `ty * tilesX + tx`（`tilebuffer.h:96`）；边缘瓦片按实际尺寸（`tileBounds`）。
- 索引基准 0-based；矩形 right/bottom 为**闭区间**（`QRect` 语义，`forEachTileInRect` 用 `right()/kTileSize`，`tilebuffer.cpp:314-317`）。
- 记账：层内**新增**瓦片 → `TileCache::instance().noteResident(bytes)`；**删除/移出**瓦片 → `noteEvicted(bytes)`；装入的槽必须 `TileCache::instance().markDirty(slot)`（否则换页开启时该槽的 swap 副本可能不更新，`tilecache.cpp:351-354`）。字节用 `slotResidentBytes`/`imageBytes` 的既有口径（`tilebuffer.cpp:77-88`）。

**可复用的既有工具函数**：

- `TileBuffer::tileAt/ensureTile/tileBounds/forEachAllocatedTile/visitResidentSlots/materialize/allocatedTileCount`（`tilebuffer.h:54-91`）。
- `TileCache::markDirty(TileSlot&)`、`noteResident/noteEvicted`、`swapEnabled()`、`instance()`（`tilecache.h:63-102`）。
- `ImageDocument::markDirty(rect)`（含相交层缓存失效，`imagedocument.cpp:1198-1210`）。
- `PsTests::diffCount(before, after)`（比较两张图，`tests/testutil.h`）、`Reporter::check`。
- 快捷键/菜单**不用改**（`mainwindow.cpp:603-619` 只读 `canUndo/undoText`）。

## 6. 验收口径

- **功能验收**：
  - 一笔撤销后逐像素回到操作前（`diffCount == 0`），redo 后逐像素回到撤销前。
  - 交替 undo/redo ≥ 20 次后仍逐像素一致（守住 swap 对称性）。
  - 撤销后 `dirtyRect` 小于整幅（除非层 extent = 画布且样式外扩覆盖全幅）。
  - `undoBytes` 增量与"触碰瓦片数 × 瓦片字节"同量级（T3 断言 + T5 实测数字）。
- **回归项（必须保持不变）**：
  - `phase1a` 的 A2（带蒙版层扩尺寸后撤销，蒙版逐字节相同）、A27（会话层删除后取消不污染别层）、A34（预乘插值）、A35（纯单击不压撤销）。
  - `tilecache`（换页往返 bit-exact、预算遵守、降级）、`filterundo`（滤镜撤销原子性）、`transformlive`、`composite`、`projectio`/`psdio`（存读保真）。
  - 清除/填充在空选区下 `return false` 时**仍会**在栈上留下条目（既有行为，不在本计划修）。
- **测试命令**（构建方式见 `docs/tests.md:13-18`，用你现有的构建目录即可）：
  ```bat
  <build-dir>\release\psdemo_tests.exe undocow undotile undoregion undodirty
  <build-dir>\release\psdemo_tests.exe
  ```
- **手工验收步骤**（GUI）：
  1. 新建 2048×2048 文档 → 新建图层 → 画笔在右下角画一笔。
  2. Ctrl+Z：画面只重绘该笔区域（观察窗口标题的脏区日志或 `PSLITE_MEM` 输出），像素与画前一致；Ctrl+Y 恢复。
  3. 连续画 10 笔 → 交替 Ctrl+Z/Ctrl+Y 五次，画面始终与对应步骤一致。
  4. 图层带蒙版：涂黑左半 → 画笔在蒙版上画 → Ctrl+Z，蒙版逐字节恢复（对照 A2 的现象：不得变全白）。
  5. 设 `PSLITE_TILE_CACHE_MB=64` 重复第 2–3 步（换页开启路径），结果必须相同。

## 7. 风险与回退

| # | 风险 | 现象 | 回退办法 |
|---|---|---|---|
| R1 | `QPainter` 画共享 `QImage` 不触发 detach | T1 用例③ 红；撤销后画面出现"笔迹没被撤销/撤销后新旧混在一起" | 在 `stampdabop.cpp:34` 前加一行 `(void)target.bits();` 强制 detach（`QImage::bits()` 非 const 会深拷贝），并把该行注释为"COW 契约"；T1 用例保留为护栏 |
| R2 | 快照时层内有"仅在 swap"的瓦片 | 撤销后出现成片旧像素 | `takeRefSnapshot()` 内部一律先 `makeResident`（`tilebuffer.cpp:191-236`）；T2 用例④ 用 `setSwapStoreForTest` 专门覆盖 |
| R3 | 调用点选错模式（操作改了 extent 却用 tile-ref） | 层尺寸错乱 / 撤销后尺寸不对 | `pop` 的安全网：extent 不符 → `qWarning` + `Q_ASSERT` + 整层兜底，绝不按错误网格交换 |
| R4 | 漏记账（新增/删除瓦片未 `noteResident/noteEvicted`） | `PSLITE_TILE_CACHE_MB` 预算判断失真、换页过早/过晚 | T2 用例断言 `residentBytes()` 与 `allocatedTileCount()` 在"快照→写→交换→再交换"后回到初值 |
| R5 | 脏区矩形算小 | 撤销后残留旧像素（静默错像素） | 取并集时保守：旧 bounds ∪ 新 bounds ∪ `styleBoundsInDocument()`；T4 用例② 用带样式的层钉住 |
| R6 | 撤销条目仍用绝对层下标（本计划不修） | 层增删后旧条目可能指向别的层（既有问题） | 本计划不引入新风险；修它属"2 档前置"（层稳定 id），另立计划 |
| R7 | 空脏区仍压条目（既有） | 多一步"看起来没反应"的 Ctrl+Z | 本计划使它的代价降到 O(引用数)；若要修，另开 3 行任务（先算脏区或在 `return false` 前弹出刚压的条目） |
| R8 | 快照持有的瓦片不计入页缓存预算 | 实际内存高于 `PSLITE_TILE_CACHE_MB` 的账面 | 快照字节进 `byteSize()`（进而进 `historyBytes()` 与 256MB 上限）；T5 的 `PSLITE_MEM` 快照要打印 `historyBytes` 与 `allocatedTileCount` |

**整体回退**：所有改动都落在 4 个产品文件内（`tilebuffer.*`、`undoitem.*`、`imagedocument.*`）+ 1 个调用点（`transformtool.cpp`）。把 `pushLayerPixelsUndo` 的默认模式改回 `WholeLayer` 即回到改造前行为（一处改动，无需回滚其他代码）。

## 8. 实施方记录要求

- 每完成一个任务，在本文件 §0"进度总览"登记：任务号、完成日期、验收证据（命令 + 关键输出行 + 实测数字）。
- 同步更新：`docs/pending-dev.md` 新增一节「撤销粒度：整层 → 瓦片引用快照（2026-10）」，写现象/根因/改法/被否掉的方案（矩形快照与命令模式）/三组实测数字/仍欠（2 档：非线性、层稳定 id）。
- 若过程中发现"某个像素入栈点的操作会改 extent"这类新事实，**追加到本文件 §2**，不要静默处理。
- 提交信息格式沿用工程习惯：`fix:` / `feat:` / `test:` / `docs:` 前缀 + 中文摘要（见 `git log --oneline -20`）。
- 未在计划内的额外改动，需在完成后单独列出并说明原因。

## 附：给实施方的一句话提示词

```text
请阅读 photoshopDemo/docs/undo-region-plan.md，按 T1→T2→T3→T5 顺序实施（T4 可与 T1 并行先做）；每个任务完成后按 §0 登记验收证据（命令 + 输出 + 实测数字）；不要做计划外改动；遇到计划与代码现状冲突时停下并告知我，尤其注意 T1 的 QPainter COW 验证结果。
```
