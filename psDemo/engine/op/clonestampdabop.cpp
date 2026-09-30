/**
 * clonestampdabop.cpp — clonestampdabop.h 实现（engine/op 层）。
 *
 * 逐像素：刷盖度 × 采样像素 SourceOver 写入瓦片；有选区时子区备份回滚。
 */
#include "clonestampdabop.h"

#include "paintclip.h"
#include "domain/tilebuffer.h"

#include <QtMath>

namespace Ps {

namespace {

/** 硬/软边盖度 [0,1]：与 StampDabOp 径向渐变 stop 语义对齐。 */
qreal brushCover(qreal dist, qreal radius, qreal hardness)
{
    if (radius <= 0.0 || dist >= radius)
        return 0.0;
    hardness = qBound(0.0, hardness, 1.0);
    const qreal stop = qBound(0.05, hardness, 0.98) * radius;
    if (dist <= stop)
        return 1.0;
    const qreal t = (dist - stop) / (radius - stop);
    return qBound(0.0, 1.0 - t, 1.0);
}

/** 预乘 SourceOver，额外乘 cover∈[0,1]。 */
void blendPremulCover(QRgb *dst, QRgb src, qreal cover)
{
    if (cover <= 0.0)
        return;
    if (cover >= 1.0) {
        // 完全覆盖且源不透明时直接替换；否则仍走混合
        if (qAlpha(src) >= 255) {
            *dst = src;
            return;
        }
    }
    const int ca = qBound(0, qRound(qAlpha(src) * cover), 255);
    if (ca <= 0)
        return;
    const int cr = qBound(0, qRound(qRed(src) * cover), 255);
    const int cg = qBound(0, qRound(qGreen(src) * cover), 255);
    const int cb = qBound(0, qRound(qBlue(src) * cover), 255);

    const QRgb d = *dst;
    const int da = qAlpha(d);
    const int inv = 255 - ca;
    const int oa = ca + (da * inv + 127) / 255;
    const int or_ = cr + (qRed(d) * inv + 127) / 255;
    const int og = cg + (qGreen(d) * inv + 127) / 255;
    const int ob = cb + (qBlue(d) * inv + 127) / 255;
    *dst = qRgba(qBound(0, or_, 255), qBound(0, og, 255), qBound(0, ob, 255), qBound(0, oa, 255));
}

void cloneDabOnTile(QImage &tile,
                    const QPoint &tileOrigin,
                    const QPointF &centerLayer,
                    const QPointF &sourceDoc,
                    qreal radius,
                    qreal hardness,
                    qreal opacity,
                    const QImage &sample)
{
    if (tile.isNull() || sample.isNull() || radius <= 0.0)
        return;

    const QRect dab = CloneStampDabOp::dabBounds(centerLayer, radius);
    const QRect area = dab.intersected(QRect(tileOrigin, tile.size()));
    if (area.isEmpty())
        return;

    const int sw = sample.width();
    const int sh = sample.height();
    opacity = qBound(0.0, opacity, 1.0);

    for (int ly = area.top(); ly <= area.bottom(); ++ly) {
        const int ty = ly - tileOrigin.y();
        QRgb *dst = reinterpret_cast<QRgb *>(tile.scanLine(ty));
        for (int lx = area.left(); lx <= area.right(); ++lx) {
            const qreal dx = lx + 0.5 - centerLayer.x();
            const qreal dy = ly + 0.5 - centerLayer.y();
            const qreal dist = qSqrt(dx * dx + dy * dy);
            const qreal cover = brushCover(dist, radius, hardness) * opacity;
            if (cover <= 0.0)
                continue;

            // 层内偏移 = 文档偏移（同源缩放）；采样点 = 源中心 + (像素 − dab 中心)
            const int sx = qFloor(sourceDoc.x() + dx);
            const int sy = qFloor(sourceDoc.y() + dy);
            if (sx < 0 || sy < 0 || sx >= sw || sy >= sh)
                continue;

            const QRgb src = reinterpret_cast<const QRgb *>(sample.constScanLine(sy))[sx];
            blendPremulCover(&dst[lx - tileOrigin.x()], src, cover);
        }
    }
}

} // namespace

QRect CloneStampDabOp::dabBounds(const QPointF &center, qreal radius)
{
    const int rCeil = qCeil(radius) + 1;
    return QRect(qFloor(center.x()) - rCeil,
                 qFloor(center.y()) - rCeil,
                 rCeil * 2 + 1,
                 rCeil * 2 + 1);
}

bool CloneStampDabOp::prepare(OpContext &ctx)
{
    if (!BufferOp::prepare(ctx) || m_radius <= 0.0 || !m_sample || m_sample->isNull())
        return false;
    TileBuffer &tiles = *ctx.tiles;
    return tiles.width() > 0 && tiles.height() > 0;
}

QRect CloneStampDabOp::process(OpContext &ctx)
{
    TileBuffer &tiles = *ctx.tiles;
    const PaintSelectionClip &clip = ctx.clip;
    const QRect layerRect(0, 0, tiles.width(), tiles.height());
    const QRect dabRect = dabBounds(m_center, m_radius).intersected(layerRect);
    if (dabRect.isEmpty())
        return {};

    QRect workRect = dabRect;
    if (OpPaintClip::clipActive(clip)) {
        workRect = workRect.intersected(
            OpPaintClip::selectionRectInLayer(clip, layerRect.width(), layerRect.height()));
        if (workRect.isEmpty())
            return {};
    }

    const QImage &sample = *m_sample;
    tiles.forEachTileInRect(workRect, true, [&](int, int, QImage &tile, const QRect &bounds) {
        if (!OpPaintClip::clipActive(clip)) {
            cloneDabOnTile(tile, bounds.topLeft(), m_center, m_sourceCenter,
                           m_radius, m_hardness, m_opacity, sample);
            return;
        }
        const QRect area = dabRect.intersected(bounds);
        if (area.isEmpty())
            return;
        const QPoint inTile = area.topLeft() - bounds.topLeft();
        const QImage before = tile.copy(QRect(inTile, area.size()));
        cloneDabOnTile(tile, bounds.topLeft(), m_center, m_sourceCenter,
                       m_radius, m_hardness, m_opacity, sample);
        OpPaintClip::restoreOutsideSelection(tile, before, inTile, area.topLeft(), clip);
    });

    return workRect;
}

} // namespace Ps
