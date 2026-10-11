/**
 * filterdialog.h — 滤镜对话框（ui 层）。
 *
 * 复用 `AdjustmentPropsHost` 作参数面板；OK 应用、Cancel 取消。
 * **对话框本身无状态、不碰文档**：slider/spin 变化只发 `previewChanged`，
 * 由调用方（MainWindow）决定是否即时重合成 —— 这样它既可用于像素层滤镜，
 * 也可复用于将来的调整滤镜。
 */
#ifndef UI_FILTERDIALOG_H
#define UI_FILTERDIALOG_H

#include "domain/filternode.h"

#include <QDialog>
#include <QVector>

class QTimer;
class AdjustmentPropsHost;

/**
 * 滤镜参数对话框（**非模态**）。
 *
 * 【为什么非模态】对照 GIMP：GEGL 滤镜在 2.10 里是**工具**（`GimpGeglTool` → `GimpOperationTool`，
 * 参数 UI 填进工具选项框），所以是**非模态**的 —— 拖参数时还能缩放/平移画布看细节。
 * 我们原来的模态对话框没有这个能力，还逼出一个补丁（模态会吞掉主窗口快捷键）。
 *
 * 【职责边界】对话框**无状态、不碰文档**：只发 `previewChanged`（自带 80ms 节流），
 * 「预览写到哪个图层」「确定/取消怎么落盘」都由调用方决定。
 * 非模态下 `exec()` 的返回值不再可用，调用方改接 `accepted()` / `rejected()`。
 *
 * 【对话框内的 Ctrl+Z】保留：它只回退本对话框的参数（非模态后主窗口的撤销键在画布获焦时也可用，
 * 但那条会去撤销**上一次**文档操作，不是回退本次参数）。
 */
class FilterDialog : public QDialog
{
    Q_OBJECT
public:
    explicit FilterDialog(QWidget *parent = nullptr);

    void setNode(const Ps::FilterNode &node);
    Ps::FilterNode node() const;

signals:
    /** 参数变化（已按 80ms 节流，永远是最新值）。 */
    void previewChanged(const Ps::FilterNode &node);

private:
    /** 对话框内 Ctrl+Z：回退到上一次参数提交之前。 */
    void undoLastChange();

    AdjustmentPropsHost *m_host = nullptr;
    Ps::FilterNode m_last;               ///< 上次参数提交后的节点（撤销栈基准）
    QVector<Ps::FilterNode> m_paramUndo; ///< 各次提交「之前」的节点
    QTimer *m_throttle = nullptr;        ///< 预览节流（非模态下调用方拿不到同步返回值，节流放这里）
    Ps::FilterNode m_latest;             ///< 待发的预览值
};

#endif // UI_FILTERDIALOG_H
