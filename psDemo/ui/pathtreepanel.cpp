/**
 * pathtreepanel.cpp — 路径树面板实现（ui 层）。
 */
#include "pathtreepanel.h"
#include "ui_pathtreepanel.h"

#include <QIcon>
#include <QListWidgetItem>
#include <QSize>
#include <QToolButton>

PathTreePanel::PathTreePanel(QWidget *parent)
    : ItemTreePanel(parent)
    , ui(new Ui::PathTreePanel)
{
    ui->setupUi(this);

    // 图标 / iconSize 见 pathtreepanel.ui


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

    // 【诚实标注】尚无 path domain，故只有一行占位。
    // 给行加路径标记图标（不是像素缩略图 —— 路径是矢量，没有像素可缩）。
    auto *work = new QListWidgetItem(QStringLiteral("工作路径"), ui->itemList);
    work->setFlags(work->flags() | Qt::ItemIsSelectable);
    work->setIcon(ItemTreePanel::svgIcon(QStringLiteral(":/icons/paths/new-path.svg"),
                                          ItemTreePanel::kThumbSize));
    work->setToolTip(tr("路径为矢量，暂无轮廓预览（待 path domain）"));

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
