/**
 * layermodecatalog.h — BlendMode → OpName 映射（engine/op 层）。
 *
 * Demo 各模式共用 OpName::LayerMode，再用 BlendMode 属性选算法。
 * 对照 gimp-layer-modes.c 的 gimp_layer_mode_get_operation_name。
 */
#ifndef ENGINE_OP_LAYERMODECATALOG_H
#define ENGINE_OP_LAYERMODECATALOG_H

#include "opname.h"
#include "domain/blendmode.h"

#include <QtGlobal>

namespace Ps {

/**
 * 图层模式 → 算子枚举（对照 gimp-layer-modes.c：GimpLayerMode → op_name）。
 * Demo 各模式共用 OpName::LayerMode，再用 BlendMode 属性选算法。
 */
inline OpName layerModeOperation(BlendMode mode)
{
    Q_UNUSED(mode);
    return OpName::LayerMode;
}

} // namespace Ps

#endif // ENGINE_OP_LAYERMODECATALOG_H
