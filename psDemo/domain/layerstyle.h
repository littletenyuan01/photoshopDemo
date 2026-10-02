/**
 * layerstyle.h — 图层样式效果节点（domain 层）。
 *
 * 对齐 PS「图层样式 / fx」入口；实现对照 GIMP 3 DrawableFilter +
 * PSD 导入映射（gegl:dropshadow / inner-glow / color-overlay），非破坏挂在层上。
 */
#ifndef DOMAIN_LAYERSTYLE_H
#define DOMAIN_LAYERSTYLE_H

#include <QColor>
#include <QString>
#include <QtGlobal>

namespace Ps {

/** 已实现的样式种类（菜单其余项仍灰显）。 */
enum class LayerStyleKind {
    DropShadow = 0,   ///< 投影 ≈ gegl:dropshadow
    InnerShadow,      ///< 内阴影 ≈ gegl:inner-glow + 偏移（PSD 映射）
    OuterGlow,        ///< 外发光 ≈ dropshadow(x=y=0)
    InnerGlow,        ///< 内发光 ≈ gegl:inner-glow
    Stroke,           ///< 描边（外侧）
    ColorOverlay,     ///< 颜色叠加 ≈ gegl:color-overlay
};

/** 单条图层样式（对照 GimpDrawableFilter 参数包的瘦身版）。 */
class LayerStyleEffect
{
public:
    LayerStyleEffect() = default;
    explicit LayerStyleEffect(LayerStyleKind kind);

    LayerStyleKind kind() const { return m_kind; }
    void setKind(LayerStyleKind kind) { m_kind = kind; }

    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool on) { m_enabled = on; }

    QString title() const;

    qreal opacity() const { return m_opacity; }
    void setOpacity(qreal v) { m_opacity = qBound(0.0, v, 1.0); }

    QColor color() const { return m_color; }
    void setColor(const QColor &c) { m_color = c.isValid() ? c : QColor(Qt::black); }

    qreal angle() const { return m_angle; }
    void setAngle(qreal deg) { m_angle = deg; }

    qreal distance() const { return m_distance; }
    void setDistance(qreal v) { m_distance = qMax(0.0, v); }

    qreal size() const { return m_size; }
    void setSize(qreal v) { m_size = qMax(0.0, v); }

    qreal spread() const { return m_spread; }
    void setSpread(qreal v) { m_spread = qBound(0.0, v, 100.0); }

    int paddingNeeded() const;
    void shadowOffset(int *dx, int *dy) const;

    /** 是否落在内容外侧（投影/外发光/描边需外扩画布）。 */
    bool isExterior() const;
    /** 是否需要 angle/distance（投影/内阴影）。 */
    bool usesOffset() const;

    static LayerStyleEffect makeDefault(LayerStyleKind kind);

    /** 持久化用稳定整数 id（与枚举值一致）。 */
    static int kindId(LayerStyleKind kind);
    static bool kindFromId(int id, LayerStyleKind *out);

private:
    LayerStyleKind m_kind = LayerStyleKind::DropShadow;
    bool m_enabled = true;
    qreal m_opacity = 0.75;
    QColor m_color = Qt::black;
    qreal m_angle = 120.0;
    qreal m_distance = 5.0;
    qreal m_size = 5.0;
    qreal m_spread = 0.0;
};

} // namespace Ps

#endif // DOMAIN_LAYERSTYLE_H
