#include "tilebuffer.h"

#include <QPainter>
#include <QtMath>

namespace Ps {

namespace {

bool isFullyTransparent(const QColor &color)
{
    return color.alpha() == 0;
}

} // namespace

TileBuffer::TileBuffer(int width, int height)
{
    reset(width, height);
}

void TileBuffer::reset(int width, int height)
{
    m_width = qMax(0, width);
    m_height = qMax(0, height);
    m_tilesX = m_width > 0 ? (m_width + kTileSize - 1) / kTileSize : 0;
    m_tilesY = m_height > 0 ? (m_height + kTileSize - 1) / kTileSize : 0;
    m_tiles.clear();
}

void TileBuffer::clearTiles()
{
    m_tiles.clear();
}

bool TileBuffer::validTileIndex(int tx, int ty) const
{
    return tx >= 0 && ty >= 0 && tx < m_tilesX && ty < m_tilesY;
}

QRect TileBuffer::tileBounds(int tx, int ty) const
{
    if (!validTileIndex(tx, ty))
        return {};
    const int x = tx * kTileSize;
    const int y = ty * kTileSize;
    const int w = qMin(kTileSize, m_width - x);
    const int h = qMin(kTileSize, m_height - y);
    return QRect(x, y, w, h);
}

QImage *TileBuffer::ensureTile(int tx, int ty)
{
    if (!validTileIndex(tx, ty))
        return nullptr;

    const qint64 k = key(tx, ty);
    auto it = m_tiles.find(k);
    if (it != m_tiles.end())
        return &it.value();

    const QRect bounds = tileBounds(tx, ty);
    QImage tile(bounds.width(), bounds.height(), QImage::Format_ARGB32_Premultiplied);
    tile.fill(Qt::transparent);
    it = m_tiles.insert(k, std::move(tile));
    return &it.value();
}

QImage *TileBuffer::tileAt(int tx, int ty)
{
    if (!validTileIndex(tx, ty))
        return nullptr;
    auto it = m_tiles.find(key(tx, ty));
    return it == m_tiles.end() ? nullptr : &it.value();
}

const QImage *TileBuffer::tileAt(int tx, int ty) const
{
    if (!validTileIndex(tx, ty))
        return nullptr;
    auto it = m_tiles.constFind(key(tx, ty));
    return it == m_tiles.constEnd() ? nullptr : &it.value();
}

void TileBuffer::fill(const QColor &color)
{
    if (m_width <= 0 || m_height <= 0)
        return;

    if (isFullyTransparent(color)) {
        clearTiles();
        return;
    }

    // 实色：覆盖文档的全部瓦片都要有数据（白底背景层路径）
    for (int ty = 0; ty < m_tilesY; ++ty) {
        for (int tx = 0; tx < m_tilesX; ++tx) {
            QImage *tile = ensureTile(tx, ty);
            if (tile)
                tile->fill(color);
        }
    }
}

void TileBuffer::setFromImage(const QImage &image)
{
    const QImage src = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    reset(src.width(), src.height());
    if (src.isNull())
        return;

    for (int ty = 0; ty < m_tilesY; ++ty) {
        for (int tx = 0; tx < m_tilesX; ++tx) {
            const QRect bounds = tileBounds(tx, ty);
            QImage *tile = ensureTile(tx, ty);
            if (!tile)
                continue;
            QPainter p(tile);
            p.setCompositionMode(QPainter::CompositionMode_Source);
            p.drawImage(0, 0, src, bounds.x(), bounds.y(), bounds.width(), bounds.height());
        }
    }
}

QImage TileBuffer::materialize() const
{
    QImage out(m_width, m_height, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    if (m_tiles.isEmpty() || m_width <= 0 || m_height <= 0)
        return out;

    QPainter p(&out);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    forEachAllocatedTile([&](int, int, const QImage &tile, const QRect &bounds) {
        p.drawImage(bounds.topLeft(), tile);
    });
    return out;
}

void TileBuffer::forEachTileInRect(const QRect &rect, bool allocateMissing, const TileCallback &fn)
{
    if (!fn || m_tilesX <= 0 || m_tilesY <= 0)
        return;

    const QRect area = rect.intersected(QRect(0, 0, m_width, m_height));
    if (area.isEmpty())
        return;

    const int tx0 = area.left() / kTileSize;
    const int ty0 = area.top() / kTileSize;
    const int tx1 = area.right() / kTileSize;
    const int ty1 = area.bottom() / kTileSize;

    for (int ty = ty0; ty <= ty1; ++ty) {
        for (int tx = tx0; tx <= tx1; ++tx) {
            QImage *tile = allocateMissing ? ensureTile(tx, ty) : tileAt(tx, ty);
            if (!tile)
                continue;
            fn(tx, ty, *tile, tileBounds(tx, ty));
        }
    }
}

void TileBuffer::forEachAllocatedTile(const ConstTileCallback &fn) const
{
    if (!fn)
        return;
    for (auto it = m_tiles.constBegin(); it != m_tiles.constEnd(); ++it) {
        const qint64 k = it.key();
        const int tx = int(k % m_tilesX);
        const int ty = int(k / m_tilesX);
        fn(tx, ty, it.value(), tileBounds(tx, ty));
    }
}

} // namespace Ps
