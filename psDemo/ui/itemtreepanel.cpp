#include "itemtreepanel.h"

#include "domain/imagedocument.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QListWidget>
#include <QToolButton>

ItemTreePanel::ItemTreePanel(QWidget *parent)
    : QWidget(parent)
{
}

ItemTreePanel::~ItemTreePanel() = default;

void ItemTreePanel::bindSkeleton(QFrame *optionsHost, QListWidget *itemList, QFrame *toolbarHost)
{
    // 与 GimpItemTreeView 私有结构中的 options_box / 树 / editor button_box 对齐
    m_optionsHost = optionsHost;
    m_itemList = itemList;
    m_toolbarHost = toolbarHost;
}

QToolButton *ItemTreePanel::addToolbarButton(const QString &objectName,
                                             const QString &text,
                                             const QString &toolTip)
{
    if (!m_toolbarHost)
        return nullptr;

    auto *layout = qobject_cast<QHBoxLayout *>(m_toolbarHost->layout());
    if (!layout)
        return nullptr;

    auto *btn = new QToolButton(m_toolbarHost);
    btn->setObjectName(objectName);
    btn->setText(text);
    btn->setToolTip(toolTip);
    btn->setAutoRaise(true);

    // 插在末尾 spacer 之前，保持「图标左对齐、右侧留白」
    int insertAt = layout->count();
    for (int i = 0; i < layout->count(); ++i) {
        if (layout->itemAt(i)->spacerItem()) {
            insertAt = i;
            break;
        }
    }
    layout->insertWidget(insertAt, btn);
    return btn;
}

void ItemTreePanel::setDocument(Ps::ImageDocument *document)
{
    if (m_document == document)
        return;

    if (m_document)
        disconnect(m_document, nullptr, this, nullptr);

    m_document = document;
    onDocumentChanged();
}

void ItemTreePanel::onDocumentChanged()
{
    refreshFromDocument();
}

void ItemTreePanel::onNewItem()
{
}

void ItemTreePanel::onDeleteItem()
{
}
