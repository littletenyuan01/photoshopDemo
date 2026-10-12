/**
 * fragmentfilterop.cpp — 四角 (±o,±o) 取样平均。
 */
#include "fragmentfilterop.h"

namespace Ps {

bool FragmentFilterOp::prepare(const FilterContext &ctx)
{
    return ctx.node && !ctx.size.isEmpty();
}

void FragmentFilterOp::finish(const FilterContext &ctx)
{
    Q_UNUSED(ctx);
}

void FragmentFilterOp::process(QImage &image, const FilterContext &ctx)
{
    if (image.isNull())
        return;
    const int w = image.width();
    const int h = image.height();
    const int o = kOffset;
    const QImage src = image.copy();

    for (int y = 0; y < h; ++y) {
        if ((y & 31) == 0 && aborted(ctx))
            return;
        QRgb *d = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            auto sample = [&](int xx, int yy) -> QRgb {
                if (xx < 0 || yy < 0 || xx >= w || yy >= h)
                    return 0;
                return reinterpret_cast<const QRgb *>(src.constScanLine(yy))[xx];
            };
            const QRgb a = sample(x - o, y - o);
            const QRgb b = sample(x + o, y - o);
            const QRgb c = sample(x - o, y + o);
            const QRgb e = sample(x + o, y + o);
            d[x] = qRgba((qRed(a) + qRed(b) + qRed(c) + qRed(e)) / 4,
                         (qGreen(a) + qGreen(b) + qGreen(c) + qGreen(e)) / 4,
                         (qBlue(a) + qBlue(b) + qBlue(c) + qBlue(e)) / 4,
                         (qAlpha(a) + qAlpha(b) + qAlpha(c) + qAlpha(e)) / 4);
        }
    }
}

} // namespace Ps
