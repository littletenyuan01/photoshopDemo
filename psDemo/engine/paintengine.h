#ifndef PAINTENGINE_H
#define PAINTENGINE_H

#include <QColor>
#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QRect>

namespace Ps {

class TileBuffer;

/**
 * 像素绘制引擎（engine）。
 *
 * 【对照 GIMP】
 * - GIMP：`app/tools` 只处理指针/UI；真正写缓冲在 `app/paint/GimpPaintCore`
 * - 本项目：圆形 dab + 线段插值 + 油漆桶洪泛填充 + 渐变填充；可写整幅 QImage 或懒分配 TileBuffer。
 *
 * 约定：
 * - 图像须为 Format_ARGB32_Premultiplied。
 * - 坐标为**图像像素坐标**。
 * - Paint：SourceOver；Erase：DestinationOut；Fill：直接写入目标色（预乘）；
 *   Gradient：先画到透明叠加层再 SourceOver。
 */
class PaintEngine
{
public:
    enum class Mode {
        Paint,
        Erase,
    };

    static void stampDab(QImage &target,
                         const QPointF &center,
                         qreal radius,
                         const QColor &color,
                         Mode mode,
                         qreal hardness = 0.85);

    /** 写入瓦片缓冲：只 ensure dab 覆盖到的格（对照 GEGL 写时分配）。 */
    static void stampDab(TileBuffer &tiles,
                         const QPointF &center,
                         qreal radius,
                         const QColor &color,
                         Mode mode,
                         qreal hardness = 0.85);

    static QPointF strokeSegment(QImage &target,
                                 const QPointF &from,
                                 const QPointF &to,
                                 qreal radius,
                                 const QColor &color,
                                 Mode mode,
                                 qreal hardness = 0.85,
                                 qreal spacing = 0.25);

    static QPointF strokeSegment(TileBuffer &tiles,
                                 const QPointF &from,
                                 const QPointF &to,
                                 qreal radius,
                                 const QColor &color,
                                 Mode mode,
                                 qreal hardness = 0.85,
                                 qreal spacing = 0.25);

    /**
     * 油漆桶填充（对照 GIMP Bucket Fill 的瘦身版）。
     *
     * GIMP 分层：
     * - tools：`gimpbucketfilltool.c`（事件）+ `gimpbucketfilloptions.c`（threshold / fill-mode…）
     * - core：`gimpdrawable-bucket-fill.c`（建 fill buffer 并 apply）
     * - 区域：`gimppickable-contiguous-region.cc`（by_seed / by_color）
     *
     * 本函数把「求连通域 + 写入填充色」合并；无选区相交、sample-merged、对角邻接、抗锯齿软边、图案。
     * @param seed 种子点（图像像素）
     * @param fillColor 填充色（含 alpha；内部转预乘写入）
     * @param tolerance 容差 0..255（GIMP 选项 threshold，内部再 /255 进 float；此处直接在 8bit 比）
     * @param contiguous true≈by_seed；false≈by_color（GIMP 桶工具默认始终 by_seed）
     * @return 脏矩形；未改动返回空
     */
    static QRect floodFill(TileBuffer &tiles,
                           const QPoint &seed,
                           const QColor &fillColor,
                           int tolerance,
                           bool contiguous);

    /**
     * 渐变形状（选项栏下标；对照 GimpGradientType 子集）。
     * UI 文案贴近 PS 五种；算法公式来自 `gimpoperationgradient.c`。
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
     *
     * GIMP 分层：
     * - tools：`gimpgradienttool.c`（拖拽起止）+ `gimpgradientoptions.c`（offset/type/dither…）
     * - core：`gimpdrawable-gradient.c`（建缓冲 → 跑 `gimp:gradient` → apply）
     * - op：`gimpoperationgradient.c`（逐像素算 factor → 采样渐变色）
     *
     * 本函数：两色 FG→BG（等价 FG-BG 渐变）、REPEAT_NONE、无 shapeburst/螺旋/超采样/选区。
     * @param offsetPercent GIMP `offset` 0..100（起点空段比例）；负值按 0 处理
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
                               bool dither);
};

} // namespace Ps

#endif // PAINTENGINE_H
