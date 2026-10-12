/**
 * edgepreserve.h — 保边邻域均值（表面模糊 / 特殊模糊共用）。
 */
#ifndef ENGINE_OP_EDGEPRESERVE_H
#define ENGINE_OP_EDGEPRESERVE_H

#include <QImage>
#include <functional>

namespace Ps {
namespace EdgePreserve {

/** 直通 ARGB：邻域内 max(|ΔR|,|ΔG|,|ΔB|)≤thr 的像素均值（至少含中心）。 */
void blurStraight(QImage *straightArgb32, int radius, int threshold,
                  const std::function<bool()> &shouldAbort);

/**
 * 直通 ARGB 边缘图：邻域内存在超阈值像素 → 白，否则黑；保留 alpha。
 * @p radius 为搜索半径（≥1）。
 */
void edgesStraight(QImage *straightArgb32, int radius, int threshold,
                   const std::function<bool()> &shouldAbort);

} // namespace EdgePreserve
} // namespace Ps

#endif
