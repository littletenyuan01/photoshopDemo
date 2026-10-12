/**
 * tilesfilterop.cpp — 拼贴：N×N 切块，每块整体确定性偏移粘贴；空隙按填充模式。
 */
#include "tilesfilterop.h"

namespace Ps {

namespace {

quint32 tileHash(int tx, int ty, quint32 seed)
{
    quint32 h = seed ^ (quint32(tx) * 0x9E3779B9u) ^ (quint32(ty) * 0x85EBCA6Bu);
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    h *= 0x846CA68Bu;
    h ^= h >> 16;
    return h;
}

int signedOffset(quint32 h, int maxAbs)
{
    if (maxAbs <= 0)
        return 0;
    const int span = maxAbs * 2 + 1;
    return int(h % quint32(span)) - maxAbs;
}

} // namespace

bool TilesFilterOp::prepare(const FilterContext &ctx)
{
    if (!ctx.node || ctx.size.isEmpty())
        return false;
    m_count = qBound(2, ctx.node->tilesCount(), 100);
    m_maxOffset = qBound(0, ctx.node->tilesMaxOffset(), 100);
    m_fill = qBound(0, ctx.node->tilesFill(), 2);
    m_seed = ctx.node->tilesSeed();
    return true;
}

void TilesFilterOp::finish(const FilterContext &ctx)
{
    Q_UNUSED(ctx);
    m_count = 10;
    m_maxOffset = 10;
    m_fill = 0;
    m_seed = 0xC0FFEE01u;
}

void TilesFilterOp::process(QImage &image, const FilterContext &ctx)
{
    if (image.isNull() || m_count < 2)
        return;
    const int w = image.width();
    const int h = image.height();
    if (w < 2 || h < 2)
        return;

    QImage src = image.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < h; ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(src.scanLine(y));
        for (int x = 0; x < w; ++x)
            row[x] = qUnpremultiply(row[x]);
    }

    QImage dst(w, h, QImage::Format_ARGB32);
    if (m_fill == 1) {
        for (int y = 0; y < h; ++y) {
            const QRgb *s = reinterpret_cast<const QRgb *>(src.constScanLine(y));
            QRgb *d = reinterpret_cast<QRgb *>(dst.scanLine(y));
            for (int x = 0; x < w; ++x) {
                const QRgb p = s[x];
                d[x] = qRgba(255 - qRed(p), 255 - qGreen(p), 255 - qBlue(p), qAlpha(p));
            }
        }
    } else if (m_fill == 2) {
        dst = src.copy();
    } else {
        dst.fill(qRgba(0, 0, 0, 0));
    }

    const int n = m_count;
    for (int ty = 0; ty < n; ++ty) {
        if ((ty & 3) == 0 && aborted(ctx))
            return;
        const int y0 = (ty * h) / n;
        const int y1 = ((ty + 1) * h) / n;
        const int th = y1 - y0;
        if (th <= 0)
            continue;
        const int maxOy = (th * m_maxOffset) / 100;
        for (int tx = 0; tx < n; ++tx) {
            const int x0 = (tx * w) / n;
            const int x1 = ((tx + 1) * w) / n;
            const int tw = x1 - x0;
            if (tw <= 0)
                continue;
            const int maxOx = (tw * m_maxOffset) / 100;
            const quint32 h0 = tileHash(tx, ty, m_seed);
            const quint32 h1 = tileHash(tx + 17, ty + 31, m_seed ^ 0xA5A5A5A5u);
            const int ox = signedOffset(h0, maxOx);
            const int oy = signedOffset(h1, maxOy);

            // 源块 (x0..x1,y0..y1) 粘到 (x0+ox, y0+oy)；越界裁掉
            for (int sy = y0; sy < y1; ++sy) {
                const int dy = sy + oy;
                if (dy < 0 || dy >= h)
                    continue;
                const QRgb *srow = reinterpret_cast<const QRgb *>(src.constScanLine(sy));
                QRgb *drow = reinterpret_cast<QRgb *>(dst.scanLine(dy));
                for (int sx = x0; sx < x1; ++sx) {
                    const int dx = sx + ox;
                    if (dx < 0 || dx >= w)
                        continue;
                    drow[dx] = srow[sx];
                }
            }
        }
    }

    image = QImage(w, h, image.format());
    for (int y = 0; y < h; ++y) {
        const QRgb *s = reinterpret_cast<const QRgb *>(dst.constScanLine(y));
        QRgb *d = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < w; ++x)
            d[x] = qPremultiply(s[x]);
    }
}

} // namespace Ps
