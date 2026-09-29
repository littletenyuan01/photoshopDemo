#ifndef ENGINE_OP_POINTOP_H
#define ENGINE_OP_POINTOP_H

#include "operation.h"

namespace Ps {

/**
 * 逐样本点算子（对照 GeglOperationPointFilter）。
 * 样品布局由子类约定；当前 LayerModeOp 以 blendPixel 充当逐点 process。
 * （点算子由 Compositor 热路径调用，不走 OpRunner 的缓冲三段式。）
 */
class PointOp : public Operation
{
public:
    ~PointOp() override = default;
};

} // namespace Ps

#endif // ENGINE_OP_POINTOP_H
