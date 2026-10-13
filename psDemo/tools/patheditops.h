/**
 * patheditops.h — 路径锚点编辑纯函数（tools 层，无 Qt Widgets）。
 *
 * PenTool 的 Ctrl/Alt/线段分支与 Add/Delete/Convert 三工具共用，禁止各写一份几何。
 */
#ifndef PATHEDITOPS_H
#define PATHEDITOPS_H

#include "domain/path.h"

namespace Ps {
namespace PathEditOps {

/** 在 (sub, seg) 上按 t 插入锚点（de Casteljau，形状不变）。 */
bool addAnchorAt(Path &p, int sub, int seg, qreal t);

/** 删除锚点；端点坐标保持不动。 */
bool deleteAnchorAt(Path &p, int sub, int idx);

/**
 * 尖角 ↔ 平滑切换。
 * 切到平滑时把 out 设为 in 的镜像（共线），与钢笔 Alt+点 同口径。
 */
bool toggleCorner(Path &p, int sub, int idx);

} // namespace PathEditOps
} // namespace Ps

#endif
