/**
 * highpassfilterop.cpp — 高通保留实现（engine/op 层）。
 */
#include "highpassfilterop.h"

namespace Ps {

bool HighPassFilterOp::prepare(const FilterContext &ctx)
{
    if (!ctx.node || ctx.size.isEmpty())
        return false;
    m_radius = qBound(1, ctx.node->highPassRadius(), 64);
    return true;
}

void HighPassFilterOp::finish(const FilterContext &ctx)
{
    Q_UNUSED(ctx);
    m_radius = 0;
}

void HighPassFilterOp::process(QImage &image, const FilterContext &ctx)
{
    if (m_radius <= 0)
        return;

    // 1) 模糊级：复用盒模糊（与 USM 同套路）
    QImage blurred = image; // 隐式共享；盒模糊内部写 scanLine 时会 detach
    FilterOp *box = filterOpInstance(OpName::BoxBlur);
    if (!box)
        return;
    FilterNode blurNode(OpName::BoxBlur);
    blurNode.setBlurRadius(m_radius);
    FilterContext bctx;
    bctx.node = &blurNode;
    bctx.size = blurred.size();
    bctx.shouldAbort = ctx.shouldAbort;
    if (box->prepare(bctx))
        box->process(blurred, bctx);
    box->finish(bctx);
    if (aborted(ctx))
        return;

    // 2) out = src - blur + 128（逐通道，alpha 保留）
    const int w = image.width();
    const int h = image.height();
    for (int y = 0; y < h; ++y) {
        if ((y & 31) == 0 && aborted(ctx))
            return;
        QRgb *d = reinterpret_cast<QRgb *>(image.scanLine(y));
        const QRgb *b = reinterpret_cast<const QRgb *>(blurred.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb sp = qUnpremultiply(d[x]);
            const QRgb bp = qUnpremultiply(b[x]);
            const int alpha = qAlpha(sp);
            d[x] = qPremultiply(qRgba(qBound(0, qRed(sp) - qRed(bp) + 128, 255),
                                      qBound(0, qGreen(sp) - qGreen(bp) + 128, 255),
                                      qBound(0, qBlue(sp) - qBlue(bp) + 128, 255),
                                      alpha));
        }
    }
}

} // namespace Ps
