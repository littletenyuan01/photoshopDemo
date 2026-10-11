/**
 * unsharpmaskfilterop.cpp — USM 锐化实现（engine/op 层）。
 */
#include "unsharpmaskfilterop.h"

#include <QtGlobal>

namespace Ps {

bool UnsharpMaskFilterOp::prepare(const FilterContext &ctx)
{
    if (!ctx.node || ctx.size.isEmpty())
        return false;
    m_radius = qBound(1, ctx.node->usmRadius(), 64);
    m_amount = ctx.node->usmAmount();
    m_threshold = ctx.node->usmThreshold();
    if (m_amount <= 0.0)
        return false; // 强度 0 = 无操作：跳过 process（finish 仍会跑）

    // 资源申请：按 ROI 尺寸准备模糊缓冲；尺寸不变则复用
    if (m_cached != ctx.size) {
        m_blurred = QImage(ctx.size, QImage::Format_ARGB32);
        m_cached = ctx.size;
    }
    m_oversized = qint64(ctx.size.width()) * ctx.size.height() > kMaxRetainedPixels;
    return !m_blurred.isNull();
}

void UnsharpMaskFilterOp::finish(const FilterContext &ctx)
{
    Q_UNUSED(ctx);
    // 只释放「不该跨调用保留」的超大缓冲（理由见 boxblurfilterop.h）
    if (m_oversized) {
        m_blurred = QImage();
        m_cached = QSize();
        m_oversized = false;
    }
    m_radius = 0;
    m_amount = 1.0;
    m_threshold = 0;
}

void UnsharpMaskFilterOp::process(QImage &image, const FilterContext &ctx)
{
    Q_UNUSED(ctx);
    if (m_radius <= 0 || m_amount <= 0.0)
        return;

    // 防御：prepare 未跑或尺寸不符时退回本地缓冲（正确性优先，只是慢一点）
    QImage local;
    QImage *pBlurred = &m_blurred;
    if (m_blurred.size() != image.size()) {
        local = QImage(image.size(), QImage::Format_ARGB32);
        pBlurred = &local;
    }
    QImage &blurred = *pBlurred;

    // 1) 模糊级：交给盒模糊算子（走它自己的三段式；它也是每线程常驻实例）
    blurred = image; // 隐式共享；盒模糊内部写 scanLine 时会 detach，不会改到 image
    FilterOp *box = filterOpInstance(OpName::BoxBlur);
    if (!box)
        return; // 盒模糊未注册（漏调 opsInit）：宁可不处理，也不产出半成品
    FilterNode blurNode(OpName::BoxBlur);
    blurNode.setBlurRadius(m_radius);
    FilterContext bctx;
    bctx.node = &blurNode;
    bctx.size = blurred.size();
    if (box->prepare(bctx))
        box->process(blurred, bctx);
    box->finish(bctx);

    // 2) 合并：|diff| > threshold 才锐化
    const int w = image.width();
    const int h = image.height();
    const qreal threshold = m_threshold;
    const qreal amount = m_amount;
    for (int y = 0; y < h; ++y) {
        QRgb *d = reinterpret_cast<QRgb *>(image.scanLine(y));
        const QRgb *b = reinterpret_cast<const QRgb *>(blurred.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb sp = qUnpremultiply(d[x]); // 直通源
            const QRgb bp = qUnpremultiply(b[x]); // 直通模糊
            const int dr = int(qRed(sp)) - int(qRed(bp));
            const int dg = int(qGreen(sp)) - int(qGreen(bp));
            const int db = int(qBlue(sp)) - int(qBlue(bp));
            const auto sharpened = [amount, threshold](int s, int diff) -> int {
                if (qAbs(diff) <= threshold)
                    return s;
                return qBound(0, int(s + amount * diff + 0.5), 255);
            };
            const QRgb out = qRgba(sharpened(qRed(sp), dr), sharpened(qGreen(sp), dg),
                                   sharpened(qBlue(sp), db), qAlpha(sp));
            d[x] = qPremultiply(out);
        }
    }
}

} // namespace Ps
