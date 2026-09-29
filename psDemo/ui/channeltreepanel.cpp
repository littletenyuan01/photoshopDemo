#include "channeltreepanel.h"
#include "ui_channeltreepanel.h"

#include "domain/imagedocument.h"
#include "domain/tilebuffer.h"
#include "engine/compositor.h"

#include <QIcon>
#include <QImage>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPixmap>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSize>
#include <QSlider>
#include <QTimer>
#include <QToolButton>
#include <QtGlobal>

namespace {

/** 行 ↔ 通道种类的映射存在 UserRole（与图层面板存栈下标同理，避免靠文字认行）。 */
constexpr int kChannelRole = Qt::UserRole;

/** 停笔多久之后重算缩略图（毫秒）。与 LayerTreePanel 保持一致。 */
constexpr int kThumbnailDebounceMs = 250;

/** 一个通道行：显示名 + 缩略图派生方式。 */
struct ChannelRow {
    QString name;
    ThumbChannel kind;
    const char *tip;
};

/** 与 PS 初始态一致：RGB 复合通道 + 三个分量 + Alpha。 */
const ChannelRow kRows[] = {
    {QStringLiteral("RGB"), ThumbChannel::Composite,
     QT_TR_NOOP("RGB 复合通道（由合成图推算）")},
    {QStringLiteral("红"), ThumbChannel::Red, QT_TR_NOOP("红分量（由合成图推算）")},
    {QStringLiteral("绿"), ThumbChannel::Green, QT_TR_NOOP("绿分量（由合成图推算）")},
    {QStringLiteral("蓝"), ThumbChannel::Blue, QT_TR_NOOP("蓝分量（由合成图推算）")},
    {QStringLiteral("Alpha"), ThumbChannel::Alpha,
     QT_TR_NOOP("Alpha 通道（白 = 不透明，由合成图推算）")},
};

} // namespace

ChannelTreePanel::ChannelTreePanel(QWidget *parent)
    : ItemTreePanel(parent)
    , ui(new Ui::ChannelTreePanel)
{
    ui->setupUi(this);
    // 图标 / iconSize 见 channeltreepanel.ui

    // GIMP：channels-new / channels-delete；选区相关见 channels-selection-*
    connect(ui->btnNew, &QToolButton::clicked, this, &ChannelTreePanel::onNewItem);
    connect(ui->btnDelete, &QToolButton::clicked, this, &ChannelTreePanel::onDeleteItem);
    // 0–100 用滑动条（percent-sliders）；通道不透明度尚无 domain，仅更新右侧百分比
    connect(ui->channelOpacitySlider, &QSlider::valueChanged, this, [this](int value) {
        ui->channelOpacityValueLabel->setText(QStringLiteral("%1%").arg(value));
    });

    // 缩略图防抖：拖动时连发多次 contentChanged 只重算一次
    m_thumbTimer = new QTimer(this);
    m_thumbTimer->setSingleShot(true);
    m_thumbTimer->setInterval(kThumbnailDebounceMs);
    connect(m_thumbTimer, &QTimer::timeout, this, &ChannelTreePanel::onThumbnailTimer);

    refreshFromDocument();
}

ChannelTreePanel::~ChannelTreePanel()
{
    delete ui;
}

void ChannelTreePanel::onDocumentChanged()
{
    if (Ps::ImageDocument *doc = document()) {
        // 像素变化 → 防抖 + 增量；**不再**整表重建（会丢选中项，且每帧重算代价高）
        // 只订阅 pixelsChanged：contentChanged 是所有信号的汇总，连结构变化/属性变化
        // 都会走到这里，而那时 refreshFromDocument 刚重建过缩略图 → 白合成一遍。
        // 像素变化 → 防抖 + 累计脏区增量合成缩略图底图
        connect(doc, &Ps::ImageDocument::pixelsChanged,
                this, [this](const QRect &rect) {
                    if (!rect.isEmpty()) {
                        m_compositeDirtyRect = m_compositeDirtyRect.isNull()
                                                   ? rect
                                                   : m_compositeDirtyRect.united(rect);
                    }
                    scheduleThumbnailRefresh();
                });
        // 结构真的变了（目前通道行固定，仅换文档时走到）才重建
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
    m_compositeCache = QImage();
    m_compositeDirtyRect = QRect();
    if (doc) {
        m_compositeCache = Ps::Compositor::composite(*doc);
        m_compositeDirtyRect = QRect();
    }

    for (const ChannelRow &row : kRows) {
        auto *item = new QListWidgetItem(row.name, ui->itemList);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
        item->setCheckState(Qt::Checked);
        item->setData(kChannelRole, static_cast<int>(row.kind));
        item->setToolTip(tr(row.tip));

        const QImage thumb = ItemTreePanel::makeChannelThumbnail(m_compositeCache, row.kind);
        if (!thumb.isNull())
            item->setIcon(QIcon(QPixmap::fromImage(thumb)));
    }

    if (ui->itemList->count() > 0)
        ui->itemList->setCurrentRow(0);
}

void ChannelTreePanel::scheduleThumbnailRefresh()
{
    // 面板不可见（通道 Tab 在后台）时不做任何计算，只记 dirty，
    // 等 showEvent 再补刷。实测 4000×3000 文档单次刷新 216 ms，省下来很可观。
    if (!isVisible()) {
        m_thumbDirty = true;
        return;
    }
    if (m_thumbTimer)
        m_thumbTimer->start();   // 已在计时则重新计时，实现防抖
}

void ChannelTreePanel::onThumbnailTimer()
{
    updateThumbnails();
}

void ChannelTreePanel::showEvent(QShowEvent *event)
{
    ItemTreePanel::showEvent(event);
    if (m_thumbDirty) {
        m_thumbDirty = false;
        updateThumbnails();
    }
}

void ChannelTreePanel::updateThumbnails()
{
    Ps::ImageDocument *doc = document();
    if (!doc || ui->itemList->count() == 0)
        return;

    const int w = doc->width();
    const int h = doc->height();
    if (w <= 0 || h <= 0)
        return;

    const QRect full(0, 0, w, h);
    const bool cacheOk = !m_compositeCache.isNull()
                         && m_compositeCache.size() == full.size()
                         && m_compositeCache.format() == QImage::Format_ARGB32_Premultiplied;

    QRect dirty = m_compositeDirtyRect.intersected(full);
    // 与投影块对齐，减少碎矩形
    constexpr int kChunk = Ps::TileBuffer::kTileSize;
    if (!dirty.isEmpty() && dirty != full) {
        const int x0 = qMax(0, (dirty.left() / kChunk) * kChunk);
        const int y0 = qMax(0, (dirty.top() / kChunk) * kChunk);
        const int x1 = qMin(full.right(), ((dirty.right() + kChunk) / kChunk) * kChunk - 1);
        const int y1 = qMin(full.bottom(), ((dirty.bottom() + kChunk) / kChunk) * kChunk - 1);
        dirty = QRect(QPoint(x0, y0), QPoint(x1, y1));
    }

    const bool patch = cacheOk
                       && !dirty.isEmpty()
                       && dirty != full
                       && (qint64(dirty.width()) * dirty.height()
                           < qint64(full.width()) * full.height());

    if (patch) {
        if (!Ps::Compositor::compositeRegion(m_compositeCache, *doc, dirty))
            m_compositeCache = Ps::Compositor::composite(*doc);
    } else {
        m_compositeCache = Ps::Compositor::composite(*doc);
    }
    m_compositeDirtyRect = QRect();

    if (m_compositeCache.isNull())
        return;

    // 只换图标：不碰文字 / 勾选 / 选中态，否则会打断用户操作
    for (int i = 0; i < ui->itemList->count(); ++i) {
        QListWidgetItem *item = ui->itemList->item(i);
        if (!item)
            continue;
        const auto kind = static_cast<ThumbChannel>(item->data(kChannelRole).toInt());
        const QImage thumb = ItemTreePanel::makeChannelThumbnail(m_compositeCache, kind);
        if (!thumb.isNull())
            item->setIcon(QIcon(QPixmap::fromImage(thumb)));
    }
}

void ChannelTreePanel::onNewItem()
{
    // TODO：对照 channels-commands.c 的 channels_new_cmd_callback
}

void ChannelTreePanel::onDeleteItem()
{
    // TODO：对照 channels-commands.c；分量通道不可删
}
