/**
 * pathtreepanel.cpp — 路径树面板实现（ui 层）。
 */
#include "pathtreepanel.h"
#include "ui_pathtreepanel.h"

#include "domain/imagedocument.h"
#include "domain/path.h"

#include <QListWidgetItem>
#include <QSignalBlocker>

PathTreePanel::PathTreePanel(QWidget *parent)
    : ItemTreePanel(parent)
    , ui(new Ui::PathTreePanel)
{
    ui->setupUi(this);

    connect(ui->btnNew, &QToolButton::clicked, this, &PathTreePanel::onNewItem);
    connect(ui->btnDelete, &QToolButton::clicked, this, &PathTreePanel::onDeleteItem);
    connect(ui->btnFillPath, &QToolButton::clicked, this, &PathTreePanel::onFillPath);
    connect(ui->btnStrokePath, &QToolButton::clicked, this, &PathTreePanel::onStrokePath);
    connect(ui->btnPathToSelection, &QToolButton::clicked, this, &PathTreePanel::onPathToSelection);
    connect(ui->btnSelectionToPath, &QToolButton::clicked, this, &PathTreePanel::onSelectionToPath);
    connect(ui->itemList, &QListWidget::itemSelectionChanged,
            this, &PathTreePanel::onListSelectionChanged);
    connect(ui->itemList, &QListWidget::itemChanged, this, &PathTreePanel::onItemChanged);

    refreshFromDocument();
}

PathTreePanel::~PathTreePanel()
{
    delete ui;
}

void PathTreePanel::onDocumentChanged()
{
    Ps::ImageDocument *doc = document();
    if (doc) {
        connect(doc, &Ps::ImageDocument::pathsChanged, this, &PathTreePanel::refreshFromDocument,
                Qt::UniqueConnection);
    }
    refreshFromDocument();
}

void PathTreePanel::refreshFromDocument()
{
    const QSignalBlocker blocker(ui->itemList);
    m_blockItemChanged = true;
    ui->itemList->clear();

    Ps::ImageDocument *doc = document();
    if (!doc) {
        m_blockItemChanged = false;
        return;
    }

    for (int i = 0; i < doc->paths().count(); ++i) {
        const Ps::Path *p = doc->paths().pathAt(i);
        if (!p)
            continue;
        auto *item = new QListWidgetItem(p->name.isEmpty() ? tr("路径 %1").arg(i + 1) : p->name);
        item->setFlags(item->flags() | Qt::ItemIsSelectable | Qt::ItemIsEditable
                       | Qt::ItemIsUserCheckable);
        item->setCheckState(p->visible ? Qt::Checked : Qt::Unchecked);
        item->setData(Qt::UserRole, i);
        item->setIcon(ItemTreePanel::svgIcon(QStringLiteral(":/icons/paths/new-path.svg"),
                                               ItemTreePanel::kThumbSize));
        ui->itemList->addItem(item);
    }

    const int active = doc->activePathIndex();
    if (active >= 0 && active < ui->itemList->count())
        ui->itemList->setCurrentRow(active);
    m_blockItemChanged = false;
}

int PathTreePanel::pathIndexOfItem(QListWidgetItem *item) const
{
    if (!item)
        return -1;
    return item->data(Qt::UserRole).toInt();
}

void PathTreePanel::onListSelectionChanged()
{
    Ps::ImageDocument *doc = document();
    if (!doc || !ui->itemList->currentItem())
        return;
    const int idx = pathIndexOfItem(ui->itemList->currentItem());
    if (idx >= 0)
        doc->setActivePathIndex(idx);
}

void PathTreePanel::onItemChanged(QListWidgetItem *item)
{
    if (m_blockItemChanged || !item)
        return;
    Ps::ImageDocument *doc = document();
    if (!doc)
        return;
    const int idx = pathIndexOfItem(item);
    if (idx < 0)
        return;

    const Ps::Path *p = doc->paths().pathAt(idx);
    if (!p)
        return;

    const bool wantVis = (item->checkState() == Qt::Checked);
    const QString wantName = item->text();
    if (p->visible != wantVis || p->name != wantName) {
        doc->pushPathUndo(tr("修改路径"));
        if (p->visible != wantVis)
            doc->setPathVisible(idx, wantVis);
        if (p->name != wantName)
            doc->setPathName(idx, wantName);
    }
}

void PathTreePanel::onNewItem()
{
    Ps::ImageDocument *doc = document();
    if (!doc)
        return;
    doc->pushPathUndo(tr("新建路径"));
    Ps::Path path;
    path.name = tr("路径 %1").arg(doc->paths().count() + 1);
    doc->addPath(std::move(path));
}

void PathTreePanel::onDeleteItem()
{
    Ps::ImageDocument *doc = document();
    if (!doc)
        return;
    const int idx = doc->activePathIndex();
    if (idx < 0)
        return;
    doc->pushPathUndo(tr("删除路径"));
    doc->removePath(idx);
}

void PathTreePanel::onFillPath()
{
    Ps::ImageDocument *doc = document();
    if (!doc)
        return;
    // 前景色由画布工具上下文提供；面板侧用黑作为合理默认，
    // MainWindow 若接了 fg 可后续改信号。此处用黑色保证可测。
    doc->fillActivePath(Qt::black);
}

void PathTreePanel::onStrokePath()
{
    Ps::ImageDocument *doc = document();
    if (!doc)
        return;
    doc->strokeActivePath(Qt::black, 1.0);
}

void PathTreePanel::onPathToSelection()
{
    Ps::ImageDocument *doc = document();
    if (!doc)
        return;
    doc->pathToSelection(Ps::ChannelOp::Replace);
}

void PathTreePanel::onSelectionToPath()
{
    Ps::ImageDocument *doc = document();
    if (!doc)
        return;
    doc->selectionToPath();
}
