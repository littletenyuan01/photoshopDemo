/**
 * pathstrokeop.h — 沿路径等距落 dab 描边（engine/op 层）。
 *
 * 【为什么不用 QPainter::strokePath】那会得到纯色描边，与画笔 dab 观感不一致；
 * 本类复用 PaintEngine::stampDab，与 painttool 同一条路径。
 * 【不进 OpName】同 PathFillOp（pen-tool-plan §4.3）。
 */
#ifndef ENGINE_OP_PATHSTROKEOP_H
#define ENGINE_OP_PATHSTROKEOP_H

#include "domain/path.h"
#include "engine/paintengine.h"
#include "engine/painttypes.h"

#include <QColor>
#include <QRect>
#include <QVector>

namespace Ps {

class TileBuffer;

class PathStrokeOp
{
public:
    /**
     * 沿路径描边。
     * @param halfWidth dab 半径（= 选项栏线宽 / 2）
     * @param spacing   相邻 dab 间距系数（0.25 ≈ PS 默认）
     * @return 层内脏矩形并集；空路径返回空矩形。
     */
    static QRect stroke(const Path &path, TileBuffer &tiles, const QColor &color,
                        qreal halfWidth, qreal spacing = 0.25,
                        PaintEngine::Mode mode = PaintMode::Paint,
                        qreal hardness = 0.85,
                        PaintSelectionClip clip = PaintSelectionClip());

    /**
     * 仅计算会落 dab 的中心点（测间距用；不改像素）。
     * 步长 = spacing * 2 * halfWidth。
     */
    static QVector<QPointF> dabCenters(const Path &path, qreal halfWidth,
                                       qreal spacing = 0.25);
};

} // namespace Ps

#endif
