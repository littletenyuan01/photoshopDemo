/**
 * solidfillop.cpp — solidfillop.h 实现（engine/op 层）。
 *
 * operationWindow 限窗；透明整层清除走 clearTiles 释放瓦片。
 */
#include "solidfillop.h"

#include "paintclip.h"
#include "domain/tilebuffer.h"
#include "engine/premul.h"

#include <QtGlobal>

namespace Ps {

bool SolidFillOp::prepare(OpContext &ctx)
{
    if (!BufferOp::prepare(ctx))
        return false;

    const TileBuffer &tiles = *ctx.tiles;
    m_window = OpPaintClip::operationWindow(ctx.clip, ctx.roi, tiles.width(), tiles.height());
    return !m_window.isEmpty();
}

QRect SolidFillOp::process(OpContext &ctx)
{
    TileBuffer &tiles = *ctx.tiles;
    const PaintSelectionClip &clip = ctx.clip;
    const QRect layerRect(0, 0, tiles.width(), tiles.height());

    // 无选区且窗口 = 整层：交给 TileBuffer::fill。
    // 透明填充会走 clearTiles 释放瓦片，比逐格写 0 更省内存
    // （透明 = 未分配瓦片，是 TileBuffer 的核心约定）。
    if (!OpPaintClip::clipActive(clip) && m_window == layerRect) {
        tiles.fill(m_color);
        return layerRect;
    }

    const QRgb premul = Premul::toPremultipliedRgb(m_color);
    bool any = false;
    bool skipped = false; // 有像素被选区挡下：本层就不是「整层都被清空」
    int minX = m_window.right() + 1;
    int minY = m_window.bottom() + 1;
    int maxX = -1;
    int maxY = -1;

    // 只遍历窗口覆盖的瓦片（对照 GIMP：空 ROI 直接短路，不碰整层缓冲）
    tiles.forEachTileInRect(m_window, true, [&](int, int, QImage &tile, const QRect &bounds) {
        const QRect area = m_window.intersected(bounds);
        if (area.isEmpty())
            return;

        for (int ly = area.top(); ly <= area.bottom(); ++ly) {
            QRgb *line = reinterpret_cast<QRgb *>(tile.scanLine(ly - bounds.y()));
            for (int lx = area.left(); lx <= area.right(); ++lx) {
                if (!OpPaintClip::layerPixelSelected(clip, lx, ly)) {
                    skipped = true;
                    continue;
                }
                line[lx - bounds.x()] = premul;
                any = true;
                minX = qMin(minX, lx);
                minY = qMin(minY, ly);
                maxX = qMax(maxX, lx);
                maxY = qMax(maxY, ly);
            }
        }
    });

    if (!any)
        return {};

    // 透明填充且整层每个像素都写到了（例如「全选 → 清除」）⇒ 内容等价于空层，
    // 释放瓦片而不是留着满格 0。不这么做就比旧的 setFromImage 路径更费内存
    // （setFromImage 会跳过全透明块）。对齐 TileBuffer「透明 = 未分配」的约定。
    if (premul == 0 && !skipped && m_window == layerRect) {
        tiles.clearTiles();
        return layerRect;
    }

    return QRect(QPoint(minX, minY), QPoint(maxX, maxY));
}

} // namespace Ps
