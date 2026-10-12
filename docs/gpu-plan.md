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
/** 每算子的「每像素成本」（纳秒）。**-1 = 未登记**（用例会红，强制补表）。 */
int opCostNsPerPixel(OpName name);

constexpr int kGpuChainCostThresholdNsPerPixel = 5; ///< 见 §4.2（严格大于）
constexpr int kMinPixelsForGpu = 100 * 1000;        ///< 见 §4.3
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

### 4.2 判据

一次往返（8.7 Mpx ≈ 35 MB，PCIe ~10 GB/s）≈ 7 ms ≈ **0.8 ns/px**；像素数两边成比例会约掉，
于是判据化简为**链的静态属性**：

```
shouldUseGpu =  开关 != "cpu"
             && gpuBackend 存在 && isAvailable()
             && !熔断中
             && gpuBackend->supports(stack)                             // §3.3 / §7.1；不支持≠失败
             && !stack.hasPositionalOp() && !stack.containsOffset()     // 与 supports 双重保险
             && pixelCount >= kMinPixelsForGpu                          // = image 像素数（含 pad）
             && Σ(opCostNsPerPixel(每个**启用**节点)) > kGpuChainCostThresholdNsPerPixel
```

> **严格 `>`，不是 `≥`**：单独 `BoxBlur`（表内恰 5 ns/px）**不会**触发 GPU——落在临界线上，偏向 CPU。
> 若将来实测盒模糊端到端仍赚，再改表或改阈值，不要默默改成 `≥`。
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
| `GaussianBlur` | 20 | 估值（三次盒模糊） |
| `CustomFilter` | 20 | 估值（3×3 + 权重读取） |
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
---

## 5. 分期总览

| 期 | 内容 | 前置 | 决策点 |
|---|---|---|---|
| **P0** | 壳与调度（**不写任何 GPU 代码**） | 无 | 做完即有价值，**建议立刻执行** |
| **P1** | 只做**中值**一个 GPU kernel | P0 + API spike | **端到端 ≥3× 才继续**；不到就停在 P0 |
| **P2** | 按成本表逐算子铺开 | P1 达标 | 每算子单独验收 |
| **P3** | 绘制算子特例（另一量级） | 另行评估 | 只有"拖动/绘制也要快"成为真实需求才启动 |

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

#### 7.2.1 Spike 结论（2026-10-12，本机）—— **停在 P0，不进 P1 生产代码**

| # | 问题 | 结论 |
|---|---|---|
| 1 | MinGW + qmake 无痛接入？ | **QRhi：可以**（`QT += gui-private`，D3D11）。**OpenCL：可以**（自带 `OpenCL.dll` + Khronos 头 + `dlltool` 导入库；无 NVIDIA/AMD SDK） |
| 2 | compute / 中值内核？ | **两者都能跑**；插入排序 7×7 窗口，与朴素 CPU **bit-exact（diffs=0）** |
| 3 | 端到端 ≥3×？ | **否**。见下表 |
| 4 | 无 GPU 可捕获？ | 本机有 Iris Xe，未测「完全无设备」；调度器侧 P0 已覆盖「无后端 → CPU」 |
| 5 | 目标平台 | **当前以 Windows + MinGW 为准**（与主工程一致） |

**实测**（中值 r=3；GPU 计时 = 上传 + kernel + 回读；管线/缓冲已池化；生产 CPU 按既有 **187 ns/px** 估算）：

| 尺寸 | API | GPU 端到端 | vs 生产 CPU 估算 | vs 朴素 nth_element |
|---|---|---|---|---|
| 512² | QRhi/D3D11 | ~53 ms | **0.93×** | ~3.3×（朴素偏慢，不能当门槛） |
| 1024×768 | QRhi/D3D11 | ~162 ms | **0.91×** | ~2.9× |
| 1600×1200 | QRhi/D3D11 | ~400 ms | **0.90×** | ~2.9× |
| 1024×768 | OpenCL (Iris Xe) | ~135 ms | **1.09×** | ~3.1× |

**判定**：门槛是「相对**生产** CPU ≥3×」。QRhi / OpenCL 在本机集成显卡上端到端约 **0.9～1.1×**，**未达标**。  
按 §5 / §7.3：**停在 P0**，不合并生产用 `GpuRasterBackend`。Spike 代码在 `.build-verify/gpu_spike/`（已 gitignore）。

**根因判断（供日后复测）**：集成 GPU 上「整图上传 + 回读」占比过高；中值虽重，但生产 CPU（滑窗直方图 + 增量 cum）已经很快。若将来换独立显卡/零拷贝纹理路径，应**重跑本 spike**，不要直接开工。

### 7.3 P1 的 Acceptance

| # | 验收项 |
|---|---|
| 1 | 中值结果与 CPU 一致（容差见 §8） |
| 2 | **端到端（含传输）≥ 3×** CPU；**不到 3× 就停在 P0**，并把实测数据写进本文档 |
| 3 | 无 GPU 机器上全部用例照旧绿 |
| 4 | `PSLITE_RASTER=cpu` ⇒ GPU 一次都不被调用 |
| 5 | 显存不足 / 提交失败 ⇒ 逐次退回 CPU，结果正确（硬约束 3） |
| 6 | **上传前与回读后格式都必须是 `Format_ARGB32_Premultiplied`**（原 §10.2 缺口 5） |
| 7 | 多线程并发调用不崩（跑 `filterasync`/`posasync`/`jobteardown` + 全量连跑 ≥20 次） |

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
### P1（P0 完成后另起）

- [x] **T1.1** API 选型 spike（§7.2）—— **已结论：端到端未达 ≥3×，停在 P0**（见 §7.2.1）
- [ ] **T1.2** GPU 后端骨架 —— **搁置**（门槛未过）
- [ ] **T1.3** 中值 kernel —— **搁置**
- [ ] **T1.4** 准入 —— **搁置**
- [ ] **T1.5** Acceptance —— **搁置**（spike 已证明达不到）
- [x] **T1.6** 文档：结论写回 §7.2.1

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
---

## 12. 需要人拍板的开放问题

| # | 问题 | 挡谁？ | 建议 |
|---|---|---|---|
| Q1 | 要不要顺便做线性空间（sRGB → linear）？ | **不挡** P0/P1 | 单独立项；会改旧文件观感 |
| Q2 | 容差定多少？ | **不挡**（§8 已有默认） | P1 目标 bit-exact，护栏 ≤1 |
| Q3 | `kMinPixelsForGpu = 100k`、阈值 `5 ns/px`？ | **不挡**（先用默认） | P1 有端到端数据再调 |
| Q4 | 图形 API 选哪个？ | **已关闭** | Spike：QRhi/OpenCL 都能接，但**端到端未达 3× → 不选、不停在实现**（§7.2.1） |
| Q5 | **目标平台有哪些？** | **暂定 Windows+MinGW** | 与主工程一致；跨平台待有独立 GPU 收益证据再开 |
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