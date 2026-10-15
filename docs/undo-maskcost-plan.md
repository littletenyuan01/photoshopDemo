# 蒙版撤销成本外账（O4）修复任务书

> 状态：待实施（T3/D2 已在评估收口时预修）
> 更新：2026-10-15 ｜ 本文件是交给实施方（Cursor）的唯一契约，不依赖任何对话上下文。
> 来源：[pending-dev.md](pending-dev.md) 优化项 **O4**、[undo-nonlinear-review.md](undo-nonlinear-review.md) §7/§9/§10
> 定位：**撤销改造的收尾小项**（≈0.3 批次），只改「怎么存 / 怎么算」，**不改任何交互、不改文件格式**。

## 0. 进度总览

| 阶段 | 状态 | 关键数字 / 证据 |
|---|---|---|
| T1 属性类蒙版改引用（capture + restore 两侧） | ⬜ 未开始 | `undomaskprop`：共享蒙版被属性撤销引用后 `cacheKey` 相同 |
| T2 蒙版按「是否仍共享」记账 | ⬜ 未开始 | 2048² 带蒙版连改 100 次显隐 → `historyTrimmed` **0**，`undoBytes` 增量 < 1 MB；detach 后下界护栏绿 |
| T3 修 D2：`t_undomaskcow` 的 `markDirty` 用真实写域 | ✅ 2026-10-15 | 评估收口预修：`stampMaskDab` 返回值 + `translated(offset)`；`undomaskcow` 仍绿 |
| T4 回归 | ⬜ 未开始 | 全量 `psdemo_tests.exe` 退出码 0；数字写入 `pending-dev` O4 |

## 1. 目标与边界

**目标（一句话）**：让**没碰蒙版的操作**不再独占一整张蒙版内存，并让属性类蒙版的账面与「是否仍共享」一致。

**完成标准（可验收的现象）**：

1. 2048² 带蒙版图层上**连续改 100 次显隐/不透明度/改名**：`historyTrimmed` 发射次数 **0**，`historyBytes()` / `undoBytes` 增量 **< 1 MB**（不是 ≈ 100 × 4 MB）。
2. **两类场景分开看**（勿混）：
   - **属性类**（本项主收益）：浅拷贝后多条 `LayerPropUndo` **共享同一张**蒙版缓冲，物理增量 ≈ 0；`m_maskShared==true` 时账面不计整张。
   - **蒙版笔画**（走 `LayerPixelsUndo`，本项不改其快照语义）：每笔仍需自己那一版蒙版，**物理上不可省**；只要求账面不低报（见 T2 下界）。
3. 全量用例退出码 0；`phase1a`（A2/A23）、`undomaskcow`、`undoregion`、`undojump` / `undopanel` / `undobookmark` 保持绿。
4. `.pslite` / PSD 文件格式**不变**（`t_projectio`、`t_psdio` 保持绿）。

**不做什么（Out of scope）**：

- **不做**蒙版差分、压缩、按区域存储（快照语义不变）。
- **不做** [pending-dev.md](pending-dev.md) O1 / O2 / O3（均已判定不做）。
- 不动 A2/A23 语义；不改 `LayerPropSnapshot` 字段集合（只改 `maskGray` **持有方式**）。
- 不引入图层稳定 id；不改 `LayerPixelsUndo` 的蒙版交换语义（T3 非线性已浅拷贝，本项勿再动 `swapLayerMask`）。

## 2. 现状事实

> 每条都在代码里查过，带 `文件:行`（2026-10-15）。

- 属性快照**每个**条目深拷贝整张蒙版：`s.maskGray = layer.mask()->image().copy()`（`app/undoitem.cpp:108`）。
- `LayerPropUndo::byteSize()` 无条件计入 `m_snap.maskGray.sizeInBytes()`（`app/undoitem.cpp:332-338`）。
- 恢复侧深拷贝：`mask->setFromImage(s.maskGray)` → 内部 `m_gray = gray.copy()`（`undoitem.cpp:127`；`domain/layermask.cpp` `setFromImage`）。
- 可用的浅装入口：`LayerMask::image()` 非 const 清 `isFullyOpaque`（`domain/layermask.h`）——像素路径 T3 已在 `swapLayerMask` 用同法（`undoitem.cpp:74`、`:78`）。
- 蒙版写路径会 detach：`stampMaskDab`→`scanLine`、`LayerMask::fill`→`QImage::fill`、`expand` 整对象替换。
- 像素侧可对齐钩子：`markDirty` 已有 T0 TileRefs 记账（`imagedocument.cpp`）；`HistoryStack::refreshTopBytes` / `topUndoItem`。
- 量级：2048² Grayscale8 蒙版 = **4 194 304 B**；同尺寸像素一笔 T0 后按触碰计 **16 512 B**。
- **默认历史上限 256 MB**：蒙版笔画若按整张计，约 60 笔即触 trim——T2 下界测试**必须临时抬高 `byteLimit`**，否则「≥ 200×4MB」永不成立（与上限冲突，勿写死）。

## 3. 方案（唯一答案）

1. **持有方式改引用**：`captureLayerProps` 去 `.copy()`；`applyLayerProps` 恢复用 `mask->image() = snap.maskGray`（不用 `setFromImage`）。属性类条目在「蒙版未被写」时共享缓冲 ⇒ 物理增量 0。
2. **记账按「是否仍共享」**：`LayerPropUndo` 增加 `bool m_maskShared = true`（capture 时共享）。
   - `markDirty` 在 T0 钩子旁再加：若栈顶是 `LayerPropUndo`，且条目蒙版与图层蒙版 `cacheKey` 不同 → `noteMaskDetached()` + `refreshTopBytes()`。
   - `byteSize()`：`m_maskShared` 为真**不计** `maskGray`；为假按整张计。
   - 为何不给 `byteSize()` 加 document：接口是 `const` 且无 doc；`markDirty` 已有文档上下文且蒙版写入路径必调。
3. **T3 / D2**（已预修）：`t_undomaskcow` 计时段用 `stampMaskDab` 返回值换算文档矩形。

**不采用**：给 `byteSize()` 加 doc 参数（波及 7 个条目类）；在 `LayerMask::image()` 非 const 打「已写」标记（非撤销路径也会写）；蒙版瓦片化（超出本项）。

## 4. 任务分解

### T1 属性类蒙版改引用（capture + restore）

- **改动的文件**：`psDemo/app/undoitem.cpp`、`psDemo/app/undoitem.h`（若需）、`psDemo/tests/t_undomaskprop.cpp`（新增）、`psDemo/tests.pro`、`psDemo/tests/testmain.cpp`
- **依赖**：无
- **规模**：约 4 文件 / **+70 行**（产品 ≈ 4 行）
- **要点**：`captureLayerProps` 去掉 `.copy()`；`applyLayerProps` 用 `mask->image() = snap.maskGray`；`hasMask/maskEnabled/maskLinked` / `setMask(nullptr)`（A23）不变。
- **风险点**：恢复仍走 `setFromImage` ⇒ 假绿；写路径不走 `image()` 非 const ⇒ 撤销后蒙版错（`undomaskcow` 已钉写路径）。
- **验收**：
  ```text
  <build-dir>\release\psdemo_tests.exe undomaskprop phase1a
  ```
  期望：带蒙版推一条属性撤销后，条目与图层蒙版 `cacheKey()` **相同**；改显隐 → undo → 蒙版 `diffCount==0`；A2/A23 绿。

### T2 蒙版按「是否仍共享」记账

- **改动的文件**：`psDemo/app/undoitem.h`、`psDemo/app/undoitem.cpp`、`psDemo/domain/imagedocument.cpp`、`psDemo/tests/t_undomaskprop.cpp`
- **依赖**：T1
- **规模**：约 4 文件 / **+60 行**
- **要点**：见 §3.2 与 §5；**只准高估不准低估**。
- **风险点**：cacheKey 条件写反 → 账面低报 → 256 MB 上限失效。
- **验收**：
  ```text
  <build-dir>\release\psdemo_tests.exe undomaskprop undoregion phase1a
  ```
  期望（三条都要）：
  1. 2048² 带蒙版连改 **100** 次显隐 → `historyTrimmed==0`，`undoBytes` 增量 **< 1 MB**
  2. **LayerPropUndo detach 下界**：推一条属性撤销后对蒙版 `stampMaskDab` + `markDirty` → 栈顶 `byteSize()` ≥ `4 194 304 × 0.75`
  3. **LayerPixelsUndo 蒙版账面护栏**（防误伤像素路径）：临时 `setByteLimit(2ull<<30)`，蒙版上画 **50** 笔（`pushLayerPixelsUndo`）→ `undoBytes` ≥ `50 × 4 194 304 × 0.75`（勿用 200 笔×默认 256MB——会 trim，断言无意义）

### T3 修 D2：用例 `markDirty` 用真实写域 — ✅ 已预修

- **改动的文件**：`psDemo/tests/t_undomaskcow.cpp`（512² 与 2048² 计时段）
- **要点**：`const QRect loc = stampMaskDab(...); markDirty(loc.translated(layer->offsetX(), layer->offsetY()));`
- **验收**：`undomaskcow` 仍绿。实施本计划时若代码已是上述写法，**跳过改动**，§0 保持 ✅ 即可。

### T4 回归与记录

- **改动的文件**：`docs/pending-dev.md`（O4 → **已落地** + 两数字）、本文件 §0、`docs/undo-nonlinear-review.md`（§10 D2 保持已修；O4 链到落地）
- **依赖**：T1–T2（T3 已预修）
- **验收**：全量退出码 0；写入「100 次显隐增量」与「detach / 50 笔下界」数字。

## 5. 精确实施规格

```cpp
// 文件：psDemo/app/undoitem.h（LayerPropUndo）
public:
    int layerIndexForMaskProbe() const { return m_layerIndex; }
    bool maskSharedWith(const QImage &liveMask) const
    {
        return m_maskShared && m_snap.hasMask && !m_snap.maskGray.isNull()
            && m_snap.maskGray.cacheKey() == liveMask.cacheKey();
    }
    void noteMaskDetached(); // 幂等：m_maskShared = false

private:
    bool m_maskShared = true; // capture 时共享；detach 后 false
```

```cpp
// 文件：psDemo/app/undoitem.cpp
quint64 LayerPropUndo::byteSize() const
{
    quint64 n = sizeof(*this) + quint64(m_snap.name.size()) * 2 + 64;
    n += quint64(m_snap.styles.size()) * sizeof(LayerStyleEffect);
    n += quint64(m_snap.filters.size()) * sizeof(FilterNode);
    if (!m_maskShared)
        n += quint64(m_snap.maskGray.sizeInBytes());
    return n;
}
// captureLayerProps：s.maskGray = layer.mask()->image();   // 去 .copy()
// applyLayerProps ：mask->image() = s.maskGray;             // 不用 setFromImage
```

```cpp
// 文件：psDemo/domain/imagedocument.cpp（markDirty，紧接既有 T0 钩子）
    if (shouldRecordHistory() && m_history) {
        if (auto *prop = dynamic_cast<LayerPropUndo *>(m_history->topUndoItem())) {
            const int i = prop->layerIndexForMaskProbe();
            if (Layer *layer = m_layers.layerAt(i);
                layer && layer->hasMask() && layer->mask()
                && !prop->maskSharedWith(layer->mask()->image())) {
                prop->noteMaskDetached();
                m_history->refreshTopBytes();
            }
        }
    }
```

**接口契约**：

- `noteMaskDetached()` 只置位（幂等）；置位后必须 `refreshTopBytes()`。
- 浅拷贝后条目与图层共享同一 `QImage`；任一侧写入 detach（与像素侧同构）。
- `snap.hasMask == false` 仍 `setMask(nullptr)`（A23）。
- 蒙版 = 层局部 Grayscale8；`markDirty` = 文档坐标。

## 6. 验收口径

- **功能**：① 100 次显隐不 trim 且增量 < 1 MB；② LayerPropUndo detach 后 `byteSize` 下界；③ 抬高上限后 50 笔蒙版 `undoBytes` 下界；④ 撤销后蒙版逐字节等于快照。
- **回归**：`phase1a` A2/A23、`undomaskcow`、`undoregion`/`undojump`/`undopanel`/`undobookmark`、`projectio`/`psdio`。
- **命令**：
  ```bat
  <build-dir>\release\psdemo_tests.exe undomaskprop undomaskcow phase1a
  <build-dir>\release\psdemo_tests.exe
  ```
- **手工**：2048 文档 → 加蒙版 → 连点显隐 30 次 → `PSLITE_MEM=1` 无「每次 +4 MB」阶梯 → Ctrl+Z 蒙版正确。

## 7. 风险与回退

| # | 风险 | 现象 | 回退办法 |
|---|---|---|---|
| R1 | 恢复仍走 `setFromImage` | 收益减半，T1 假绿 | 断言条目与图层 `cacheKey` 相同 |
| R2 | 写路径不走 `image()` 非 const | 撤销后蒙版错 | `undomaskcow` + `undomaskprop`；不成立整项回退 `.copy()` |
| R3 | cacheKey 写反、账面低报 | 上限失效 | T2 下界断言 + `PSLITE_MEM` |
| R4 | `dynamic_cast` 热路径 | 每 `markDirty` 一次 RTTI | 与 T0 钩子同级；必要时改 `type()` 枚举 |
| R5 | 下界用默认 256MB + 200 笔 | trim 后断言永远失败 | **已在 §4 T2 改为抬高上限 + 50 笔** |

**整体回退**：恢复 `capture` 的 `.copy()`、`apply` 的 `setFromImage`、`byteSize` 无条件计入即可。

## 8. 实施方记录要求

- 每任务在本文件 §0 登记：命令 + 关键输出 + 实测数字。
- `pending-dev.md`：O4 改「**已落地**」并附数字；「仍欠」去掉 O4 派单表述。
- `undo-nonlinear-review.md`：O4 从「已派单」改为「已落地」（T4 时）。
- 提交：`fix:`/`test:`/`docs:` + 中文；**只 add 本任务文件**。
- 与代码冲突时停下告知，勿扩大改动面（尤其勿动 O1/O2/O3、`swapLayerMask`）。

## 附：给实施方的执行提示词

```text
请阅读 photoshopDemo/docs/undo-maskcost-plan.md，按 T1→T2→T4 实施（T3/D2 已预修，先确认 undomaskcow 里 markDirty 已用 stampMaskDab 返回值；若是则跳过）。先写 undomaskprop 用例再改产品代码；T1 必须断言「条目与图层蒙版 cacheKey 相同」。T2 必须同时给出：① 100 次显隐不 trim 且增量 <1MB；② LayerPropUndo detach 后 byteSize 下界；③ 临时 setByteLimit(2GB) 后蒙版 50 笔 undoBytes 下界（勿用默认 256MB×200 笔）。不要做 O1/O2/O3；遇到冲突停下告知我。
```
