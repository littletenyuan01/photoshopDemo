#ifndef ENGINE_PAINTSELECTIONCLIP_H
#define ENGINE_PAINTSELECTIONCLIP_H

namespace Ps {

class Selection;

/**
 * 可选选区裁剪参数（对照 drawable 与 image mask 相交）。
 * selection 为空指针、或选区为空时不裁剪（整层可画）。
 */
struct PaintSelectionClip {
    const Selection *selection = nullptr;
    int layerOffsetX = 0;
    int layerOffsetY = 0;
};

} // namespace Ps

#endif // ENGINE_PAINTSELECTIONCLIP_H
