#include "layer.h"

#include "imagedocument.h"

#include <QPointF>
#include <QRect>
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
    // 打开图片：整图拆成 64×64 瓦片写入
    m_tiles.setFromImage(pixels);
}

void Layer::notifyPropertiesChanged()
{
    // 未入栈（无 owner）时静默：避免临时 Layer 误广播
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

void Layer::setOffset(int x, int y)
{
    if (m_offsetX == x && m_offsetY == y)
        return;
    m_offsetX = x;
    m_offsetY = y;
    notifyPropertiesChanged();
}

void Layer::setOffsetSilent(int x, int y)
{
    m_offsetX = x;
    m_offsetY = y;
}

void Layer::translate(int dx, int dy)
{
    // 只改数值；广播由 ImageDocument::translateLayer 按脏区统一发
    if (dx == 0 && dy == 0)
        return;
    m_offsetX += dx;
    m_offsetY += dy;
}

QPointF Layer::toLayerLocal(const QPointF &imagePos) const
{
    return QPointF(imagePos.x() - m_offsetX, imagePos.y() - m_offsetY);
}

QRect Layer::boundsInDocument() const
{
    return QRect(m_offsetX, m_offsetY, width(), height());
}

qreal Layer::opacityAtDocumentPos(int docX, int docY) const
{
    // 【功能】点选命中测试：读层内预乘 alpha（对照 gimp_pickable_get_opacity_at）
    if (!m_visible || m_opacity <= 0.0 || !hasPixelData())
        return 0.0;

    const int lx = docX - m_offsetX;
    const int ly = docY - m_offsetY;
    if (lx < 0 || ly < 0 || lx >= width() || ly >= height())
        return 0.0;

    const int tx = lx / TileBuffer::kTileSize;
    const int ty = ly / TileBuffer::kTileSize;
    const QImage *tile = m_tiles.tileAt(tx, ty);
    if (!tile)
        return 0.0;

    const QRect bounds = m_tiles.tileBounds(tx, ty);
    const int px = lx - bounds.x();
    const int py = ly - bounds.y();
    if (px < 0 || py < 0 || px >= tile->width() || py >= tile->height())
        return 0.0;

    const int a = qAlpha(tile->pixel(px, py));
    // 层不透明度乘到取样结果上（点选时隐藏/半透明层更「难点中」）
    return (a / 255.0) * m_opacity;
}

void Layer::fill(const QColor &color)
{
    // 委托瓦片缓冲：透明 → clearTiles；实色 → 全格分配
    m_tiles.fill(color);
}

void Layer::replaceFromImage(const QImage &pixels)
{
    // 尺寸可能变化（图像大小 / 画布大小）；不在此发属性信号
    m_tiles.setFromImage(pixels);
}

} // namespace Ps
