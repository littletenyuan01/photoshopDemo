/**
 * tilepatch.h — 瓦片 ↔ 矩形补丁拷贝（engine/op 层共用）。
 *
 * Focus / Tone 等「抽邻域 → 处理 → 写回」路径共用，避免各 op 复制一份 memcpy 循环。
 */
#ifndef ENGINE_OP_TILEPATCH_H
#define ENGINE_OP_TILEPATCH_H

#include "domain/tilebuffer.h"

#include <QImage>
#include <QPoint>
#include <QRect>
#include <QRgb>

#include <cstring>

namespace Ps {
namespace TilePatch {

/** 从瓦片稀疏缓冲拷贝矩形到独立图像（层内坐标；未分配瓦片为透明）。 */
inline QImage extract(const TileBuffer &tiles, const QRect &rect)
{
    QImage out(rect.size(), QImage::Format_ARGB32_Premultiplied);
    out.fill(0);
    if (rect.isEmpty())
        return out;

    const int x0 = rect.left() / TileBuffer::kTileSize;
    const int y0 = rect.top() / TileBuffer::kTileSize;
    const int x1 = rect.right() / TileBuffer::kTileSize;
    const int y1 = rect.bottom() / TileBuffer::kTileSize;

    for (int ty = y0; ty <= y1; ++ty) {
        for (int tx = x0; tx <= x1; ++tx) {
            const QImage *tile = tiles.tileAt(tx, ty);
            if (!tile)
                continue;
            const QRect bounds = tiles.tileBounds(tx, ty);
            const QRect overlap = bounds.intersected(rect);
            if (overlap.isEmpty())
                continue;
            const QPoint srcTL = overlap.topLeft() - bounds.topLeft();
            const QPoint dstTL = overlap.topLeft() - rect.topLeft();
            for (int y = 0; y < overlap.height(); ++y) {
                const QRgb *src = reinterpret_cast<const QRgb *>(tile->constScanLine(srcTL.y() + y))
                                  + srcTL.x();
                QRgb *dst = reinterpret_cast<QRgb *>(out.scanLine(dstTL.y() + y)) + dstTL.x();
                memcpy(dst, src, size_t(overlap.width()) * sizeof(QRgb));
            }
        }
    }
    return out;
}

/** 把补丁写回瓦片（allocateMissing）。 */
inline void blit(TileBuffer &tiles, const QRect &rect, const QImage &patch)
{
    if (rect.isEmpty() || patch.size() != rect.size())
        return;
    tiles.forEachTileInRect(rect, true, [&](int, int, QImage &tile, const QRect &bounds) {
        const QRect overlap = bounds.intersected(rect);
        if (overlap.isEmpty())
            return;
        const QPoint srcTL = overlap.topLeft() - rect.topLeft();
        const QPoint dstTL = overlap.topLeft() - bounds.topLeft();
        for (int y = 0; y < overlap.height(); ++y) {
            const QRgb *src = reinterpret_cast<const QRgb *>(patch.constScanLine(srcTL.y() + y))
                              + srcTL.x();
            QRgb *dst = reinterpret_cast<QRgb *>(tile.scanLine(dstTL.y() + y)) + dstTL.x();
            memcpy(dst, src, size_t(overlap.width()) * sizeof(QRgb));
        }
    });
}

} // namespace TilePatch
} // namespace Ps

#endif // ENGINE_OP_TILEPATCH_H
