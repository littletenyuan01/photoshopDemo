/**
 * layerstyledialog.h — 图层样式对话框（ui 层）。
 *
 * 控件与布局在 layerstyledialog.ui；本类只做数据同步与槽接线。
 */
#ifndef LAYERSTYLEDIALOG_H
#define LAYERSTYLEDIALOG_H

#include "domain/layerstyle.h"

#include <QDialog>
#include <QHash>
#include <QVector>

QT_BEGIN_NAMESPACE
namespace Ui {
class LayerStyleDialog;
}
QT_END_NAMESPACE

class QListWidgetItem;

namespace Ps {
class Layer;
}

class LayerStyleDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LayerStyleDialog(const Ps::Layer *layer, QWidget *parent = nullptr);
    ~LayerStyleDialog() override;

    QVector<Ps::LayerStyleEffect> resultEffects() const;

private slots:
    void onEffectRowChanged(int row);
    void onEffectItemChanged(QListWidgetItem *item);
    void onPickColor();
    void onParamEdited();

private:
    Ps::LayerStyleKind kindAtRow(int row) const;
    Ps::LayerStyleEffect &draft(Ps::LayerStyleKind kind);
    void selectKind(Ps::LayerStyleKind kind);
    void loadCurrentToUi();
    void saveUiToCurrent();
    void refreshColorButton();
    void updateParamEnabled();

    Ui::LayerStyleDialog *ui;
    QHash<int, Ps::LayerStyleEffect> m_draft;
    Ps::LayerStyleKind m_current = Ps::LayerStyleKind::DropShadow;
    bool m_block = false;
};

#endif // LAYERSTYLEDIALOG_H
