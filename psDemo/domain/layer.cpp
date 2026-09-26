#include "layer.h"

#include "imagedocument.h"

#include <QColor>
#include <QtGlobal>

namespace Ps {

Layer::Layer(const QString &name, int width, int height)
    : m_name(name)
    // 预乘格式：合成公式更简单，也与多数 GPU/加速路径习惯一致
    , m_pixels(width, height, QImage::Format_ARGB32_Premultiplied)
{
    m_pixels.fill(Qt::transparent);
}

Layer::Layer(const QString &name, const QImage &pixels)
    : m_name(name)
    // 打开外部图时格式不一，这里统一，避免合成时直通/预乘混用
    , m_pixels(pixels.convertToFormat(QImage::Format_ARGB32_Premultiplied))
{
}

void Layer::notifyPropertiesChanged()
{
    // owner 为空表示该层尚未入栈（构造中/游离层），静默即可
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
    m_pixels.fill(color);
}

} // namespace Ps
