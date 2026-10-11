/**
 * dustscratchfilterop.cpp — 蒙尘与划痕 / 减少杂色。
 */
#include "dustscratchfilterop.h"

#include "medianblurfilterop.h"

#include <QVector>

namespace Ps {
namespace {

void runMedian(QImage &image, const FilterContext &ctx, int radius)
{
    FilterNode med(OpName::MedianBlur);
    med.setMedianRadius(radius);
    FilterContext medCtx = ctx;
    medCtx.node = &med;
    MedianBlurFilterOp op;
    if (op.prepare(medCtx))
        op.process(image, medCtx);
    op.finish(medCtx);
}

int channelMaxAbsDiff(QRgb a, QRgb b)
{
    const int dr = qAbs(qRed(a) - qRed(b));
    const int dg = qAbs(qGreen(a) - qGreen(b));
    const int db = qAbs(qBlue(a) - qBlue(b));
    return qMax(dr, qMax(dg, db));
}

} // namespace

bool DustAndScratchesFilterOp::prepare(const FilterContext &ctx)
{
    if (!ctx.node || ctx.size.isEmpty())
        return false;
    m_radius = qBound(1, ctx.node->medianRadius(), 8);
    m_threshold = qBound(0, ctx.node->threshold(), 255);
    return true;
}

void DustAndScratchesFilterOp::finish(const FilterContext &ctx)
{
    Q_UNUSED(ctx);
    m_radius = 1;
    m_threshold = 10;
}

void DustAndScratchesFilterOp::process(QImage &image, const FilterContext &ctx)
{
    if (image.isNull())
        return;
    const QImage src = image.copy();
    runMedian(image, ctx, m_radius);
    const int w = image.width();
    const int h = image.height();
    const int thr = m_threshold;
    for (int y = 0; y < h; ++y) {
        if ((y & 31) == 0 && aborted(ctx))
            return;
        const QRgb *s = reinterpret_cast<const QRgb *>(src.constScanLine(y));
        QRgb *d = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            // 中值结果已在 image（预乘）；与 src 比差时解预乘后再比更稳，这里用预乘通道差近似
            if (channelMaxAbsDiff(s[x], d[x]) <= thr)
                d[x] = s[x];
        }
    }
}

bool ReduceNoiseFilterOp::prepare(const FilterContext &ctx)
{
    if (!ctx.node || ctx.size.isEmpty())
        return false;
    m_radius = qBound(1, ctx.node->medianRadius(), 8);
    m_strength = qBound(0, ctx.node->reduceNoiseStrength(), 100);
    return true;
}

void ReduceNoiseFilterOp::finish(const FilterContext &ctx)
{
    Q_UNUSED(ctx);
    m_radius = 2;
    m_strength = 50;
}

void ReduceNoiseFilterOp::process(QImage &image, const FilterContext &ctx)
{
    if (image.isNull() || m_strength <= 0)
        return;
    if (m_strength >= 100) {
        runMedian(image, ctx, m_radius);
        return;
    }
    const QImage src = image.copy();
    runMedian(image, ctx, m_radius);
    const int w = image.width();
    const int h = image.height();
    const int a = m_strength;
    const int b = 100 - a;
    for (int y = 0; y < h; ++y) {
        if ((y & 31) == 0 && aborted(ctx))
            return;
        const QRgb *s = reinterpret_cast<const QRgb *>(src.constScanLine(y));
        QRgb *d = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb p = s[x];
            const QRgb m = d[x];
            d[x] = qRgba((qRed(p) * b + qRed(m) * a) / 100,
                         (qGreen(p) * b + qGreen(m) * a) / 100,
                         (qBlue(p) * b + qBlue(m) * a) / 100,
                         (qAlpha(p) * b + qAlpha(m) * a) / 100);
        }
    }
}

} // namespace Ps
