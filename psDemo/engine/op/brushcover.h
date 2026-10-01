/**
 * brushcover.h — 圆形笔刷盖度（engine/op 层共用）。
 *
 * 对照 StampDab 径向渐变 stop：硬核内为 1，外缘线性落到 0。
 * 提供 dist² 版以避免圆外像素无意义的 sqrt。
 */
#ifndef ENGINE_OP_BRUSHCOVER_H
#define ENGINE_OP_BRUSHCOVER_H

#include <QtMath>

namespace Ps {
namespace BrushCover {

/**
 * @param dist2    到笔心距离的平方（文档/层像素）
 * @param radius   笔刷半径
 * @param hardness [0,1]，对照渐变 stop
 * @return 盖度 [0,1]
 */
inline qreal fromDist2(qreal dist2, qreal radius, qreal hardness)
{
    if (radius <= 0.0)
        return 0.0;
    const qreal r2 = radius * radius;
    if (dist2 >= r2)
        return 0.0;

    hardness = qBound(0.0, hardness, 1.0);
    const qreal stop = qBound(0.05, hardness, 0.98) * radius;
    const qreal stop2 = stop * stop;
    if (dist2 <= stop2)
        return 1.0;

    // 软边才开方
    const qreal dist = qSqrt(dist2);
    const qreal t = (dist - stop) / (radius - stop);
    return qBound(0.0, 1.0 - t, 1.0);
}

} // namespace BrushCover
} // namespace Ps

#endif // ENGINE_OP_BRUSHCOVER_H
