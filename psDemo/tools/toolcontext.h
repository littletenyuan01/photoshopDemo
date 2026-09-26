#ifndef TOOLCONTEXT_H
#define TOOLCONTEXT_H

#include <QColor>
#include <QPointF>
#include <QRect>
#include <QString>

namespace Ps {

class ImageDocument;

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

    bool hasDocument() const { return document != nullptr; }
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
