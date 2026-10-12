/**
 * edgepreserve.cpp — 保边邻域均值 / 边缘检测。
 */
#include "edgepreserve.h"

#include <QtGlobal>

namespace Ps {
namespace EdgePreserve {

namespace {

int chanDiff(QRgb a, QRgb b)
{
    return qMax(qAbs(qRed(a) - qRed(b)),
                qMax(qAbs(qGreen(a) - qGreen(b)), qAbs(qBlue(a) - qBlue(b))));
}

} // namespace

void blurStraight(QImage *img, int radius, int threshold, const std::function<bool()> &shouldAbort)
{
    if (!img || img->isNull() || radius <= 0)
        return;
    const int w = img->width();
    const int h = img->height();
    const int r = radius;
    const int thr = qMax(0, threshold);
    const QImage src = *img;

    for (int y = 0; y < h; ++y) {
        if ((y & 15) == 0 && shouldAbort && shouldAbort())
            return;
        QRgb *dst = reinterpret_cast<QRgb *>(img->scanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb center = reinterpret_cast<const QRgb *>(src.constScanLine(y))[x];
            qint64 R = 0, G = 0, B = 0, A = 0;
            int n = 0;
            for (int dy = -r; dy <= r; ++dy) {
                const int yy = y + dy;
                if (yy < 0 || yy >= h)
                    continue;
                const QRgb *row = reinterpret_cast<const QRgb *>(src.constScanLine(yy));
                for (int dx = -r; dx <= r; ++dx) {
                    const int xx = x + dx;
                    if (xx < 0 || xx >= w)
                        continue;
                    const QRgb p = row[xx];
                    if (chanDiff(center, p) > thr)
                        continue;
                    R += qRed(p);
                    G += qGreen(p);
                    B += qBlue(p);
                    A += qAlpha(p);
                    ++n;
                }
            }
            if (n <= 0) {
                dst[x] = center;
            } else {
                dst[x] = qRgba(int(R / n), int(G / n), int(B / n), int(A / n));
            }
        }
    }
}

void edgesStraight(QImage *img, int radius, int threshold, const std::function<bool()> &shouldAbort)
{
    if (!img || img->isNull())
        return;
    const int w = img->width();
    const int h = img->height();
    const int r = qMax(1, radius);
    const int thr = qMax(0, threshold);
    const QImage src = *img;

    for (int y = 0; y < h; ++y) {
        if ((y & 15) == 0 && shouldAbort && shouldAbort())
            return;
        QRgb *dst = reinterpret_cast<QRgb *>(img->scanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb center = reinterpret_cast<const QRgb *>(src.constScanLine(y))[x];
            const int a = qAlpha(center);
            bool edge = false;
            for (int dy = -r; dy <= r && !edge; ++dy) {
                const int yy = y + dy;
                if (yy < 0 || yy >= h)
                    continue;
                const QRgb *row = reinterpret_cast<const QRgb *>(src.constScanLine(yy));
                for (int dx = -r; dx <= r; ++dx) {
                    if (dx == 0 && dy == 0)
                        continue;
                    const int xx = x + dx;
                    if (xx < 0 || xx >= w)
                        continue;
                    if (chanDiff(center, row[xx]) > thr) {
                        edge = true;
                        break;
                    }
                }
            }
            dst[x] = edge ? qRgba(255, 255, 255, a) : qRgba(0, 0, 0, a);
        }
    }
}

} // namespace EdgePreserve
} // namespace Ps
