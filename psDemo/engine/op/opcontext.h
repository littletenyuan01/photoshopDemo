/**
 * opcontext.h — 缓冲算子运行时上下文（engine/op 层）。
 *
 * tiles / selection / clip / roi 由 PaintEngine 组装；OpRunner 校验 pad 后交给算子。
 * 对照 GEGL OperationContext / pad 绑定。
 */
#ifndef ENGINE_OP_OPCONTEXT_H
#define ENGINE_OP_OPCONTEXT_H

#include "engine/paintselectionclip.h"

#include <QRect>

namespace Ps {

class Selection;
class TileBuffer;

/**
 * 算子运行时上下文（对照 GEGL OperationContext / pad 绑定）。
 * tiles → OpPad::Tiles；selection → OpPad::Selection（写入目标）；
 * clip.selection → 绘制时可选裁剪（与写入 pad 不同用途）。
 * 由 PaintEngine 组装；OpRunner 按注册表校验后再交给算子。
 */
struct OpContext
{
    TileBuffer *tiles = nullptr;     ///< 读写目标（层内坐标）；选区算子可为空
    Selection *selection = nullptr;  ///< 文档级选区写入目标；绘制算子可为空
    PaintSelectionClip clip;         ///< 绘制裁剪：空选区 / 空指针 → 不裁
    /**
     * 可选兴趣区（层内坐标）；空 = 整个缓冲。
     *
     * 缓冲算子经 `OpPaintClip::operationWindow()` / `roiWindow()` 消费它，
     * 把遍历与临时缓冲限制在窗口内（对照 GEGL：ROI 决定 affected region）。
     * 脏区驱动重算（见 docs/architecture.md §8.1 主线 ②）落地后由上层注入。
     */
    QRect roi;

    /** 从层瓦片与选区裁剪参数构造上下文（roi 默认空 = 整层）。 */
    static OpContext fromTiles(TileBuffer &tiles, const PaintSelectionClip &clip)
    {
        OpContext ctx;
        ctx.tiles = &tiles;
        ctx.clip = clip;
        return ctx;
    }

    /** 选区写入算子上下文（对照 gimp_channel_select_* 作用在 image mask 上）。 */
    static OpContext fromSelection(Selection &selection)
    {
        OpContext ctx;
        ctx.selection = &selection;
        return ctx;
    }
};

} // namespace Ps

#endif // ENGINE_OP_OPCONTEXT_H
