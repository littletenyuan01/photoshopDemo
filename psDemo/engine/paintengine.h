#ifndef PAINTENGINE_H
#define PAINTENGINE_H

#include <QColor>
#include <QImage>
#include <QPointF>

namespace Ps {

class TileBuffer;

/**
 * 像素绘制引擎（engine）。
 *
 * 【对照 GIMP】
 * - GIMP：`app/tools` 只处理指针/UI；真正写缓冲在 `app/paint/GimpPaintCore`
 * - 本项目：圆形 dab + 线段插值；可写整幅 QImage 或懒分配 TileBuffer。
 *
 * 约定：
 * - 图像须为 Format_ARGB32_Premultiplied。
 * - 坐标为**图像像素坐标**。
 * - Paint：SourceOver；Erase：DestinationOut。
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
};

} // namespace Ps

#endif // PAINTENGINE_H
