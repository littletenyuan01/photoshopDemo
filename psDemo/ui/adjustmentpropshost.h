/**
 * adjustmentpropshost.h — 调整图层属性页宿主（按 OpName 切换 .ui）。
 */
#ifndef UI_ADJUSTMENTPROPSHOST_H
#define UI_ADJUSTMENTPROPSHOST_H

#include "domain/filternode.h"

#include <QHash>
#include <QWidget>

#include <functional>

class QStackedWidget;
class QLabel;
class QSlider;
class QSpinBox;

/**
 * 对照 PS 属性面板：选中调整层时显示对应参数页。
 * 拖动中 emit previewChanged；松手 emit commitChanged（供 undo）。
 */
class AdjustmentPropsHost : public QWidget
{
    Q_OBJECT
public:
    explicit AdjustmentPropsHost(QWidget *parent = nullptr);

    void setNode(const Ps::FilterNode &node);
    Ps::FilterNode node() const { return m_node; }
    void clear();

signals:
    void previewChanged(const Ps::FilterNode &node);
    void commitChanged(const Ps::FilterNode &node);

private:
    void ensurePages();
    void showPageFor(Ps::OpName op);
    void loadControlsFromNode();
    void wireSliderSpin(QSlider *slider, QSpinBox *spin,
                        const std::function<void(int)> &applyToNode);
    void emitPreview();
    void emitCommit();

    QLabel *m_title = nullptr;
    QStackedWidget *m_stack = nullptr;
    QHash<int, int> m_opToPage;
    Ps::FilterNode m_node;
    bool m_block = false;
    bool m_pagesReady = false;
};

#endif // UI_ADJUSTMENTPROPSHOST_H
