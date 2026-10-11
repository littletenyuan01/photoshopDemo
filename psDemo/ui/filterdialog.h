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

class AdjustmentPropsHost;

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
    AdjustmentPropsHost *m_host = nullptr;
};

#endif // UI_FILTERDIALOG_H
