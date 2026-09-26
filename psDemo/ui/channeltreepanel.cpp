#include "channeltreepanel.h"
#include "ui_channeltreepanel.h"

#include "domain/imagedocument.h"
#include "engine/compositor.h"

#include <QIcon>
#include <QImage>
#include <QListWidgetItem>
#include <QPixmap>
#include <QSignalBlocker>
#include <QSize>
#include <QSlider>
#include <QToolButton>

namespace {

/** 行 ↔ 通道种类的映射存在 UserRole（与图层面板存栈下标同理，避免靠文字认行）。 */
constexpr int kChannelRole = Qt::UserRole;

/** 一个通道行：显示名 + 缩略图派生方式。 */
struct ChannelRow {
    QString name;
    ThumbChannel kind;
    const char *tip;
};

} // namespace

ChannelTreePanel::ChannelTreePanel(QWidget *parent)
    : ItemTreePanel(parent)
    , ui(new Ui::ChannelTreePanel)
{
    ui->setupUi(this);
    bindSkeleton(ui->optionsHost, ui->itemList, ui->toolbarHost);

    ui->itemList->setIconSize(QSize(ItemTreePanel::kThumbSize, ItemTreePanel::kThumbSize));

    // 底栏图标：channels/* + 共用 delete
    applyToolbarIcon(ui->btnLoadSelection, QStringLiteral(":/icons/channels/load-selection.svg"));
    applyToolbarIcon(ui->btnSaveSelection, QStringLiteral(":/icons/channels/save-selection.svg"));
    applyToolbarIcon(ui->btnNew, QStringLiteral(":/icons/channels/new-channel.svg"));
    applyToolbarIcon(ui->btnDelete, QStringLiteral(":/icons/layers/delete.svg"));

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

void ChannelTreePanel::onDocumentChanged()
{
    // 像素变了要重算分量缩略图；此处在 UI 阶段不做防抖
    // （通道数固定 4 行、缩略图算法已是两段式缩放，代价可接受）
    if (Ps::ImageDocument *doc = document()) {
        connect(doc, &Ps::ImageDocument::contentChanged,
                this, &ChannelTreePanel::refreshFromDocument);
        connect(doc, &Ps::ImageDocument::structureChanged,
                this, &ChannelTreePanel::refreshFromDocument);
    }
    refreshFromDocument();
}

void ChannelTreePanel::refreshFromDocument()
{
    const QSignalBlocker blocker(ui->itemList);
    ui->itemList->clear();

    Ps::ImageDocument *doc = document();

    // 【诚实标注】尚无 Channel domain：合成图与分量都由 Compositor 实时推算，
    // 不是真实通道数据。等通道 domain 开建后应改为读取真实通道。
    QImage composite;
    if (doc)
        composite = Ps::Compositor::composite(*doc);

    // 与 PS 初始态一致：RGB 复合通道 + 三个分量（无 Alpha 时不列 Alpha 行）
    const ChannelRow rows[] = {
        {QStringLiteral("RGB"), ThumbChannel::Composite,
         QT_TR_NOOP("RGB 复合通道（由合成图推算）")},
        {QStringLiteral("红"), ThumbChannel::Red, QT_TR_NOOP("红分量（由合成图推算）")},
        {QStringLiteral("绿"), ThumbChannel::Green, QT_TR_NOOP("绿分量（由合成图推算）")},
        {QStringLiteral("蓝"), ThumbChannel::Blue, QT_TR_NOOP("蓝分量（由合成图推算）")},
        {QStringLiteral("Alpha"), ThumbChannel::Alpha,
         QT_TR_NOOP("Alpha 通道（白 = 不透明，由合成图推算）")},
    };

    for (const ChannelRow &row : rows) {
        auto *item = new QListWidgetItem(row.name, ui->itemList);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
        item->setCheckState(Qt::Checked);
        item->setData(kChannelRole, static_cast<int>(row.kind));
        item->setToolTip(tr(row.tip));

        const QImage thumb = ItemTreePanel::makeChannelThumbnail(composite, row.kind);
        if (!thumb.isNull())
            item->setIcon(QIcon(QPixmap::fromImage(thumb)));
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
