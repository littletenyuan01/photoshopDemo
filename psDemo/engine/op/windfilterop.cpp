/**
 * windfilterop.cpp — 风：每行在上风向窗口内取亮度最大像素（爆发=2×强度；交错=奇数行反向）。
 */
#include "windfilterop.h"

namespace Ps {

namespace {

int luma(QRgb p)
{
    return (qRed(p) * 30 + qGreen(p) * 59 + qBlue(p) * 11) / 100;
}

} // namespace

bool WindFilterOp::prepare(const FilterContext &ctx)
{
    if (!ctx.node || ctx.size.isEmpty())
        return false;
    m_dir = qBound(0, ctx.node->windDirection(), 1);
    m_method = qBound(0, ctx.node->windMethod(), 2);
    m_strength = qBound(1, ctx.node->windStrength(), 20);
    return true;
}

void WindFilterOp::finish(const FilterContext &ctx)
{
    Q_UNUSED(ctx);
    m_dir = 0;
    m_method = 0;
    m_strength = 5;
}

void WindFilterOp::process(QImage &image, const FilterContext &ctx)
{
    if (image.isNull() || m_strength <= 0)
        return;
    const int w = image.width();
    const int h = image.height();
    const int baseS = (m_method == 1) ? (m_strength * 2) : m_strength;
    const int baseDir = (m_dir == 0) ? +1 : -1; // +1：上风在左

    QImage work = image.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < h; ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(work.scanLine(y));
        for (int x = 0; x < w; ++x)
            row[x] = qUnpremultiply(row[x]);
    }

    QImage out(w, h, QImage::Format_ARGB32);
    for (int y = 0; y < h; ++y) {
        if ((y & 31) == 0 && aborted(ctx))
            return;
        const int dir = (m_method == 2 && (y & 1)) ? -baseDir : baseDir;
        const int S = baseS;
        const QRgb *srcRow = reinterpret_cast<const QRgb *>(work.constScanLine(y));
        QRgb *dstRow = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < w; ++x) {
            QRgb best = srcRow[x];
            int bestL = luma(best);
            for (int k = 1; k <= S; ++k) {
                const int sx = x - k * dir;
                if (sx < 0 || sx >= w)
                    break;
                const QRgb p = srcRow[sx];
                const int L = luma(p);
                if (L > bestL) {
                    bestL = L;
                    best = p;
                }
            }
            dstRow[x] = best;
        }
    }

    image = QImage(w, h, image.format());
    for (int y = 0; y < h; ++y) {
        const QRgb *s = reinterpret_cast<const QRgb *>(out.constScanLine(y));
        QRgb *d = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < w; ++x)
            d[x] = qPremultiply(s[x]);
    }
}

} // namespace Ps
