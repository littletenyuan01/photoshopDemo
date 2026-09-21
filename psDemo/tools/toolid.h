#ifndef TOOLID_H
#define TOOLID_H

namespace Ps {

/**
 * 工具 ID（对应 GIMP GimpToolInfo / Photoshop 工具条项）。
 * 分组显示由 ToolBox 的 ToolSlot 负责（参考 GIMP etc/toolrc 的 GimpToolGroup、
 * Photoshop 右键飞出菜单）。
 */
enum class ToolId {
    Move = 0,

    // 选框组
    RectSelect,
    EllipseSelect,

    // 套索
    Lasso,

    // 选择
    MagicWand,

    Crop,
    Eyedropper,

    Brush,
    Eraser,

    // 填充组
    PaintBucket,
    Gradient,

    // 文字
    Type,

    // 形状组（矩形/椭圆/三角/直线 — 同一工具栏占位）
    ShapeRect,
    ShapeEllipse,
    ShapeTriangle,
    ShapeLine,

    Hand,
    Zoom,
};

} // namespace Ps

#endif // TOOLID_H
