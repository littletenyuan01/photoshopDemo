/**
 * mosaicfilterop.cpp — 方块均值马赛克。
 */
#include "mosaicfilterop.h"

namespace Ps {

bool MosaicFilterOp::prepare(const FilterContext &ctx)
{
    if (!ctx.node || ctx.size.isEmpty())
        return false;
    m_cell = qBound(2, ctx.node->mosaicCell(), 64);
    return true;
}

void MosaicFilterOp::finish(const FilterContext &ctx)
{
    Q_UNUSED(ctx);
    m_cell = 8;
}

void MosaicFilterOp::process(QImage &image, const FilterContext &ctx)
{
    if (image.isNull() || m_cell <= 1)
        return;
    const int w = image.width();
    const int h = image.height();
    const int cell = m_cell;

    // 预乘 → 直通，再按块均值写回预乘（与多数面积算子同口径）
    QImage work = image.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < h; ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(work.scanLine(y));
        for (int x = 0; x < w; ++x)
            row[x] = qUnpremultiply(row[x]);
    }

    for (int cy = 0; cy < h; cy += cell) {
        if ((cy & 31) == 0 && aborted(ctx))
            return;
        const int y1 = qMin(cy + cell, h);
        for (int cx = 0; cx < w; cx += cell) {
            const int x1 = qMin(cx + cell, w);
            qint64 R = 0, G = 0, B = 0, A = 0;
            int n = 0;
            for (int y = cy; y < y1; ++y) {
                const QRgb *row = reinterpret_cast<const QRgb *>(work.constScanLine(y));
                for (int x = cx; x < x1; ++x) {
                    const QRgb p = row[x];
                    R += qRed(p);
                    G += qGreen(p);
                    B += qBlue(p);
                    A += qAlpha(p);
                    ++n;
                }
            }
            if (n <= 0)
                continue;
            const QRgb avg = qRgba(int(R / n), int(G / n), int(B / n), int(A / n));
            for (int y = cy; y < y1; ++y) {
                QRgb *row = reinterpret_cast<QRgb *>(work.scanLine(y));
                for (int x = cx; x < x1; ++x)
                    row[x] = avg;
            }
        }
    }

    image = QImage(w, h, image.format());
    for (int y = 0; y < h; ++y) {
        const QRgb *s = reinterpret_cast<const QRgb *>(work.constScanLine(y));
        QRgb *d = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < w; ++x)
            d[x] = qPremultiply(s[x]);
    }
}

} // namespace Ps
