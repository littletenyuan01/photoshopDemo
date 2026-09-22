#ifndef CANVASWORKSPACE_H
#define CANVASWORKSPACE_H

#include <QWidget>

class CanvasView;
class RulerWidget;

QT_BEGIN_NAMESPACE
namespace Ui {
class CanvasWorkspace;
}
QT_END_NAMESPACE

/**
 * 画布工作区（ui）：左上角块 + 顶标尺 + 左标尺 + CanvasView。
 *
 * 布局文件：canvasworkspace.ui（对齐 Photoshop / GIMP display 壳）。
 * 【对照 GIMP】display shell 把 hrule/vrule 与画布拼在一起，并在
 * scroll/scale 后调用 gimp_display_shell_rulers_update。
 */
class CanvasWorkspace : public QWidget
{
    Q_OBJECT

public:
    explicit CanvasWorkspace(QWidget *parent = nullptr);
    ~CanvasWorkspace() override;

    CanvasView *canvasView() const;

private slots:
    void syncRulers();
    void onCanvasMouseMoved(const QPointF &imagePos, bool inside);

private:
    Ui::CanvasWorkspace *ui;
};

#endif // CANVASWORKSPACE_H
