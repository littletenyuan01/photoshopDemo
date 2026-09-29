#ifndef PAINTENGINE_H
#define PAINTENGINE_H

#include <QColor>
#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QRect>

namespace Ps {

class TileBuffer;
class Selection;

/**
 * 可选选区裁剪参数（对照 drawable 与 image mask 相交）。
 * selection 为空指针、或选区为空时不裁剪（整层可画）。
 * 放在 PaintEngine 外，避免嵌套类 + 默认实参在 MinGW 下的完整性错误。
 */
struct PaintSelectionClip {
    const Selection *selection = nullptr;
    int layerOffsetX = 0;
    int layerOffsetY = 0;
};

/**
 * 像素绘制引擎（engine）。
 *
 * 【对照 GIMP】
 * - GIMP：`app/tools` 只处理指针/UI；真正写缓冲在 `app/paint/GimpPaintCore`
 * - 本项目：圆形 dab + 线段插值 + 油漆桶洪泛填充 + 渐变填充；可写整幅 QImage 或懒分配 TileBuffer。
 *
 * 约定：
 * - 图像须为 Format_ARGB32_Premultiplied。
 * - 坐标为**层内像素坐标**（调用方已减 Layer offset）。
 * - Paint：SourceOver；Erase：DestinationOut；Fill：直接写入目标色（预乘）；
 *   Gradient：先画到透明叠加层再 SourceOver。
 * - 选区：对照 `gimp_item_mask_intersect` —— **空选区不约束**；非空则只改 mask>0 的文档像素
 *   （层内坐标 + layerOffset → 文档坐标查 Selection）。
 */
class PaintEngine
{
public:
    enum class Mode {
        Paint,
        Erase,
    };

    /** @deprecated 兼容旧名；请用 PaintSelectionClip。 */
    using SelectionClip = PaintSelectionClip;

    static void stampDab(QImage &target,
                         const QPointF &center,
                         qreal radius,
                         const QColor &color,
                         Mode mode,
                         qreal hardness = 0.85,
                         PaintSelectionClip clip = PaintSelectionClip());

    /** 写入瓦片缓冲：只 ensure dab 覆盖到的格（对照 GEGL 写时分配）。 */
    static void stampDab(TileBuffer &tiles,
                         const QPointF &center,
                         qreal radius,
                         const QColor &color,
                         Mode mode,
                         qreal hardness = 0.85,
                         PaintSelectionClip clip = PaintSelectionClip());

    static QPointF strokeSegment(QImage &target,
                                 const QPointF &from,
                                 const QPointF &to,
                                 qreal radius,
                                 const QColor &color,
                                 Mode mode,
                                 qreal hardness = 0.85,
                                 qreal spacing = 0.25,
                                 PaintSelectionClip clip = PaintSelectionClip());

    static QPointF strokeSegment(TileBuffer &tiles,
                                 const QPointF &from,
                                 const QPointF &to,
                                 qreal radius,
                                 const QColor &color,
                                 Mode mode,
                                 qreal hardness = 0.85,
                                 qreal spacing = 0.25,
                                 PaintSelectionClip clip = PaintSelectionClip());

    /**
     * 油漆桶填充（对照 GIMP Bucket Fill 的瘦身版）。
     *
     * 有选区时只填 mask 内；种子在选区外则不填。
     * @param seed 种子点（层内像素）
     * @return 脏矩形；未改动返回空
     */
    static QRect floodFill(TileBuffer &tiles,
                           const QPoint &seed,
                           const QColor &fillColor,
                           int tolerance,
                           bool contiguous,
                           PaintSelectionClip clip = PaintSelectionClip());

    /**
     * 渐变形状（对照 GimpGradientType 子集；数值 = 选项栏 gradTypeCombo 顺序）。
     */
    enum class GradientType {
        Linear = 0,   ///< ≈ GIMP_GRADIENT_LINEAR
        Radial,       ///< ≈ GIMP_GRADIENT_RADIAL
        Angle,        ///< ≈ GIMP_GRADIENT_CONICAL_ASYMMETRIC
        Reflected,    ///< ≈ GIMP_GRADIENT_BILINEAR（对称/双线性）
        Diamond,      ///< ≈ GIMP_GRADIENT_SQUARE（轴对齐方距；非旋转菱形）
    };

    /**
     * 渐变填充（对照 GIMP Blend/Gradient 工具的瘦身版）。
     * 有选区时只写入 mask 内像素。
     */
    static QRect applyGradient(TileBuffer &tiles,
                               const QPointF &start,
                               const QPointF &end,
                               const QColor &foreground,
                               const QColor &background,
                               GradientType type,
                               qreal opacity,
                               int offsetPercent,
                               bool reverse,
                               bool dither,
                               PaintSelectionClip clip = PaintSelectionClip());
};

} // namespace Ps

#endif // PAINTENGINE_H
