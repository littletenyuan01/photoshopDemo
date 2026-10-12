/**
 * radialblurfilterop.cpp — 径向模糊：绕图心旋转弧 / 向图心缩放取样平均。
 */
#include "radialblurfilterop.h"

#include <QtMath>

namespace Ps {

bool RadialBlurFilterOp::prepare(const FilterContext &ctx)
{
    if (!ctx.node || ctx.size.isEmpty())
        return false;
    m_amount = qBound(1, ctx.node->radialAmount(), 50);
    m_method = qBound(0, ctx.node->radialMethod(), 1);
    return true;
}

void RadialBlurFilterOp::finish(const FilterContext &ctx)
{
    Q_UNUSED(ctx);
    m_amount = 10;
    m_method = 0;
}

void RadialBlurFilterOp::process(QImage &image, const FilterContext &ctx)
{
    if (image.isNull() || m_amount <= 0)
        return;
    const int w = image.width();
    const int h = image.height();
    const double cx = (w - 1) * 0.5;
    const double cy = (h - 1) * 0.5;
    // 取样数随强度略增，上限 24（oracle 与实现共用同一公式）
    const int N = qBound(2, 4 + m_amount / 2, 24);

    QImage work = image.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < h; ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(work.scanLine(y));
        for (int x = 0; x < w; ++x)
            row[x] = qUnpremultiply(row[x]);
    }

    auto sample = [&](double fx, double fy) -> QRgb {
        const int ix = qBound(0, int(qRound(fx)), w - 1);
        const int iy = qBound(0, int(qRound(fy)), h - 1);
        return reinterpret_cast<const QRgb *>(work.constScanLine(iy))[ix];
    };

    QImage out(w, h, QImage::Format_ARGB32);
    if (m_method == 0) {
        // 旋转：总弧 = amount 度，对称取样
        const double halfArc = (double(m_amount) * M_PI / 180.0) * 0.5;
        for (int y = 0; y < h; ++y) {
            if ((y & 15) == 0 && aborted(ctx))
                return;
            QRgb *dst = reinterpret_cast<QRgb *>(out.scanLine(y));
            for (int x = 0; x < w; ++x) {
                qint64 R = 0, G = 0, B = 0, A = 0;
                const double dx = x - cx;
                const double dy = y - cy;
                for (int i = 0; i < N; ++i) {
                    const double t = (N == 1) ? 0.0 : (double(i) / (N - 1) * 2.0 - 1.0);
                    const double ang = t * halfArc;
                    const double ca = qCos(ang);
                    const double sa = qSin(ang);
                    const QRgb p = sample(cx + dx * ca - dy * sa, cy + dx * sa + dy * ca);
                    R += qRed(p);
                    G += qGreen(p);
                    B += qBlue(p);
                    A += qAlpha(p);
                }
                dst[x] = qRgba(int(R / N), int(G / N), int(B / N), int(A / N));
            }
        }
    } else {
        // 缩放：向图心收缩，最大收缩量 = amount/100
        const double zoomFrac = double(m_amount) / 100.0;
        for (int y = 0; y < h; ++y) {
            if ((y & 15) == 0 && aborted(ctx))
                return;
            QRgb *dst = reinterpret_cast<QRgb *>(out.scanLine(y));
            for (int x = 0; x < w; ++x) {
                qint64 R = 0, G = 0, B = 0, A = 0;
                const double dx = x - cx;
                const double dy = y - cy;
                for (int i = 0; i < N; ++i) {
                    const double t = (N == 1) ? 0.0 : double(i) / (N - 1);
                    const double s = 1.0 - t * zoomFrac;
                    const QRgb p = sample(cx + dx * s, cy + dy * s);
                    R += qRed(p);
                    G += qGreen(p);
                    B += qBlue(p);
                    A += qAlpha(p);
                }
                dst[x] = qRgba(int(R / N), int(G / N), int(B / N), int(A / N));
            }
        }
    }

    image = QImage(w, h, image.format());
    for (int y = 0; y < h; ++y) {
        const QRgb *s = reinterpret_cast<const QRgb *>(out.constScanLine(y));
        QRgb *d = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < w; ++x)
            d[x] = qPremultiply(s[x]);
    }
}

} // namespace Ps
