/**
 * layerstyle.cpp — LayerStyleEffect 默认值与外扩估算。
 */
#include "layerstyle.h"

#include <QtMath>

namespace Ps {

LayerStyleEffect::LayerStyleEffect(LayerStyleKind kind)
    : m_kind(kind)
{
    *this = makeDefault(kind);
}

QString LayerStyleEffect::title() const
{
    switch (m_kind) {
    case LayerStyleKind::DropShadow: return QStringLiteral("投影");
    case LayerStyleKind::InnerShadow: return QStringLiteral("内阴影");
    case LayerStyleKind::OuterGlow: return QStringLiteral("外发光");
    case LayerStyleKind::InnerGlow: return QStringLiteral("内发光");
    case LayerStyleKind::Stroke: return QStringLiteral("描边");
    case LayerStyleKind::ColorOverlay: return QStringLiteral("颜色叠加");
    }
    return QStringLiteral("样式");
}

bool LayerStyleEffect::isExterior() const
{
    switch (m_kind) {
    case LayerStyleKind::DropShadow:
    case LayerStyleKind::OuterGlow:
    case LayerStyleKind::Stroke:
        return true;
    case LayerStyleKind::InnerShadow:
    case LayerStyleKind::InnerGlow:
    case LayerStyleKind::ColorOverlay:
        return false;
    }
    return false;
}

bool LayerStyleEffect::usesOffset() const
{
    return m_kind == LayerStyleKind::DropShadow
           || m_kind == LayerStyleKind::InnerShadow;
}

void LayerStyleEffect::shadowOffset(int *dx, int *dy) const
{
    if (!dx || !dy)
        return;
    if (!usesOffset()) {
        *dx = 0;
        *dy = 0;
        return;
    }
    const qreal rad = qDegreesToRadians(m_angle);
    *dx = int(qRound(-qCos(rad) * m_distance));
    *dy = int(qRound(qSin(rad) * m_distance));
}

int LayerStyleEffect::paddingNeeded() const
{
    if (!m_enabled || !isExterior())
        return 0;

    const int grow = int(qCeil(m_size * (m_spread / 100.0)));
    const int blur = int(qCeil(m_size));

    switch (m_kind) {
    case LayerStyleKind::DropShadow: {
        int ox = 0, oy = 0;
        shadowOffset(&ox, &oy);
        return qMax(qAbs(ox), qAbs(oy)) + blur + grow + 1;
    }
    case LayerStyleKind::OuterGlow:
        return blur + grow + 1;
    case LayerStyleKind::Stroke:
        return int(qCeil(m_size)) + 1;
    default:
        return 0;
    }
}

int LayerStyleEffect::kindId(LayerStyleKind kind)
{
    return static_cast<int>(kind);
}

bool LayerStyleEffect::kindFromId(int id, LayerStyleKind *out)
{
    if (!out || id < 0 || id > static_cast<int>(LayerStyleKind::ColorOverlay))
        return false;
    *out = static_cast<LayerStyleKind>(id);
    return true;
}

LayerStyleEffect LayerStyleEffect::makeDefault(LayerStyleKind kind)
{
    LayerStyleEffect e;
    e.m_kind = kind;
    e.m_enabled = true;
    switch (kind) {
    case LayerStyleKind::DropShadow:
        e.m_opacity = 0.75;
        e.m_color = QColor(0, 0, 0);
        e.m_angle = 120.0;
        e.m_distance = 5.0;
        e.m_size = 5.0;
        e.m_spread = 0.0;
        break;
    case LayerStyleKind::InnerShadow:
        e.m_opacity = 0.75;
        e.m_color = QColor(0, 0, 0);
        e.m_angle = 120.0;
        e.m_distance = 5.0;
        e.m_size = 5.0;
        e.m_spread = 0.0;
        break;
    case LayerStyleKind::OuterGlow:
        e.m_opacity = 0.75;
        e.m_color = QColor(255, 255, 190);
        e.m_angle = 0.0;
        e.m_distance = 0.0;
        e.m_size = 5.0;
        e.m_spread = 0.0;
        break;
    case LayerStyleKind::InnerGlow:
        e.m_opacity = 0.75;
        e.m_color = QColor(255, 255, 190);
        e.m_angle = 0.0;
        e.m_distance = 0.0;
        e.m_size = 5.0;
        e.m_spread = 0.0;
        break;
    case LayerStyleKind::Stroke:
        e.m_opacity = 1.0;
        e.m_color = QColor(0, 0, 0);
        e.m_size = 3.0;
        e.m_spread = 0.0;
        e.m_distance = 0.0;
        break;
    case LayerStyleKind::ColorOverlay:
        e.m_opacity = 1.0;
        e.m_color = QColor(255, 0, 0);
        e.m_size = 0.0;
        break;
    }
    return e;
}

} // namespace Ps
