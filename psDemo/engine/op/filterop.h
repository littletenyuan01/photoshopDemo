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
 */
#ifndef ENGINE_OP_FILTEROP_H
#define ENGINE_OP_FILTEROP_H

#include "domain/filternode.h"
#include "opname.h"

#include <QImage>
#include <QSize>

#include <functional>
#include <memory>

namespace Ps {

/** 一次滤镜求值的上下文（对照 BufferOp 的 OpContext 瘦身版）。 */
struct FilterContext {
    const FilterNode *node = nullptr; ///< 参数（借用，调用期间有效）
    QSize size;                       ///< 本次作用的图像尺寸（= ROI）
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

    /** 就地处理 @p image。 */
    virtual void process(QImage &image, const FilterContext &ctx) = 0;

    /** 释放不该跨调用保留的资源（无条件调用）。 */
    virtual void finish(const FilterContext &ctx) { Q_UNUSED(ctx); }
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

} // namespace Ps

#endif // ENGINE_OP_FILTEROP_H
