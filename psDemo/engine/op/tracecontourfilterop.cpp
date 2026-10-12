/**
 * tracecontourfilterop.cpp — 等高线：4-邻接跨 level 则黑，否则白；保留 alpha。
 */
#include "tracecontourfilterop.h"

namespace Ps {

namespace {

int luma(QRgb p)
{
    return (qRed(p) * 30 + qGreen(p) * 59 + qBlue(p) * 11) / 100;
}

} // namespace

bool TraceContourFilterOp::prepare(const FilterContext &ctx)
{
    if (!ctx.node || ctx.size.isEmpty())
        return false;
    m_level = qBound(0, ctx.node->contourLevel(), 255);
    m_edge = qBound(0, ctx.node->contourEdge(), 1);
    return true;
}

void TraceContourFilterOp::finish(const FilterContext &ctx)
{
    Q_UNUSED(ctx);
    m_level = 128;
    m_edge = 0;
}

void TraceContourFilterOp::process(QImage &image, const FilterContext &ctx)
{
    if (image.isNull())
        return;
    const int w = image.width();
    const int h = image.height();

    QImage work = image.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < h; ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(work.scanLine(y));
        for (int x = 0; x < w; ++x)
            row[x] = qUnpremultiply(row[x]);
    }

    auto sampleL = [&](int x, int y) -> int {
        if (x < 0 || y < 0 || x >= w || y >= h)
            return luma(reinterpret_cast<const QRgb *>(work.constScanLine(qBound(0, y, h - 1)))
                            [qBound(0, x, w - 1)]);
        return luma(reinterpret_cast<const QRgb *>(work.constScanLine(y))[x]);
    };

    QImage out(w, h, QImage::Format_ARGB32);
    for (int y = 0; y < h; ++y) {
        if ((y & 31) == 0 && aborted(ctx))
            return;
        const QRgb *srcRow = reinterpret_cast<const QRgb *>(work.constScanLine(y));
        QRgb *dstRow = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const int Lp = luma(srcRow[x]);
            const int a = qAlpha(srcRow[x]);
            bool mark = false;
            const int nbs[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
            for (const auto &nb : nbs) {
                const int Ln = sampleL(x + nb[0], y + nb[1]);
                const bool cross = (Lp < m_level && Ln >= m_level) || (Lp >= m_level && Ln < m_level);
                if (!cross)
                    continue;
                if (m_edge == 0) {
                    // 较低：任意跨越
                    mark = true;
                    break;
                }
                // 较高：中心不低于阈值时才画
                if (Lp >= m_level) {
                    mark = true;
                    break;
                }
            }
            dstRow[x] = mark ? qRgba(0, 0, 0, a) : qRgba(255, 255, 255, a);
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
