/**
 * sharpenedgesfilterop.cpp — 锐化边缘。
 */
#include "sharpenedgesfilterop.h"

#include "sharpenfilterop.h"

#include <QVector>

#include <cmath>
#include <cstring>

namespace Ps {
namespace {

int sobelLumaMag(const QRgb *prev, const QRgb *cur, const QRgb *next,
                 bool hasPrev, bool hasNext, int x, int w)
{
    const auto at = [&](const QRgb *row, bool ok, int xx) -> int {
        if (!ok || !row || xx < 0 || xx >= w)
            return 0;
        const QRgb p = row[xx];
        // 预乘图上的亮度近似（与 FindEdges 解预乘路径不同；此处 mask 只需相对边缘强度）
        return (qRed(p) * 2 + qGreen(p) * 5 + qBlue(p)) / 8;
    };
    int gx = 0, gy = 0;
    for (int dy = -1; dy <= 1; ++dy) {
        const QRgb *row = (dy < 0) ? prev : ((dy == 0) ? cur : next);
        const bool ok = (dy < 0) ? hasPrev : ((dy == 0) ? true : hasNext);
        for (int dx = -1; dx <= 1; ++dx) {
            const int v = at(row, ok, x + dx);
            const int kx = dx * (dy == 0 ? 2 : 1);
            const int ky = dy * (dx == 0 ? 2 : 1);
            gx += kx * v;
            gy += ky * v;
        }
    }
    return qBound(0, int(std::sqrt(double(gx) * gx + double(gy) * gy) + 0.5), 255);
}

} // namespace

bool SharpenEdgesFilterOp::prepare(const FilterContext &ctx)
{
    if (!ctx.node || ctx.size.isEmpty())
        return false;
    m_strength = qBound(1, ctx.node->sharpenStrength(), 4);
    return true;
}

void SharpenEdgesFilterOp::finish(const FilterContext &ctx)
{
    Q_UNUSED(ctx);
    m_strength = 1;
}

void SharpenEdgesFilterOp::process(QImage &image, const FilterContext &ctx)
{
    if (image.isNull())
        return;
    const QImage src = image.copy();

    FilterNode sharp(OpName::Sharpen);
    sharp.setSharpenStrength(m_strength);
    FilterContext sharpCtx = ctx;
    sharpCtx.node = &sharp;
    SharpenFilterOp sop;
    if (sop.prepare(sharpCtx))
        sop.process(image, sharpCtx);
    sop.finish(sharpCtx);

    const int w = image.width();
    const int h = image.height();
    QVector<QRgb> prev(w), cur(w), next(w);
    const auto loadRow = [w](QVector<QRgb> &dst, const QImage &img, int y) {
        std::memcpy(dst.data(), img.constScanLine(y), size_t(w) * sizeof(QRgb));
    };
    loadRow(cur, src, 0);

    for (int y = 0; y < h; ++y) {
        if ((y & 31) == 0 && aborted(ctx))
            return;
        const bool hasPrev = (y > 0);
        const bool hasNext = (y + 1 < h);
        if (hasNext)
            loadRow(next, src, y + 1);

        const QRgb *s = reinterpret_cast<const QRgb *>(src.constScanLine(y));
        QRgb *d = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const int e = sobelLumaMag(prev.constData(), cur.constData(), next.constData(),
                                       hasPrev, hasNext, x, w);
            const int inv = 255 - e;
            const QRgb a = s[x];
            const QRgb b = d[x];
            d[x] = qRgba((qRed(a) * inv + qRed(b) * e) / 255,
                         (qGreen(a) * inv + qGreen(b) * e) / 255,
                         (qBlue(a) * inv + qBlue(b) * e) / 255,
                         (qAlpha(a) * inv + qAlpha(b) * e) / 255);
        }
        std::swap(prev, cur);
        std::swap(cur, next);
    }
}

} // namespace Ps
