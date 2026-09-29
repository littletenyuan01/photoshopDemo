#ifndef ENGINE_OP_LAYERMODEOP_H
#define ENGINE_OP_LAYERMODEOP_H

#include "pointop.h"
#include "domain/blendmode.h"

namespace Ps {

/**
 * 图层混合色算子（对照 gimpoperationlayermode-blend）。
 * Alpha union 合成仍由 Compositor 完成；溶解取舍直接调 Blend::dissolveKeeps。
 */
class LayerModeOp : public PointOp
{
public:
    explicit LayerModeOp(BlendMode mode = BlendMode::Normal);

    QString id() const override;
    QString name() const override;

    void setMode(BlendMode mode) { m_mode = mode; }
    BlendMode mode() const { return m_mode; }

    /** 逐通道混合：in/layer 直通 0..255；Dissolve 在 Blend::pixel 内等同 Normal。 */
    void blendPixel(const int backdrop[3], const int source[3], float comp[3]) const;

private:
    BlendMode m_mode = BlendMode::Normal;
};

} // namespace Ps

#endif // ENGINE_OP_LAYERMODEOP_H
