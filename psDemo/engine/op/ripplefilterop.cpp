/**
 * ripplefilterop.cpp — 波纹实现。
 */
#include "ripplefilterop.h"

#include <QtMath>

#include <cmath>

namespace Ps {

bool RippleFilterOp::prepare(const FilterContext &ctx)
{
    if (!ctx.node || ctx.size.isEmpty())
        return false;
    m_amplitude = qBound(0, ctx.node->rippleAmplitude(), 64);
    m_wavelength = qBound(1, ctx.node->rippleWavelength(), 256);
    return m_amplitude > 0;
}

void RippleFilterOp::finish(const FilterContext &ctx)
{
    Q_UNUSED(ctx);
    m_amplitude = 5;
    m_wavelength = 16;
}

void RippleFilterOp::process(QImage &image, const FilterContext &ctx)
{
    const int w = image.width();
    const int h = image.height();
    const int A = m_amplitude;
    const int L = m_wavelength;
    if (w <= 0 || h <= 0 || A <= 0)
        return;

    // 不就地写：取样位置可能落在尚未写出的像素上
    const QImage src = image.copy();
    QImage out(w, h, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);

    const double twoPiOverL = 2.0 * M_PI / double(L);
    for (int y = 0; y < h; ++y) {
        if ((y & 31) == 0 && aborted(ctx))
            return;
        const int dx = int(std::lround(A * std::sin(twoPiOverL * y)));
        const QRgb *srow = nullptr;
        QRgb *d = reinterpret_cast<QRgb *>(out.scanLine(y));
        // 同行取样：源行就是 y
        srow = reinterpret_cast<const QRgb *>(src.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            const int sx = x + dx;
            if (sx < 0 || sx >= w)
                continue; // abyss = 透明
            d[x] = srow[sx];
        }
    }
    image = out;
}

} // namespace Ps
