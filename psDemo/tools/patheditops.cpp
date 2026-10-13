/**
 * patheditops.cpp — PathEditOps 实现（薄封装 Path 几何）。
 */
#include "patheditops.h"

namespace Ps {
namespace PathEditOps {

bool addAnchorAt(Path &p, int sub, int seg, qreal t)
{
    return p.insertAnchor(sub, seg, t);
}

bool deleteAnchorAt(Path &p, int sub, int idx)
{
    return p.deleteAnchor(sub, idx);
}

bool toggleCorner(Path &p, int sub, int idx)
{
    if (sub < 0 || sub >= p.subPaths.size())
        return false;
    auto &anchors = p.subPaths[sub].anchors;
    if (idx < 0 || idx >= anchors.size())
        return false;
    PathAnchor &a = anchors[idx];
    const bool toCorner = !a.corner;
    if (!p.setCorner(sub, idx, toCorner))
        return false;
    // 切到平滑：镜像 out ← in，保证共线（对照 GIMP FEATURE_ALIGNED）
    if (!toCorner)
        a.outHandle = a.point - (a.inHandle - a.point);
    return true;
}

} // namespace PathEditOps
} // namespace Ps
