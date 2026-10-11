/**
 * sharpenfilterop.cpp — 3×3 锐化实现（engine/op 层）。
 */
#include "sharpenfilterop.h"

#include <QVector>

#include <cstring>

namespace Ps {

bool SharpenFilterOp::prepare(const FilterContext &ctx)
{
    if (!ctx.node || ctx.size.isEmpty())
        return false;
    m_strength = qBound(1, ctx.node->sharpenStrength(), 4);
    return true; // 无区域尺寸资源：滚动窗口在 process 里按需分配（O(w)，很小）
}

void SharpenFilterOp::finish(const FilterContext &ctx)
{
    Q_UNUSED(ctx);
    m_strength = 1; // 无跨调用保留的资源（见 prepare）
}

void SharpenFilterOp::process(QImage &image, const FilterContext &ctx)
{
    Q_UNUSED(ctx);
    const int w = image.width();
    const int h = image.height();
    if (w <= 0 || h <= 0)
        return;
    const int s = m_strength;
    const int center = 1 + 4 * s;

    // 1) 预乘 → 直通（与其它面积算子同口径）
    for (int y = 0; y < h; ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < w; ++x)
            row[x] = qUnpremultiply(row[x]);
    }

    // 滚动窗口：prev / cur 是**原始（直通）**像素，next 读还没被写过的行
    QVector<QRgb> prev(w), cur(w), next(w);
    const auto loadRow = [w](QVector<QRgb> &dst, const QImage &img, int y) {
        const QRgb *src = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        std::memcpy(dst.data(), src, size_t(w) * sizeof(QRgb));
    };
    const auto at = [w](const QVector<QRgb> &row, bool valid, int x) -> QRgb {
        if (!valid || x < 0 || x >= w)
            return 0u; // abyss = 透明
        return row[x];
    };
    loadRow(cur, image, 0);

    for (int y = 0; y < h; ++y) {
        const bool hasPrev = (y > 0);
        const bool hasNext = (y + 1 < h);
        if (hasNext)
            loadRow(next, image, y + 1);

        QRgb *d = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb p = cur[x];
            const QRgb up = at(prev, hasPrev, x);
            const QRgb down = at(next, hasNext, x);
            const QRgb left = at(cur, true, x - 1);
            const QRgb right = at(cur, true, x + 1);
            const auto conv = [&](int c) -> int {
                const auto ch = [c](QRgb q) -> int {
                    return (c == 0) ? qRed(q) : ((c == 1) ? qGreen(q) : ((c == 2) ? qBlue(q) : qAlpha(q)));
                };
                const int v = center * ch(p)
                              - s * (ch(up) + ch(down) + ch(left) + ch(right));
                return qBound(0, v, 255);
            };
            d[x] = qRgba(conv(0), conv(1), conv(2), conv(3));
        }

        std::swap(prev, cur);
        std::swap(cur, next);
    }

    // 2) 直通 → 预乘
    for (int y = 0; y < h; ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < w; ++x)
            row[x] = qPremultiply(row[x]);
    }
}

} // namespace Ps
