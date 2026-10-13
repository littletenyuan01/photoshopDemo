/**
 * pathfillop.h — 路径栅格化为灰度覆盖率 mask（engine/op 层）。
 *
 * 【对照】GIMP gimp_path_to_selection 的落地。
 * 【为什么是静态工具类、不进 OpName】
 * 由钢笔/路径树直接调用，不进 FilterStack / 调整层；若塞进 BrightnessContrast..Count
 * 段会被 isAdjustmentOp 默认当成调整层（见 pen-tool-plan §4.3）。
 *
 * 【为什么用 QPainterPath 而不是手写扫描线】
 * PS 口径是 non-zero（Qt::WindingFill）且要抗锯齿；Qt 光栅引擎两样都已做对。
 */
#ifndef ENGINE_OP_PATHFILLOP_H
#define ENGINE_OP_PATHFILLOP_H

#include "domain/path.h"

#include <QImage>
#include <QSize>
#include <Qt>

namespace Ps {

class PathFillOp
{
public:
    /**
     * 产出 Format_Grayscale8 覆盖率 mask（0..255，含 AA 软边）。
     * @param docSize 文档尺寸；空路径/单点返回全 0。
     * @param rule 默认 WindingFill（对齐 PS；测例会抓误用 OddEvenFill）。
     */
    static QImage rasterize(const Path &path, const QSize &docSize,
                            Qt::FillRule rule = Qt::WindingFill);
};

} // namespace Ps

#endif
