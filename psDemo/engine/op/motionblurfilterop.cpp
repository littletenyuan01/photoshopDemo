/**
 * motionblurfilterop.cpp — 运动模糊实现（engine/op 层）。
 *
 * 一维盒模糊：每行（水平）或每列（垂直）用滑动窗口求和，O(1)/像素。
 * abyss = 透明（越界样本 0，仍除以完整窗口 2r+1）——与盒模糊同口径，故边缘会自然淡出。
 * 45° = 先水平一趟再垂直一趟（近似对角，口径已在头文件注明）。
 */
#include "motionblurfilterop.h"

namespace Ps {

bool MotionBlurFilterOp::prepare(const FilterContext &ctx)
{
    if (!ctx.node || ctx.size.isEmpty())
        return false;
    m_distance = qBound(1, ctx.node->motionDistance(), 128);
    m_angle = ctx.node->motionAngle();
    return true;
}

void MotionBlurFilterOp::finish(const FilterContext &ctx)
{
    Q_UNUSED(ctx);
    m_distance = 0;
    m_angle = 0;
}

void MotionBlurFilterOp::process(QImage &image, const FilterContext &ctx)
{
    const int w = image.width();
    const int h = image.height();
    const int r = m_distance;
    if (w <= 0 || h <= 0 || r <= 0)
        return;

    const bool doH = (m_angle == 0 || m_angle == 45);
    const bool doV = (m_angle == 90 || m_angle == 45);

    // 预乘 → 直通（与其它模糊算子同口径）
    for (int y = 0; y < h; ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < w; ++x)
            row[x] = qUnpremultiply(row[x]);
    }

    const int n = 2 * r + 1;
    // 水平一趟：out(x,y) = Σ_{k=-r..r} in(x+k, y) / n（越界按 0）
    const auto boxH = [&](const QImage &in) -> QImage {
        QImage out(in.size(), QImage::Format_ARGB32);
        for (int y = 0; y < h; ++y) {
            const QRgb *s = reinterpret_cast<const QRgb *>(in.constScanLine(y));
            QRgb *d = reinterpret_cast<QRgb *>(out.scanLine(y));
            int R = 0, G = 0, B = 0, A = 0;
            for (int k = -r; k <= r; ++k) {
                if (k < 0 || k >= w)
                    continue;
                const QRgb p = s[k];
                R += qRed(p); G += qGreen(p); B += qBlue(p); A += qAlpha(p);
            }
            for (int x = 0; x < w; ++x) {
                d[x] = qRgba(R / n, G / n, B / n, A / n);
                const int add = x + r + 1;
                const int del = x - r;
                if (add < w) {
                    const QRgb p = s[add];
                    R += qRed(p); G += qGreen(p); B += qBlue(p); A += qAlpha(p);
                }
                if (del >= 0) {
                    const QRgb p = s[del];
                    R -= qRed(p); G -= qGreen(p); B -= qBlue(p); A -= qAlpha(p);
                }
            }
        }
        return out;
    };

    // 垂直一趟：out(x,y) = Σ_{k=-r..r} in(x, y+k) / n（越界按 0）
    const auto boxV = [&](const QImage &in) -> QImage {
        QImage out(in.size(), QImage::Format_ARGB32);
        for (int x = 0; x < w; ++x) {
            int R = 0, G = 0, B = 0, A = 0;
            for (int k = -r; k <= r; ++k) {
                if (k < 0 || k >= h)
                    continue;
                const QRgb p = reinterpret_cast<const QRgb *>(in.constScanLine(k))[x];
                R += qRed(p); G += qGreen(p); B += qBlue(p); A += qAlpha(p);
            }
            for (int y = 0; y < h; ++y) {
                reinterpret_cast<QRgb *>(out.scanLine(y))[x] = qRgba(R / n, G / n, B / n, A / n);
                const int add = y + r + 1;
                const int del = y - r;
                if (add < h) {
                    const QRgb p = reinterpret_cast<const QRgb *>(in.constScanLine(add))[x];
                    R += qRed(p); G += qGreen(p); B += qBlue(p); A += qAlpha(p);
                }
                if (del >= 0) {
                    const QRgb p = reinterpret_cast<const QRgb *>(in.constScanLine(del))[x];
                    R -= qRed(p); G -= qGreen(p); B -= qBlue(p); A -= qAlpha(p);
                }
            }
        }
        return out;
    };

    QImage work = image; // 已是直通
    if (aborted(ctx))
        return;
    if (doH) {
        work = boxH(work);
        if (aborted(ctx))
            return;
    }
    if (doV) {
        work = boxV(work);
        if (aborted(ctx))
            return;
    }

    // 直通 → 预乘，并**保持原格式**（中间图是 Format_ARGB32，直接 `image = work`
    // 会把管线约定的 ARGB32_Premultiplied 改掉 → 下游逐像素比较/合成就对不上）。
    QImage result(image.size(), QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < h; ++y) {
        const QRgb *s = reinterpret_cast<const QRgb *>(work.constScanLine(y));
        QRgb *d = reinterpret_cast<QRgb *>(result.scanLine(y));
        for (int x = 0; x < w; ++x)
            d[x] = qPremultiply(s[x]);
    }
    image = result;
}

} // namespace Ps
