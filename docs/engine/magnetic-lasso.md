# 磁性套索：算法与标定

> 本页记录**磁性套索的边缘吸附原理、代价函数的标定，以及一次针对「贴边不稳」的修正**。
> 代码引用给**文件 + 符号**，不给行号（会漂移）。

---

## 1. 两层算法

这套工具的思想有两层，容易混：

| 层 | 做法 | 谁在用 |
|----|------|--------|
| **A. 局部贪心吸附** | 光标附近一个窗口内找最大梯度点 | **PS 磁性套索**（每帧 O(Width²)，无全局搜索） |
| **B. Live-wire 最短路径** | 代价图上求最小代价路径（Dijkstra） | Mortensen & Barrett 论文 / **GIMP 智能剪刀** |

PS 的磁性套索**不做** B：锚点之间就是**直线**，靠"每个锚点都吸到边缘 + 锚点够密"来贴合。
所以它快、跟手，但会跳到附近更强的边上（用户抱怨的"乱吸"）。

本项目是 **A + B 的混合**：吸附用 A（`snapToGridCorner`），**锚点之间的段用 B**
（`walkGridEdge`）——比 PS 更贴边，代价是每帧/每次落锚要跑一次 ROI 内的 Dijkstra。

---

## 2. 本实现的流程

```pseudocode
mousePress(p):
    首击   → m_active = true；落锚 = nearestGridCorner(p)   # 刻意不合成图，保证首击即时反馈
    双击   → 吸附 → 补一段 → commit
    点回起点(10px 热区) → commit
    已活动 → ensureSource() → snapToGridCorner(Width) → rebuildLiveSegment → addFastener

mouseMove(p):
    ensureSource()                          # 首击后第一次移动才做合成（避免首击卡顿）
    trackAlongEdge(p):
        tip = nearestGridCorner(p)
        若 Width ≥ 1: 在 min(2, Width) px 内 snapToGridCorner 微调   # 终点跟光标，不甩到远端强边
        若 tip 格点变化 → rebuildLiveSegment(tip)                   # 见 §4 优化 ③
        累计光标行进距离 ≥ spacing(Frequency) → addFastener(tip)     # 见 §4 优化 ②

mouseRelease → 只结束拖拽，**不闭合**（点选式，与 PolygonalLassoTool 一致）
keyPress    → Enter 闭合 / Esc 取消 / Backspace·Delete 撤上一个锚点
commit      → selectPolygon(m_edgePath + tip)
```

**会话与拖拽是两件事**：`m_active`（会话，首个锚点 → 闭合）与 `m_dragging`（按住左键）
分开。搜索范围圈挂 `m_active`，所以**松手后仍然显示**。

---

## 3. 代价函数与标定

`walkGridEdge` 的边代价（`engine/magneticedgesnap.h`）：

```
stepCost = nodeCost(目标节点) × 步长(1 或 √2)      # f_G 项
         + omegaDir × dirPenalty(目标节点方向, 移动方向)   # f_D 项
```

| 项 | 含义 | 对应 GIMP |
|----|------|-----------|
| `nodeCost` = `255 − normalizeMag(mag) + 5` | 强边便宜 | `grad1 = 255 - grad1`，权重 `OMEGA_G = 0.8` |
| `dirPenalty` = `\|cos(移动方向 − 梯度方向)\| × 255`（**预计算 8×8 表**） | 沿边≈0、横穿≈255 | `direction_value[dir][link]`，权重 `OMEGA_D = 0.2` |
| `omegaDir = 0.5` | 两项相对权重 | 由 GIMP 常量反推，**未经实测标定** |

`normalizeMag` 是把**原始 Sobel 幅值映射到 0..255**：

```
kSobelMax = 4 × 255 = 1020            # Sobel 单轴满量程（核权重和 = 4）
normalizeMag(raw) = min(255, raw × 255 / 1020)
```

**为什么取 1020 而不是理论 hypot 最大值 1442.5**：1442.5 只有理想棋盘格
（sx、sy 同时满量程）才能达到，真实边缘够不着 → 用它会白扔约 30% 量程。
取 1020 后，竖直/水平台阶边（`mag = 4V`）归一化**恰好等于灰阶差 V**：

| 灰阶差 V | 原始 mag | 归一化 | 节点代价 `255−norm+5` | 旧实现（`260−min(mag,255)`） |
|---|---|---|---|---|
| 8 | 32 | 8 | 252（弱边再 +90） | 228（+90） |
| 32 | 128 | 32 | 228 | 132 |
| 64 | 256 | 64 | 196 | **5（开始饱和）** |
| 128 | 512 | 128 | 132 | **5** |
| 255 | 1020 | 255 | **5** | **5** |

即：**修好后整条 0..255 的对比度量程都单调可分**；旧实现从 V≥64（≈25% 对比度）
往上全部塌成同一个代价。

**为什么必须归一化**：GIMP 的梯度图先铺满它自己的量程
（`gimptilehandleriscissors.c`：`gradmap = gradient * 255 / MAX_GRADIENT`，`MAX_GRADIENT ≈ 179.6`），
所以它的 `255 - grad` 有区分度（不过 GIMP 的卷积结果被 int8 夹在 ±127，
可用窗口其实只到 V≈32）。本项目原先直接拿原始幅值再 `min(mag, 255)`，
可用窗口只到 V≈64，再往上就没有区分度了。

> **精度提醒**：`minMag`（弱边门槛，来自 Contrast）仍按**原始幅值**判定，
> 与 `nodeCost` 的归一化口径**故意分开** —— 否则同一个 Contrast 取值会突然苛刻好几倍。
> `snapToGridCorner` 也走原始幅值（只比大小，量程不影响结果）。

### 参数标定

| PS 参数 | 选项栏默认 | 内部量 | 换算 |
|---|---|---|---|
| 宽度 Width 1–256 | 10 | `m_searchRadius` | 原值（圆半径） |
| 对比度 Contrast 1–100 | 40 | `m_minEdge` | `10 + contrast` ∈ [11,110]（**原始幅值量程**） |
| 频率 Frequency 1–100 | 57 | `m_anchorSpacing` | `clamp(40 − 0.36×f, 4, 40)` px，57 → 约 19px |

---

## 4. 本次修正（针对「贴边不稳 / 横跳」）

| # | 问题 | 修法 |
|---|------|------|
| ① | **代价图在强边上饱和**：`min(mag,255)` 让所有 ≥25% 对比度的边缘代价相同 → Dijkstra 只能靠 tie-break 选路 | `nodeCost` 里先 `normalizeMag` 再算代价（对照 GIMP 的先归一化） |
| ② | **Frequency 与鼠标速度耦合**：旧实现数的是「tip 格点变化次数」，划得快落锚稀、慢则密 | 改成累计**光标行进距离** ≥ `m_anchorSpacing` 才落锚 |
| ③ | **同一格点重复跑 Dijkstra**：`tip` 没变也重算 livewire（鼠标在同一像素内抖动就会触发） | 只在 `tip` 格点真的变化时才 `rebuildLiveSegment` |
| ④ | **路径的弱边门槛是 `m_minEdge × 0.35`**：系数无依据，且让路径比吸附松 3 倍（同一 Contrast 两套口径） | 改为直接用 `m_minEdge`：吸附不接受其上的边，路径上也额外罚 |

> **热路径纪律**：方向项若在邻居循环里现场算 `atan2`/`cos`，最坏情形是
> 约 3.7 万格点 × 8 邻居 ≈ **30 万次三角函数/段**，会明显拖慢拖拽。
> 因此 `linkAngle` 与 `dirPenalty` 都做成查表（后者建表只算一次 64 个 `cos`）。
> 这一条属于「先按复杂度估算、不做现场 trig」，**实际耗时仍需实测**（见 §6）。

顺带修正：`fitsFromFrequency` 的注释曾写「f=57 → 约 8 步」，实际公式给出 13 步；
改成距离口径后此注释已被替换。

**未做的项**（有意保留）：

- **f_Z 拉普拉斯零交叉项**：GIMP 也没有，不做。
- **Width 在"移动跟踪"时被压到 `min(2, Width)`**：这是刻意的 —— 若把tip 甩到圆内远端强边，
  橡皮筋会跑到光标前面，手感很差。代价是选项栏的"宽度"在跟踪时几乎不改变吸附范围
  （只在**单击强制落锚**时用满 Width），它主要影响搜索圆的视觉大小。

---

## 5. 与 GIMP 常量对照

| 本项目 | GIMP（`gimpiscissorstool.c` / `gimptilehandleriscissors.c`） |
|---|---|
| `kSobelMax = 4×255 = 1020`（单轴满量程，真实边缘用满 0..255） | `MAX_GRADIENT = sqrt(127²+127²) ≈ 179.6`（但卷积被 int8 夹在 ±127 → 可用窗口只到 V≈32） |
| `kDirBins = 8`（[0,π) 量化） | 方向字节 0..254 对应 -π/2..π/2，`255` = 无方向 |
| `dirPenalty` 按 `\|cos\|` **预计算成 8×8 表**（热路径禁 trig） | `direction_value[i][0..3] = (127-\|127-i\|)*2` 等 8×4 查表 |
| `omegaDir = 0.5` | `OMEGA_D = 0.2`、`OMEGA_G = 0.8` |
| `kMaxSide = 160`（ROI 上限） | 包围盒 `+ EXTEND_BY(0.2) + FIXED(5)`，无硬上限 |
| 方向项取**目标节点** | 取 link **两端节点**求平均 |
| 椭圆约束（不许绕到光标前方） | 无（GIMP 是点击式，逐顶点求路径） |

---

## 6. 怎么验证（可复现数字）

`engine/magneticedgesnap.h` 全是 inline 纯函数、无副作用，可以独立驱动。**一次性**验证程序：

```cpp
// 1) 饱和验证：合成一张有真实边缘的图，统计归一化前后的代价分布
QImage img = /* 构造或读入 */;
int saturated = 0, total = 0;
for (y...) for (x...) {
    const qreal raw = Ps::MagneticEdgeSnap::detail::cornerStrength(img, x, y);
    if (raw > 0.01) { ++total; if (raw >= 255.0) ++saturated; }   // 旧式会全部压平
}
qInfo() << "边缘像素中 raw>=255 的占比:" << double(saturated) / total;   // 预期：很高 → 证明饱和
// 修好后同样的像素在 nodeCost 里应给出不同的代价，可用 map 统计不同取值的个数

// 2) 贴边验证：固定起终点，比较修正前后路径与"真实边缘线"的偏离
const auto path = Ps::MagneticEdgeSnap::walkGridEdge(img, A, B, minMag);
// 统计 path 中落在局部 Sobel 最大线上的像素比例（偏离率），前后各取一个数字
```

`no-latent-bugs.mdc` §9 要求的是**可复现数字**，所以上面两处必须打出具体比例，
不能只写"看起来更贴边"。**用完即删**，不要入库。
