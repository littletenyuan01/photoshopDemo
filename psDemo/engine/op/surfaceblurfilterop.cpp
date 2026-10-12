/**
 * surfaceblurfilterop.cpp — 表面模糊。
 */
#include "surfaceblurfilterop.h"

#include "edgepreserve.h"

namespace Ps {

bool SurfaceBlurFilterOp::prepare(const FilterContext &ctx)
{
    if (!ctx.node || ctx.size.isEmpty())
        return false;
    m_radius = qBound(1, ctx.node->surfaceRadius(), 20);
    m_threshold = qBound(0, ctx.node->surfaceThreshold(), 255);
    return true;
}

void SurfaceBlurFilterOp::finish(const FilterContext &ctx)
{
    Q_UNUSED(ctx);
    m_radius = 5;
    m_threshold = 15;
}

void SurfaceBlurFilterOp::process(QImage &image, const FilterContext &ctx)
{
    if (image.isNull() || m_radius <= 0)
        return;
    const int w = image.width();
    const int h = image.height();

    QImage work = image.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < h; ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(work.scanLine(y));
        for (int x = 0; x < w; ++x)
            row[x] = qUnpremultiply(row[x]);
    }

    const auto abortFn = [&]() { return aborted(ctx); };
    EdgePreserve::blurStraight(&work, m_radius, m_threshold, abortFn);

    image = QImage(w, h, image.format());
    for (int y = 0; y < h; ++y) {
        const QRgb *s = reinterpret_cast<const QRgb *>(work.constScanLine(y));
        QRgb *d = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < w; ++x)
            d[x] = qPremultiply(s[x]);
    }
}

} // namespace Ps
