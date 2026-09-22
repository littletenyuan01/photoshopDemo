#ifndef PAINTENGINE_H
#define PAINTENGINE_H

#include <QColor>
#include <QImage>
#include <QPointF>

namespace Ps {

/**
 * 像素绘制引擎（engine）。
 *
 * 【对照 GIMP】
 * - GIMP：`app/tools` 只处理指针/UI；真正写缓冲在 `app/paint/GimpPaintCore`
 *   （start → paint → finish，带 stroke 间距与 undo extents）。
 * - 本项目瘦身：无 GEGL、无笔刷资源库、无对称绘制；仅圆形 dab + 线段插值。
 *
 * 约定：
 * - 目标图像须为 Format_ARGB32_Premultiplied（与 Layer 一致）。
 * - 坐标为**图像像素坐标**（非控件坐标）。
 * - Paint：SourceOver 叠前景色；Erase：DestinationOut 按羽化 alpha 掏空。
 */
class PaintEngine
{
public:
    enum class Mode {
        Paint,  // 画笔
        Erase,  // 橡皮
    };

    /**
     * 在 center 盖一颗圆形笔触。
     * @param radius 半径（像素，图像空间）
     * @param hardness [0,1]，1=硬边实心圆，越小边缘越软
     */
    static void stampDab(QImage &target,
                         const QPointF &center,
                         qreal radius,
                         const QColor &color,
                         Mode mode,
                         qreal hardness = 0.85);

    /**
     * 从 from 画到 to，按间距重复 stamp（对齐 GIMP paint core 的 distance/spacing 思路）。
     * @param spacing 相对直径的步长比例，默认 0.25（每 1/4 直径一颗 dab）
     * @return 本次实际盖下的最后一颗 dab 中心（供下一段续画）
     */
    static QPointF strokeSegment(QImage &target,
                                 const QPointF &from,
                                 const QPointF &to,
                                 qreal radius,
                                 const QColor &color,
                                 Mode mode,
                                 qreal hardness = 0.85,
                                 qreal spacing = 0.25);
};

} // namespace Ps

#endif // PAINTENGINE_H
