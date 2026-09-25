#include "channeltreepanel.h"
#include "ui_channeltreepanel.h"

#include <QListWidgetItem>
#include <QSlider>

ChannelTreePanel::ChannelTreePanel(QWidget *parent)
    : ItemTreePanel(parent)
    , ui(new Ui::ChannelTreePanel)
{
    ui->setupUi(this);
    bindSkeleton(ui->optionsHost, ui->itemList, ui->toolbarHost);

    // GIMP：channels-new / channels-delete；选区相关见 channels-selection-*
    connect(ui->btnNew, &QToolButton::clicked, this, &ChannelTreePanel::onNewItem);
    connect(ui->btnDelete, &QToolButton::clicked, this, &ChannelTreePanel::onDeleteItem);
    // 0–100 用滑动条（percent-sliders）；通道不透明度尚无 domain，仅更新右侧百分比
    connect(ui->channelOpacitySlider, &QSlider::valueChanged, this, [this](int value) {
        ui->channelOpacityValueLabel->setText(QStringLiteral("%1%").arg(value));
    });

    refreshFromDocument();
}

ChannelTreePanel::~ChannelTreePanel()
{
    delete ui;
}

void ChannelTreePanel::refreshFromDocument()
{
    // 无 Channel 域模型前：固定展示 RGB 分量 + 示例 Alpha（对齐 PS/GIMP 常见初始态）
    ui->itemList->clear();
    const QStringList names = {
        QStringLiteral("RGB"),
        QStringLiteral("红"),
        QStringLiteral("绿"),
        QStringLiteral("蓝"),
    };
    for (const QString &name : names) {
        auto *item = new QListWidgetItem(name, ui->itemList);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
        item->setCheckState(Qt::Checked);
    }
    if (ui->itemList->count() > 0)
        ui->itemList->setCurrentRow(0);
}

void ChannelTreePanel::onNewItem()
{
    // TODO：对照 channels-commands.c 的 channels_new_cmd_callback
}

void ChannelTreePanel::onDeleteItem()
{
    // TODO：对照 channels-commands.c；分量通道不可删
}
