#ifndef ENGINE_OP_OPCONTEXT_H
#define ENGINE_OP_OPCONTEXT_H

#include "engine/paintselectionclip.h"

#include <QRect>

namespace Ps {

class TileBuffer;

/**
 * 算子运行时上下文（对照 GEGL OperationContext / pad 绑定）。
 * tiles → OpPad::Tiles；clip.selection → OpPad::Selection。
 * 由 PaintEngine 组装；OpRunner 按注册表校验后再交给算子。
 */
struct OpContext
{
    TileBuffer *tiles = nullptr; ///< 读写目标（层内坐标）
    PaintSelectionClip clip;     ///< 空选区 / 空指针 → 不裁
    /**
     * 可选兴趣区（层内坐标）；空 = 整个缓冲。
     *
     * 缓冲算子经 `OpPaintClip::operationWindow()` / `roiWindow()` 消费它，
     * 把遍历与临时缓冲限制在窗口内（对照 GEGL：ROI 决定 affected region）。
     * 脏区驱动重算（见 docs/architecture.md §8.1 主线 ②）落地后由上层注入。
     */
    QRect roi;

    static OpContext fromTiles(TileBuffer &tiles, const PaintSelectionClip &clip)
    {
        OpContext ctx;
        ctx.tiles = &tiles;
        ctx.clip = clip;
        return ctx;
    }
};

} // namespace Ps

#endif // ENGINE_OP_OPCONTEXT_H
