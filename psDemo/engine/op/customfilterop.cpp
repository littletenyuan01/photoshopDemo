/**
 * customfilterop.cpp — 自定滤镜实现（engine/op 层）。
 */
#include "customfilterop.h"

#include <cstring>

namespace Ps {

bool CustomFilterOp::prepare(const FilterContext &ctx)
{
    if (!ctx.node || ctx.size.isEmpty())
        return false;
    for (int i = 0; i < 9; ++i)
        m_w[i] = ctx.node->customWeight(i);
    m_scale = ctx.node->customScale();
    m_offset = ctx.node->customOffset();
    return true;
}

void CustomFilterOp::finish(const FilterContext &ctx)
{
    Q_UNUSED(ctx);
    m_scale = 1;
    m_offset = 0;
}

void CustomFilterOp::process(QImage &image, const FilterContext &ctx)
{
    const int w = image.width();
    const int h = image.height();
    if (w <= 0 || h <= 0)
        return;

    // 1) 预乘 → 直通（RGB 在直通空间卷积；alpha 原样保留）
    for (int y = 0; y < h; ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < w; ++x)
            row[x] = qUnpremultiply(row[x]);
    }

    // 另开一张结果图：就地写会覆盖还没读的邻域
    QImage out(w, h, QImage::Format_ARGB32_Premultiplied);

    // 3×3 权重按行主序：k = dy*3 + dx
    const auto at = [&](const QImage &img, int x, int y) -> QRgb {
        if (x < 0 || x >= w || y < 0 || y >= h)
            return 0u; // abyss = 透明
        return reinterpret_cast<const QRgb *>(img.constScanLine(y))[x];
    };

    for (int y = 0; y < h; ++y) {
        if (aborted(ctx))
            return; // 协作式取消（见 filterop.h）
        QRgb *drow = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < w; ++x) {
            int r = 0, g = 0, b = 0;
            const QRgb center = at(image, x, y);
            const int alpha = qAlpha(center);
            for (int dy = -1, k = 0; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx, ++k) {
                    const QRgb p = at(image, x + dx, y + dy);
                    const int wt = m_w[k];
                    if (wt == 0)
                        continue;
                    r += wt * qRed(p);
                    g += wt * qGreen(p);
                    b += wt * qBlue(p);
                }
            }
            if (m_scale != 0) {
                r /= m_scale;
                g /= m_scale;
                b /= m_scale;
            }
            r += m_offset;
            g += m_offset;
            b += m_offset;
            // alpha 原样保留（见头文件：对 alpha 卷积无意义）
            drow[x] = qPremultiply(
                qRgba(qBound(0, r, 255), qBound(0, g, 255), qBound(0, b, 255), alpha));
        }
    }

    image = out;
}

} // namespace Ps
