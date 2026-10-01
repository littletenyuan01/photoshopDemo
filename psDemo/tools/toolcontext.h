/**
 * toolcontext.h — 工具运行上下文与 ViewPort 接口（tools 层）。
 *
 * 由 CanvasView 填好文档/颜色/笔刷/视图变换；工具只读，不反向依赖 UI。
 */
#ifndef TOOLCONTEXT_H
#define TOOLCONTEXT_H

#include "engine/painttypes.h"

#include <QColor>
#include <QPointF>
#include <QRect>
#include <QString>

namespace Ps {

class ImageDocument;

/**
 * 油漆桶填充源（对照 GIMP GimpBucketFillMode / PS 填充下拉）。
 * 数值 = 选项栏 fillTypeCombo 下标；Pattern 尚未实现，工具侧回退 Foreground。
 */
enum class FillSource {
    Foreground = 0,
    Background = 1,
    Pattern = 2, ///< UI 占位；实现前按 Foreground 处理
};

/**
 * 工具运行所需的上下文（tools 层）。
 * 由 CanvasView 在分发事件前填好，工具只读此结构，**不反向依赖 UI**。
 *
 * 【对照 GIMP】对应 GimpTool 里从 GimpDisplay / GimpContext 取到的那些状态
 * （image、foreground、brush 等）。这里收成一个扁平结构，避免工具持有 UI 指针。
 */
struct ToolContext
{
    ImageDocument *document = nullptr; ///< 当前文档；可能为 nullptr
    QColor foreground {Qt::black};     ///< 前景色（画笔颜色）
    QColor background {Qt::white};     ///< 背景色
    qreal brushRadius = 10.0;          ///< 画笔半径（图像像素）

    // —— 油漆桶（对照 GimpBucketFillOptions 属性名）——
    int fillTolerance = 32;            ///< ≈ threshold；GIMP 默认 15，此处 32 对齐 PS
    bool fillContiguous = true;        ///< PS「连续」；GIMP 相似色路径固定 by_seed
    FillSource fillSource = FillSource::Foreground;
    qreal fillOpacity = 1.0;           ///< ≈ context opacity [0,1]；无 paint-mode / sample-merged

    // —— 渐变（对照 GimpGradientOptions / PaintOptions）——
    GradientType gradientType = GradientType::Linear;
    qreal gradientOpacity = 1.0;       ///< ≈ context opacity [0,1]
    int gradientOffsetPercent = 0;     ///< ≈ offset 0..100
    bool gradientReverse = false;      ///< ≈ gradient-reverse
    bool gradientDither = true;        ///< ≈ dither

    // —— 魔棒 / 快速选择（对照 GimpRegionSelectOptions）——
    int selTolerance = 32;             ///< ≈ threshold
    bool selContiguous = true;         ///< 连续（魔棒）；快速选择内部强制连续
    bool selSampleMerged = false;      ///< 对所有图层取样（合成图）

    // —— 磁性套索（对照 PS Width / Contrast / Frequency）——
    int magneticWidth = 10;            ///< 边缘搜索半径（文档像素）
    int magneticContrast = 40;         ///< 1..100 → 梯度门槛
    int magneticFrequency = 57;        ///< 1..100 → 锚点密度（越高越密）

    // —— 仿制图章（对照 GimpCloneOptions：align-mode / sample-merged）——
    bool cloneAlign = true;            ///< 对齐（跨笔保留源-目标偏移）
    bool cloneSampleMerged = false;    ///< 对所有图层取样

    // —— 形状（对照选项栏 pageShape）——
    bool shapeFill = true;             ///< 填充（前景色）；图案/渐变未做
    bool shapeStroke = false;          ///< 描边
    qreal shapeStrokeWidth = 2.0;      ///< 描边粗细（像素）
    qreal shapeCornerRadius = 0.0;     ///< 矩形圆角
    bool shapeAntialias = true;        ///< 消除锯齿

    // —— 视图变换（供工具浮层把文档坐标画到控件上；由 CanvasView 填）——
    qreal viewZoom = 1.0;              ///< 当前缩放
    QPointF viewOffset;                ///< 文档原点在控件中的位置

    /** 文档坐标 → 控件坐标（与 CanvasView::imageToWidget 同构）。 */
    QPointF imageToWidget(const QPointF &imagePos) const
    {
        return viewOffset + imagePos * viewZoom;
    }
};

/**
 * 工具可请求的视图操作（由 CanvasView 实现）。
 *
 * 视图变换（缩放/平移）属显示层职责，不应由工具直接改 QWidget。
 * GIMP 中对应 GimpDisplayShell 暴露的 gimp_display_shell_scale / scroll 系列函数。
 */
class ViewPort
{
public:
    virtual ~ViewPort() = default;

    /** 以 widgetPos 为锚点按 factor 缩放（factor>1 放大）。 */
    virtual void zoomAt(const QPointF &widgetPos, qreal factor) = 0;
    /** 相对位移平移画布（控件像素）。 */
    virtual void panBy(const QPointF &deltaWidget) = 0;
    /** 请求重绘。 */
    virtual void requestRepaint() = 0;
};

} // namespace Ps

#endif // TOOLCONTEXT_H
