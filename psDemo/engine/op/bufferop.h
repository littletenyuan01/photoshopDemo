#ifndef ENGINE_OP_BUFFEROP_H
#define ENGINE_OP_BUFFEROP_H

#include "operation.h"
#include "opcontext.h"

#include <QRect>

namespace Ps {

/**
 * 缓冲区域算子（对照 GeglOperationFilter）。
 *
 * 生命周期由 **OpRunner** 显式驱动（对照 GEGL prepare → process + dispose）：
 *   prepare → process → finish
 * 算子自身不提供「一把梭」的 apply；也不自管注册（见 OpRegistry / opsInit）。
 */
class BufferOp : public Operation
{
public:
    ~BufferOp() override = default;

    /** 申请 / 校验临时资源。失败则 Runner 跳过 process，仍会 finish。 */
    virtual bool prepare(OpContext &ctx)
    {
        return ctx.tiles != nullptr;
    }

    /** 改像素；未改动返回空矩形。 */
    virtual QRect process(OpContext &ctx) = 0;

    /** 释放 prepare 申请的资源。 */
    virtual void finish(OpContext &ctx)
    {
        Q_UNUSED(ctx);
    }
};

} // namespace Ps

#endif // ENGINE_OP_BUFFEROP_H
