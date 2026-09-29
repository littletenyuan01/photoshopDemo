/**
 * opsinit.cpp — opsinit.h 实现（engine/op 层）。
 *
 * 向 OpRegistry / PointOpRegistry 注册缓冲算子 + LayerModeOp；只存工厂，不构造实例。
 */
#include "opsinit.h"

#include "floodfillop.h"
#include "gradientop.h"
#include "layermodeop.h"
#include "opname.h"
#include "oppad.h"
#include "opregistry.h"
#include "pointopregistry.h"
#include "selectpolygonop.h"
#include "solidfillop.h"
#include "stampdabop.h"

namespace Ps {

void opsInit()
{
    // 绘制算子：Selection 裁剪由算子内自行判断 clip，不写入 requiredPads
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

    // 选区写入：requiredPads = Selection（对照 gimp_channel_select_polygon）
    OpRegistry::add({
        OpName::SelectPolygon,
        {OpPad::Selection},
        [] { return std::unique_ptr<BufferOp>(new SelectPolygonOp); },
    });

    PointOpRegistry::add({
        OpName::LayerMode,
        [] { return std::unique_ptr<PointOp>(new LayerModeOp); },
    });
}

} // namespace Ps
