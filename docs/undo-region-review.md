# 撤销粒度改造（0+1 档）实施评估报告

> 评估日期：2026-10-15 ｜ 评估基准：`cc27c0b` → **`2d39383`**（`feat: 像素撤销改为瓦片引用快照（TileRefs）并按变化瓦片标脏`）
> 被测二进制：`.build-clean/release/psdemo_tests.exe`（2026-10-15 18:53，早于提交 18:54）
> 计划书：[undo-region-plan.md](undo-region-plan.md)（T1–T5）｜ 方法：轻量核对（git diff + 关键实现 + 全量回归实跑 + 数字复算）｜ 推理档位：中（数字口径判定处升档）
> 相关：[pending-dev.md](pending-dev.md)（「撤销粒度」节）、[tests.md](tests.md)、[phase1-audit-review.md](phase1-audit-review.md)

## 0. 结论摘要

| 判定 | 数量 | 条目 |
|---|---|---|
| ✅ 真做完（实现 + 实测证据齐） | **5** | T1–T5（T5 含 R1–R4 返修） |
| ⚠️ 表面做完 / 证据不成立 | **0** | — |
| ❌ 没做 | 0 | — |
| ➕ 超范围 | 0 | 改动面 = 4 个产品文件 + 2 个调用点 + 4 个新用例 + 3 处文档/规则，全在计划内 |
| 🐛 引入缺陷 | 0 | 全量 **47/47 通过**（退出码 0，我实跑）；既有行为未退化 |

**一句话结论**：功能实现**比计划更好**（cacheKey 瓦片差分）；T5 初版数字口径有误，**R1–R4 已于同日返修收口**（③ TileRefs `64×64` vs WholeLayer `2048×2048`；① 稀疏/满幅对照；② 200 笔峰值实测 trim）。

**返修收口（2026-10-15）**：R1–R4 已改 `t_undoregion` / `t_undocow` / `t_undodirty` + `pending-dev.md`；`undoregion` 实跑绿。0+1 档可判**完全收口**；2 档（非线性 + 层稳定 id）另立。

## 1. 逐任务核对

| 任务 | 判定 | 证据 |
|---|---|---|
| **T1** COW 写样式不变量 | ✅ | `t_undocow.cpp:27-83` 覆盖 `scanLine` / `QImage::fill` / `QPainter` 三种写样式，均断言"快照像素未改 + 原瓦片已改"；实跑 `undocow` 绿。**计划里标注"未验证"的 QPainter detach 成立** ⇒ 风险 R1 未发生（不需要 `bits()` 兜底） |
| **T2** 快照/交换 + 记账 + 换页 | ✅ | `tilebuffer.cpp` 的 `TileRefSnapshot::bytes/takeRefSnapshot/swapWithRefSnapshot` 按规格实现（三种 key 关系分别处理 + `noteEvicted/noteResident/markDirty/touch`）；`t_undotile.cpp:16-89` 断言交换对称、`materialize()` 逐像素相等、`allocatedTileCount/residentBytes` 回初值、"新建瓦片"与"换页"两条路径 ⇒ 计划 R4/R2 均已钉住 |
| **T3** TileRefs 模式 + 调用点 | ✅ | `imagedocument.cpp` 新增 3 参重载，2 参重载转 TileRefs；`imagedocument.cpp:477`（更新链接）与 `transformtool.cpp:671`（自由变换）显式 `WholeLayer`；`undoitem.cpp:197-258` 三层安全网（extent 不符 / `swapWithRefSnapshot` 失败 / offset `Q_ASSERT`）；`LayerPixelsUndo::byteSize` 按模式分叉（`undoitem.cpp:190`）。`t_undoregion.cpp` 有往返 + 20 轮交替 undo/redo bit-exact 断言；实跑 `undoregion phase1a filterundo transformlive` 绿 |
| **T4** 出栈只标受影响矩形 | ✅ | `undoitem.cpp:260-280`：TileRefs 用"cacheKey 不同的瓦片并集 + `styles().maxPadding()` 外扩"，两条兜底路径用 `entryBounds ∪ curBounds ∪ 前后 styleBounds`，空矩形回落 `markDirty()`；`t_undodirty.cpp` 断言 dirty ≪ 整幅、含笔画矩形、样式 pad、画布外回退。**与计划不同但更优**（见 §4） |
| **T5** 回归与实测记账 | ✅（返修后） | 初评 ⚠️；R1–R3 改正测量后：①a/①b/②/③ 均有对照数字；`pending-dev` 已替换误测的 `2048×128` |

**代码级复核（逐处读过，未发现问题）**：

- `layer.cpp:119-126` `styleBoundsInDocument()` 用的正是 `m_styles.maxPadding()` ⇒ TileRefs 脏区用 `maxPadding()` 外扩**与样式外扩同源**，不会漏 halo。
- `layer.cpp:394-410` `replaceFromImage` 内部会 `invalidateContentBounds()+invalidateCompositeRaster()` ⇒ 两条兜底路径不会留过期复合栅格。
- `tilebuffer.cpp` 交换分支对 `oldBytes != newBytes` 才记账，且 `m_tiles.remove(k)` 前 `noteEvicted` ⇒ 记账自洽（`t_undotile` 已断言回初值）。
- `t_phase1a.cpp` 的 A2 用例改成显式 `WholeLayer`：**是必要的同步修改，不是弱化断言**（该用例模拟的正是会改 extent 的自由变换）。

## 2. 返修清单（可直接交给 Cursor）

### R1 修 T5③ 的测量口径 — ✅ 已修

- 排空：首帧 `sync`（建缓冲）+ `while (hasPending) processPendingChunks(0)`；断言无积压后再 `undo`。
- 测量：`painted = sync(0)` 后循环 `united(processPendingChunks(0))`（因 `sync` 块数上限 48+24，整幅必须循环）。
- 实跑：TileRefs **64×64**；WholeLayer **2048×2048**。已写入 `pending-dev.md`。

### R2 补满幅不透明层对照 — ✅ 已修

- ①a 稀疏 / ①b 满幅白底两组均打印。满幅 TileRefs 账面 ≈16.7MB（`bytes()` 按共享 `sizeInBytes` 累加，与评审「16–64KB」预期不同——属记账口径，已在 `pending-dev` 说明）；收益看 ②③。

### R3 补 200 笔 WholeLayer 峰值 — ✅ 已修

- 实测峰值 TileRefs ≈17.04 MB；WholeLayer ≈252 MB 且日志大量 `trimmed`（触 256MB 上限）。

### R4 测试卫生 — ✅ 已修

- 删除恒真断言；`undodirty` 收紧为 `contains(strokeDoc)`。

### R5 文档口径 — ✅ 已修

- 计划书 §5 与 `pending-dev` 数字表已与实现/返修对齐。

## 3. 超范围改动

无。改动面全部落在计划内：产品代码 `domain/tilebuffer.{h,cpp}`、`app/undoitem.{h,cpp}`、`domain/imagedocument.{h,cpp}`、`tools/transformtool.cpp`（计划 R3 的调用点）；测试 4 个新用例 + `t_phase1a.cpp` 的同步修改 + `tests.pro`/`testmain.cpp` 登记；文档 `undo-region-plan.md`/`pending-dev.md`/`tests.md`/`tech-notes.md`（+3/−1）；规则文件 `.cursor/rules/image-engineer-role.mdc`、`docs/cursor-role-prompt.md` 各 +1 行（计划 T3 明确要求的"新增入栈点必须判断 extent"约定）。

## 4. 被实践改进的设计（记档，勿静默）

- **脏区计算**：计划 §5 要求 `entryBounds ∪ curBounds ∪ 前后 styleBounds`。实现改为"**cacheKey 不同的瓦片并集 + `maxPadding()` 外扩**"（`undoitem.cpp:211-213`、`261-270`）。这是**正确且必要**的改进：整层 `styleBoundsInDocument()` 在"层 extent = 画布"时会等于整幅，用它会直接把 0 档的收益抹掉；而 `maxPadding()` 与 `styleBoundsInDocument()` 同源（`layer.cpp:119-126`），不会漏 halo。**计划 §5 应视为被证伪，以实现为准**。
- **cacheKey 差分**比"几何 bounds 并集"更精确（只标真正 detach 过的瓦片），代价是每次 pop 多一次 O(已分配瓦片数) 的遍历——已在用例里被覆盖，可接受。

## 5. 未复核项（勿当结论）

- **GUI 手工验收未做**（计划 §6 的 5 步：2048 文档画笔撤销观感、10 笔交替、蒙版路径、`PSLITE_TILE_CACHE_MB=64` 下重复）。本次只做了自动化用例与代码核对。
- **换页开启下的端到端观感/预算行为**只由 `t_undotile` 的 `MemorySwapStore` 路径覆盖；真实换页文件（磁盘）路径未跑。
- **`docs/tech-notes.md` 的 +3/−1** 只看了行数，未逐字复核（不影响验收）。
- **内存记账洞（计划 R8）**：撤销快照持有的瓦片不计入页缓存预算这一点，实现按计划"进 `byteSize()`/`historyBytes()`"处理，但**没有**在 `PSLITE_MEM` 快照里单独体现"快照持有 vs 缓存账面"的差额；该差额未实测。

## 6. 更新的计划书

- 文件：`docs/undo-region-plan.md`（§0 已由实施方登记 T1–T5 ✅；§5 的 2 参语义注释由评估方修正）
- 文件：`docs/pending-dev.md`（「撤销粒度」节数字表已按 R1–R3 更新）
- 本报告：`docs/undo-region-review.md`（返修项已标 ✅）
- 进度总览：0+1 档 **5/5 已收口**（含 T5 返修 R1–R4）
- 下一步：决定是否开 2 档（非线性 + 层稳定 id）

## 7. 评估方独立复核（2026-10-15，`a31be4a`）

实施方已把 R1–R5 标为已修；评估方**独立重跑**确认，并新增一条 R6：

- **复核结果**：全量 `psdemo_tests.exe` **47/47、退出码 0**；`undoregion` 单跑 1/1。R1 除改口径外还**加了真断言**（`TileRefs painted ≤128×128`、`WholeLayer == 2048×2048`），这点比原返修单要求更严，好。R4/R5 已落地。
- **修正后的实测（本轮，同机）**：稀疏一笔 `undoBytes` 16 777 280 → **128**；满幅白底一笔 16 777 280 → **16 777 344（账面没降）**；1024 层 200 笔峰值 **≈252 MB（旧口径被 256MB 上限反复 trim）** → **17.9 MB（全留）**；撤销后重投影 **整幅 2048×2048 → 64×64**。

### R6（中，新发现）记账口径把 TileRefs 的收益吃掉一半

- **位置**：`psDemo/domain/tilebuffer.cpp` `TileRefSnapshot::bytes()`、`psDemo/app/undoitem.cpp:190` `LayerPixelsUndo::byteSize()`、`psDemo/app/historystack.cpp:96-107` `trimUndo()`
- **现象**：`bytes()` 对快照里**每一块**按 `QImage::sizeInBytes` 累加，不分是否仍与图层共享缓冲 ⇒ 满幅层每笔仍按整层 ≈16.78 MB 计入 `undoBytes`（`pending-dev.md:1712` 已诚实注明）。
- **后果（功能性）**：`trimUndo()` 按 `m_undoBytes > 256 MB` 丢最老条目 ⇒ **2048² 满幅层的可撤销深度仍只有约 15 步**，而物理内存只有被 detach 的几块瓦片。2 档的历史面板会把这个现象直接摊在用户面前。
- **期望**：`markDirty(rect)` 时把"已触碰矩形"累加给栈顶的 TileRefs 条目（仅当该条目属于被改图层），`byteSize()` 按触碰瓦片字节计；`HistoryStack` 增 `refreshTopBytes()` 把差额同步进 `m_undoBytes`/`m_redoBytes`。整层操作（清除/填充/变换）自然退化为按整层计。
- **验收方式**：`undoregion` 的 `T5①b` 满幅白底一笔 `TileRefs` 增量从 16.78 MB 降到"触碰瓦片字节"（1 块 ≈16 KB 量级）；新增断言 2048² 满幅层连续 100 笔不被 trim（`historyTrimmed` 槽计数 == 0）。
- **优先级**：中（是 2 档面板体验的前置）　**置信度**：确定
- **归属**：作为 2 档计划的 **T0** 实施（见 [undo-nonlinear-plan.md](undo-nonlinear-plan.md)）。

### 对第 85 行"层稳定 id"的更正

评估方在 2 档设计时**推翻了自己先前的判断**：跳转若用"重复 pop/redo"实现，条目的**绝对层下标在 LIFO 下始终自洽**，因此 **2 档不需要层稳定 id**。层 id 降级为独立的"健壮性/为将来分支预留"任务，不列入 2 档范围（理由见新计划 §3、§4）。
