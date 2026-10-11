# GPU 加速开发计划（**可选加速后端**）—— 可执行规格

> 关联：[filter-roadmap.md](filter-roadmap.md)、[engine/operators.md](engine/operators.md)、
> [engine/roi.md](engine/roi.md)、[graph-engine.md](graph-engine.md)、[dev-plan.md](dev-plan.md)、[tests.md](tests.md)。
>
> **本文用途**：交给执行方（Cursor）按 §10 的清单施工。§11 是歧义时的默认选择表，
> §12 是需要人拍板的问题，§14 记录了「原 §10.2 六个缺口」的关闭结论。
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
| `RasterScheduler` | 决策只读共享状态；熔断计数用 `std::atomic<int>` |
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

**因此 P0 的动作是**：把**这一段（①–⑦）整段搬进** `CpuRasterBackend::applyStack`，
**不是**搬一个循环体。搬完必须与搬前 **bit-exact**（既有全量用例兜底）。

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

/**
 * 栅格后端：把**整条滤镜链**作用在一张 QImage 上。
 *
 * 【并发】实现必须**并发安全** —— `LayerFilterJob` 会多线程同时调用（见 gpu-plan.md §1.5）。
 *
 * 【几何】@p srcRect / @p outRect 用**文档坐标**（与 FilterStack::apply 的约定一致）：
 * 输入图 = srcRect 抽出来的那块，outRect ⊆ srcRect 是合法输出区。
 * CPU 后端把它们原样透传给图里的 PointFilterOp（见 §2.1 步骤 ⑤/⑦）。
 * 传空矩形 = 整张图。
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
     * 就地把整条链作用在 @p image 上（图路径的等价实现，见 §2.1）。
     * @return false = **本次失败**：调度器必须退回 CPU 后端重算。
     *         实现内部**不得**自行回退 —— 回退逻辑只在一处（调度器）。
     */
    virtual bool applyStack(QImage &image, const FilterStack &stack, const QRect &srcRect,
                            const QRect &outRect,
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

    /** 执行整条链：按决策挑后端，失败则退回 CPU 重算。@return 实际生效的后端 id。 */
    QString runStack(QImage &image, const FilterStack &stack, const QRect &srcRect,
                     const QRect &outRect, const std::function<bool()> &shouldAbort);

    /** 决策（纯函数，可单测）。 */
    bool shouldUseGpu(const FilterStack &stack, const QSize &size) const;

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

constexpr int kGpuChainCostThresholdNsPerPixel = 5; ///< 见 §4.2
constexpr int kMinPixelsForGpu = 100 * 1000;        ///< 见 §4.3
}
```

### 3.2 `FilterStack::apply` 的改法（签名与行为不变）

```cpp
QImage FilterStack::apply(const QImage &source, const QRect &srcRect, const QRect &outRect,
                          const std::function<bool()> &shouldAbort) const
{
    if (source.isNull() || !hasEnabled())
        return source;                                   // 早退保持在原地
    QImage out = source;
    if (out.format() != QImage::Format_ARGB32_Premultiplied)
        out = out.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    RasterScheduler::instance().runStack(out, *this, srcRect, outRect, shouldAbort);
    return out;
}
```

（另两个重载继续委托给它，**不动**。）

### 3.3 GPU 后端的**额外准入条件**（原 §10.2 缺口 2 的关闭结论）

几何**照传**（接口带 `srcRect/outRect`），但 GPU 后端额外要求：

```
链里**不含**位置相关算子（`hasPositionalOp()`：AddNoise / Ripple）与位移（`containsOffset()`：Offset）
```

原因：这两类依赖**输入图的绝对原点**（噪声按 `(x,y,seed)` 取、位移按取模取「别处」），
而 P1 的 GPU 实现只处理「位置无关」的链。`shouldUseGpu` 直接返回 `false`，走 CPU。

> 这条比「④ 只允许 src==out 走 GPU」更好：分块路径（`srcRect != outRect`）**不必**被排除，
> 因为几何是透传的、中值本身位置无关；而且像素数门槛（§4.3）通常已经把分块请求筛掉了。

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
             && !stack.hasPositionalOp() && !stack.containsOffset()     // §3.3
             && size.width()*size.height() >= kMinPixelsForGpu
             && Σ(opCostNsPerPixel(每个**启用**节点)) > kGpuChainCostThresholdNsPerPixel
```

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

> **注 1（位置相关算子）**：`AddNoise` / `Ripple` 现在走「整层求值」（`FilterStack::hasPositionalOp`）。
> 它们不影响上表判据（§3.3 已直接排除），P2 做它们的 GPU 版时要单独处理
> —— GPU 上纯散列无状态，反而更简单。
>
> **注 2（覆盖性）**：未登记返回 `-1`，**用例会红**（§6.6 第 6 条）。这样将来新增算子
> （例如批次 A/B 正在加的 `Ripple`、`Solarize`…）**不可能被漏掉**。

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
成功一次          ⇒  计数清零
失败定义          = GPU 后端 applyStack 返回 false
计数              = std::atomic<int>（多线程安全）
禁用后            = shouldUseGpu() 恒 false；不再调用 GPU 后端
复位              = 仅 resetForTest()（测试用）；生产需重启进程
```

### 6.5 P0 的 Acceptance（DoD）

| # | 验收项 | 怎么验 |
|---|---|---|
| 1 | **既有全部用例绿** | 跑全套；此时后端只有 CPU，行为必须逐像素不变 |
| 2 | 全量连跑 **≥20 次 0 崩溃** | 循环跑（§1.5） |
| 3 | 主程序**干净重建** 0 warning / 0 error | §9 命令 |
| 4 | `PSLITE_RASTER=cpu` ⇒ GPU 一次都不被调用 | 假后端计次 == 0 |
| 5 | `PSLITE_RASTER=gpu` 且无 GPU ⇒ 不报错、静默走 CPU | 无 GPU 机器跑全套 |
| 6 | 假后端「总是失败」⇒ **每次结果仍正确** | 与 CPU 结果逐像素一致 |
| 7 | 假后端失败 3 次后不再被调用 | 调用次数 == 3 |
| 8 | 成本表覆盖所有 `OpName < Count`（无 -1） | 新用例断言 |
| 9 | `shouldUseGpu` 边界正确 | 见 §6.6 第 5 条 |

### 6.6 P0 新增用例 `rasterbackend` 的断言清单

1. 默认（`auto`）+ 无 GPU ⇒ `runStack` 返回 `"cpu"`，结果与直接 CPU 计算一致；
2. `PSLITE_RASTER=cpu` ⇒ GPU 后端调用次数 == 0；
3. 假后端「总是失败」⇒ 结果仍与 CPU 一致；失败 3 次后不再被调用；
4. 假后端「先失败后成功」⇒ 计数清零；
5. `shouldUseGpu`：
   - 纯点算子链（亮度 + 反相）⇒ `false`
   - 含中值的链 ⇒ `true`
   - 像素数 < `kMinPixelsForGpu` ⇒ `false`
   - 含 `AddNoise` 或 `Offset` ⇒ `false`（§3.3）
   - 熔断后 ⇒ `false`
6. 成本表覆盖性：`for i in [0, Count)` ⇒ `opCostNsPerPixel != -1`；
7. **带几何的重载**（`apply(source, srcRect, outRect, abort)`）也走后端，且结果与搬前一致。

---

## 7. P1 详细规格（只做中值）

### 7.1 准入条件（原 §10.2 缺口 6 的关闭结论）

```
P1 的 GPU 后端**只接受**「启用节点**全是** MedianBlur」的链（可多个中值节点）。
否则 `shouldUseGpu` 返回 false → CPU。
```

**明确不混跑半链**：一个节点 CPU、一个节点 GPU 的交错执行**不做**（那会引入多次往返，
正是 §4.1 要避免的）。扩算子集合是 P2 的事。

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
| **容差** | **目标 bit-exact**（中值选的是输入里已有的像素值，理论上可完全一致）；**允许逐通道 ≤1**；**不允许 >1 或结构性差异** |
| **降级 / 熔断** | 见 §6.4、§6.6 |
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

- [ ] **T0.1** 新建 `psDemo/engine/backend/rasterbackend.h`（§3.1 签名照抄，含并发与几何注释）
- [ ] **T0.2** 新建 `psDemo/engine/backend/cpurasterbackend.{h,cpp}`
      —— 把 `filterstack.cpp` 里 **§2.1 的 ①–⑦ 整段**搬入（含 `PsGraph` 构建与 `evaluate`），
      **行为与搬前 bit-exact**
- [ ] **T0.3** 新建 `psDemo/engine/backend/rastercost.{h,cpp}`（§4.3 表 + 两个常量；未登记返回 `-1`）
- [ ] **T0.4** 新建 `psDemo/engine/backend/rasterscheduler.{h,cpp}`（§4.2 判据 + §3.3 准入 + §6.3 开关日志 + §6.4 熔断）
- [ ] **T0.5** 改 `domain/filterstack.cpp` 的 `apply`（§3.2）：签名与三个重载的对外行为不变
- [ ] **T0.6** `psDemo/app.pri` 与 `psDemo/tests.pro` 登记新增 `.cpp`；**重跑 qmake**
- [ ] **T0.7** 新建 `psDemo/tests/t_rasterbackend.cpp`（§6.6 的 7 条断言）
- [ ] **T0.8** `psDemo/tests/testmain.cpp` 登记 `rasterbackend` 用例
- [ ] **T0.9** 跑通 §6.5 的 9 条 Acceptance（含 ≥20 次连跑、主程序干净重建）
- [ ] **T0.10** 用一次性程序（`.build-verify/`）把 §4.3 标「估值」的项**实测并回填**
- [ ] **T0.11** 文档：`pending-dev.md`（本轮留痕）、`engine/operators.md`（补「后端调度」一节）、
      `features.md`（一行）、`tests.md`（用例表 + 计数）
- [ ] **T0.12** 提交（文案要点见 §13）

### P1（P0 完成后另起）

- [ ] **T1.1** API 选型 spike（§7.2 的 5 个问题），结论写回 §7.2
- [ ] **T1.2** GPU 后端骨架：惰性初始化 / 上传 / 回读 / **设备缓冲池** / 单队列（§1.5）
- [ ] **T1.3** 中值 kernel —— 注意现在的「滑窗直方图 + 增量 cum」沿行**串行**，是 CPU 算法形状；
      GPU 上要换**排序网络**（小半径）或**并行直方图 + 前缀和**。**不是移植，是重写。**
- [ ] **T1.4** 准入：仅「全为中值」的链（§7.1）
- [ ] **T1.5** 跑通 §7.3 的 7 条 Acceptance；**不到 3× 就停在 P0**
- [ ] **T1.6** 文档 + 提交

---

## 11. 歧义时的默认选择（执行方不要自行发挥）

| 遇到 | 默认选择 |
|---|---|
| 要不要顺手把 `FilterEval::applyNode` 也接进后端？ | **不要**。只有整链进后端 |
| 要不要顺手改 `Projection`/`Compositor`/`TileBuffer`？ | **不要**。那是 P3，另一个量级 |
| 要不要顺便把 sRGB 换成线性空间？ | **不要**。那是独立决策（改语义），见 §12 Q1 |
| 要不要拆更多 kernel？ | **不要**。P1 只做中值，且只接受「全中值」的链 |
| 成本常量测不准？ | **先测再填**；测不了填保守小值（偏向 CPU）并标「待测」 |
| 某个算子不在表里？ | 返回 `-1` **让用例红** —— 不要静默返回 0 |
| GPU 后端并发怎么办？ | **单队列 + 互斥（单飞）**；失败则该次降级 CPU（§1.5） |
| 假后端放生产代码吗？ | **不要**。`setGpuBackendForTest` 仅测试，不进 UI |
| 要不要加 `#ifdef` 编译开关整段关掉？ | **不要**。用环境变量；编译开关会让"CPU 路径永远完整"难验证 |
| 日志用什么？ | `qDebug()`，格式照 §6.3 |
| 提交粒度？ | **P0 一次、P1 一次**，不要混 |

---

## 12. 需要人拍板的开放问题

| # | 问题 | 影响 | 建议 |
|---|---|---|---|
| Q1 | 要不要顺便做线性空间（sRGB → linear）？ | **语义变更**：旧 `.pslite` 重开结果会变（模糊变亮）。**不放进 P0/P1** | 单独立项；要么加颜色空间版本标记，要么接受一次性变化 |
| Q2 | 容差定多少？ | §8 | 目标 bit-exact，允许 ≤1 |
| Q3 | `kMinPixelsForGpu = 100k`、阈值 `5 ns/px` 合适吗？ | GPU 何时介入 | 先按此值，P1 拿到实测再调 |
| Q4 | 图形 API 选哪个？ | P1 的门槛 | 由 §7.2 spike 决定，**不要提前赌** |
| Q5 | **目标平台有哪些？** | 影响 API 与动态加载策略 | 需明确（仅 Windows？还是含 Linux/macOS？） |

---

## 13. 提交文案要点（P0）

```
feat: 滤镜链后端调度（可选加速后端）+ CPU 后端

- 新增 engine/backend/：RasterBackend 接口 + CpuRasterBackend
  （把 FilterStack::apply 的 PsGraph 构建与求值整段搬入，行为 bit-exact）
  + RasterCost 成本表 + RasterScheduler（探测/决策/降级/熔断/开关）
- FilterStack::apply 三个重载改走调度器；签名与对外行为不变
- 决策按「链」而非「算子」：Σ(每算子 ns/px) > 5 ⇒ 整链走 GPU
  （既然要上传，点算子一起带上更省 —— 逐算子决策会多付一次往返）
- 准入：含位置相关算子（添加杂色/波纹）或位移的链一律 CPU
- 硬约束：CPU 路径永远完整（正确性基准 + 降级路径）；启动不因 GPU 失败而失败；
  逐次可降级；连续失败 3 次熔断；不改 .pslite
- 开关 PSLITE_RASTER=cpu|gpu|auto、PSLITE_RASTER_LOG=1
- 新增 rasterbackend 用例（7 条断言）
- **本批未接入任何 GPU**：只做壳与调度（见 docs/gpu-plan.md §6）
- 全套 N/N 通过，连跑 20 次 0 崩溃，主程序 0 warning
```

---

## 14. 缺口关闭记录（原 §10.2 六条 → 结论）

| 原缺口 | 关闭结论 | 落在本文哪一节 |
|---|---|---|
| ① 图形 API 选型 | 保持为 spike；**补充了 5 个必答问题**（含构建链兼容性与目标平台） | §7.2 |
| ② 后端接口未含 `srcRect`/`outRect` | **接口带上几何（文档坐标，透传）**；另加 GPU 准入条件「不含位置相关/位移算子」。**分块路径不必排除** | §3.1、§3.3 |
| ③ GPU 与条带并行的关系 | 定为**单队列 + 互斥（单飞）**，后端自己解决并发，**不许要求调用方串行化**；并写入 §1.5 线程模型 | §1.5、§6.4、§7.3 |
| ④ 一致性测试与 bit-exact oracle 冲突 | **不放宽 CPU oracle 一条断言**；新增「有 GPU 才跑」的门控用例（不满足则跳过并打印原因） | §8 |
| ⑤ 预乘格式契约 | 定为**上传前与回读后都必须 `Format_ARGB32_Premultiplied`**，写进 P1 验收 | §7.3 第 6 条 |
| ⑥ 「整链可 GPU」vs「只实现中值」 | 定为 **P1 = 仅当启用节点全是中值** 才走 GPU；**明确不做半链混跑** | §7.1 |
| （补）`FilterStack::apply` 不是循环而是 PsGraph | 已核实并写进 §2.1；P0 的搬迁对象是**整段图路径**，不是循环体 | §2.1、§10 T0.2 |

---

## 15. 工期粗估（一人、熟悉本仓库；供排期，非承诺）

| 阶段 | 粗估 | 阻塞点 |
|---|---|---|
| P0 | 1–2 天 | 无（**可立刻开工**） |
| API spike | 2–4 天 | MinGW 依赖 / QRhi compute 能力 / 目标平台 |
| P1 | 5–10 天 | spike 结论；中值算法需 GPU 侧重写 |
| P2 | 每算子 0.5–2 天 | 排序统计类偏重 |
| P3 | 另开文档评估 | 本计划不做 |

**缺信息未补齐前（§12 的 Q1–Q5），禁止跳过 P0 直接写生产用 GPU kernel。**
