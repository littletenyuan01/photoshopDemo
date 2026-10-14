# 一期审计修复实施评估报告

> 评估日期：2026-10-14 ｜ 初评基准：`56954b0`→`f8dfe79`；R3 收口至含 `projectionvalid` 的 HEAD
> 计划书：[phase1-audit.md](phase1-audit.md)（A1–A36 + GPU 台账 #22）｜ 方法：轻量核对 + 全量回归实跑
> 相关：[pending-dev.md](pending-dev.md)、[tests.md](tests.md)、[tech-notes.md](tech-notes.md)

## 0. 结论摘要

| 判定 | 数量 | 条目 |
|---|---|---|
| ✅ 真做完（代码 + 回归实跑绿） | **33** | A1–A3、A5–A18、A20、A23–A36；GPU #22 |
| ⚠️ 代码核对通过、**缺行为证据** | **4** | A4、A19、A21、A22（成本高 / UI 事件环，可后置） |
| ❌ 没做 | 0 | — |
| 🐛 引入缺陷 | 0 | 全量 **43/43** 通过（含新用例 `projectionvalid`） |

**一句话结论**：37 项全部有实现；**R1–R3 优先项已落地**后 33 项有实测绿。R3 顺带发现并修了 `processPendingChunks` 在 `ensureBuffer` 重建后误 `return {}` 的逻辑洞。

**建议下一步**：可选补 A4/A19/A21/A22；**不建议**回头重审已绿项的实现。

## 1. 有回归实跑证据（33 项，✅）

| 条目 | 证据用例 |
|---|---|
| A1–A3、A5–A9、A11–A15、A17、A20、A23–A30、A34、A35 | `phase1a` |
| A10 | `filterpanel` |
| A16 | `psdio`（改写层矩形 → 拒载） |
| A18 | `pentool` |
| A28、A29 | `phase1a`（R1：`tempFilePath`） |
| A31、A32、A33、A36 | `projectionvalid` |
| GPU #22 | `gpumedian` |

## 2. 返修清单

### R1 / R2 — ✅ 已做

见既有说明（A28/A29 脚手架；台账与清单对齐）。

### R3 — ✅ 优先项已做（余 4 项可后置）

| 项 | 状态 | 说明 |
|---|---|---|
| A31/A32 | ✅ | `t_projectionvalid`；`invalidChunkCount()` 公开；**顺带修** `processPendingChunks`：`ensureBuffer` 重建后继续合成（勿因返回 false 直接 return） |
| A33 | ✅ | 同用例：`compositePreview` 时 `compositeViaGraph == composite` |
| A16 | ✅ | `t_psdio` 改写合法 PSD 层矩形为 `0x7FFFFFFF` |
| A36 | ✅ | 同 `projectionvalid`：隐藏/删除后 overlay 零像素 |
| A21/A22 | ⏳ | UI 事件环，后置 |
| A4/A19 | ⏳ | 缺可观测出口 / 探针成本高，后置 |

## 3. 复现命令

```bat
.build-clean\release\psdemo_tests.exe                         :: 43/43
.build-clean\release\psdemo_tests.exe projectionvalid psdio phase1a
```

## 4. 政策提醒（非缺陷）

- `ProjectIo` 只认 `ProjectFormat::Current`（现为 5）；v4 `.pslite` 开不了是既有策略。
- GPU #22 走 CPU 回退（T5.1），非扩 `chR[81]`。

## 5. 未复核项（勿当结论）

- A4 / A19 / A21 / A22 仅代码核对，无行为回归。
- 未做 GUI 人工验收（拖拽跟手度、观感）。

## 6. 进度总览

- **37/37 有实现** → **33 项实测绿** → 余 **4 项**可后置（A4/A19/A21/A22）
- 用例数：42 → **43**（+`projectionvalid`）
