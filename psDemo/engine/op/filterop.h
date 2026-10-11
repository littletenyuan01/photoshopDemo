/**
 * filterop.h — 滤镜算子基类 + **每线程**常驻实例表（engine/op 层）。
 *
 * 【为什么另立一族，而不是复用 BufferOp】
 * 项目原有两族算子：
 *  · `BufferOp` —— 作用在 `TileBuffer`（绘制类，**直接改层瓦片**），由 `OpRunner` 驱动；
 *  · `PointOp`  —— 逐样本点算子，无临时资源，由合成热路径直接调用。
 * 滤镜（`FilterNode` 链）作用在一张**临时 QImage**（层栅格 / 调整层的下方合成结果）上，
 * 且**绝不写回层瓦片**。若硬塞进 `BufferOp`，就得让 `OpContext` 同时装 `TileBuffer` 与 `QImage`，
 * 两种「资源」形态混在一起 —— 所以单列一族，调用点是 `FilterEval::applyNode`。
 *
 * 【生命周期】对照 BufferOp 的三段式：
 *   prepare → process → finish
 *  · `prepare`：申请/校验本次需要的资源（临时缓冲、LUT…）。**失败则跳过 process，仍会 finish**。
 *  · `process`：就地处理图像。
 *  · `finish`：释放**不该跨调用保留**的资源（无条件调用，≈ finally）。
 *
 * 注意「常驻实例」与「finish 释放」的分工：实例跨调用存活，其**固定尺寸**的临时缓冲
 * 是实例状态（这正是常驻的意义，对照 GEGL 由 buffer backend 复用临时 tile，而非每次 malloc）；
 * 而**超出保留上限**的大缓冲会在 `finish` 里放掉，避免大图在多线程下白占内存。
 *
 * 【线程协议】采用**每线程一份实例**（thread_local 表），而不是加锁：
 * 并行分块合成会**并发**调用同一算子，共享可变实例状态正是并行崩溃的根源
 * （前例：`LayerModeOp` 的共享 `m_mode` 曾导致并行合成访问冲突，见 docs/pending-dev.md）。
 * 注意该表**故意不析构**（容器在堆上、生命周期到进程结束）—— 理由是退出时的 TLS 析构顺序，
 * 见 `filterop.cpp` 的 `instanceTable()` 注释。
 */
#ifndef ENGINE_OP_FILTEROP_H
#define ENGINE_OP_FILTEROP_H

#include "domain/filternode.h"
#include "opname.h"

#include <QImage>
#include <QRect>
#include <QSize>

#include <functional>
#include <memory>

namespace Ps {

/**
 * 一次滤镜求值的上下文（对照 BufferOp 的 OpContext 瘦身版）。
 *
 * 【几何：srcRect / outRect】ROI 通用化的**第 ① 步**（见 docs/engine/roi.md §5）。
 * 现在 ctx 能完整表达「这次算的是哪块输出、喂进来的输入覆盖哪块」，而不再让算子
 * 只能靠「文档里那句『按 pad 取中心』」去猜。
 *
 * **坐标系（务必按这个读）**：两个矩形都在**调用方传入的那张图自己的坐标系**里。
 * `applyNode(image, …)` 拿到 image 时，二者的真值就是 `image.rect()`（= 本地坐标），
 * 所以算子只需要 `outRect` 来定位自己该写哪一块、`srcRect` 来知道手里这张图覆盖了
 * 输出的哪个外扩范围。调用方那一层（`Layer::compositeRasterRegion` 的 want/srcWant、
 * `PointFilterOp` 的 out/requiredInputs）用的是「请求区/所需输入区」那套文档坐标 ——
 * 它通过**裁剪与传参**体现，不需要算子换算。
 *
 * 这样分层的好处：① 七面积算子**一行都不用改**就能拿到正确几何；② 将来几何类算子
 * （offset 等）可以在 `process(src, out, ctx)` 里直接按两个矩形的映射取样。
 *
 * 【两者必须自洽】`srcRect` / `outRect` 只描述「手里这张图里的两个矩形」，
 * 因此恒有 `image.rect().contains(outRect)`，且 `srcRect` 是 `outRect` 的外扩
 * （点算子/无外扩时 `srcRect == outRect`）。违反这一条应视为调用方 bug。
 */
struct FilterContext {
    const FilterNode *node = nullptr; ///< 参数（借用，调用期间有效）
    /**
     * 本次作用的图像尺寸（= ROI）。
     * @note 语义上等价于 `outRect.size()`；保留它是因为 7 个面积算子的常驻临时缓冲
     *       都按它分配（`boxblur` / `unsharpmask` 的 `m_cached`、`median` 的 `isEmpty` 判据）。
     *       两者若不一致，以 `outRect` 为准。
     */
    QSize size;
    /**
     * 输入覆盖区（**输入图自己的坐标系**）：手里这张图对应输出的哪一块。
     * 点算子/无外扩时为 `outRect` 本身；面积算子比 `outRect` 大一圈 pad。
     */
    QRect srcRect;
    /** 本次要产出的输出区（**输入图自己的坐标系**）：算子应当写出的那块。 */
    QRect outRect;
    /**
     * 协作式取消谓词（可为空 = 不取消）。
     *
     * 【为什么要它】面积算子是**长任务**：实测 512² r=3 的中值要 ~49 ms，而演示文档
     * （2480×3508）整层是秒级；投影按 64×64 块推进，所以「定稿」会拆成很多次调用。
     * 用户在此期间改参数 / 撤销 / 关文档时，必须能**中途放弃**，否则后台任务会一直
     * 占着线程池算一份已经没人要的结果（且与「结果过期就丢弃」的世代号机制双保险）。
     *
     * 【协议】算子在自己的行循环里**周期性**调用（建议每 16~64 行一次），
     * 返回 true 表示应中止：算子直接返回、**不保证输出完整**，由调用方丢弃结果。
     * 与 `prepare` 返回 false 的区别：那个是「本次无事可做」，这个是「算到一半被取消」。
     */
    std::function<bool()> shouldAbort;
};

/** 滤镜算子基类（对照 GeglOperationFilter）。 */
class FilterOp
{
public:
    virtual ~FilterOp() = default;

    /** 算子名（调试 / 一致性）。 */
    virtual OpName name() const = 0;

    /** 申请 / 校验本次资源；返回 false 则跳过 process（仍会 finish）。 */
    virtual bool prepare(const FilterContext &ctx)
    {
        Q_UNUSED(ctx);
        return true;
    }

    /**
     * 就地处理 @p image。
     * @warning 长内环（面积算子）必须周期性检查 `ctx.shouldAbort`（见 FilterContext 注释）。
     */
    virtual void process(QImage &image, const FilterContext &ctx) = 0;

    /** 释放不该跨调用保留的资源（无条件调用）。 */
    virtual void finish(const FilterContext &ctx) { Q_UNUSED(ctx); }

protected:
    /** 取消谓词的统一读法：无谓词 = 永不取消。 */
    static bool aborted(const FilterContext &ctx)
    {
        return ctx.shouldAbort && ctx.shouldAbort();
    }
};

/** 工厂：返回 nullptr 视为注册失败。 */
using FilterOpFactory = std::function<std::unique_ptr<FilterOp>()>;

/**
 * 注册滤镜算子工厂（由 `opsInit` 在**主线程**调用一次）。
 * 注册后工厂表只读 → 多线程读安全。
 */
void registerFilterOp(OpName name, FilterOpFactory factory);

/**
 * 取**本线程**的常驻实例；未注册返回 nullptr（调用方据此回退到无资源的直接路径）。
 * 首次调用时按工厂惰性创建并缓存。
 */
FilterOp *filterOpInstance(OpName name);

// —— 测试探针（ROI 通用化第 ① 步：证明「几何真的传到了算子」）——
//
// 为什么需要它：`FilterContext` 的几何是**契约**，光看代码无法证明两条真实求值路径
// （像素层 `Layer::compositeRasterRegion`、图路径 `PointFilterOp`）真的按坐标空间填了值。
// 这里留一个**默认为 null** 的回调，由测试挂上并断言（生产不挂 = 一次空指针判断）。
// 数据只在对齐的 `QRect`/`QSize`（POD）上，测试内单线程使用。
struct FilterGeometryProbe {
    QRect srcRect;
    QRect outRect;
    QSize imageSize;
};

/** 探针回调类型（便于声明「返回函数指针」的取用函数）。 */
using FilterGeometryProbeFn = void (*)(const FilterGeometryProbe &);

/** 设置/清除几何探针（传 nullptr 清除）。不属于线程安全 API，仅测试使用。 */
void setFilterGeometryProbe(FilterGeometryProbeFn fn);

/** 取当前探针（供 `applyNode` 调用；测试之外恒为 nullptr）。 */
FilterGeometryProbeFn filterGeometryProbe();

} // namespace Ps

#endif // ENGINE_OP_FILTEROP_H
