/**
 * layer.cpp — Layer 属性 setter、坐标换算与内容包围盒扫描（domain 层）。
 */
#include "layer.h"

#include "imagedocument.h"
#include "engine/layerstyleeval.h"

#include <QPointF>
#include <QRect>
#include <QtGlobal>

#include <cstring>

namespace Ps {

/** 透明新建：只预定 extent，不分配瓦片。 */
Layer::Layer(const QString &name, int width, int height)
    : m_name(name)
    , m_tiles(width, height)
{
    // 故意不 ensureTile / fill：透明层 0 块瓦片，对齐 GIMP 懒分配
}

/** 打开图片：整图拆入 TileBuffer。 */
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
    // 改名不影响投影像素；只刷新面板标签
    if (m_owner)
        m_owner->notifyLayerLabelChanged(*this);
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

QRect Layer::styleBoundsInDocument() const
{
    QRect b = boundsInDocument();
    const int pad = m_styles.maxPadding();
    if (pad > 0)
        b.adjust(-pad, -pad, pad, pad);
    return b;
}

void Layer::invalidateCompositeRaster() const
{
    m_compositeRasterValid = false;
    m_compositeRaster = QImage();
    m_compositeOriginDx = 0;
    m_compositeOriginDy = 0;
}

Layer::CompositeRaster Layer::ensureCompositeRaster() const
{
    if (m_compositeRasterValid) {
        CompositeRaster hit;
        hit.image = m_compositeRaster;
        hit.originDx = m_compositeOriginDx;
        hit.originDy = m_compositeOriginDy;
        return hit;
    }

    QImage base = materialize();
    if (m_filters.hasEnabled())
        base = m_filters.apply(base);

    CompositeRaster out;
    if (m_styles.hasEnabled()) {
        const StyledLayerResult styled = LayerStyleEval::apply(base, m_styles);
        out.image = styled.image;
        out.originDx = styled.originDx;
        out.originDy = styled.originDy;
    } else {
        out.image = base;
        out.originDx = 0;
        out.originDy = 0;
    }

    m_compositeRaster = out.image;
    m_compositeOriginDx = out.originDx;
    m_compositeOriginDy = out.originDy;
    m_compositeRasterValid = true;
    return out;
}

void Layer::invalidateContentBounds() const
{
    m_contentBoundsValid = false;
}

QRect Layer::computeContentBoundsLocal() const
{
    if (!hasPixelData())
        return QRect();

    int minX = width();
    int minY = height();
    int maxX = -1;
    int maxY = -1;

    m_tiles.forEachAllocatedTile([&](int /*tx*/, int /*ty*/, const QImage &tile, const QRect &bounds) {
        for (int y = 0; y < tile.height(); ++y) {
            const QRgb *line = reinterpret_cast<const QRgb *>(tile.constScanLine(y));
            for (int x = 0; x < tile.width(); ++x) {
                if (qAlpha(line[x]) == 0)
                    continue;
                const int lx = bounds.x() + x;
                const int ly = bounds.y() + y;
                if (lx < minX) minX = lx;
                if (ly < minY) minY = ly;
                if (lx > maxX) maxX = lx;
                if (ly > maxY) maxY = ly;
            }
        }
    });

    if (maxX < minX || maxY < minY)
        return QRect();
    return QRect(minX, minY, maxX - minX + 1, maxY - minY + 1);
}

QRect Layer::contentBoundsInDocument() const
{
    // 【功能】PS 变换控件框：非透明像素最小外接矩形（文档坐标）
    if (!m_contentBoundsValid) {
        m_contentBoundsLocal = computeContentBoundsLocal();
        m_contentBoundsValid = true;
    }
    if (m_contentBoundsLocal.isEmpty())
        return QRect();
    return m_contentBoundsLocal.translated(m_offsetX, m_offsetY);
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

    int a = qAlpha(tile->pixel(px, py));
    if (m_mask && m_mask->isEnabled() && !m_mask->isNull())
        a = (a * int(m_mask->valueAt(lx, ly)) + 127) / 255;
    return (a / 255.0) * m_opacity;
}

void Layer::setMask(std::unique_ptr<LayerMask> mask)
{
    m_mask = std::move(mask);
}

void Layer::fill(const QColor &color)
{
    // 委托瓦片缓冲：透明 → clearTiles；实色 → 全格分配
    m_tiles.fill(color);
    invalidateContentBounds();
    invalidateCompositeRaster();
}

void Layer::replaceFromImage(const QImage &pixels)
{
    // 尺寸可能变化（图像大小 / 画布大小）；不在此发属性信号
    const int oldW = width();
    const int oldH = height();
    m_tiles.setFromImage(pixels);
    if (m_mask && (m_mask->width() != width() || m_mask->height() != height())) {
        // 尺寸变了：蒙版跟到新尺寸，缺省全白（避免错位）；undo 会整栈恢复
        m_mask = std::make_unique<LayerMask>(width(), height(), 255);
    }
    Q_UNUSED(oldW);
    Q_UNUSED(oldH);
    invalidateContentBounds();
    invalidateCompositeRaster();
}

QPoint Layer::expandToIncludeLocal(const QRect &localNeeded)
{
    if (localNeeded.isEmpty())
        return {};

    const int w = width();
    const int h = height();
    if (w <= 0 || h <= 0)
        return {};

    const int padL = qMax(0, -localNeeded.left());
    const int padT = qMax(0, -localNeeded.top());
    const int padR = qMax(0, localNeeded.right() - (w - 1));
    const int padB = qMax(0, localNeeded.bottom() - (h - 1));
    if (padL == 0 && padT == 0 && padR == 0 && padB == 0)
        return {};

    const int newW = w + padL + padR;
    const int newH = h + padT + padB;

    QImage neu(newW, newH, QImage::Format_ARGB32_Premultiplied);
    neu.fill(0);
    if (hasPixelData()) {
        const QImage old = materialize();
        // 旧像素贴到新缓冲的 (padL,padT)；offset 同步左上移，文档位置不变
        for (int y = 0; y < old.height(); ++y) {
            const QRgb *src = reinterpret_cast<const QRgb *>(old.constScanLine(y));
            QRgb *dst = reinterpret_cast<QRgb *>(neu.scanLine(y + padT));
            memcpy(dst + padL, src, size_t(old.width()) * sizeof(QRgb));
        }
    }
    m_tiles.setFromImage(neu);
    if (m_mask)
        m_mask->expand(padL, padT, padR, padB, 255);
    m_offsetX -= padL;
    m_offsetY -= padT;
    invalidateContentBounds();
    invalidateCompositeRaster();
    notifyPropertiesChanged();
    return QPoint(padL, padT);
}

} // namespace Ps
