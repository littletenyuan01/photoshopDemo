#include "layermodeop.h"

#include "opname.h"
#include "engine/blend.h"

namespace Ps {

LayerModeOp::LayerModeOp(BlendMode mode)
    : m_mode(mode)
{
}

QString LayerModeOp::id() const
{
    return opNameId(OpName::LayerMode);
}

QString LayerModeOp::name() const
{
    return opNameTitle(OpName::LayerMode);
}

void LayerModeOp::blendPixel(const int backdrop[3], const int source[3], float comp[3]) const
{
    Blend::pixel(m_mode, backdrop, source, comp);
}

} // namespace Ps
