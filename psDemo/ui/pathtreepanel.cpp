#include "pathtreepanel.h"
#include "ui_pathtreepanel.h"

#include <QListWidgetItem>

PathTreePanel::PathTreePanel(QWidget *parent)
    : ItemTreePanel(parent)
    , ui(new Ui::PathTreePanel)
{
    ui->setupUi(this);
    bindSkeleton(ui->optionsHost, ui->itemList, ui->toolbarHost);

    // GIMP：paths-new / paths-delete；另有 fill/stroke/to-selection（paths-actions）
    connect(ui->btnNew, &QToolButton::clicked, this, &PathTreePanel::onNewItem);
    connect(ui->btnDelete, &QToolButton::clicked, this, &PathTreePanel::onDeleteItem);

    refreshFromDocument();
}

PathTreePanel::~PathTreePanel()
{
    delete ui;
}

void PathTreePanel::refreshFromDocument()
{
    ui->itemList->clear();
    auto *work = new QListWidgetItem(QStringLiteral("工作路径"), ui->itemList);
    work->setFlags(work->flags() | Qt::ItemIsSelectable);
    if (ui->itemList->count() > 0)
        ui->itemList->setCurrentRow(0);
}

void PathTreePanel::onNewItem()
{
    // TODO：对照 paths-commands.c
}

void PathTreePanel::onDeleteItem()
{
    // TODO：对照 paths-commands.c
}
