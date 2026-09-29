#include "opsinit.h"

#include "floodfillop.h"
#include "gradientop.h"
#include "layermodeop.h"
#include "opname.h"
#include "oppad.h"
#include "opregistry.h"
#include "pointopregistry.h"
#include "solidfillop.h"
#include "stampdabop.h"

namespace Ps {

void opsInit()
{
    // Selection 为可选依赖：不写入 requiredPads；算子内自行判断 clip
    OpRegistry::add({
        OpName::StampDab,
        {OpPad::Tiles},
        [] { return std::unique_ptr<BufferOp>(new StampDabOp); },
    });

    OpRegistry::add({
        OpName::FloodFill,
        {OpPad::Tiles},
        [] { return std::unique_ptr<BufferOp>(new FloodFillOp); },
    });

    OpRegistry::add({
        OpName::Gradient,
        {OpPad::Tiles},
        [] { return std::unique_ptr<BufferOp>(new GradientOp); },
    });

    OpRegistry::add({
        OpName::SolidFill,
        {OpPad::Tiles},
        [] { return std::unique_ptr<BufferOp>(new SolidFillOp); },
    });

    PointOpRegistry::add({
        OpName::LayerMode,
        [] { return std::unique_ptr<PointOp>(new LayerModeOp); },
    });
}

} // namespace Ps
