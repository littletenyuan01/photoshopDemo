/**
 * pathtreepanel.h — 路径树面板（ui 层）。
 *
 * 【对照 GIMP】app/widgets/gimppathtreeview.*
 * 真读 ImageDocument::paths()；底栏：填充/描边/路径↔选区/新建/删除。
 */
#ifndef PATHTREEPANEL_H
#define PATHTREEPANEL_H

#include "itemtreepanel.h"

class QListWidgetItem;

QT_BEGIN_NAMESPACE
namespace Ui {
class PathTreePanel;
}
QT_END_NAMESPACE

class PathTreePanel : public ItemTreePanel
{
    Q_OBJECT

public:
    explicit PathTreePanel(QWidget *parent = nullptr);
    ~PathTreePanel() override;

protected:
    void onDocumentChanged() override;
    void refreshFromDocument() override;
    void onNewItem() override;
    void onDeleteItem() override;

private slots:
    void onListSelectionChanged();
    void onItemChanged(QListWidgetItem *item);
    void onFillPath();
    void onStrokePath();
    void onPathToSelection();
    void onSelectionToPath();

private:
    int pathIndexOfItem(QListWidgetItem *item) const;

    Ui::PathTreePanel *ui;
    bool m_blockItemChanged = false;
};

#endif
