#include "layer.h"

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

void Layer::setOpacity(qreal opacity)
{
    m_opacity = qBound(0.0, opacity, 1.0);
}

void Layer::fill(const QColor &color)
{
    m_pixels.fill(color);
}

} // namespace Ps
