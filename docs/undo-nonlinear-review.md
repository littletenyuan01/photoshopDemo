# 非线性撤销（历史面板 + 跳转 + 书签）实施评估报告

> 评估日期：2026-10-15 ｜ 评估基准：`a31be4a` → **`24ceea7`**（T0–T6）→ R1 收口（同日）
> 被测二进制：`.build-clean/release/psdemo_tests.exe`（2026-10-15）
> 计划书：[undo-nonlinear-plan.md](undo-nonlinear-plan.md) ｜ 方法：轻量核对（git diff + 关键实现逐处读 + 回归实跑 + 大图多采样）｜ 推理档位：中
> 相关：[pending-dev.md](pending-dev.md)、[undo-maskcost-plan.md](undo-maskcost-plan.md)（O4 派单）、[tests.md](tests.md)、[undo-region-review.md](undo-region-review.md)

## 0. 结论摘要

| 判定 | 数量 | 条目 |
|---|---|---|
| ✅ 真做完（实现 + 实测证据齐） | **7** | T0–T6（含 R1 大图多采样） |
| ⚠️ 证据口径待补 | **0** | — |
| ❌ 没做 | 0 | — |
| ➕ 超范围 | 0 | 改动均在计划内 |
| 🐛 引入缺陷 | 0 | 本计划用例全绿；`tilecache` P13 偶发属既有抖动（R2 / O5） |

**一句话结论**：七个任务**全部按计划交付且已评估收口**。大图跳转 **5–40 ms** / 带蒙版 **10–20 ms**（多采样区间，含评估方复跑上限），远低于 100 ms。优化台账 O1–O5 已落 `pending-dev`；**O4 已派单** → [undo-maskcost-plan.md](undo-maskcost-plan.md)。§10 遗留 D1–D3 均已处置（见下）。

**返修收口**：
- **R1**：2048² 对照 + 耗时区间约定（§9.1）
- **R2 / O5**：P13 记档、不改阈值
- **O4**：另立 [undo-maskcost-plan.md](undo-maskcost-plan.md)（T1–T4；其中 T3/D2 已预修）

**建议下一步**：空脏区仍压条目（undo-region R7）另议。**O4 已落地**（见 [undo-maskcost-plan.md](undo-maskcost-plan.md) §0）。

## 1. 逐任务核对

| 任务 | 判定 | 证据 |
|---|---|---|
| **T0** 记账按触碰瓦片 | ✅ | 满幅一笔 **16 777 280 → 16 512**；100 笔 `historyTrimmed=0`（收口 0+1 档 R6） |
| **T1** `ChangeBatch` | ✅ | flush 顺序固定；`undobatch` 批量内外计数契约绿 |
| **T2** 跳转 API | ✅ | `jumpToStep` + 随机跳 `diffCount==0`；每次 `contentChanged==1` |
| **T3** 蒙版引用快照 | ✅ | `swapLayerMask` 两行浅拷贝；`captureLayerProps` 仍 `.copy()`（不扩散 → O4） |
| **T4** 历史面板 | ✅ | 行数 / 点行 / trim 重建 |
| **T5** 命名书签 | ✅ | trim 重基 / 清 redo / 上限 64 |
| **T6** 回归与实测 | ✅ | 小图+大图；区间采样；文档与 O 台账齐 |

**红线**：`io/*` 零改动；无层稳定 id；无分支树/历史画笔/真快照。

## 2. 返修清单

### R1 补大图跳转耗时 — ✅ 已修

| 场景 | 耗时（区间） | historyBytes 峰值 |
|---|---|---|
| 256²×200，Δ1/10/100/200 | 0 / 0 / 0 / 0 ms | 4 531 200 |
| **2048²×200，200→0** | **5–40 ms**（≥5 次 + 评估方 37 ms） | **7 398 400** |
| 带蒙版 512²×200 | **1 ms** | — |
| **带蒙版 2048²×200** | **10–20 ms**（≥3 次 + 评估方 15 ms） | — |

WaitCursor 阈值不必下调；O2 **不做**。

### R2 `tilecache` P13 — ✅ 已记档（不改阈值）

= pending-dev **O5**。

## 3. 超范围改动

无。

## 4. 值得记档的实现细节

- T0 宁可高估；跳转中 `isApplying` 不记账；跳转结束统一 trim。
- 耗时写**区间 + 采样数**（§9.1）。
- 属性蒙版深拷贝是 T3「不扩散」留下的外账 → **O4**。

## 5. 未复核项（勿当结论）

- GUI 手工验收（计划 §6）未做。
- 真实磁盘换页与跳转组合未跑。
- `t_undobookmark` 未逐行审构造边界。

## 6. 更新的计划书与文档

| 文件 | 状态 |
|---|---|
| `undo-nonlinear-plan.md` §0 | T0–T6 ✅ |
| `pending-dev.md` | 区间采样；O1–O5；**O4 已派单** |
| `undo-maskcost-plan.md` | O4 实施契约（新建） |
| `tests.md` / `architecture` / `tech-notes` | 已同步 |
| 本报告 | 7/7 收口；§10 D1–D3 已处置 |

**进度总览**：2 档 **7/7 已收口**；收尾小项 O4 **已派单未实施**（T3/D2 预修除外）。

## 7. 体积构成与「更省的替代」

约 **+2108 行 / 25 文件**。产品 ~43% / 测试 ~35% / 文档 ~21%。ChangeBatch 精确路线偏重 → **O1 暂不做**。不建议回退；下次计划给「最小/完整」两栏点选。

## 8. 与 0+1 档的衔接

| 0+1 档项 | 本档处置 |
|---|---|
| R6 满幅记账吃深度 | **T0** 落地 |
| 「需要层稳定 id」 | **证伪**，不做 |
| 蒙版整张深拷贝 | **O4 已落地** → [undo-maskcost-plan.md](undo-maskcost-plan.md) |
| 空脏区仍压条目 | undo-region R7（仍欠） |

## 9. 优化项落档与耗时口径

| # | 一句话 | 建议 |
|---|---|---|
| O1 | ChangeBatch 换 Blocker | **暂不做** |
| O2 | `changedTilesDocRect` → O(触碰) | **不做** |
| O3 | `byteSize` 每 dirty 重算 | **不做** |
| O4 | 蒙版外账 | **已落地** [undo-maskcost-plan.md](undo-maskcost-plan.md)（100 次显隐增量 28400） |
| O5 | P13 抖动 | **已记档、不改阈值** |

### 9.1 耗时单次值不可复现 → 工程约定

耗时类实测必须写**区间 + 采样数**。`pending-dev` ①b = **5–40 ms**；④b = **10–20 ms**（已含评估方复跑上限）。

## 10. 增量复核（R1 收口）与遗留处置

**判定：R1 通过**。评估方实跑曾见 37 ms / 15 ms；全量 52/52 绿。

| # | 现象 | 处置 |
|---|---|---|
| **D1** | 区间上限偏窄（记录 5–34 / 10–12，复跑 37 / 15） | ✅ 已放宽为 **5–40 ms** / **10–20 ms**（`pending-dev`） |
| **D2** | `t_undomaskcow` 计时段 `markDirty(0,0,64,64)` ≠ 真实写域 | ✅ **已预修**（`stampMaskDab` 返回值 + `translated(offset)`）；亦为 [undo-maskcost-plan.md](undo-maskcost-plan.md) **T3** |
| **D3** | `.build-clean/` 未 ignore | ✅ 已在 `photoshopDemo/.gitignore`（与 `.build-tests/` 同类） |

**流程**：R1 / 评估文档 / O4 任务书若尚未提交，建议分提交固化（`test:` 大图实测与 D2；`docs:` 评估 + pending-dev + maskcost 计划），避免工作区丢失。

## 11. O4 复核与撤销功能收口判定（2026-10-15，基准 `84d9ad7`）

**判定：O4 通过；撤销功能可以收口。**

评估方实跑（`.build-clean` 20:40 构建）：全量 **53/53 通过、退出码 0**（用例表已 53 条）。`undomaskprop` 单跑数字与 `pending-dev.md` 记录**逐项一致**：

| O4 验收项 | 结果 |
|---|---|
| T1 属性条目与图层蒙版共享 | `cacheKey` 相同 + undo 后蒙版逐字节相同 ✓ |
| T2① 2048² 连改 100 次显隐 | `undoBytes` 增量 **28 400**、`historyTrimmed` **0**（改造前 ≈100×4 MB） ✓ |
| T2② detach 后分档 | 共享 **284** / detach **4 194 588** ✓ |
| T2③ **防低报下界** | 50 笔蒙版增量 **211 212 544** ≥ 下界 157 286 400（=50×4 MB×0.75） ✓ |
| 实现方式 | 捕获浅拷贝 + `mask->image() = snap` 恢复 + `markDirty` 钩子按 `cacheKey` 翻转（`imagedocument.cpp:1341-1350`）+ `pop` 后重算共享 ✓ |

**收口时发现的 1 处残留（不阻塞收口，已记 `pending-dev` **O6**）**：O4 只覆盖**属性**路径，**像素**路径仍深拷贝整张蒙版——`pushLayerPixelsUndo` 的捕获（`imagedocument.cpp:246`）仍是 `.copy()`，且 `LayerPixelsUndo::byteSize()` 无条件按整张计。后果：**"层 + 蒙版 + 画像素"（最常见的蒙版用法）每笔仍 4 MB**，256 MB 上限下带蒙版文档的画笔深度仍只有 ~60 步。同模式另两处：`applyLayerMask` 捕获（`:1162`）、`removeLayerMask` 后钩子因 `hasMask==false` 跳过而**低报**（`:1344-1349`）。⚠️ 这是**出 O4 任务书时的枚举漏项（评估方责任）**，任务书 §2/§3 只写了属性路径，不是实施方偏差。

**撤销功能最终形态（收口清单）**：

| 能力 | 状态 |
|---|---|
| 线性双栈 + 交换语义 + 256 MB 上限（undo/redo 合计记账） | 既有，保持 |
| 像素粒度：**瓦片引用快照（QImage COW）** + 按"触碰瓦片"记账 | 已落地 |
| 出栈标脏：按 `cacheKey` 差分只标变化瓦片 + 样式 pad | 已落地 |
| 蒙版（属性类）：引用快照 + "是否仍共享"分档记账 | 已落地（O4） |
| 非线性：历史面板 + 任意跳转（重复 pop/redo + `ChangeBatch` 一次重投影）+ 命名书签 | 已落地（2 档） |
| **明确不做**：分支树 / 历史画笔 / 真·状态快照克隆 / 层稳定 id / O1·O2·O3 | 已记档 |
| **已知遗留**：O6（像素路径蒙版成本 + 删蒙版低报）、O5（P13 抖动）、空脏区仍压条目（undo-region R7）、蒙版笔画每笔一张（快照语义，不可省） | 均在 `pending-dev` |

**收口数字（可对外引用）**：满幅 2048² 一笔 `undoBytes` **16 777 280 → 16 512**；同深度历史峰值 **≈252 MB（触上限被 trim）→ 17.9 MB（全留）**；撤销后重投影 **整幅 2048×2048 → 64×64**；2048²×200 步跳转 **5–40 ms**（带蒙版 10–20 ms）；带蒙版改 100 次显隐 **28 400 B**。

**收口后建议**：① 把工作区里两份无关改动（`docs/undo-region-review.md`、`docs/学习路径/README.md`）提交或回退，保持工作区干净；② O6 可作为"下一件小事"（≈0.2 批次）或就此挂账；③ 撤销主题三个计划文件（`undo-region-plan` / `undo-nonlinear-plan` / `undo-maskcost-plan`）均标"已完成"，不再扩范围。
