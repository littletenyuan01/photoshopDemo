/**
 * motionblurfilterop.cpp — 运动模糊实现（engine/op 层）。
 */
#include "motionblurfilterop.h"

#include <QtMath>

#include <cmath>

namespace Ps {

bool MotionBlurFilterOp::prepare(const FilterContext &ctx)
{
    if (!ctx.node || ctx.size.isEmpty())
        return false;
    m_distance = qBound(1, ctx.node->motionDistance(), 128);
    m_angle = qBound(0, ctx.node->motionAngle(), 179);
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

    // 预乘 → 直通
    for (int y = 0; y < h; ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < w; ++x)
            row[x] = qUnpremultiply(row[x]);
    }

    const int n = 2 * r + 1;
    const int ang = m_angle % 180;

    const auto boxH = [&](const QImage &in) -> QImage {
        QImage out(in.size(), QImage::Format_ARGB32);
        for (int y = 0; y < h; ++y) {
            if ((y & 31) == 0 && aborted(ctx))
                return out;
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

    const auto boxV = [&](const QImage &in) -> QImage {
        QImage out(in.size(), QImage::Format_ARGB32);
        for (int x = 0; x < w; ++x) {
            if ((x & 31) == 0 && aborted(ctx))
                return out;
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

    QImage work = image;
    if (aborted(ctx))
        return;

    if (ang == 0) {
        work = boxH(work);
    } else if (ang == 90) {
        work = boxV(work);
    } else {
        // 任意角：沿方向最近邻取样平均
        const double rad = double(ang) * M_PI / 180.0;
        const double c = std::cos(rad);
        const double s = std::sin(rad);
        QImage out(w, h, QImage::Format_ARGB32);
        for (int y = 0; y < h; ++y) {
            if ((y & 15) == 0 && aborted(ctx))
                return;
            QRgb *d = reinterpret_cast<QRgb *>(out.scanLine(y));
            for (int x = 0; x < w; ++x) {
                int R = 0, G = 0, B = 0, A = 0;
                for (int k = -r; k <= r; ++k) {
                    const int xx = int(std::lround(x + k * c));
                    const int yy = int(std::lround(y + k * s));
                    if (xx < 0 || xx >= w || yy < 0 || yy >= h)
                        continue;
                    const QRgb p = reinterpret_cast<const QRgb *>(work.constScanLine(yy))[xx];
                    R += qRed(p); G += qGreen(p); B += qBlue(p); A += qAlpha(p);
                }
                d[x] = qRgba(R / n, G / n, B / n, A / n);
            }
        }
        work = out;
    }

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
