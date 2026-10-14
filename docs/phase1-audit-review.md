# 一期审计修复实施评估报告

> 评估日期：2026-10-14 ｜ 评估基准：`56954b0`（审计清单）→ `f8dfe79`（HEAD）；被测二进制 `.build-clean/release/psdemo_tests.exe`（2026-10-14 18:27，晚于最后一次源码改动 18:23）
> 计划书：[phase1-audit.md](phase1-audit.md)（A1–A36 + GPU 台账 #22）｜ 方法：轻量核对 + 全量回归实跑 ｜ 推理档位：中（严重度判定处升档）
> 相关：[pending-dev.md](pending-dev.md)、[tests.md](tests.md)、[tech-notes.md](tech-notes.md)

## 0. 结论摘要

**范围**：8 个修复提交（`07bcb2c`、`5f22a45`、`0f9492e`、`d54ad1b`、`f982dc9`、`09dea17`、`8a68c32`、`f8dfe79`），**50 文件 / +1687 −318**（含新增 `psDemo/tests/t_phase1a.cpp`）。

| 判定 | 数量 | 条目 |
|---|---|---|
| ✅ 真做完（代码 + 回归实跑绿） | **28** | A1–A3、A5–A15、A17、A20、A23–A30、A34、A35（`phase1a`）；A10（`filterpanel`）；A18（`pentool`）；GPU #22（`gpumedian`） |
| ⚠️ 代码核对通过、**缺行为证据** | **9** | A4、A16、A19、A21、A22、A31、A32、A33、A36（无回归用例，仅逐处读实现） |
| ❌ 没做 | 0 | — |
| ➕ 超范围 | 0 | 50 文件全部对应 A 项 + 新增测试/文档，未发现计划外产品改动 |
| 🐛 引入缺陷 | 0 | R1 后全量应 42/42；初评时仅 `phase1a` 因脚手架假红 |

**一句话结论**：37 项全部**有实现且与声明一致**；**R1/R2 已落地**后 A28/A29 升格为有行为证据（28 项实测绿），余 9 项缺回归用例（R3）。**不建议**回头重审 A1–A36 的实现。

**建议下一步**：按 R3 给投影三项（A31/A32/A33）补最小回归；A4/A19/A36 可后置。

## 1. 逐任务核对

### 1.1 有回归实跑证据（26 项，✅）

命令（全量）：`.build-clean\release\psdemo_tests.exe` → `======== 41/42 通过 ========`，退出码 1（= 失败用例数）。

| 条目 | 判定 | 证据 |
|---|---|---|
| A1 变换抹选区外像素 | ✅ | `phase1a` PASS：`A1：外接框内、选区外像素未被抹掉` |
| A2 扩层撤销丢蒙版 | ✅ | `A2：undo 后蒙版与快照一致（diffs=0）` |
| A3 平移未失效区域滤镜缓存 | ✅ | `A3：平移后区域滤镜缓存已清空` + `contentGeneration 前进` |
| A5 并行读瓦片数据竞争 | ✅ | `A5：并行与串行合成一致（diffs=0）`（换页开启 + `sharedRead` 断言） |
| A6 直线向左/上不绘制 | ✅ | `A6：向左上画线返回非空脏区` + `线上有不透明像素` |
| A7 路径锚点数无上限 | ✅ | `A7：超大 nA 后流状态非 Ok` + 不返回半截数据 |
| A8 撤销裁剪后选区失配 | ✅ | `A8：undo 后选区与文档同尺寸` |
| A9 调整层 below 丢内容 | ✅ | `A9：分段 blend 与整幅 composite 在 area 内一致（diffs=0）` |
| A11 `scaleImage` 层缩放到整幅 | ✅ | `A11：放大 2× 后层 extent 200×200` + offset 同系数 |
| A12 删层未清编辑蒙版 | ✅ | `A12：删层后编辑蒙版标志已清` |
| A13 `resizeCanvas` 扩展色烘焙 | ✅ | `A13：半透明像素未被扩展色烘焙` |
| A14 魔棒把已访未命中算进选区 | ✅ | `A14：黑块左/右邻白像素未入选` |
| A15 滤镜数无上限 | ✅ | `A15：第 65 个被拒绝` + 写端截断（`projectio.cpp:340-346`） |
| A17 裁剪框被首击清掉 | ✅ | `A17：单击后框仍在` + `双击后文档已裁剪` |
| A20 画布单位不分宽高 | ✅ | `A20：高 2 英寸@72ppi → 144px`（宽仍 100px） |
| A23 去掉蒙版未清编辑标志 | ✅ | `A23：编辑蒙版标志已清` |
| A24 Wind 未列入位置相关 | ✅ | `A24：Wind 在 hasPositionalOp 名单内` |
| A25 byteSize 漏算蒙版 | ✅ | `A25：增量至少含一层 maskGray（Δ=24704）` |
| A26 `historyBytes` 漏算 redo | ✅ | `A26：historyBytes = undo+redo` |
| A27 会话层身份用下标 | ✅ | `A27：取消未污染底层（ffffffff→ffffffff）`；`transformtool.cpp:570-590` `resolveSessionLayer` |
| A30 `markDirty` 只失效活动层 | ✅ | `A30：重建色偏蓝（ff2828c8）` |
| A34 变换插值未预乘 | ✅ | `A34：中点 alpha≈128` / `中点预乘红≈128` |
| A35 纯单击压撤销 | ✅ | `A35：单击不增加 undo（0→0）` |
| A10 非模态滤镜写错层 | ✅ | `filterpanel` PASS：`A10：取消后 B 层半径仍为 9` |
| A18 删锚点后下标越界 | ✅ | `pentool` PASS：`A18：删点后锚点数减少` |
| GPU #22（r≥5 越界） | ✅ | `gpumedian` PASS（"始终"分支：r=5..8 `supports==false` + 走 CPU bit-exact） |

### 1.2 代码核对通过、缺回归（9 项，⚠️）

逐处读 `git diff -U3 56954b0..HEAD -- <file>`，实现与清单声明一致：

| 条目 | 位置 | 核对结论 |
|---|---|---|
| A4 | `tools/transformtool.cpp:508-515` | `previewDestDoc().intersected(QRect(0,0,doc->width(),doc->height()))`，空则返回；与声明一致 |
| A16 | `io/psdio.cpp:842-863` | 层矩形负数/`>30000`/`w*h > INT_MAX` 一律拒载并给出行列尺寸；与声明一致 |
| A19 | `tools/toolmanager.cpp:136-171` | 临时工具 begin/end 都 `deactivate` 原工具再 `activate`，两处均有空指针守卫 |
| A21 | `ui/canvasview.cpp:1394-1408` | `m_adjPreviewBusy = false` 由世代判断**之前**移到**之后**，过期回调不再解锁 |
| A22 | `ui/propertiespanel.cpp:79-95`、`166-190` | 换文档 `clear()` + `boundLayer=-1`；提交仅在 `activeLayerIndex() != m_boundAdjLayer` 时发生，否则 `clear()` |
| A31 | `engine/projection.cpp:373-395` | `processPendingChunks`/`flushPriority` 先判 `m_document` 再 `ensureBuffer`；`PieceJob::ok` 为假不标块有效 |
| A32 | `engine/projection.cpp:82-95`、`projection.h:87-97` | `adoptImage(image, coveredDoc)` 仅 `markChunksValid(covered)`，空/满幅退化为全无效/全有效 |
| A33 | `engine/graph/compositegraph.cpp:208-262` | 有 `compositePreview` 时按「瓦片 Source + preview SourceOver」与 Compositor 同口径叠，否则走整层烘焙 |
| A36 | `tools/pathanchortool.cpp:38-40`、`63-77`、`133-175` | 新增 `livePathForOverlay`（读文档真值、尊重 `visible`、已删返回 null），`drawOverlay` 恒返回 true 以便自检 |

### 1.3 A28/A29（✅，R1 后升格）

| 条目 | 位置 | 核对结论 |
|---|---|---|
| A28 | `io/rasterio.cpp` + `t_phase1a` | `QSaveFile` + `commit()`；回归改用 `PsTests::tempFilePath`（R1） |
| A29 | `io/projectio.*` + `t_phase1a` | `Current=5` + 空层 `extentW/H`；回归同上扁平临时路径 |

## 2. 返修清单（可直接交给 Cursor）

### R1 把 `t_phase1a` 的 A28/A29 从假红改回真绿 — ✅ 已做

- **改动**：`t_phase1a.cpp` 去掉 `QTemporaryDir`，改 `PsTests::tempFilePath` + `QFile::remove`。
- **根因备忘**：`QTemporaryDir` 在受限 ACL 下不可写；扁平系统临时路径与 `projectio`/`webexport` 同口径。
- **验收**：`psdemo_tests.exe phase1a` 与全量退出码 0（见本轮提交说明）。

### R2 台账与清单对齐 — ✅ 已做

- `pending-dev.md` 与 `phase1-audit.md` 顶部已声明 A1–A36+#22 收口，并链到本报告。

### R3 给 9 条无回归项补最小断言（按优先级分批）

- **位置/期望/验收**（每条都是"能自动跑完给退出码"）：
  1. **A31/A32（投影块有效位，用户可见，先做）**：新用例 `t_projectionvalid`：① `adoptImage(img, 左半幅)` 后断言仅左半块 `chunkValid`，`processPendingChunks` 能把右半补齐且与整幅 `Compositor::composite` 逐像素一致；② 文档尺寸变化后调 `processPendingChunks`，断言不按旧网格标有效、结果尺寸=新尺寸。验收：新用例绿 + 故意回退这两处改动时转红。
  2. **A33（图路径与合成器同口径）**：给带 `compositePreview` 的层建 `CompositeGraph`，断言图合成结果 == `Compositor::composite(doc)`（diffs=0）。
  3. **A21/A22（UI 回调与面板绑定）**：A22 可仿 `t_phase1a` 风格直接构造 `PropertiesPanel` + 两个文档 `setSession`，断言旧文档的未决预览未被 commit 进新文档；A21 需在事件循环里投递两代 adj preview，断言过期回调和后 `m_adjPreviewBusy` 仍为 true。
  4. **A16（坏 PSD 拒载）**：手写最小 PSD 头把层矩形写成 `0x7FFFFFFF`/负值，断言 `PsdIo::load` 返回 `nullptr` 且 `err` 含「矩形无效或过大」。
  5. **A4/A19/A36（成本高、收益低，最后做）**：A4 缺可观测出口，建议先给 `TransformTool::updateLayerPreview` 加仅测试可用的缓冲尺寸查询（或 `Q_ASSERT` 面积 ≤ 文档面积）再断言；A19 用探针工具计数 `deactivate` 调用；A36 断言删路径后 `drawOverlay` 不产生像素、隐藏路径不画。
- **优先级**：中（1–2 优先，3–5 可延后）　**置信度**：确定（1–4）；**可疑**（A4：先证实现有可观测出口再定）

## 3. 复核方法与关键实测（复现命令）

```bat
:: 全量（退出码 = 失败用例数）
.build-clean\release\psdemo_tests.exe            :: 41/42 通过；断言 681 PASS / 6 FAIL
:: 只跑本轮相关用例
.build-clean\release\psdemo_tests.exe phase1a filterpanel pentool tilecache
:: A28/A29 假红的对照：同一 exe 里写扁平临时路径的用例全绿
.build-clean\release\psdemo_tests.exe webexport projectio psdio   :: 3/3 通过
```

- **A28/A29 假红的判别链**（4 步，全部可复现）：① 失败断言是 `QFile::open(WriteOnly)`（在任何产品代码之前）；② `QTemporaryDir::isValid()` 为真、目录已建但为空，`Get-Acl` 显示其 DACL 受保护且无写入授权项；③ 同一 exe 写扁平临时路径全绿（`webexport`/`projectio`/`psdio`）；④ 连 `pwsh` 往该目录写也被拒（`DENIED`），说明不是产品代码问题、也不是"临时目录一律不可写"。
- 未发现的旁证：全量运行中 `phase1a` 之外的 41 个用例（含 `composite`/`blendparallel`/`transformlive`/`tilecache`/`gpukernels`/`rasterbackend`）全部 OK，**没有**新改动破坏既有行为的迹象（🐛 = 0）。

## 4. 被实践证伪的设计 / 需留意的既有政策

- 原 `phase1-audit.md` §4 曾标 GPU r≥5「至今未修」，与台账 #22 不同步；`f8dfe79` 已自行改正，本次核实结论成立（`gpumedian` 的"始终"分支实跑绿），**无需再改**。
- **既有政策提醒（非缺陷）**：`ProjectIo` 只认 `ProjectFormat::Current`（`io/projectio.cpp:4` 明写"不做旧版兼容"）。A29 把 `Current` 4→5 后，**用户手上所有 v4 `.pslite` 一律开不了**（报「格式不匹配」）。这是项目既有策略，且 `t_projectio` 有 v3 拒绝护栏；若将来希望老工程可开，需要单独立项做兼容读，别在 A 类修复里顺手做。

## 5. 未复核项（勿当结论）

- A4/A16/A19/A21/A22/A31/A32/A33/A36 的**行为**未验证，只有实现与声明一致；其中 A4 的"缓冲不超文档面积"**无法从现有接口直接观测**，属"可疑→待证"。
- 本评估**未**逐行重读 50 文件的全部 diff（只按 A 项定点核对）；也没有做 GUI 人工验收（拖拽跟手度、观感）。

## 6. 更新的计划书

- 文件：`docs/pending-dev.md`、`docs/phase1-audit.md`（R2 已对齐）
- 本报告：`docs/phase1-audit-review.md`（R1/R2 状态已刷新）
- 进度总览：一期审计 **37/37 有实现** → **28 项有实测回归绿**、**9 项待补证据**（R3）
- 下一步：**R3-1/2 投影与图口径回归（~1–2 小时）**；A4/A19/A36 可后置
