# GPU 加速开发计划（**可选加速后端**）—— 可执行规格

> 关联：[filter-roadmap.md](filter-roadmap.md)、[engine/operators.md](engine/operators.md)、
> [engine/roi.md](engine/roi.md)、[graph-engine.md](graph-engine.md)、[dev-plan.md](dev-plan.md)、[tests.md](tests.md)。
>
> **本文用途**：交给执行方（Cursor）按 §10 的清单施工。§11 是歧义时的默认选择表，
> §12 是需要人拍板的问题（仅 Q4/Q5 挡 P1），§14 是缺口关闭记录（含复审硬伤）。
> **执行 P0 不需要任何 GPU 知识。**

---

## 0. 一句话与非目标

把 GPU 做成**可选的加速后端**：**有 GPU 就用它加速，没有就原样走 CPU**。
**不是**把 CPU 实现换成 GPU 实现 —— CPU 路径永远是基线、永远是降级路径、永远可跑。

**非目标**：瓦片/投影/合成上 GPU；改变 `.pslite` 格式或文档语义；把点算子单独送上 GPU；追求 bit-exact。

### 0.1 ⚠️ 当前状态（先读这一行）

> **P1（中值）+ P1′（自定 3×3）已入库**（`1fe8dc3`）。**默认策略仍是 opt-in**：`Auto` = CPU。
>
> | 样本（标准测法，T4.3 后） | 端到端 | 一致性 |
> |---|---|---|
> | 中值 r=3 | **≈2.1～2.3×** | bit-exact |
> | 自定锐化 / 稠密盒 | **≈4.4～7.0×** / **≈2.8～4.4×** | bit-exact |
>
> **判定**：轻核已跨 3×；重核仍 1–3×。维持 **opt-in**（默认 Auto=CPU）；不整开 P2。Q8/Q10–Q15 已关。
>
> ### ★ 当前优先（T4）
>
> 1. ✅ T4.1 入库 · ✅ T4.2 分段 · ✅ **T4.3** 解/预乘进 shader（§16.6）。
> 2. 随后：T4.4 独显 / T4.5 成本表 187 / T3.0 AddNoise（T4.7 已落地）。
>
> 瓦片 / 投影 / 合成上 GPU = **非目标**（§0）；见 §16。

---

## 1. 硬约束（所有设计都不许违反）

| # | 约束 | 为什么 | 怎么被验证 |
|---|---|---|---|
| 1 | **CPU 路径永远存在且完整** | 它是**正确性基准**（所有 oracle 用例的参照物）+ 降级路径 | 强制 CPU 下全部既有用例绿 |
| 2 | **启动不许因 GPU 失败而失败** | 探测不到 / 驱动缺失 / 显存不足 ⇒ 静默走 CPU | 无 GPU 机器上全套绿 |
| 3 | **逐次调用可降级** | 单次 GPU 提交失败 ⇒ 这一次退回 CPU 重算，不报错 | 注入「总是失败」假后端，结果仍正确 |
| 4 | **连续失败要熔断** | 避免每次都白试一遍拖慢主流程 | 假后端失败 3 次后不再被调用 |
| 5 | **结果必须与 CPU 一致（容差内）** | 既有常驻用例就是 oracle（数量以 `testmain` 实际登记为准） | 见 §8 |
| 6 | **不改 `.pslite` 格式、不改文档语义** | GPU 是实现细节 | `projectio` 用例保持绿 |
| 7 | **可强制开关** | A/B 与问题复现 | `PSLITE_RASTER=cpu` 时 GPU 一次都不被调用 |

> 第 1 条决定架构：这是**策略模式（可插拔后端）**，不是替换。
> 所以第一步（P0）不是写 kernel，而是**把接口和调度做出来**——这部分与有没有 GPU 完全无关，
> 却最容易出错，正好可以用 CPU 实现全部验证完。

---

## 1.5 线程模型（**必须遵守，否则会出偶发崩溃**）

```
LayerFilterJob::start()
   └─ QtConcurrent::run → blockingMap(outBands, bandWorker)
                            └─ 每条带一个线程 → stack.apply(...)   ← 并发调用！
```

`RasterScheduler::runStack` **会被多线程同时调用**。因此：

| 对象 | 要求 |
|---|---|
| `RasterScheduler` | 决策只读共享状态；熔断计数用 `std::atomic<int>`（**启发式闸门，允许竞态；不要为它加全局锁**，那会把 GPU 提交串行化） |
| `CpuRasterBackend` | 无共享可变状态，`applyStack` 可 `const`（它只建本地图并求值） |
| `GpuRasterBackend` | **自己解决并发**：单队列 + 互斥（P1 采用**单飞/串行化**）；**不许**要求调用方串行化 |
| 环境变量 | 只在首次初始化读一次（`static` 惰性），不要每次 `qgetenv` |
| 假后端注入 | 仅测试用，生产不暴露 |

> 本项目因「对象先析构、后台任务还在跑」出过偶发堆损坏（`pending-dev.md` 台账 #21）。
> **并发相关的改动，验收必须包含「全量连跑 ≥20 次 0 崩溃」。**

---

## 2. 现状（CPU 全包，代码位置）

| 层 | 现状 | 位置 |
|---|---|---|
| **滤镜链入口** | `FilterStack::apply` 三个重载 | `domain/filterstack.{h,cpp}` |
| **单算子求值** | `FilterEval::applyNode`：有临时资源的走 `FilterOp` 三段式（每线程一份实例），点算子直接分发 | `engine/filtereval.{h,cpp}`、`engine/op/*` |
| **异步任务** | `LayerFilterJob`：`QtConcurrent` 按行带并行（见 §1.5） | `engine/layerfilterjob.{h,cpp}` |
| **瓦片** | `QHash<qint64, QImage> m_tiles`，64×64，稀疏 | `domain/tilebuffer.{h,cpp}` |
| **投影** | 整文档 `QImage m_buffer` + `QBitArray m_chunkValid`；`sync(maxMs)` 同步语义 | `engine/projection.{h,cpp}` |
| **合成** | 逐图层逐 64×64 块，CPU 逐像素混合 | `engine/compositor.{h,cpp}` |

**本计划只动第一行。**

### 2.1 `FilterStack::apply` 的真实形状（**P0 的关键前提，别想当然**）

它**不是**「for 循环依次 applyNode」。最后那个重载（`filterstack.cpp`）实际做的是：

```cpp
QImage FilterStack::apply(const QImage &source, const QRect &srcRect, const QRect &outRect,
                          const std::function<bool()> &shouldAbort) const
{
    if (source.isNull() || !hasEnabled())
        return source;                                   // ① 早退：原样返回
    QImage out = source;                                 // ② 强制 ARGB32_Premultiplied
    if (out.format() != QImage::Format_ARGB32_Premultiplied)
        out = out.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    const QRect srcIn = srcRect.isNull() ? out.rect() : srcRect;   // ③ 几何缺省
    const QRect dstIn = outRect.isNull() ? out.rect() : outRect;
    Q_ASSERT_X(...);                                     // ④ 几何自洽断言

    PsGraph graph;                                       // ⑤ **每次现建一条链图**
    GraphNode::Id prev = graph.addNode(std::make_shared<ImageSourceOp>(out));
    for (const FilterNode &node : m_nodes) {
        if (!node.isEnabled()) continue;
        if (shouldAbort && shouldAbort()) return QImage();      // ⑥ 取消 → 交回空图
        auto op = std::make_shared<PointFilterOp>(node);
        op->setShouldAbort(shouldAbort);
        op->setGeometry(srcIn, dstIn);
        const GraphNode::Id cur = graph.addNode(op);
        graph.connect(prev, cur, 0);
        prev = cur;
    }
    return graph.evaluate(prev, out.rect());             // ⑦ 走图引擎求值
}
```

**因此 P0 的动作是**：把 **③–⑦** 搬进 `CpuRasterBackend::applyStack`（① 早退与 ② 预乘留在
`FilterStack::apply`，见 §3.2），**不是**搬一个循环体。搬完必须与搬前 **bit-exact**
（既有全量用例兜底）；取消仍返回空图（§3.4）。

---

## 3. 目标架构

```
              ┌──────── 调用方（不改签名）────────┐
  Layer::compositeRasterRegion / LayerFilterJob / 测试
              └────────────────┬────────────────┘
                               │ apply(source, srcRect, outRect, abort)
                    ┌──────────▼──────────┐
                    │  FilterStack::apply │   ← 唯一改动的接缝（签名不变）
                    └──────────┬──────────┘
                               │
              ┌────────────────▼─────────────────┐
              │      RasterScheduler（新）        │
              │  ① 探测（惰性一次）               │
              │  ② 决策：链成本 + 像素数 + 开关    │
              │  ③ 执行；失败 → 退回 CPU 重算      │
              │  ④ 熔断（原子计数）                │
              └───────┬───────────────┬──────────┘
                      │               │
            ┌─────────▼──────┐  ┌─────▼──────────┐
            │ CpuRasterBackend│  │ GpuRasterBackend│
            │ = 现有图路径     │  │ 上传→链→回读     │
            │ **永远可用**     │  │ 可选 / 可能不存在 │
            └────────────────┘  └────────────────┘
```

### 3.1 接口（P0 定稿，**签名照抄**）

```cpp
// psDemo/engine/backend/rasterbackend.h
#ifndef ENGINE_BACKEND_RASTERBACKEND_H
#define ENGINE_BACKEND_RASTERBACKEND_H

#include <QImage>
#include <QRect>
#include <QString>
#include <functional>
#include <memory>

namespace Ps {

class FilterStack;

/** applyStack 的三种结局 —— **取消 / 失败 / 不支持 不得混用**（见 §3.4）。 */
enum class ApplyStackResult {
    Ok,           ///< image 已是最终结果
    Failed,       ///< 后端尝试执行但**失败**。**契约：image 必须与调用前逐字节相同**（见类注释与 §3.4）
    Cancelled,    ///< shouldAbort；实现须把 image 置为**空图**；调度器**不得重试、不得计熔断**
    Unsupported   ///< 本后端不能跑这条链；**不计熔断**；调度器应在调用前用 supports() 拦下
};

/**
 * 栅格后端：把**整条滤镜链**作用在一张 QImage 上。
 *
 * 【并发】实现必须**并发安全** —— `LayerFilterJob` 会多线程同时调用（见 gpu-plan.md §1.5）。
 *
 * 【几何】@p srcRect / @p outRect 用**文档坐标**（与 FilterStack::apply 的约定一致）：
 * 输入图 = srcRect 抽出来的那块，outRect ⊆ srcRect 是合法输出区。
 * CPU 后端把它们原样透传给图里的 PointFilterOp（见 §2.1 步骤 ⑤/⑦）。
 * 传空矩形 = 整张图。
 *
 * 【失败必须无副作用 —— 这是 `Failed` 能安全降级的前提】
 * 调度器在 `Failed` 后**要在同一张 image 上**用 CPU 重算，所以后端**不得**在失败路径上
 * 写坏它。实现纪律：**先在设备/临时缓冲上算完，最后一步才提交（assign）到 @p image**；
 * 任何中途失败都必须直接返回，不动 @p image。
 * 否则 GPU 半成品会污染 CPU 重算的输入（而"先给调度器拍快照"要额外 35 MB memcpy ≈ 7 ms，
 * 正好把 GPU 的收益吃掉 —— 所以这条纪律不能靠调度器兜底）。
 */

class RasterBackend
{
public:
    virtual ~RasterBackend() = default;

    /** "cpu" / "gpu:<api>"。仅用于日志与测试断言。 */
    virtual QString id() const = 0;

    /** CPU 后端恒 true；GPU 后端做惰性探测。 */
    virtual bool isAvailable() const = 0;

    /**
     * 本后端**是否声称能跑**这条启用链（不含传输/成本判断）。
     * - CPU：恒 true
     * - GPU（P1）：仅当启用节点全是 MedianBlur（§7.1）；其余 false
     * 【纪律】supports==false 时调度器**不得**调用 applyStack，也**不得**把这记成失败。
     */
    virtual bool supports(const FilterStack &stack) const = 0;

    /**
     * 就地把整条链作用在 @p image 上（图路径的等价实现，见 §2.1 ③–⑦）。
     * 实现内部**不得**自行回退到另一后端 —— 回退逻辑只在调度器。
     */
    virtual ApplyStackResult applyStack(QImage &image, const FilterStack &stack,
                                        const QRect &srcRect, const QRect &outRect,
                                        const std::function<bool()> &shouldAbort) = 0;
};

} // namespace Ps
#endif
```

```cpp
// psDemo/engine/backend/rasterscheduler.h
namespace Ps {

class RasterScheduler
{
public:
    static RasterScheduler &instance();

    /**
     * 执行整条链：按决策挑后端；GPU Failed → 退回 CPU 重算；
     * Cancelled → 立即返回（不重试）。
     * @return 实际生效的后端 id；若 Cancelled，仍返回当时尝试的后端 id，
     *         且 @p image 为空（由 FilterStack::apply 再换成返回空图）。
     */
    QString runStack(QImage &image, const FilterStack &stack, const QRect &srcRect,
                     const QRect &outRect, const std::function<bool()> &shouldAbort,
                     ApplyStackResult *resultOut = nullptr);

    /**
     * 决策（可单测）。@p pixelCount = **将要上传的缓冲**像素数（= image.width()*height()，
     * 含 pad 后的整块，不是 outRect 面积）。
     */
    bool shouldUseGpu(const FilterStack &stack, int pixelCount) const;

    // —— 可观测性 / 测试 ——
    bool gpuDisabled() const;
    int consecutiveFailures() const;
    void setGpuBackendForTest(std::unique_ptr<RasterBackend> backend); // 仅测试
    void resetForTest();                                               // 仅测试
};

} // namespace Ps
```

```cpp
// psDemo/engine/backend/rastercost.h
namespace Ps {
enum class OpCostNsPx : int { UNREGISTERED = -1, MEDIAN_MEASURED = 187, /* … §4.3 */ };
enum class GpuDecisionThreshold : int {
    CHAIN_COST_NS_PER_PIXEL = int(OpCostNsPx::LIGHT_3X3), // =5，单独 BoxBlur 不上
    MIN_PIXELS = 100 * 1000,
};
OpCostNsPx opCostNsPerPixel(OpName name); // 未登记 → UNREGISTERED（用例会红）
int stackCostNsPerPixel(const FilterStack &); // Σ ns/px；遇未登记 → -1
}
```

### 3.2 职责划分与 `FilterStack::apply` 的改法

**谁做什么（钉死，避免 §2.1 与本节打架）**

| 步骤 | 落点 |
|---|---|
| ① 空图 / 无启用节点早退 | **留在** `FilterStack::apply` |
| ② 转成 `ARGB32_Premultiplied`（只做一次） | **留在** `FilterStack::apply` |
| ③–⑦ 几何缺省、断言、建 PsGraph、取消检查、`evaluate` | **`CpuRasterBackend::applyStack`** |
| 调度 / 降级 / 熔断 | `RasterScheduler` |

```cpp
QImage FilterStack::apply(const QImage &source, const QRect &srcRect, const QRect &outRect,
                          const std::function<bool()> &shouldAbort) const
{
    if (source.isNull() || !hasEnabled())
        return source;                                   // ① 早退
    QImage out = source;
    if (out.format() != QImage::Format_ARGB32_Premultiplied)
        out = out.convertToFormat(QImage::Format_ARGB32_Premultiplied); // ②
    ApplyStackResult r = ApplyStackResult::Ok;
    RasterScheduler::instance().runStack(out, *this, srcRect, outRect, shouldAbort, &r);
    if (r == ApplyStackResult::Cancelled)
        return QImage();                                 // 与搬前契约一致：调用方丢弃
    return out;
}
```

（另两个重载继续委托给它，**不动**。）

### 3.3 GPU 后端的**额外准入条件**（原 §10.2 缺口 2 的关闭结论）

几何**照传**（接口带 `srcRect/outRect`）。`shouldUseGpu` / `GpuRasterBackend::supports` 还要求：

```
链里**不含**位置相关算子（`hasPositionalOp()`：AddNoise / Ripple）与位移（`containsOffset()`：Offset）
```

原因：这两类依赖**输入图的绝对原点**（噪声按 `(x,y,seed)` 取、位移读「别处」），
P1 GPU 只处理位置无关链。不满足 ⇒ `supports==false` ⇒ 走 CPU（**不计熔断**）。

> 分块路径（`srcRect != outRect`）**不必**排除：几何透传、中值位置无关；
> 像素数门槛（§4.3）通常已把过小的分块请求筛掉。

**GPU 后端不使用 `PsGraph`。** 这是有意的：图引擎（`engine/graph`）是 CPU 侧的调度与缓存设施
（节点缓存、脏传播、ROI 反向传播），把整张图搬上 GPU 会牵动 `GraphNode` 的求值模型，
远超本计划范围。GPU 后端只做一件事：**按启用节点的顺序，在设备上依次执行 kernel**
（一次上传 → N 个 kernel → 一次回读）。

这只有在"链是**线性**的且 `supports()` 覆盖了**全部**节点"时才与图求值等价 ——
而这正是 §7.1 的准入条件（P1：全为中值）。**不要**试图把 `PsGraph` 移植到 GPU。

### 3.4 取消 / 失败 / 不支持（硬伤关闭）

| 结局 | 谁产生 | 调度器动作 | 计熔断？ |
|---|---|---|---|
| `Ok`（**GPU**） | GPU 跑完 | 返回 `gpu:*` | **清零** |
| `Ok`（CPU 降级补算） | 上一步 GPU `Failed` 后的 CPU 重算 | 返回 `"cpu"` | **不清零**（见纪律 3） |
| `Failed` | GPU 提交/编译/显存等**真正失败** | **先加计数**，再在**同一 image 上**用 CPU 重算 | **是**（连续 3 次 ⇒ 禁用 GPU） |
| `Cancelled` | 任后端见 `shouldAbort==true` | **不重试**；`image` 已空；FilterStack 返回空图 | **否**（计数完全不动） |
| `Unsupported` | 误调用了 `supports==false` 的后端 | 视为**编程错误**：Debug 下 `Q_ASSERT_X(false,…)`；Release 下打日志走 CPU | **否** |
| `Failed`（**来自 CPU 后端**） | CPU 是最后防线，正常不可能失败 | **不回退**（会死循环）：返回空图 + `qWarning` | **否** |

**三条纪律**

1. **`Failed` 必须无副作用**：后端中途失败时必须保证 `@p image` **与调用前逐字节相同**
   （见 §3.1 类注释的"最后一步才提交"）。
   调度器据此在**同一张 image 上**用 CPU 重算 —— **不拍快照**
   （拍快照 = 35 MB memcpy ≈ 7 ms，正好把 GPU 的收益吃掉，所以这条不能靠调度器兜底）。
2. **CPU 后端返回 `Failed` 不得触发"退回 CPU"**：那会变成同一后端上的死循环。
   CPU 是最后防线 ⇒ 直接 `qWarning` + 返回空图（正常永不发生，属断言级路径）。
3. **只有 GPU 自己返回 `Ok` 才清零计数；CPU 降级补算的成功不清零**。
   否则会出现：GPU 一直失败 → CPU 每次都补算成功 → 计数反复清零 → **熔断永不触发**，
   每次调用都白试一次 GPU。这条是 §6.4 与 §6.6 第 4 条要钉的。

**反面例子（禁止）**：把「GPU 还不支持蒙尘与划痕」当成 `Failed` → 用户连开三次对话框 → 熔断 → 真中值也被关掉。  
正确：`supports()` 先拦下，根本不调用 `applyStack`。

---

## 4. 决策规则

### 4.1 按「链」决策，**不按「算子」**

```
错（逐算子）：亮度(CPU) → 上传 → 中值(GPU) → 回读 → 反相(CPU)    两次往返
对（整链）  ：上传 → 亮度 → 中值 → 反相 → 回读                   一次往返
```

既然要传，就把整条链都放上去（点算子在 GPU 上本就近乎免费）。

### 4.2 判据 —— **⚠️ 已随 T1.9（opt-in）简化：成本阈值不再参与决策**

**当前实现（`rasterscheduler.cpp`，T1.9 之后）**：

```
shouldUseGpu =  开关 == "gpu"            // Auto/ForceCpu 一律 false（§7.2.2 方案 A）
             && gpuBackend 存在 && isAvailable()
             && !熔断中
             && gpuBackend->supports(stack)     // §3.3 / §7.1；不支持≠失败
             && pixelCount >= kMinPixelsForGpu  // = image 像素数（含 pad）
```

**为什么去掉成本阈值**：改成 opt-in 之后，是否用 GPU 已经是**用户的显式选择**，
再加一层"成本不够就别用"只会让 `PSLITE_RASTER=gpu` 时行为不可预期
（例如门控用例会时灵时不灵）。所以简化成"你说了算 + supports/像素/熔断三道硬门槛"。

> **`kGpuChainCostThresholdNsPerPixel` 与 `stackCostNsPerPixel` 现在只用于日志**
> （`[raster] ops=… cost=… backend=…`）与**成本表覆盖性用例**（§6.6 第 7 条）。
> **它们不再影响任何决策** —— 别再照下面那段旧公式实现。
> 保留它们是给**方案 B**（GPU 成本模型）留的地基。

**（历史公式，仅存档）** 早先版本按"链的每像素总成本 vs 一次往返"决策，推导如下：
一次往返（8.7 Mpx ≈ 35 MB，PCIe ~10 GB/s）≈ 7 ms ≈ **0.8 ns/px**；像素数两边成比例会约掉，
于是判据化简为链的静态属性 —— `Σ(ns/px) > 阈值`。
**这个思路本身没错**，但在"opt-in"策略下没有意义；而且**它隐含假设"GPU 一定更快"**，
与 bitonic 时代实测（§7.2.1.2：r=3 只有 0.53×）冲突；计数选择后约 2×（§7.2.1.4）
仍未达 3× ⇒ 若将来要恢复"自动判断"，**必须先有 GPU 侧的成本模型**，
而不是拿 CPU 的 ns/px 去比一个固定阈值。
### 4.3 成本表（`rastercost.cpp`）

**取值纪律**：标「实测」的是本项目真实量到的；标「估值」的必须在 P0 用一次性程序
（`.build-verify/`，不参与编译）**测出来再回填**；标「待测」的不得凭空填数。

| OpName | ns/px | 来源 |
|---|---|---|
| `MedianBlur` | 187 | **实测**（512² r=3 = 49 ms） |
| `Maximum` / `Minimum` | 100 | 估值（与中值同族） |
| `DustAndScratches` | 100 | 估值（中值 + 阈值） |
| `ReduceNoise` | 50 | 估值（中值 + 混合） |
| `AddNoise` | 22 | **实测**（1600×1200 高斯彩噪 42 ms） |
| `GaussianBlur` | 19 | **实测**（T2.0：512² r=3 ≈20.6 ns/px；1024×768 ≈18.2） |
| `CustomFilter` | 48 | **实测**（T2.7 标准测法：锐化 ≈46～51 ns/px；稠密盒 ≈49～53） |
| `UnsharpMask` / `HighPass` | 15 | 估值（= 盒模糊 + 合并） |
| `MotionBlur` | 15 | 估值（距离大时） |
| `SharpenEdges` | 10 | 估值（Sobel + 锐化） |
| `BoxBlur` | 5 | **实测**（临界线上） |
| `FindEdges` / `Sharpen` / `Emboss` | 5 | 估值（3×3） |
| `Ripple` | 2 | 估值（重映射采样；**位置相关 ⇒ 走整层求值**，见下注 1） |
| `Offset` | 1.5 | **实测**（memcpy） |
| 调整类全体（`BrightnessContrast`/`Levels`/`Curves`/`Exposure`/`Vibrance`/`HueSaturation`/`ColorBalance`/`BlackAndWhite`/`PhotoFilter`/`ChannelMixer`/`ColorLookup`/`Invert`/`Posterize`/`Threshold`/`GradientMap`/`SelectiveColor`） | 1 | 估值（点算子 0.5–2） |
| `Solarize` | 0.5 | 估值（点算子） |
| 绘制/选区/合成类（`StampDab`/`FloodFill`/`Gradient`/`SolidFill`/`SelectPolygon`/`SelectFlood`/`CloneStampDab`/`FocusDab`/`ToneDab`/`ShapeFill`/`FreeTransform`/`LayerMode`） | 0 | **不走滤镜链**，登记为 0 |

> **注 1（位置相关算子）**：`AddNoise` / `Ripple` 走「整层求值」（`FilterStack::hasPositionalOp`）。
> 上表仍登记成本便于审计；决策上 §3.3 / `supports` 已排除。P2 若做 GPU 版须单独处理原点。
>
> **注 2（覆盖性）**：未登记返回 `-1`，**用例会红**（§6.6）。**每新增一个 `OpName`（加在 `Count` 之前）必须同步补本表**——`Ripple` / `Solarize` 等已落地的项不得再标「正在加」。

### 4.4 ★ 选型判据：**「GPU 友好度」优先，CPU 慢只是次要信号**（第四轮修正）

**这条是本计划最重要的一次自我修正，写在最显眼处。**

§4.3 那张表按 **CPU 的 ns/px** 排序，于是"最慢的中值"被选成了 P1 的证伪样本。**这个推理是错的**：

> **CPU 慢 ≠ GPU 值得。** `ns/px` 高只说明"这件事 CPU 干得费劲"，
> 完全没说"GPU 干这件事是否顺手"。

实测把这一点暴露得很干净：中值 CPU **166 ns/px**（表内第一），但 GPU 侧只能
「排序（`O(n log²n)`，2688 次比较交换/像素，r=3 掉到 0.53×）」或「计数选择」，
而它的 CPU 优势恰恰来自**沿行串行增量的 cum** —— 正是 GPU 用不上的形状。
⇒ **"CPU 最擅长、GPU 最别扭"的算子，不该拿来回答"GPU 值不值得"。**

**应当先看的四问**（顺序即权重）：

| # | 问题 | 为什么 |
|---|---|---|
| 1 | **有没有串行依赖？** | 沿行/沿列的滑窗递推（中值的 cum、盒模糊的运行和）是 CPU 的杀手锏，GPU 只能改成并行形式 |
| 2 | **每像素算力够不够摊平传输？** | 传输往返 ≈ **0.8 ns/px**（§4.2 历史推导）。每像素运算量远高于它，GPU 才有机会 |
| 3 | **访存是否规律？** | 逐像素定窗口、定核卷积 → 极好；随机/分支（排序、随机邻居）→ 差 |
| 4 | **每线程私有状态有多大？** | 大数组（如排序窗口 4×N×4B）会溢出到局部内存，直接把并行度优势吃掉 |

**按这个判据重排现有算子**：

| 算子 | CPU ns/px | 四问结论 | GPU 友好度 | 该不该做 GPU |
|---|---|---|---|---|
| **`CustomFilter`（3×3 卷积）** | **48（实测）** | 无串行依赖 / 访存规律 / 无私有数组 | **★★★** | **已试：锐化 2.5～3.4×；稠密 2.6～2.8×**（§7.4.1） |
| **`GaussianBlur`（三次可分离）** | **19（实测）** | 可分离、无跨像素依赖 / 加法律 / 规律 | **★★☆** | **排在 AddNoise 之后**（算力更弱，§4.4.1） |
| `BoxBlur` / `MotionBlur`（一维卷积） | 5 / ~15 | 同上，但每像素运算量偏低（传输占比上升） | ★★☆ | 可试，预期倍数低 |
| `UnsharpMask` / `HighPass` | ~15 | = 盒模糊 + 合并，跟着模糊级走 | ★★☆ | 随模糊级 |
| `Sharpen` / `Emboss` / `FindEdges`（3×3） | 5 | 同 `CustomFilter`，但每像素运算量小 | ★★☆ | 可试 |
| `AddNoise` | 22 | 纯散列、无状态（**本来最适合 GPU**），但成本低 | ★★☆ | **特殊**：位置相关走整层，GPU 上反而更简单（§3.3 注） |
| `MedianBlur` / `Maximum` / `Minimum` | **187 / ~100** | **有串行增量**；计数选择后无私有大数组 | **☆～★** | **T1.13 已落地**：r=3 ≈**2×**（§7.2.1.4），仍 <3× ⇒ 维持 opt-in；不必再啃中值 |
| `Offset` / 点算子 | ≤1.5 | 传输就吃掉了 | ☆ | 不做 |

**结论**：**P1 用中值做证伪样本是选错了题目**，它得到的否定答案**只能覆盖中值这一类
（排序统计 + 串行增量）**，覆盖不了"GPU 加速"这件事本身。

⇒ 因此新增 **P1′：第二次证伪，改用卷积族**（见 §7.4）。**在这一步出结果之前，
不得把结论写成"GPU 无价值"。**

### 4.4.1 ★「下一个做哪个」——用四问打分（第五轮修正）

§4.4 定下判据后，**"下一优先"必须由它推出来**，不能凭直觉。把已实测的候选代进去：

| 候选 | CPU ns/px（实测） | ① 串行依赖 | ② 算力/传输线（0.8 ns/px） | ③ 访存 | ④ 私有状态 | 结论 |
|---|---|---|---|---|---|---|
| **`AddNoise`** | **22**（高斯彩噪） | **无** ✓ | **≈27×** ✓ | **纯顺序** ✓ | **零** ✓ | **★★★★ 四问全过** |
| `CustomFilter` 稠密核（9 权重） | **~50（实测）** | 无 ✓ | ≈62× ✓ | 规律 ✓ | 无 ✓ | ★★★ **已测 2.6～2.8×；未跨 3×** |
| `CustomFilter` 锐化核（5 权重） | **~48（实测）** | 无 ✓ | ≈60× ✓ | 规律 ✓ | 无 ✓ | ★★★ **已试 2.5～3.4×** |
| `GaussianBlur` | **19** | 无 ✓ | ≈24× | 规律 ✓ | 无 ✓ | **★★☆ 比上面都弱** |
| `MedianBlur` | 187 | **有**（增量 cum） ✗ | — | 排序/选择 | 大（或计数） | ☆～★ 已试 ≈2× |
| `Offset` / 点算子 | ≤1.5 | 无 | **≈1.9×（传输就吃掉）** ✗ | — | — | ☆ 不做 |

**⇒ 两个修正（都能直接执行）**：

1. ⚠️ **不要把 `GaussianBlur` 当"下一优先"** —— 它 19 ns/px 是**表里最低的卷积候选**，
   按判据② 恰恰是**最弱**的一个。P2 里它该排在后面。
2. ★ **`AddNoise` 才是四问全过的那个**：无串行依赖、算力 ≈27× 于传输线、**纯顺序访存**、
   **零私有状态** —— 这是整张表里 **GPU 友好度最高**的算子；而且它的 kernel **最简单**
   （纯散列，无窗口、无核、无邻域），实现量明显小于中值/自定。
   ⚠️ 注意它现在是**位置相关算子**（`hasPositionalOp` → 整层求值），
   但 GPU 上"整层"本来就是自然的粒度 ⇒ **不构成障碍**（§3.3 注 1）。
   主要工作量是**把 `noiseHash` 逐位搬到 GLSL**（纯整数运算，注意 uint32 溢出语义与 `qreal` 舍入）。

---

## 5. 分期总览

> **当前进度（2026-10-12）**：**P0 ✅**；中值 / 自定已入库；**T2.7–T2.9 / T4.1–T4.3 已关闭**。
> T4.3 后标准测法：锐化 ≈**4.4～7.0×**（跨 3×）；稠密盒 ≈**2.8～4.4×**；中值 r=3 ≈**2.1～2.3×**。
> 维持 opt-in。见 §16.6。

| 期 | 内容 | 前置 | 决策点 | 状态 |
|---|---|---|---|---|
| **P0** | 壳与调度（**不写任何 GPU 代码**） | 无 | 做完即有价值 | ✅ **已提交** `0db3a5d` |
| **P1** | 证伪样本 #1：**中值**（排序统计） | P0 + API spike | 原为「端到端 ≥3× 才继续」；被产品决策覆盖 | ✅ 标准测法 r=3 ≈**2.4～2.9×**；opt-in |
| **P1′** | 证伪样本 #2：**卷积族**（`CustomFilter`） | P0 | ≥3× / 1–3× / <1× 三档 | ✅ **T4.3 后锐化 4.4～7×**；稠密 2.8～4.4×；仍 opt-in |
| **P2** | 按 **§4.4.1** 逐算子扩（**先 `AddNoise`**） | T2.7–T2.9 | 每算子单独验收；仍 opt-in | ⏸ **下一优先 T3.0** |
| **P3** | 绘制算子特例（另一量级） | 另行评估 | 只有"拖动/绘制也要快"成为真实需求才启动 | ⏸ 未开始 |

**顺序提醒**：P1 的"先接上"是产品决策，**不等于 P1 达标**；而 P1 未达标**也不能推出**
"GPU 不值得"——那只说明**中值**这一类不值得。**下一步是 P1′，不是 P2。**

---

## 6. P0 详细规格

### 6.1 做什么

见 §10 的勾选清单（T0.1–T0.12）。

### 6.2 P0 **不做**什么

- 不写任何 GPU / kernel / 驱动代码
- 不动 `LayerFilterJob` / `Projection` / `Compositor` / `TileBuffer`
- **不改 `FilterEval::applyNode`**（单算子入口不进后端，只有整链进）
- 不改 `FilterStack::apply` 的**签名与语义**（三个重载行为必须逐像素不变）
- 不改 `.pslite`

### 6.3 开关与日志（确切名字）

| 变量 | 取值 | 语义 |
|---|---|---|
| `PSLITE_RASTER` | `auto`（默认）/ `cpu` / `gpu` | `cpu` = 永不使用 GPU；`gpu` = 优先 GPU 但失败仍降级（**不报错**，硬约束 2） |
| `PSLITE_RASTER_LOG` | `1` | 每次决策打一行 `qDebug`，格式固定：`[raster] ops=3 cost=210 ns/px px=2073600 backend=cpu` |

首次读取后缓存到 `static`（§1.5）。

### 6.4 熔断规则（确切数字）

```
连续失败 N = 3 次  ⇒  本次会话禁用 GPU（gpuDisabled() == true）
清零              = **只有 GPU 自己返回 Ok**（CPU 降级补算的成功**不清零** —— 否则熔断永不触发）
失败定义          = GPU 的 applyStack 返回 **Failed**（仅此一种）
不计熔断          = Cancelled / Unsupported / supports()==false 未调用 / CPU 后端的 Failed
计数              = std::atomic<int>（多线程安全）
竞态              = 计数是**启发式闸门**，允许并发竞态；**不要**为它加全局锁（那会串行化 GPU 提交）
禁用后            = shouldUseGpu() 恒 false；不再调用 GPU 后端
复位              = 仅 resetForTest()（测试用）；生产需重启进程
```

### 6.5 P0 的 Acceptance（DoD）

| # | 验收项 | 怎么验 |
|---|---|---|
| 1 | **既有全部用例绿** | 跑全套；此时后端只有 CPU，行为必须逐像素不变 |
| 2 | 全量连跑 **≥20 次 0 崩溃** | 循环跑（§1.5） |
| 3 | 主程序**干净重建**：**0 error**；且相对改前**不新增 warning** | §9 命令（不要求历史遗留 warning 清零） |
| 4 | `PSLITE_RASTER=cpu` ⇒ GPU 一次都不被调用 | 假后端计次 == 0 |
| 5 | `PSLITE_RASTER=gpu` 且无 GPU ⇒ 不报错、静默走 CPU | 无 GPU 机器跑全套 |
| 6 | 假后端「总是 Failed」⇒ **每次结果仍正确**（CPU 重算） | 与纯 CPU 逐像素一致 |
| 7 | 假后端 Failed 3 次后不再被调用 | 调用次数 == 3 |
| 8 | 成本表覆盖所有 `OpName < Count`（无 -1） | 新用例断言 |
| 9 | `shouldUseGpu` / 取消 / supports 边界正确 | 见 §6.6 |

### 6.6 P0 新增用例 `rasterbackend` 的断言清单

1. 默认（`auto`）+ 无 GPU ⇒ `runStack` 返回 `"cpu"`，结果与直接 CPU 计算一致；
2. `PSLITE_RASTER=cpu` ⇒ GPU 后端调用次数 == 0；
3. 假后端「总是 Failed」⇒ 结果仍与 CPU 一致；Failed 3 次后不再被调用；
4. **计数方向（三条，第二条是重点）**：
   - 假后端「先 Failed 一次、随后 Ok」⇒ 计数清零（**GPU 自己** Ok 才算）；
   - **假后端「每次都 Failed」而 CPU 每次补算成功 ⇒ 计数仍累积到 3 并熔断**。
     这条专门钉「CPU 降级补算的成功**不得**清零」—— 否则 GPU 一直失败、CPU 每次兜底成功，
     计数被反复清零，**熔断永不触发**，每次调用都白试一次 GPU（§3.4 纪律 3 / §6.4）；
   - `Cancelled` 与 `Unsupported` ⇒ 计数**完全不动**。
5. `shouldUseGpu`（P0 注入的假 GPU：`supports` = 成本过线且非位置/位移；与真 P1 的「仅中值」不同，见下）：
   - 纯点算子链（亮度 + 反相）⇒ `false`（成本不够）
   - **仅中值**且像素数足够 ⇒ `true`
   - 中值 + 亮度（成本过线、假后端 supports 为 true）⇒ `true`（P0）；**P1 真后端 supports 收紧后此项应变 false**——P1 验收再钉（§7.1）
   - 像素数 < `kMinPixelsForGpu` ⇒ `false`
   - 含 `AddNoise` / `Ripple` / `Offset` ⇒ `false`（§3.3）
   - 假后端 `supports==false` 的高成本链（如仅蒙尘）⇒ `false`，且 **applyStack 调用次数不增加、熔断计数不变**
   - 熔断后 ⇒ `false`
6. **取消**：`shouldAbort` 一开始就 true ⇒ `FilterStack::apply` 返回空图，熔断计数不变，不把半成品当结果；
7. 成本表覆盖性：`for i in [0, Count)` ⇒ `opCostNsPerPixel != -1`；
8. **带几何的重载**（`apply(source, srcRect, outRect, abort)`）也走后端，且结果与搬前一致；
9. **`Failed` 的无副作用契约**（§3.1/§3.4 纪律 1）：P0 阶段的假后端在返回 `Failed` 时
   **必须不改动 `image`**；用例断言「假后端 Failed ⇒ 最终结果与纯 CPU 逐像素一致」
   （这正是"CPU 在同一张 image 上重算"能成立的前提）。
   真正的 GPU 后端要实现成**最后一步才提交**；实现完成后在 P1 用同样的断言复核。
---

## 7. P1 详细规格（只做中值）

### 7.1 准入条件（原 §10.2 缺口 6 的关闭结论）

```
GpuRasterBackend::supports(stack) == true  当且仅当
  所有启用节点的 op() 都是 MedianBlur（允许多个中值节点）。
否则 supports==false → shouldUseGpu==false → CPU。
绝不返回 Failed 来表示「还不支持」。
```

**明确不混跑半链**：一个节点 CPU、一个节点 GPU 的交错执行**不做**（那会引入多次往返，
正是 §4.1 要避免的）。P2 扩算子时是扩 `supports()` 的集合，仍是整链一次上传。
### 7.2 API 选型 spike（P1 第一步；**P0 不依赖**）

**要回答的问题**（按重要性）：

1. 在本项目构建链（Qt 6.7.3 + MinGW 11.2.0 + qmake）下能否**无痛接入**？
   若必须换 MSVC 或引入重依赖 ⇒ 直接否决。
2. compute shader 可用吗？中值内核写得出来吗（原子操作、共享内存够不够）？
3. **端到端**（含上传 + 回读）比 CPU 快几倍？（只报 kernel 时间会得出错误结论）
4. 无 GPU 机器上动态加载失败**可捕获**吗（不崩、不要求重装驱动）？
5. **目标平台**有哪些（仅 Windows？还是含 Linux/macOS）？（见 §12 Q5）

| 候选 | 优点 | 顾虑 |
|---|---|---|
| **Qt RHI（QRhi）** | Qt 6 内置，已抽象 Vulkan/D3D11/12/Metal/OpenGL | 为**渲染**设计，通用计算支持面窄，需验证 |
| **OpenCL** | 通用计算成熟、与 OpenGL 互操作好 | Qt 6 不内置，MinGW 下要引依赖 |
| Vulkan / D3D12 compute | 性能与可控性最好 | 代码量最大、平台分支多 |
| CUDA / HIP | 生态最好 | 平台绑定，与"可选、跨平台"定位冲突 |

**做法**：并行用 QRhi compute 与 OpenCL 各写一个中值 kernel，按问题 1–5 打分选一个。
**结论必须写回本文档**（哪怕结论是"都不行，停在 P0"）。

#### 7.2.1 Spike 结论（2026-10-12，本机）—— **未达 3×；原判定「停在 P0」已被产品决策覆盖**

| # | 问题 | 结论 |
|---|---|---|
| 1 | MinGW + qmake 无痛接入？ | **QRhi：可以**（`QT += gui-private`，D3D11）。**OpenCL：可以**（自带 `OpenCL.dll` + Khronos 头 + `dlltool` 导入库；无 NVIDIA/AMD SDK） |
| 2 | compute / 中值内核？ | **两者都能跑**；插入排序 7×7 窗口，与朴素 CPU **bit-exact（diffs=0）** |
| 3 | 端到端 ≥3×？ | **否**。见下表 |
| 4 | 无 GPU 可捕获？ | 本机有 Iris Xe，未测「完全无设备」；调度器侧 P0 已覆盖「无后端 → CPU」 |
| 5 | 目标平台 | **当前以 Windows + MinGW 为准**（与主工程一致） |

**实测**（中值 r=3；GPU 计时 = 上传 + kernel + 回读；**管线/缓冲实际是每次重建**，见 §7.2.1.1；生产 CPU 按既有 **187 ns/px** 估算）：

| 尺寸 | API | GPU 端到端 | vs 生产 CPU 估算 | vs 朴素 nth_element |
|---|---|---|---|---|
| 512² | QRhi/D3D11 | ~53 ms | **0.93×** | ~3.3×（朴素偏慢，不能当门槛） |
| 1024×768 | QRhi/D3D11 | ~162 ms | **0.91×** | ~2.9× |
| 1600×1200 | QRhi/D3D11 | ~400 ms | **0.90×** | ~2.9× |
| 1024×768 | OpenCL (Iris Xe) | ~135 ms | **1.09×** | ~3.1× |

**判定（spike 当时）**：相对生产 CPU 约 **0.9～1.1×**，未达原门槛 ≥3×。  
**产品决策（2026-10-12 修订）**：用户要求**先接上** —— 仍合并 `GpuRasterBackend`（QRhi/中值），CPU 永为降级；**不声称已达 3×**，独显上可再测。Spike 在 `.build-verify/gpu_spike/`。

#### 7.2.1.1 根因复核（2026-10-12 复审）—— **原"传输占比高"的判断不成立**

原结论写的是「集成 GPU 上整图上传/回读占比高」。**按实测数据反推，这个归因是错的**：

```
1600×1200×4B = 7.7 MB；上传 + 回读 ≈ 15.4 MB
即便按 5 GB/s（远低于 PCIe 3.0 x16 的实际带宽）也只 ≈ 3 ms
而实测端到端 ≈ 400 ms
⇒ 传输占比 < 1%
```

**真正的两个原因（都在实现侧，与"集成 GPU"无关）**：

| # | 问题 | 证据 | 影响 |
|---|---|---|---|
| 1 | **kernel 用了插入排序** | `shaders/median.comp`：`insertSort(chR/G/B/A, n)`；r=3 时 n=49 → ≈49²/4≈600 次**带数据相关的比较+搬移** ×4 通道 ≈ 2400 ops/像素；内层 `while` 是**数据相关分支**（GPU 最怕的 divergence） | 占端到端 **>95%** |
| 2 | **管线/缓冲每次调用都重建** | `gpurasterbackend.cpp`：`bufBytes = 0;` 后调 `ensurePipeline(bytes)` ⇒ 缓存判断恒不命中 ⇒ 每次重建 srcBuf/dstBuf/ubuf/srb/pipe | 每次多一次设备资源创建 + 同步 |

> ⚠️ 原表格标题写的「**管线/缓冲已池化**」与代码不符（代码是每次重建），**该行已按实际改正**。

**最关键的一条**：本计划 §10 的 **T1.3 早就写明**「现在的滑窗直方图 + 增量 cum 沿行串行，是 CPU 算法形状；
GPU 上要换**排序网络**（小半径）或**并行直方图 + 前缀和**。**不是移植，是重写**」。
而 spike 用的是**插入排序** —— 既不是排序网络，也不是并行直方图。

⇒ 因此正确的表述是：**这个 spike 测到的是「未优化实现」的下限，不足以证明「GPU 达不到 3×」**。
它是"用错的算法 + 每次重建资源"跑出来的 0.9～1.1×，而**不是** GPU 这条路线的天花板。

**下一步的正确顺序（取代原来的"换独显再测"）**：

1. **先把 kernel 换成排序网络**（49 元素 odd-even merge / bitonic，全 branchless）→ 重测；
2. 顺带把缓冲/管线做成**真正池化**（§7.3 的"设备缓冲池"本就是这个要求）；
3. **两步都做完再谈"换独显"** —— 否则换卡也只是让一个 O(n²) 内循环快一点，
   而根因（算法形状）还在。

#### 7.2.1.2 重测（2026-10-12，bitonic + 池化 + opt-in）

程序：`.build-verify/median_e2e/`（`psDemo/median_e2e.pro`）；**对照是生产 `CpuRasterBackend`**
（须 `Ps::opsInit()`，否则 Release 下面积算子静默空跑）。GPU = 上传 + kernel + 回读；
kernel = 按半径选最小 2^k bitonic（r=1→16 … r=3→64）；dst/ubuf 容量池化，src 每帧 Immutable 重建。

| 尺寸 | r | CPU | GPU | GPU÷CPU |
|---|---|---|---|---|
| 512² | 3 | 43.4 ms（166 ns/px） | 82.0 ms | **0.53×** |
| 1024×768 | 3 | 130 ms | 221 ms | **0.59×** |
| 1600×1200 | 3 | 318 ms | 545 ms | **0.58×** |
| 512² | 1 | 29.6 ms | 14.0 ms | **2.11×** |
| 512² | 2 | 35.0 ms | 30.2 ms | **1.16×** |

一致性：`gpumedian` bit-exact（逐通道差 0）。

**判定**：
- **默认半径 r=3 仍远低于 1.0×**（约半速）⇒ **维持 opt-in**；**不得**改 `Auto`=GPU；**不得**开 P2。
- 小半径（r=1）已能 **>2×**，说明「GPU 这条路可行」，且**排序网络确实生效了**（否则不会随半径
  呈现出这种尺度关系）。
- ⚠️ **「bitonic 并未更快（53→82 ms）」这个对比不可直接相减**：两个数字来自**不同的计时程序**
  （旧的在 `.build-verify/gpu_spike/`，新的是 `median_e2e`），而且**排序元素数不同**
  —— 插入排序排 `n=49`，bitonic 排 `N=64`。按比较交换次数算：约 2400 vs 2688（只差 12%），
  解释不了 +55%。**结论只能是"两者同量级"，不能得出"bitonic 更差"。**

**⚠️ 下一处方位的修正（不要按原判断走）**：原文把下一步优先级写成「**先减少 src 重建**
（纹理上传/双缓冲）」。但按 §7.2.1.3 的算式，src 重建 + shader 反序列化 + PSO 重建
在 r=3 大图里**合计 <1%**（小图 r=1 时才可观，约 10%）。**继续优化缓冲不是收益所在。**

#### 7.2.1.3 运算量复核（2026-10-12，第三次复审）—— **瓶颈是「选了排序」，不是缓冲**

把 §7.2.1.2 的 1600×1200 / r=3 / GPU 545 ms 拆开算：

| 项 | 估算 | 占端到端 |
|---|---|---|
| 全图 CPU 侧编组（`qUnpremultiply` + pack + `qPremultiply`，3 趟） | ≈ 2 ns/px × 3 × 1.92 Mpx ≈ **12 ms** | ~2% |
| src 重建 + shader 反序列化 + PSO 重建（每帧） | ≈ **1–5 ms** | <1% |
| 回读 7.7 MB | ≈ **1 ms** | <0.2% |
| **kernel** | **≈ 530 ms** | **≈97%** |

再看 kernel 是否"跑得慢" —— **不是，它跑在正常速率上**：

```
bitonic N=64：log2(64)=6 → 阶段数 6×7/2 = 21；每阶段 64/2 = 32 次比较交换
  ⇒ 672 次/通道 × 4 通道 = 2688 次比较交换/像素
530 ms ÷ 1.92 Mpx ÷ 2688 ≈ 0.10 ns / 比较交换   ← 这是 GPU 上的正常吞吐
```

**所以问题不在实现效率，在运算量**：

| | 每像素运算量 | 实测 |
|---|---|---|
| CPU（滑动直方图 + 增量 cum） | O(n)，常数极小 | **166 ns/px**（4 通道全做完） |
| GPU（全量 bitonic 排序） | **2688 次比较交换** | 276 ns/px（**光排序**） |

**关键推论（比数字本身重要）**：排序是 `O(n log²n)`，直方图是 `O(n)` ⇒
**半径越大，GPU 的相对劣势越大**。这正好解释了实测的尺度：
r=1 是 2.11×、r=2 打平、r=3 掉到 0.53× —— **与"半径越大越该上 GPU"的直觉相反**。

**唯一有量级潜力的方向：把「排序」换成「计数选择」** —— 中值只要第 k 个，**根本不需要全排序**。
在两遍计数（粗分箱 → 定位到箱 → 箱内精确定位）下，每像素运算量回落到 O(n) 量级
（≈ 百次级操作，而不是 2688 次比较交换），且**天然按像素并行** —— 这才是 GPU 该有的形状。

备选（收益较小，可按序试）：**中值/选择网络**（只求第 k 个，比较交换次数比全排序少，
但同属 `O(n log²n)` 家族，量级改善有限）。

#### 7.2.1.4 重测（2026-10-12，**计数选择** + shader 缓存 + e2e 入树）

程序：`psDemo/tools/median_e2e/`（`psDemo/median_e2e.pro`；T1.14 已入源码树）。
对照生产 `CpuRasterBackend`（须 `Ps::opsInit()`）。GPU = 上传 + kernel + 回读。
kernel = 两级计数选择（粗 16 箱 → 精 16 档；`kth=(n+1)/2`）；`cachedCs` 避免每帧反序列化（T1.15）。

**历史表**（计数选择初测，warmup=1；仅作过程记录）：512² r=3 曾报 2.05×（CPU 53.4 / GPU 26.0）。
相对 bitonic（§7.2.1.2 GPU 82.0 ms）的 **GPU 绝对改善 ≈3×** 仍成立。

**T2.7 标准测法重测**（`warmup=2`、seed=7、同图同 runs）：

| 尺寸 | r | CPU | GPU | GPU÷CPU |
|---|---|---|---|---|
| 512² | 3 | 49.6 ms（189 ns/px） | 17.0 ms | **2.92×** |
| 1024×768 | 3 | 143 ms | 51.5 ms | **2.78×** |
| 1600×1200 | 3 | 339 ms | 140 ms | **2.42×** |

一致性：`gpumedian` bit-exact。

**判定**：r=3 稳定 ≥1× 且 <3× ⇒ 保留路径、**维持 opt-in**；中值线收口。

#### 7.2.2 默认策略（已落地 opt-in）

| 方案 | 做法 | 状态 |
|---|---|---|
| **A** | `Auto` = CPU；GPU 仅 `PSLITE_RASTER=gpu` | **已落地（T1.9）**；重测仍支持此选择 |
| B | GPU 成本模型，`Auto` 估算更快才开 | 可后续；r=3 模型会倾向 CPU |
| C | 把成本阈值抬到中值之上 | 不采用（名不副实） |

`PSLITE_RASTER_LOG=1` 打出实际 `backend=`；`features.md` 已写明。

### 7.3 P1 的 Acceptance（**证伪样本 #1：中值**）

> **注**：第 2 条（≥3×）已被 §7.2.1 的**产品决策**覆盖（用户要求先接上，不声称 3×）。
> 但它**没有被删除** —— 它仍是"宣称 GPU 值得默认启用"的门槛。
> 默认策略已由 T1.9 定为 opt-in ⇒ 第 8 条自动满足。
> **本节的框架在 P1′ 里原样复用，只是换掉证伪样本（见 §7.4）。**

| # | 验收项 |
|---|---|
| 1 | 中值结果与 CPU 一致（容差见 §8） |
| 2 | **端到端（含传输）≥ 3×** CPU；**不到 3× 就停在 P0**，并把实测数据写进本文档 |
| 3 | 无 GPU 机器上全部用例照旧绿 |
| 4 | `PSLITE_RASTER=cpu` ⇒ GPU 一次都不被调用 |
| 5 | 显存不足 / 提交失败 ⇒ 逐次退回 CPU，结果正确（硬约束 3） |
| 6 | **上传前与回读后格式都必须是 `Format_ARGB32_Premultiplied`**（原 §10.2 缺口 5） |
| 7 | 多线程并发调用不崩（跑 `filterasync`/`posasync`/`jobteardown` + 全量连跑 ≥20 次） |
| 8 | **默认策略下不得慢于 CPU**（若 `Auto` 会启用 GPU）：端到端 ≥ **1.0×**。**已由 T1.9 满足** —— `Auto` = CPU，GPU 走 opt-in ⇒ 默认路径不可能变慢。opt-in 下计数选择后 r=3 ≈**2×**（§7.2.1.4）已过 1×，但 **2 条（≥3×）仍未达标** ⇒ 维持 opt-in；全局结论改看 **P1′ / Q8** |

### 7.4 ★ P1′：第二次证伪（**卷积族**）—— 为什么必须先做这个

**要回答的问题**：**「GPU 在这台机器上到底值不值得」** —— 中值答不了（§4.4）。

**选谁**（按 §4.4 的四问）：

| 候选 | 为什么是它 |
|---|---|
| **`CustomFilter`（3×3 卷积）** | **一个顶多个**：配不同核就是锐化 / 模糊 / 浮雕 / 边缘提取；36 次 MAC/像素（远高于传输的 0.8 ns/px）；无串行依赖、无私有大数组 |
| `GaussianBlur`（三次可分离） | 同上；且它是 **USM / 高通保留的模糊级** ⇒ 一处收益带三处 |

**建议先做 `CustomFilter`**：kernel 最简单（3×3 卷积 + 缩放 + 偏置），
且它覆盖**线性** 3×3 卷积族（`Sharpen` / `Emboss` / `CustomFilter`；`FindEdges` 是 Sobel 幅值，另论）。

**第 0 步（必做，约半天）**：先把 §4.3 里 `CustomFilter` / `GaussianBlur` 的
**「估值」实测出来**。现在那两个数（~20 ns/px）是**猜的**，而整套判据建立在它们上面 ——
**不能拿估值当依据**。这正是 T0.10 一直欠着的事，本轮它从"可选"**升级为前置**。

**验收（沿用 §7.3 的框架，只换样本）**：

| # | 验收项 |
|---|---|
| 1 | `CustomFilter` 结果与 CPU **bit-exact**（或逐通道 ≤1） |
| 2 | **端到端（含上传回读）≥ 3×** ⇒ 达标；**<1×** ⇒ 那时才可以下"GPU 不值得"的结论；**1–3×** ⇒ 记录数字按 §12 Q8 再议（可能是"值得做但收益有限"） |
| 3 | 无 GPU 机器上全部用例照旧绿；`PSLITE_RASTER=cpu` 时 GPU 一次不被调用 |
| 4 | 不碰 `Projection` / `Compositor` / `TileBuffer`（与 P1 同一纪律） |

**预期（明确标注为"待验证的假设"，不是结论）**：卷积族每像素几十次运算，传输只占
~0.8 ns/px 量级 ⇒ **预期 5–15×**。若实测**远低于**此，才说明本机核显的
「整图上传 + 回读」模式确实吃掉了收益 —— **那时才轮到"独显 / 零拷贝纹理"这个议题**
（注意：这与 §7.2.1.1 的教训一致 —— 先排除实现侧原因，再怀疑硬件）。

**实现增量很小**（P0 的设施全部现成）：
`supports()` 放宽到含 `CustomFilter` 的链 → 新增 `custom.comp` →
在 `GpuRasterBackend::runStack` 里按 `OpName` 分派到不同 kernel。
**不需要动**调度 / 降级 / 熔断 / 开关 / 用例门控 —— 那些已经过 P0 验证。

**⚠️ 与中值线的关系**：T1.13 已落地（§7.2.1.4：r=3 ≈2×）。P1′ 回答的是另一类样本。

#### 7.4.1 重测（P1′ + T2.7/T2.8；**T4.3 后更新**）

程序：`psDemo/tools/custom_e2e/`。**标准测法**：seed=7、warmup=2、同图；CPU 与比值共用同一次 CPU 均值。

**锐化核**（T4.3：shader 内解/预乘）：

| 尺寸 | CPU | GPU | GPU÷CPU |
|---|---|---|---|
| 512² | 12.4 ms | 2.8 ms | **4.43×** |
| 1024×768 | 33.0 ms | 4.8 ms | **6.95×** |
| 1600×1200 | 83.0 ms | 18.0 ms | **4.61×** |

**稠密盒** `{1×9}` / scale=9：

| 尺寸 | CPU | GPU | GPU÷CPU |
|---|---|---|---|
| 512² | 12.8 ms | 4.6 ms | **2.78×** |
| 1024×768 | 36.5 ms | 8.2 ms | **4.42×** |
| 1600×1200 | 78.7 ms | 19.7 ms | **4.00×** |

一致性：`gpucustom` **bit-exact（差 0）**。

**判定**：
- **T4.3 后锐化稳定 ≥4×**（跨 3×）；稠密中大图亦 ≥4×。仍 **opt-in**（产品未改默认）。
- 编组优化与 **T4.7** 已落地（§16.6）；下一优先 **T3.0 AddNoise** / T4.4 独显。
- **P2**：禁止整表铺开。

---

## 8. 测试策略（原 §10.2 缺口 4 的关闭结论）

| 层 | 做法 |
|---|---|
| **CPU 回归** | 全套用例**默认跑 CPU**；无 GPU 机器/CI 必须全绿 |
| **不放宽 CPU oracle** | 现有 bit-exact 断言（`diffs==0`）**一个都不改** |
| **GPU 一致性** | **新增**一个「有 GPU 才跑」的门控用例：`PSLITE_RASTER` 为 `gpu` **且**后端自报非 `cpu` 时才执行；门控不满足时**跳过并打印跳过原因**，不算失败 |
| **容差** | **P1（中值）目标 bit-exact**（中值取自输入已有像素，理论上可完全一致）。**允许逐通道 ≤1** 作为护栏；**不允许 >1 或结构性差异**。P2 浮点模糊类再单独评估，勿在 P1 预留「必须吃满容差」 |
| **降级 / 熔断** | 见 §3.4、§6.4、§6.6 |
| **压力** | 并发改动必须**全量连跑 ≥20 次** |
| **性能** | 一次性程序放 `.build-verify/`（不参与编译），**必须报端到端含传输** |

> **纪律**：GPU 收益必须报「端到端含传输」。只报 kernel 时间会得出错误结论
> —— 本项目已两次吃过"没量就改"的亏（预览降采样的 1/2 是白干、Box-Muller 慢 20 倍）。

---

## 9. 构建与验证命令（照抄可用）

```bat
:: 工具链
::   qmake  D:\QtSoftWare\QT6.9\6.7.3\mingw_64\bin\qmake.exe
::   make   D:\QtSoftWare\QT6.9\Tools\mingw1120_64\bin\mingw32-make.exe
:: 仓库根（本机）D:\CVlearningProject\gimp ；下面用 <repo> 占位

:: 测试（Release；构建目录已在 .gitignore）
cd <repo>\photoshopDemo
qmake psDemo\tests.pro -o .build-clean\Makefile -spec win32-g++
cd .build-clean && mingw32-make -f Makefile.Release -j8

:: 跑用例（无 GUI）
set QT_QPA_PLATFORM=offscreen
release\psdemo_tests.exe                    :: 全部（退出码 = 失败用例数）
release\psdemo_tests.exe rasterbackend      :: 只跑新用例

:: 主程序（干净重建 = 先删 .build-app 再 qmake）
qmake psDemo\psDemo.pro -o .build-app\Makefile -spec win32-g++
cd .build-app && mingw32-make -f Makefile.Release -j8
```

**本项目踩过的坑（务必遵守）**
1. 改 `app.pri` / `.pro` 后**必须重跑 qmake**，否则 `Makefile` 是旧的 ⇒ `undefined reference`；
2. `.pri` 里的相对路径按**包含它的 `.pro` 所在目录**解析（见 tests.md）；
3. 源码目录若有陈旧 `psDemo/ui_*.h` 会**遮蔽**新生成的（`-I../psDemo` 先于 `-I.`）；
4. **不要**用 PowerShell 来回读写含中文的文件（`Get-Content|Set-Content` 会毁编码）；
5. 新增 `OpName` 必须加在 **`Count` 之前**（`.pslite` 存的是枚举整数值）。

---

## 10. 文件级任务清单（执行方按序打勾）

### P0

- [x] **T0.1** 新建 `psDemo/engine/backend/rasterbackend.h`（§3.1：`ApplyStackResult` + `supports` + `applyStack`，含并发/几何注释）
- [x] **T0.2** 新建 `psDemo/engine/backend/cpurasterbackend.{h,cpp}`
      —— 搬入 **§2.1 的 ③–⑦**（几何 / 断言 / PsGraph / 取消→`Cancelled`+空图 / `evaluate`）；
      `supports` 恒 true；**行为与搬前 bit-exact**（①② 留在 FilterStack，见 §3.2）
- [x] **T0.3** 新建 `psDemo/engine/backend/rastercost.{h,cpp}`（§4.3 表 + 两个常量；未登记返回 `-1`）
- [x] **T0.4** 新建 `psDemo/engine/backend/rasterscheduler.{h,cpp}`
      （§4.2 + §3.3 + §3.4 + §6.3 + §6.4；决策须问 `supports`）
- [x] **T0.5** 改 `domain/filterstack.cpp` 的 `apply`（§3.2）：签名不变；取消仍返回空图
- [x] **T0.6** `psDemo/app.pri` 与 `psDemo/tests.pro` 登记新增 `.cpp`；**重跑 qmake**
- [x] **T0.7** 新建 `psDemo/tests/t_rasterbackend.cpp`（§6.6 全部断言）
- [x] **T0.8** `psDemo/tests/testmain.cpp` 登记 `rasterbackend` 用例
- [x] **T0.9** 跑通 §6.5 Acceptance：**27/27**；连跑 **20 次 0 崩溃**；主程序干净重建 0 error
- [ ] **T0.10**（**可选，不进 P0 DoD**）用 `.build-verify/` 把 §4.3「估值」项实测回填；
      测不了则填保守小值并标「待测」（§11）。可放到 P0 提交之后或 P1 前。
- [x] **T0.11** 文档：`pending-dev.md`、`engine/operators.md`（「后端调度」）、`features.md`、`tests.md`
- [x] **T0.12** 提交（文案要点见 §13）
### P1（产品修订：先接上，不声称 3×）

- [x] **T1.1** API 选型 spike（§7.2）—— 端到端未达 ≥3×（见 §7.2.1）；用户要求仍合并
- [x] **T1.2** `GpuRasterBackend`（QRhi D3D11；独立 RHI 线程；失败静默；单飞互斥）
- [x] **T1.3** 中值 compute（`median.comp` → `.qsb`；直通口径；越界=0；现为**计数选择**，见 T1.13）
- [x] **T1.4** 准入：仅全中值；调度惰性 `tryCreate`（仅 `PSLITE_RASTER=gpu`）
- [x] **T1.5** 一致性：`gpumedian` 门控（容差 ≤1）；**不**声称端到端 ≥3×
- [x] **T1.6** 文档：§7.2.1 + features / tests / pending-dev

#### P1 已完成（第二轮：bitonic + 池化 + opt-in + 重测）

- [x] **T1.7** `median.comp` → 按半径最小 2^k **bitonic**（全 `min/max`，无数据相关分支）；重测见 §7.2.1.2
- [x] **T1.8** 管线/缓冲**容量池化**（dst/ubuf 只增不减；src 每帧 Immutable 重建）；shader 缓存在 T1.15
- [x] **T1.9** **默认策略落地（方案 A）**：`Auto` = CPU；GPU 仅 `PSLITE_RASTER=gpu` opt-in
      （且 `ensureGpu()` 只在 ForceGpu 路径调用 ⇒ Auto 下**连设备都不创建**）
- [x] **T1.10** `rasterscheduler.cpp` 契约注释补回并指向 `t_rasterbackend` 断言名
- [x] **T1.11** `features.md` 如实写明收益与 opt-in 默认策略
- [x] **T1.12** 端到端重测写入 §7.2.1.2（r=3 ≈0.53× ⇒ 维持 opt-in）

#### P1 已完成（第三轮：计数选择 + 仓库卫生 + shader 缓存）

- [x] **T1.13** ★ **「排序」→「计数选择」**（§7.2.1.3）：两级计数；重测见 **§7.2.1.4**
      （r=3 ≈**1.9～2.5×**；`gpumedian` bit-exact）⇒ 保留 GPU 中值路径，**维持 opt-in**
- [x] **T1.14** ★ **仓库卫生**：计时源码迁入 `psDemo/tools/median_e2e/`；
      `psDemo/median_e2e.pro` 引用树内路径 ⇒ 干净 clone 可构建
- [x] **T1.15** shader 缓存：`Hub::cachedCs` + `ensureShader()`；`recreateSrcBinding()` 不再每帧
      `fromSerialized`。srb/pipe 仍随 Immutable src 重建（D3D11 约束；大图占比仍小）
- [x] **T1.16** `r≥4` 私有数组问题：**随 T1.13 自然关闭** —— 计数选择无 N=128 窗口数组；
      `supports()` **不**限 r≤3（kernel 内 `qBound(1,radius,8)`）

#### ★ P1′ 已完成（第二次证伪：卷积族）

- [x] **T2.0** 实测 CPU ns/px → 回填 §4.3（Custom ≈38、Gauss ≈19）
- [x] **T2.1** `shaders/custom.comp`（+ `.qsb`；直通、越界=0、alpha 取中心）
- [x] **T2.2** `supports()`：启用节点均在 {`MedianBlur`,`CustomFilter`}（整链，不半链混跑）
- [x] **T2.3** `runStack` 按 `OpName` 分派（双 shader 缓存 + 切换 pipe）
- [x] **T2.4** `gpucustom` 门控用例（bit-exact）
- [x] **T2.5** `tools/custom_e2e/` 重测 → **§7.4.1**
- [x] **T2.6** 判定：**1–3×** ⇒ 保留路径、维持 opt-in、**不整开 P2**

#### ★ 第五轮复审待办（已关闭）

- [x] **T2.7** ★ **统一 CPU 基线**（Q11）：标准测法 seed=7 / warmup=2 / 同图同 runs；
      根因是旧程序 T2.0 用 seed=11、T2.5 用 seed=7 且分两次计时。重报表见 §7.4.1 / §7.2.1.4
- [x] **T2.8** ★ **补测稠密核**（Q12）：`{1×9}/9` ⇒ **2.6～2.8×，未跨 3×**（假设被证伪）
- [x] **T2.9** ★ **下一优先改为 `AddNoise`**（Q13 / §4.4.1）；删「先 Gauss」残留

#### P2 待办（opt-in 下按 §4.4.1 **逐个**扩）

- [ ] **T3.0** ★ **`AddNoise` GPU**：`noiseHash`→GLSL（uint32 / 舍入对齐）；放宽位置相关 `supports`；
      ⚠️ 工程成本含调度契约，不只是 kernel
- [ ] **T3.1** `GaussianBlur` GPU（可分离 / 三次盒模糊）—— **排在 AddNoise 之后**
- [ ] **T3.2** 每个新增算子：门控用例 + e2e；bit-exact 或护栏 ≤1

#### ★ T4 收尾与验证

- [x] **T4.1** ★★ **提交 GPU 实现** —— **`1fe8dc3`**
- [x] **T4.2** ★ **三段计时探针** —— `PSLITE_GPU_STAGE=1` + `tools/stage_e2e/`；结果见 **§16.5**
- [x] **T4.3** ★ **①④ 进 shader** + 直传 `constBits()` —— 结果见 **§16.6**；门控 bit-exact
- [ ] **T4.4** **到别的机器上验证**（§16.3）：① 独显跑 e2e；② 无 GPU 机跑全套用例
- [ ] **T4.5** 核实 **`187 ns/px` vs 实测 207～239 ns/px** 的口径差（Q16）
- [ ] **T4.6** 明确记一笔 **GLES2 回退路径**的状态（shader 需 GL 4.3+/GLES 3.1+ SSBO，
      回退大概率不可用）：要么验证它、要么在 `initDevice()` 里去掉该回退并注明原因，
      **不要让读者以为有一条能用的 GL 回退**
- [x] **T4.7** ★ **近零成本扩算子覆盖**（2 → 5）：`Sharpen`/`Emboss` 复用 `custom.comp`（只换权重）；
      `Maximum`/`Minimum` 复用中值计数选择（`median.comp` 增 `kth`；0=中值）。
      **`FindEdges` 未进**：Sobel 幅值是非线性，喂不动 `custom.comp`；另开 kernel 才算，不在近零成本档。

---

## 11. 歧义时的默认选择（执行方不要自行发挥）

| 遇到 | 默认选择 |
|---|---|
| 要不要顺手把 `FilterEval::applyNode` 也接进后端？ | **不要**。只有整链进后端 |
| 要不要顺手改 `Projection`/`Compositor`/`TileBuffer`？ | **不要**。那是 P3，另一个量级 |
| 要不要顺便把 sRGB 换成线性空间？ | **不要**。那是独立决策（改语义），见 §12 Q1 |
| 要不要拆更多 kernel？ | **不要**。P1 只做中值，且只接受「全中值」的链 |
| 成本常量测不准？ | **先测再填**；测不了填保守小值（偏向 CPU）并标「待测」；不挡 P0 提交 |
| 某个算子不在表里？ | 返回 `-1` **让用例红** —— 不要静默返回 0 |
| GPU 还不支持这条链？ | `supports()==false`，**不要** `Failed`（§3.4） |
| `shouldAbort` 触发了？ | 返回 `Cancelled` + 空图；**不要**再跑 CPU |
| GPU 后端并发怎么办？ | **单队列 + 互斥（单飞）**；真正 Failed 才降级 CPU（§1.5） |
| 假后端放生产代码吗？ | **不要**。`setGpuBackendForTest` 仅测试，不进 UI |
| 要不要加 `#ifdef` 编译开关整段关掉？ | **不要**。用环境变量；编译开关会让"CPU 路径永远完整"难验证 |
| 日志用什么？ | `qDebug()`，格式照 §6.3 |
| 提交粒度？ | **P0 一次、P1 一次**，不要混 |
| 阈值改成 `≥` 让盒模糊上 GPU？ | **不要**在 P0 改；先测端到端再改表/阈值（§4.2） |
| GPU `Failed` 后要不要先给调度器拍个快照？ | **不要**。契约要求后端失败**无副作用**（最后一步才提交）；拍快照 = 35 MB memcpy ≈ 7 ms，会把收益吃掉（§3.4 纪律 1） |
| CPU 降级补算成功了，要不要清熔断计数？ | **不要**。只有 **GPU 自己 Ok** 才清零 —— 否则 GPU 一直失败、CPU 每次兜底成功 ⇒ 计数反复清零 ⇒ **熔断永不触发**（§3.4 纪律 3） |
| 要不要把 `PsGraph` 也搬到 GPU？ | **不要**。GPU 后端按节点顺序跑 kernel；只有链线性且 `supports` 全覆盖时才等价（§3.3） |
| CPU 后端返回了 `Failed` 怎么处理？ | **不回退**（会死循环）。CPU 是最后防线：`qWarning` + 返回空图（§3.4 纪律 2） |
| 插入排序够不够？ | **不够，也不许拿它当结论**。计划 T1.3 要的是**排序网络**（小半径）或**并行直方图 + 前缀和**；插入排序是数据相关分支，是 GPU 最差形状（§7.2.1.1） |
| 实测 0.9×，要不要默认启用 GPU？ | **不要**（推荐）。实测负收益就不该默认开；走 `PSLITE_RASTER=gpu` opt-in。见 §7.2.2 的三个方案 |
| 要不要把"传输占比高"写进根因？ | **不要**。传输 <1%（15.4 MB / 400 ms），根因在 kernel 算法与资源重建（§7.2.1.1） |
| 要不要顺手删掉看不懂的契约注释？ | **不要**。注释里的不变量是有价值的（台账 #21：假不变量最危险）；删除前先确认有等价断言，并把断言名写进注释 |
---

## 12. 需要人拍板的开放问题

| # | 问题 | 挡谁？ | 建议 |
|---|---|---|---|
| Q1 | 要不要顺便做线性空间（sRGB → linear）？ | **不挡** P0/P1 | 单独立项；会改旧文件观感 |
| Q2 | 容差定多少？ | **不挡**（§8 已有默认） | P1 目标 bit-exact，护栏 ≤1 |
| Q3 | `kMinPixelsForGpu = 100k`、阈值 `5 ns/px`？ | **不挡**（先用默认） | P1 有端到端数据再调 |
| Q4 | 图形 API 选哪个？ | **已关闭** | Spike：QRhi/OpenCL 都能接（§7.2.1）；当前实现选了 QRhi/D3D11 |
| Q5 | **目标平台有哪些？** | **暂定 Windows+MinGW** | 与主工程一致；跨平台待有独立 GPU 收益证据再开 |
| **Q6** | 默认策略：`Auto` 启用 GPU，还是 opt-in？ | **已关闭（T1.9）** | **opt-in 已落地**：`Auto` = CPU，GPU 走 `PSLITE_RASTER=gpu`（§7.2.2 方案 A） |
| **Q7** | 是否重做中值 kernel（排序网络）？ | **已关闭（T1.7）** | bitonic 见 §7.2.1.2；已被 T1.13 计数选择取代 |
| **Q8** | **是否做第二次证伪（P1′：卷积族）？** | **已关闭（T2.0–T2.6）** | 已做；§7.4.1：CustomFilter **2.3～3.1×**、bit-exact。落在 1–3× ⇒ 有价值、维持 opt-in、不整开 P2 |
| **Q9** | 若 P1′ 也 <1×，怎么收口？ | **未触发**（P1′ ≥1×） | 预留仍有效：若后续某算子 <1× 则撤该算子，勿默默留慢路径 |
| **Q10** | T1.13（中值计数选择）还做不做？ | **已关闭（T1.13）** | 已落地；§7.2.1.4：r=3 ≈**2×**（旧排序版 0.53×） |
| **Q11** | ★ **CPU 基线怎么统一？** | **已关闭（T2.7）** | 标准测法：seed=7、warmup=2、同图；CPU 与比值共用同一次均值。旧 38% 差来自不同 seed + 分表计时 |
| **Q12** | ★ **CustomFilter 要不要补测稠密核？** | **已关闭（T2.8）** | 已测：稠密盒 **2.6～2.8×，未跨 3×**。「必升/跨 3×」假设不成立（CPU 仅 +~10%） |
| **Q13** | ★ **下一优先到底是哪个？** | **已关闭（T2.9）** | **`AddNoise` → 再 `GaussianBlur`**（§4.4.1）；稠密核已测完，不再挡开工 |
| **Q14** | ★★ **GPU 代码什么时候提交？** | **已关闭（`1fe8dc3` / T4.1）** | GPU 后端已入库；独显机可 clone 构建，待 T4.4 实测 |
| **Q15** | ★ **要不要先优化 CPU 侧编组，而不是加算子？** | **已关闭（T4.2 / §16.5）** | **轻核（锐化）：是** —— CPU≈一半，先消 ①④。**重核（中值）：次要** —— 设备侧≈85～90%。②③ memcpy 几乎可忽略。**T4.3 优先 premul round-trip** |
| **Q16** | 成本表的 `187 ns/px` 对不对？ | 挡文档可信度 | 标准测法下实测 512² r=3 是 **207～239 ns/px**（偏高 10～28%），而全文档多处引用 187 ⇒ 需核实是"口径不同"还是"表偏乐观"（T4.5） |
---

## 13. 提交文案要点（P0）

```
feat: 滤镜链后端调度（可选加速后端）+ CPU 后端

- 新增 engine/backend/：RasterBackend（supports + ApplyStackResult）
  + CpuRasterBackend（搬入 FilterStack 图路径 ③–⑦，行为 bit-exact）
  + RasterCost + RasterScheduler（链级决策 / 降级 / 熔断 / 开关）
- FilterStack::apply 改走调度器；签名不变；取消仍返回空图（不重试、不计熔断）
- 决策按「链」：supports && Σ(ns/px) > 5 && 像素数门槛 ⇒ 才尝试 GPU
  （不支持 ≠ Failed，避免误熔断；单独盒模糊成本=5 不上 GPU）
- 准入：位置相关（杂色/波纹）与位移一律 CPU
- 硬约束：CPU 永在；启动不因 GPU 失败而失败；Failed 可降级；连续 Failed×3 熔断；不改 .pslite
- 开关 PSLITE_RASTER=cpu|gpu|auto、PSLITE_RASTER_LOG=1
- 新增 rasterbackend 用例（含取消 / supports / 熔断三条计数方向 / Failed 无副作用）
- 熔断只在 **GPU 自己 Ok** 时清零（CPU 降级补算的成功不清零，否则熔断永不触发）
- **本批未接入任何 GPU**：只做壳与调度（docs/gpu-plan.md §6）
- 全套 N/N 通过，连跑 20 次 0 崩溃，主程序 0 error
```

### 13.1 提交文案要点（**T4.1：GPU 中值 + 自定 3×3 入库**）

**必须如实写明收益与默认策略** —— 不要让 commit 历史读起来像"GPU 加速已达成"。
（历史版本曾写"0.9～1.1×、插入排序"，那是 P1 第一版，已不适用。）

```
feat: GPU 栅格后端（QRhi compute）—— 中值 + 自定 3×3，opt-in

- engine/backend/：RasterBackend（supports + ApplyStackResult 四态）
  + CpuRasterBackend + RasterCost + RasterScheduler + GpuRasterBackend
- GpuRasterBackend：QRhi/D3D11；独立 RHI 线程 + BlockingQueuedConnection；
  单飞互斥；失败静默且无副作用（image 与调用前逐字节相同）；不移植 PsGraph
- kernel：median.comp（两级计数选择 16 箱 → 16 档）、custom.comp（3×3 卷积；
  scale==0 不除、offset 在除法后、clamp、alpha 取中心；口径与 CpuRasterBackend 对齐）
- supports：启用节点全为 {MedianBlur, CustomFilter} 才放行（整链，不半链混跑）；
  位置相关（AddNoise/Ripple）与 Offset 一律 CPU
- 默认策略 opt-in：PSLITE_RASTER=auto(默认)=CPU，=gpu 才启用；=cpu 永不调用
  （Auto 下连设备都不创建）
- 熔断：连续 3 次真 Failed 禁用；只有 GPU 自己 Ok 才清零（CPU 降级补算不清零）
- 用例：rasterbackend（18 条断言，含取消/supports/三条计数方向/Failed 无副作用）
  + 门控 gpumedian / gpucustom（PSLITE_RASTER=gpu 且后端非 cpu 才跑，容差 ≤1）
- 计时程序入树：tools/median_e2e、tools/custom_e2e（T2.7 标准测法：seed=7/warmup=2/同图同 runs）
- ⚠️ 实测（Iris Xe，含上传回读，标准测法）：中值 r=3 ≈2.2～2.5×、r=1 ≈3.0×；
  自定锐化 ≈2.5～3.4×、稠密盒 ≈2.6～2.8×；均 bit-exact
- ⚠️ **不声称可默认启用**（未稳定 ≥3×）；仅 opt-in
- ⚠️ 未验证：独显 / 完全无 GPU 的机器；GLES2 回退路径大概率不可用（SSBO 需 GL 4.3+）
- 根因与判据见 docs/gpu-plan.md §4.4（GPU 友好度）、§7.2.1.3/§7.2.1.4、§7.4.1、§16
```

---

## 14. 缺口关闭记录

| 原缺口 / 复审问题 | 关闭结论 | 落在本文哪一节 |
|---|---|---|
| ① 图形 API 选型 | 保持为 spike；5 个必答问题（含构建链与目标平台） | §7.2 |
| ② 后端接口未含 ROI | 接口带 `srcRect`/`outRect`；另排除位置相关/位移；分块不必一律排除 | §3.1、§3.3 |
| ③ GPU 与条带并行 | 单队列 + 互斥（单飞）；后端自解并发 | §1.5、§7.3 |
| ④ 一致性 vs bit-exact oracle | 不放宽 CPU oracle；GPU 另开门控用例 | §8 |
| ⑤ 预乘格式 | 上传前/回读后均为 `ARGB32_Premultiplied` | §7.3 |
| ⑥ 整链 vs 只实现中值 | P1：`supports` 仅全中值；不做半链混跑 | §7.1 |
| （补）PsGraph 非整段 for 循环 | 搬迁对象是 ③–⑦ 图路径；①② 留 FilterStack | §2.1、§3.2、T0.2 |
| （复审）取消被 §3.2 样板弄丢 | `ApplyStackResult::Cancelled`；不重试、不计熔断；FilterStack 返回空图 | §3.1、§3.2、§3.4、§6.6 |
| （复审）不支持被当成 Failed 误熔断 | 必问 `supports()`；Unsupported/未调用 ≠ Failed | §3.4、§4.2、§6.4、§7.1 |
| （复审）T0.10 与 P0 工期打架 | 估值实测改为**可选**，不进 DoD | §10 T0.10、§11 |
| （复审）Q1–Q5 误挡 P0 | 仅 Q4（及平台相关 Q5）挡 P1；P0 可立刻开工 | §12、§15 |
| （复审）`Failed` 后 image 内容"未定义" | **契约改为「必须与调用前逐字节相同」**（最后一步才提交）；否则 CPU 在同图重算会读半成品 | §3.1、§3.4 纪律 1、§6.6 第 9 条 |
| （复审）CPU 后端 Failed 会死循环 | 明确 CPU 是最后防线：不回退，`qWarning` + 空图 | §3.4 纪律 2 |
| （复审）"成功清零"导致熔断永不触发 | 改为**只有 GPU 自己 Ok 才清零**；CPU 降级补算不清零；新增用例专钉此点 | §3.4 纪律 3、§6.4、§6.6 第 4 条 |
| （复审）GPU 是否要移植 PsGraph | **不移植**：GPU 后端按节点顺序跑 kernel；仅当链线性且 supports 全覆盖时等价 | §3.3 |
| （复审）spike 根因写成"传输占比高" | **不成立**：传输 <1%。真因是 kernel 用插入排序 + 每次重建资源 | §7.2.1.1 |
| （复审·第三轮）重测后根因又被写成"先减少 src 重建" | **仍不成立**：r=3 大图里 src 重建 + shader 反序列化 + PSO ≈ **<1%**；kernel ≈ **97%**，且跑在正常速率（0.10 ns/比较交换）。真因是**选了排序**：2688 次比较交换/像素 vs CPU 直方图的 O(n) | §7.2.1.3 |
| （复审·第三轮）「bitonic 比插入排序慢」的结论 | **不可直接相减**：两个数字来自不同计时程序，且元素数不同（49 vs 64，比较交换只差 12%）。只能得出"同量级" | §7.2.1.2 |
| （复审·第三轮）相对劣势随半径增大 | 排序 `O(n log²n)` vs 直方图 `O(n)` ⇒ **半径越大 GPU 越吃亏**（实测 r=1 2.11× → r=2 1.16× → r=3 0.53×）。与"半径越大越该上 GPU"的直觉相反 | §7.2.1.3 |
| （复审·第三轮）成本阈值已成死代码 | T1.9 改 opt-in 后阈值不参与决策 ⇒ §4.2 已改写；`stackCostNsPerPixel` 降级为**日志 + 覆盖性用例**，保留给方案 B | §4.2 |
| （复审·第三轮）`median_e2e.pro` 引用 gitignore 路径 | **干净 clone 无法构建**（`.build-verify/` 被忽略）⇒ 新增 T1.14 | §10 T1.14 |
| （复审·第三轮）T1.8 的池化不完整 | `recreateSrcBinding()` 每帧连 `srb`/`pipe` 一起重建并**重新反序列化 shader**；大图 <1%、r=1 小图约 10% ⇒ T1.15 **已关闭**（`cachedCs`） | §10 T1.15 |
| （复审·**第六轮**）**GPU 代码一行都没提交** | **已关闭（`1fe8dc3` / T4.1）** —— 见 §16.1 | **§16.1**、§10 T4.1 |
| （复审·第六轮）本机可执行性**已实测确认** | 主程序构建 **0 error**；常驻用例 **30/30**；两个门控用例真跑了；e2e r=3 **2.16～2.47×** | §0.1、§16.3 |
| （复审·第六轮）成本表的 `187 ns/px` 偏乐观 | 标准测法下实测 512² r=3 CPU 为 **207～239 ns/px** ⇒ 口径待核实 | §12 Q16、§10 T4.5 |
| （复审·第六轮）**CPU 侧编组可能比换卡更值** | **T4.2 已证实（§16.5）**：轻核 CPU≈半（几乎全是 ①④）；重核 device≈85～90%；②③ memcpy 可忽略 | **§16.5**、§12 Q15、§10 T4.3 |
| （复审·第六轮）`GaussianBlur` 误列为下一优先 | 已在 §4.4.1/T2.9 修正为 **`AddNoise`**（四问全过） | §4.4.1、§10 T2.9/T3.0 |
| （复审·第六轮）GLES2 回退路径疑似不可用 | shader 为 `#version 440` + `std430` SSBO（需 GL 4.3+/GLES 3.1+），而 `initDevice()` 会回退到 OpenGLES2；失败会安全降级 CPU，但**别让读者以为有条能用的 GL 回退** | §16.2、§10 T4.6 |
| （复审·第三轮）`r≥4` 的私有数组 2 KB/线程 | bitonic 时代问题；**T1.13 计数选择后自然关闭**（无 N=128 窗口数组） | §10 T1.16 |
| （复审·**第五轮**）T1.13 计数选择重测 | r=3 ≈**1.9～2.5×**、r=1 ≈**3.2×**；bit-exact；≥1× 停机条件未触发；仍 <3× ⇒ 维持 opt-in；中值线收口 | **§7.2.1.4**、§10 T1.13–16、§12 Q10 |
| （复审·第五轮）P1′ 卷积族重测 | `CustomFilter`（锐化核）**2.3～3.1×**、bit-exact；**预期 5–15× 未达到**（如实记录）；落 1–3× ⇒ 有价值但不默认启用 | **§7.4.1**、§12 Q8 |
| （复审·第五轮）**CPU 基线自相矛盾** | 根因：不同 seed + 分表计时。**T2.7 已关闭**：标准测法 seed=7/warmup=2 | §7.4.1、§12 Q11 |
| （复审·第五轮）"下一优先 GaussianBlur"与 §4.4 矛盾 | **T2.9 已关闭**：下一优先 = `AddNoise` | §4.4.1、§12 Q13 |
| （复审·第五轮）CustomFilter 只测了稀疏核 | **T2.8 已关闭**：稠密盒 **2.6～2.8×，未跨 3×**；「必升」假设不成立 | §7.4.1、§12 Q12 |
| （复审·**第四轮**）**中值被当成了"GPU 值不值得"的证伪样本** | **样本选错**：中值是"CPU 最擅长（滑窗增量 cum，O(n)）+ GPU 最别扭（排序/选择 + 大私有数组）"的组合。用**CPU 的 ns/px** 排序来选 GPU 目标，等于专挑 GPU 最不擅长的题 ⇒ **其否定答案只能覆盖中值这一类** | **§4.4（新增：GPU 友好度判据）**、§7.2.1.3 |
| （复审·第四轮）选型判据本身 | 改为**四问**（有无串行依赖 / 每像素算力是否高于 0.8 ns/px 传输线 / 访存是否规律 / 私有状态多大），并按此重排现有算子 | §4.4 |
| （复审·第四轮）下一步该做什么 | 新增 **P1′：用卷积族（`CustomFilter`，一个顶多个）做第二次证伪**；优先级**高于** T1.13 | §5、§7.4、§10 P1′、§12 Q8/Q10 |
| （复审·第四轮）判据建立在"估值"上 | `CustomFilter`/`GaussianBlur` 的 ns/px 一直是**猜的** ⇒ T0.10 从"可选"**升级为 P1′ 的前置**（T2.0） | §7.4、§10 T2.0 |
| （复审）表格称"管线/缓冲已池化" | 与代码不符（`bufBytes = 0` ⇒ 每次重建）；已按实际改正 | §7.2.1 |
| （复审）"换独显再测"的建议方向 | 改为**先换排序网络 + 真池化**，两步做完再谈换卡 —— 否则换卡只是让 O(n²) 内循环快一点 | §7.2.1.1 |
| （复审）合并后默认策略是"默认启用 GPU" | 实测 0.9× ⇒ 默认会让中值慢约 10%，与"有 GPU 就加速"的前提冲突 ⇒ **Q6 已关闭：T1.9 落地 opt-in**（`Auto` = CPU）。§7.3 第 8 条自动满足 | §7.2.2、§7.3 #8、§12 Q6 |
| （复审）P0 的四条契约是否落实 | **全部落实且有用例钉住**：`Failed` 无副作用（t_rasterbackend:366-398）、CPU `Failed` 不回退（scheduler:141-147）、**只有 GPU Ok 清零熔断**（scheduler:113-114，用例:238-250）、不移植 PsGraph | §3.4、§6.6 |
| （复审）调度器契约注释被删 | 行为与用例都在，但不变量变不可见 ⇒ 新增 T1.10 要求补回并指向断言名 | §10 T1.10、§11 |

---

## 15. 工期粗估（一人、熟悉本仓库；供排期，非承诺）

| 阶段 | 粗估 | 阻塞点 |
|---|---|---|
| P0 | 1–2 天 | 无（**可立刻开工**） |
| API spike | 2–4 天 | MinGW 依赖 / QRhi compute 能力 / 目标平台 |
| P1 | 5–10 天 | spike 结论；中值算法需 GPU 侧重写 |
| P2 | 每算子 0.5–2 天 | 排序统计类偏重 |
| P3 | 另开文档评估 | 本计划不做 |

**P0 可立刻开工**（不依赖 §12）。  
**禁止跳过 P0 写生产用 GPU kernel。**  
**P1 开工前必须关闭 Q4**（API spike 结论写回 §7.2）；Q5 随 spike 一并明确。Q1–Q3 不挡工期。

**（2026-10-12）** T2.7–T2.9 / T4.1–T4.3 关闭。Q11–Q15 关闭。维持 opt-in。
**下一优先：T4.4 独显验证** / T3.0 AddNoise（T4.7 ✅）。**禁止整表铺开。**

---

## 16. 还剩多少 / 独显机器上能不能跑（第六轮复审实测，2026-10-12）

> 这一节回答两个最常被问的问题，答案都**建立在实测上**（本轮我自己重跑了构建、用例与 e2e）。

### 16.1 入库状态（T4.1）

**已关闭**：`1fe8dc3` 已提交 `GpuRasterBackend` / shaders / e2e / 门控用例。  
别的机器可 clone 构建；还需 `PSLITE_RASTER=gpu` 与同版本 Qt/MinGW。

### 16.2 剩余工作量（三档，别按"算子个数"一刀切）

| 档 | 内容 | 量 |
|---|---|---|
| **基础设施** | 调度 / 降级 / 熔断 / opt-in / 门控用例 / e2e 入树 / shader 缓存 / 缓冲池化 | ✅ **基本齐了**（约 90%）；缺 **GPU 侧成本模型**（方案 B，仅在将来想恢复"自动判断"时需要） |
| **算子覆盖** | 约 **35 个滤镜算子**里 **5 个**走 GPU（中值/最大/最小 + 自定/锐化/浮雕；T4.7） | 扩容仍按四问判据，勿整表铺 |
| **验证** | 只在**核显（Iris Xe）**上测过 | ❌ 独显 / **完全无 GPU 的机器** / Linux·macOS 都没测；`initDevice()` 的 **GLES2 回退路径未验证**（shader 用 `#version 440` + `std430` SSBO，需 GL 4.3+ / GLES 3.1+，回退大概率不可用 —— 失败会安全降级 CPU，但那条路等于摆设） |
| **P3（瓦片/合成上 GPU）** | 整块没做 | 明确不在本计划内（§8「不做的事」） |

**算子分档（§4.4.1 的四问判据）**：

| 档 | 算子 | 成本 |
|---|---|---|
| **近乎零成本**（复用现成 kernel） | `Sharpen` / `Emboss` ⇒ `custom.comp`；`Maximum` / `Minimum` ⇒ `median.comp`+`kth`（**T4.7 ✅**）。`FindEdges` 是 Sobel 幅值，**不在此档** | 小（已做） |
| **值得做**（4 个） | `AddNoise`（**下一优先**）、`GaussianBlur`、`BoxBlur`、`MotionBlur` | 中 |
| **不值得** | 点算子（传输就吃掉）、`Offset` | — |

### 16.3 独显机器上能否执行 GPU 加速 —— 分三层

| 层 | 结论 |
|---|---|
| **(1) 现在** | ✅ **能 clone 构建**（`1fe8dc3`）；独显上是否更快 → **待 T4.4 实测** |
| **(2) 提交后能否跑** | ✅ **能**。`initDevice()` 首选 **D3D11**；无设备 ⇒ 静默降级 CPU。需 **`PSLITE_RASTER=gpu`**（默认 Auto=CPU）+ 同版本工具链 |
| **(3) 会不会更快** | ⚠️ **很可能，但未测** —— 见下面的估算（**明确标注为估算，非实测**） |

**为什么"很可能更快"，以及上限在哪**：

```
端到端 = CPU 侧编组 + 传输 + kernel
         ↑ 前两项不随显卡变快    ↑ 只有这项变快
核显上 2.2～2.5×，其中 kernel 是主要部分；
独显算力若 5–10×，kernel 会缩到很小，然后被固定的「编组 + 传输」封顶
⇒ 粗估落到 4–6×，很可能跨过 3× 门槛
```

**必须实测才算数** —— 这正是计划里一直挂着没答的那条（§7.2.1 问 4「未测完全无设备」+ §15「独显上重跑」）。

### 16.4 ★ 一条**与显卡无关**、可能比换卡更值的优化：CPU 侧编组

GPU 路径在 CPU 侧要遍历全图 **4 趟**：

| 趟 | 位置 | 能否消掉 |
|---|---|---|
| ① 解预乘 `toStraightArgb32` | `gpurasterbackend.cpp` | 可挪进 shader（或直接传预乘、在 shader 里解） |
| ② pack memcpy（拼进 `std::vector`） | `runMedianOnce`/自定 | **可直接上传 `QImage::constBits()`**（整图连续） |
| ③ 回读 memcpy | 读回 scanline | **可让回读缓冲直接当 QImage**（`QImage(ptr,…)`） |
| ④ 预乘 `toPremul` | 结尾 | 可让 shader 直接输出预乘 |

**为什么这条重要**：T2.8 已说明 CPU 成本被"解预乘/预乘 + 访存"主导；GPU 路径也在付这份钱。
**T4.2 已实测（§16.5）**：②③ memcpy **几乎可忽略**；真正大的是 **①④**。轻核先做 T4.3；重核收益有限。

### 16.5 ★ T4.2 实测（`tools/stage_e2e`，`PSLITE_GPU_STAGE=1`，本机核显）

`device` = upload + compute + readback + finish（未再拆）。稳态取 timed 段（warmup 后）。

| 场景 | unpremul | pack | **device** | unpack | premul | **cpu%** | **device%** |
|---|---:|---:|---:|---:|---:|---:|---:|
| 中值 r=3 512² | ~1.5 | ~0.1 | **~20** | ~0.6 | ~0.9 | **~13%** | **~87%** |
| 中值 r=3 1600×1200 | ~10 | ~1 | **~158** | ~3 | ~8 | **~12%** | **~88%** |
| 锐化 512² | ~1.0 | ~0.1 | **~2.6** | ~0.4 | ~1.0 | **~48%** | **~52%** |
| 锐化 1600×1200 | ~8 | ~1 | **~15** | ~2.5 | ~7 | **~55%** | **~45%** |

单位 ms/call。首帧 device 含 pipeline 冷启动（中值 ~350 ms、锐化 ~64 ms），不计。

**结论**：
1. **轻核（锐化）**：CPU 编组 ≈ **一半**；其中几乎全是 **①④**；消掉后端到端可接近再翻倍。
2. **重核（中值）**：设备侧主导；编组优化最多 ~10～15%。
3. **②③ memcpy 不是瓶颈**（合计常 <2%）；T4.3 可顺手消，但不要当主战场。
4. **Q15 关闭** → 已做 **T4.3**；见 §16.6。

### 16.6 ★ T4.3 实测（解/预乘进 shader + 直传 bits）

**改动**：`median.comp` / `custom.comp` 采样 `qUnpremultiply`、写出 `qPremultiply`（对齐 Qt）；
CPU 只 `ensurePremulArgb32`；连续图直传 `constBits()`，回读整块 `memcpy`。

**分段（`stage_e2e`，稳态）**：`format≈0`、`pack≈0`；CPU 仅剩 unpack（~1–3%）。

| 场景（标准测法） | T4.2 前 | **T4.3 后** |
|---|---|---|
| 锐化 512² / 1024×768 / 1600×1200 | ≈2.5～3.4× | **4.43× / 6.95× / 4.61×** |
| 稠密盒 同上 | ≈2.6～2.8× | **2.78× / 4.42× / 4.00×** |
| 中值 r=3 512² / 1024×768 / 1600×1200 | ≈2.1～2.9× | **2.13× / 2.08× / 2.26×** |

门控 `gpumedian` / `gpucustom`：**逐通道差 0**（bit-exact）。

**结论**：轻核已跨 3×，仍 **opt-in**（产品未改默认）；重核收益有限（设备侧主导）。  
下一优先不再是编组，而是 **T4.4 独显** / **T3.0 AddNoise**（T4.7 已完成）。