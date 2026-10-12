/**
 * smartblurfilterop.cpp — 特殊模糊。
 */
#include "smartblurfilterop.h"

#include "edgepreserve.h"

namespace Ps {

bool SmartBlurFilterOp::prepare(const FilterContext &ctx)
{
    if (!ctx.node || ctx.size.isEmpty())
        return false;
    m_radius = qBound(1, ctx.node->smartRadius(), 20);
    m_threshold = qBound(0, ctx.node->smartThreshold(), 255);
    m_mode = qBound(0, ctx.node->smartMode(), 2);
    return true;
}

void SmartBlurFilterOp::finish(const FilterContext &ctx)
{
    Q_UNUSED(ctx);
    m_radius = 3;
    m_threshold = 25;
    m_mode = 0;
}

void SmartBlurFilterOp::process(QImage &image, const FilterContext &ctx)
{
    if (image.isNull() || m_radius <= 0)
        return;
    const int w = image.width();
    const int h = image.height();
    const auto abortFn = [&]() { return aborted(ctx); };

    QImage work = image.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < h; ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(work.scanLine(y));
        for (int x = 0; x < w; ++x)
            row[x] = qUnpremultiply(row[x]);
    }

    if (m_mode == 1) {
        EdgePreserve::edgesStraight(&work, m_radius, m_threshold, abortFn);
    } else if (m_mode == 2) {
        QImage edges = work;
        EdgePreserve::edgesStraight(&edges, m_radius, m_threshold, abortFn);
        EdgePreserve::blurStraight(&work, m_radius, m_threshold, abortFn);
        for (int y = 0; y < h; ++y) {
            QRgb *d = reinterpret_cast<QRgb *>(work.scanLine(y));
            const QRgb *e = reinterpret_cast<const QRgb *>(edges.constScanLine(y));
            for (int x = 0; x < w; ++x) {
                if (qRed(e[x]) > 128) // 白 = 边缘
                    d[x] = qRgba(0, 0, 0, qAlpha(d[x]));
            }
        }
    } else {
        EdgePreserve::blurStraight(&work, m_radius, m_threshold, abortFn);
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
