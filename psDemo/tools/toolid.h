#ifndef TOOLID_H
#define TOOLID_H

namespace Ps {

/**
 * 工具 ID（对应 GIMP GimpToolInfo / Photoshop 工具条项）。
 * 分组显示由 ToolBox 的 ToolSlot 负责（参考 GIMP etc/toolrc 的 GimpToolGroup、
 * Photoshop 右键飞出菜单）：同组工具共用一个工具栏占位，右键展开子菜单。
 *
 * 顺序即工具箱从上到下的顺序，按 Photoshop 的默认工具组排列。
 *
 * ⚠️ **实现状态**：目前只有 Move / Hand / Zoom / Brush / Eraser 有实际逻辑
 * （见 `tools/ToolManager` 的注册表）。其余是 **UI 占位** —— 选中后
 * ToolManager 会回退到中性工具（不消费事件，等同无操作），选项栏提示「尚未接入」。
 * 布局对齐 PS 是为了让界面完整可演示，不代表功能已实现。
 */
enum class ToolId {
    // 移动（V）
    Move = 0,

    // 选框组（M）
    RectSelect,
    EllipseSelect,

    // 套索组（L）
    Lasso,
    PolygonalLasso,
    MagneticLasso,

    // 快速选择组（W）
    QuickSelect,
    MagicWand,

    // 裁剪组（C）
    Crop,
    PerspectiveCrop,

    // 吸管（I）
    Eyedropper,

    // 画笔组（B）
    Brush,
    Pencil,
    MixerBrush,

    // 图章（S）
    CloneStamp,

    // 橡皮擦组（E）
    Eraser,
    BackgroundEraser,

    // 填充组（G）
    PaintBucket,
    Gradient,

    // 聚焦组（模糊 / 锐化 / 涂抹）
    Blur,
    Sharpen,
    Smudge,

    // 色调组（减淡 / 海绵）
    Dodge,
    Sponge,

    // 钢笔组（P）
    Pen,
    FreeformPen,
    AddAnchorPoint,

    // 文字组（T）
    Type,
    TypeVertical,

    // 形状组（U）
    ShapeRect,
    ShapeEllipse,
    ShapeTriangle,
    ShapeLine,

    // 视图
    Hand,
    Zoom,
};

} // namespace Ps

#endif // TOOLID_H
