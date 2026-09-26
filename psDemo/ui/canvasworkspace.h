#ifndef CANVASWORKSPACE_H
#define CANVASWORKSPACE_H

#include <QWidget>

class QScrollBar;

class CanvasView;

namespace Ps {
class AppSession;
class ImageDocument;
}

QT_BEGIN_NAMESPACE
namespace Ui {
class CanvasWorkspace;
}
QT_END_NAMESPACE

/**
 * 画布工作区（ui）：标尺 + CanvasView + 底栏状态 + 滚动条。
 *
 * 【对照 GIMP】对应 GimpDisplayShell 的装配职责：持 hrule / vrule / canvas 与滚动条，
 * 并在 view 变换后刷新标尺范围与滚动条行程（gimp_display_shell_rulers_update 等）。
 *
 * 布局：canvasworkspace.ui（对齐 Photoshop 画布区）。
 * 底栏左侧为缩放%/文档信息（CanvasDocStatusBar），右侧为水平滚动条。
 *
 * 【滚动条同步】画布与滚动条是双向绑定，容易形成信号回环。
 * 统一用 QSignalBlocker（RAII）在「画布 → 滚动条」方向抑制回环，
 * 替代早先手写的 bool 守卫 —— 提前 return 或异常时也不会把守卫永久卡住。
 */
class CanvasWorkspace : public QWidget
{
    Q_OBJECT

public:
    explicit CanvasWorkspace(QWidget *parent = nullptr);
    ~CanvasWorkspace() override;

    CanvasView *canvasView() const;

    /** 订阅会话的文档广播；不取得所有权。 */
    void setSession(Ps::AppSession *session);

private slots:
    void syncRulersAndScrollBars();
    void syncDocStatus();
    void onCanvasMouseMoved(const QPointF &imagePos, bool inside);
    void onHScroll(int value);
    void onVScroll(int value);
    void onStatusZoomCommitted(qreal zoom);

private:
    /** 把单根滚动条的行程/步长/位置对齐到画布状态；无行程时用假行程保持 AlwaysOn。 */
    void syncScrollBar(QScrollBar *bar, int maxScroll, int pageSize, int value);
    /** 处理单轴滚动请求；无行程时归零。 */
    void handleScroll(QScrollBar *bar, int maxScroll, int value, bool horizontal);

    Ui::CanvasWorkspace *ui;
};

#endif // CANVASWORKSPACE_H
