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

class AdjustmentPropsHost;

/**
 * 滤镜参数对话框。
 *
 * **文档级撤销**由调用方决定：本对话框把节点以「临时」方式加进图层做预览，
 * 确定时才正式落盘（一条撤销）、取消则摘掉（零撤销）。
 *
 * **对话框内的 Ctrl+Z**：模态对话框会吞掉主窗口的撤销快捷键，故这里自带一条 ——
 * 逐步回退参数（每次提交记一份「提交前」的节点）。它只回退参数，不碰文档撤销栈。
 */
class FilterDialog : public QDialog
{
    Q_OBJECT
public:
    explicit FilterDialog(QWidget *parent = nullptr);

    void setNode(const Ps::FilterNode &node);
    Ps::FilterNode node() const;

signals:
    void previewChanged(const Ps::FilterNode &node);

private:
    /** 对话框内 Ctrl+Z：回退到上一次参数提交之前。 */
    void undoLastChange();

    AdjustmentPropsHost *m_host = nullptr;
    Ps::FilterNode m_last;               ///< 上次参数提交后的节点（撤销栈基准）
    QVector<Ps::FilterNode> m_paramUndo; ///< 各次提交「之前」的节点
};

#endif // UI_FILTERDIALOG_H
