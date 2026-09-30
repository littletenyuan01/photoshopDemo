/**
 * magneticedgesnap.h — 磁性套索局部边缘吸附（engine 层）。
 *
 * 对照 GIMP Intelligent Scissors（gimptilehandleriscissors：模糊 + 水平/垂直导数）
 * 的瘦身：不对全图建 livewire 图，只在光标邻域做 Sobel 幅值并取最大点。
 * 供 MagneticLassoTool 在拖拽采样时把折线吸到高对比边界。
 */
#ifndef ENGINE_MAGNETICEDGESNAP_H
#define ENGINE_MAGNETICEDGESNAP_H

#include <QImage>
#include <QPointF>
#include <QtGlobal>

#include <cmath>

namespace Ps {
namespace MagneticEdgeSnap {

/**
 * 在 @p source（预乘 ARGB32 合成图）上，把 @p cursor 吸附到半径 @p radiusPx
 * 内梯度幅值最大的像素中心；邻域无强边则原样返回。
 *
 * @param radiusPx 搜索半径（文档像素，≥1）
 * @param minMagnitude 低于此幅值视为「无边」，不吸附（0..~1448 for 8bit sobel）
 */
inline QPointF snap(const QImage &source, const QPointF &cursor,
                    int radiusPx = 12, qreal minMagnitude = 40.0)
{
    if (source.isNull() || source.width() < 3 || source.height() < 3)
        return cursor;

    const int w = source.width();
    const int h = source.height();
    const int cx = qBound(0, int(std::lround(cursor.x())), w - 1);
    const int cy = qBound(0, int(std::lround(cursor.y())), h - 1);
    const int R = qMax(1, radiusPx);

    auto grayAt = [&](int x, int y) -> int {
        x = qBound(0, x, w - 1);
        y = qBound(0, y, h - 1);
        const QRgb px = reinterpret_cast<const QRgb *>(source.constScanLine(y))[x];
        // 预乘近似亮度；透明处当 0，避免棋盘外噪声
        const int a = qAlpha(px);
        if (a <= 0)
            return 0;
        // 解预乘后再算 luma（整数近似）
        const int r = (qRed(px) * 255 + a / 2) / a;
        const int g = (qGreen(px) * 255 + a / 2) / a;
        const int b = (qBlue(px) * 255 + a / 2) / a;
        return (r * 30 + g * 59 + b * 11) / 100;
    };

    qreal bestMag = -1.0;
    int bestX = cx;
    int bestY = cy;

    // 邻域内逐点 Sobel；边界用 clamp（对照 iscissors 的导数卷积思路）
    for (int y = cy - R; y <= cy + R; ++y) {
        for (int x = cx - R; x <= cx + R; ++x) {
            if (x < 0 || y < 0 || x >= w || y >= h)
                continue;
            const int dx = x - cx;
            const int dy = y - cy;
            if (dx * dx + dy * dy > R * R)
                continue;

            const int g00 = grayAt(x - 1, y - 1);
            const int g10 = grayAt(x, y - 1);
            const int g20 = grayAt(x + 1, y - 1);
            const int g01 = grayAt(x - 1, y);
            const int g21 = grayAt(x + 1, y);
            const int g02 = grayAt(x - 1, y + 1);
            const int g12 = grayAt(x, y + 1);
            const int g22 = grayAt(x + 1, y + 1);

            const int sx = (g20 + 2 * g21 + g22) - (g00 + 2 * g01 + g02);
            const int sy = (g02 + 2 * g12 + g22) - (g00 + 2 * g10 + g20);
            const qreal mag = std::hypot(qreal(sx), qreal(sy));
            if (mag > bestMag) {
                bestMag = mag;
                bestX = x;
                bestY = y;
            }
        }
    }

    if (bestMag < minMagnitude)
        return cursor;

    // 像素中心，与选区光栅化网格一致
    return QPointF(bestX + 0.5, bestY + 0.5);
}

} // namespace MagneticEdgeSnap
} // namespace Ps

#endif // ENGINE_MAGNETICEDGESNAP_H
