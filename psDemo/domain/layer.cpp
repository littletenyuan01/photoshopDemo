#include "layer.h"

#include "imagedocument.h"

#include <QtGlobal>

namespace Ps {

Layer::Layer(const QString &name, int width, int height)
    : m_name(name)
    , m_tiles(width, height)
{
    // 故意不 ensureTile / fill：透明层 0 块瓦片，对齐 GIMP 懒分配
}

Layer::Layer(const QString &name, const QImage &pixels)
    : m_name(name)
{
    m_tiles.setFromImage(pixels);
}

void Layer::notifyPropertiesChanged()
{
    if (m_owner)
        m_owner->notifyLayerPropertiesChanged(*this);
}

void Layer::setName(const QString &name)
{
    if (m_name == name)
        return;
    m_name = name;
    notifyPropertiesChanged();
}

void Layer::setVisible(bool visible)
{
    if (m_visible == visible)
        return;
    m_visible = visible;
    notifyPropertiesChanged();
}

void Layer::setOpacity(qreal opacity)
{
    const qreal clamped = qBound(0.0, opacity, 1.0);
    if (qFuzzyCompare(m_opacity, clamped))
        return;
    m_opacity = clamped;
    notifyPropertiesChanged();
}

void Layer::setBlendMode(BlendMode mode)
{
    if (m_blendMode == mode)
        return;
    m_blendMode = mode;
    notifyPropertiesChanged();
}

void Layer::fill(const QColor &color)
{
    m_tiles.fill(color);
}

} // namespace Ps
