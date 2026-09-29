/**
 * paintselectionclip.h — 选区裁剪参数（engine 层）。
 *
 * 算子与 PaintEngine 共用；层坐标 + layerOffset 换算到文档选区 mask。
 * 对照 GIMP drawable 与 image selection 相交。
 */
#ifndef ENGINE_PAINTSELECTIONCLIP_H
#define ENGINE_PAINTSELECTIONCLIP_H

namespace Ps {

class Selection;

/**
 * 可选选区裁剪参数（对照 drawable 与 image mask 相交）。
 * selection 为空指针、或选区为空时不裁剪（整层可画）。
 */
struct PaintSelectionClip {
    const Selection *selection = nullptr; ///< 文档级选区；空指针 = 不裁
    int layerOffsetX = 0;                 ///< 层原点 → 文档坐标的 X 偏移
    int layerOffsetY = 0;                 ///< 层原点 → 文档坐标的 Y 偏移
};

} // namespace Ps

#endif // ENGINE_PAINTSELECTIONCLIP_H
