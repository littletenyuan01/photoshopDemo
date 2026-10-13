/**
 * findedgesfilterop.cpp — 3×3 Sobel 查找边缘实现（engine/op 层）。
 */
#include "findedgesfilterop.h"

#include <QVector>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Ps {

void FindEdgesFilterOp::process(QImage &image, const FilterContext &ctx)
{
    const int w = image.width();
    const int h = image.height();
    if (w <= 0 || h <= 0)
        return;

    // 1) 预乘 → 直通（与 BoxBlur 同口径）。
    //    为什么必须转：在**预乘**像素上做卷积/求导，半透明区的梯度会被 alpha 缩放，
    //    结果再写回预乘缓冲就等于二次预乘（半透明边缘会变得又暗又钝）。
    for (int y = 0; y < h; ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < w; ++x)
            row[x] = qUnpremultiply(row[x]);
    }

    // 滚动窗口：prev / cur 是**原始（直通）**像素（写回的是 image，不污染它们），next 读未处理的行
    QVector<QRgb> prev(w), cur(w), next(w);
    const auto loadRow = [w](QVector<QRgb> &dst, const QImage &img, int y) {
        const QRgb *s = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        std::memcpy(dst.data(), s, size_t(w) * sizeof(QRgb));
    };
    loadRow(cur, image, 0);

    // abyss = 透明：越界行/列一律当 0（与 BoxBlur 同口径）
    const auto at = [w](const QVector<QRgb> &row, bool valid, int x) -> QRgb {
        if (!valid || x < 0 || x >= w)
            return 0u;
        return row[x];
    };

    // 强度复用 usmAmount（1.0 = 100%；默认与旧无参行为一致）
    const double amount = ctx.node ? ctx.node->usmAmount() : 1.0;
    const auto mag = [amount](int gx, int gy) -> int {
        const double m = std::sqrt(double(gx) * gx + double(gy) * gy) * amount;
        return qBound(0, int(m + 0.5), 255);
    };

    for (int y = 0; y < h; ++y) {
        if ((y & 31) == 0 && aborted(ctx))
            return; // 协作式取消（见 filterop.h 的 FilterContext::shouldAbort）
        const bool hasPrev = (y > 0);
        const bool hasNext = (y + 1 < h);
        if (hasNext)
            loadRow(next, image, y + 1);

        QRgb *d = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            int gxR = 0, gxG = 0, gxB = 0;
            int gyR = 0, gyG = 0, gyB = 0;
            for (int dy = -1; dy <= 1; ++dy) {
                const QVector<QRgb> &row = (dy < 0) ? prev : ((dy == 0) ? cur : next);
                const bool valid = (dy < 0) ? hasPrev : ((dy == 0) ? true : hasNext);
                for (int dx = -1; dx <= 1; ++dx) {
                    const QRgb p = at(row, valid, x + dx);
                    // Sobel 权重：gx 用 dx（中间行加倍），gy 用 dy（中间列加倍）
                    const int kx = dx * (dy == 0 ? 2 : 1);
                    const int ky = dy * (dx == 0 ? 2 : 1);
                    const int r = qRed(p), g = qGreen(p), b = qBlue(p);
                    gxR += kx * r; gxG += kx * g; gxB += kx * b;
                    gyR += ky * r; gyG += ky * g; gyB += ky * b;
                }
            }
            // alpha 取当前像素原始 alpha（cur[x] 就是原始值）
            d[x] = qRgba(mag(gxR, gyR), mag(gxG, gyG), mag(gxB, gyB), qAlpha(cur[x]));
        }

        // 滚动：prev ← cur，cur ← next，next 的旧缓冲留作下轮装载
        std::swap(prev, cur);
        std::swap(cur, next);
    }

    // 2) 直通 → 预乘（与第 1 步配对）
    for (int y = 0; y < h; ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < w; ++x)
            row[x] = qPremultiply(row[x]);
    }
}

} // namespace Ps
