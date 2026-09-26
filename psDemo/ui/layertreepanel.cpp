#include "layertreepanel.h"
#include "ui_layertreepanel.h"

#include "domain/imagedocument.h"
#include "domain/layer.h"

#include <QIcon>
#include <QListWidget>
#include <QListWidgetItem>
#include <QSignalBlocker>
#include <QSize>
#include <QSlider>
#include <QTimer>
#include <QToolButton>

#include <cmath>

namespace {

/** 列表行 ↔ LayerStack 下标互转：第 0 行是视觉最上层 = 栈顶（最大下标）。 */
int stackIndexFromRow(const Ps::ImageDocument *doc, int row)
{
    if (!doc || row < 0)
        return -1;
    return doc->layers().count() - 1 - row;
}

int rowFromStackIndex(const Ps::ImageDocument *doc, int stackIndex)
{
    if (!doc || stackIndex < 0)
        return -1;
    return doc->layers().count() - 1 - stackIndex;
}

/** 停笔多久之后重算缩略图（毫秒）。 */
constexpr int kThumbnailDebounceMs = 250;

} // namespace

LayerTreePanel::LayerTreePanel(QWidget *parent)
    : ItemTreePanel(parent)
    , ui(new Ui::LayerTreePanel)
{
    ui->setupUi(this);

    // 缩略图列：图标尺寸与行高对齐（缩略图在 ItemTreePanel 内生成）
    ui->itemList->setIconSize(QSize(ItemTreePanel::kThumbSize, ItemTreePanel::kThumbSize));
    ui->itemList->setGridSize(QSize(0, 0)); // 0 = 交给样式自动算行高

    // 底栏图标：resources/icons/layers/（自绘线框，非 Adobe 资源）
    const QString iconDir = QStringLiteral(":/icons/layers/");
    applyToolbarIcon(ui->btnLinkLayers, iconDir + QStringLiteral("link.svg"));
    applyToolbarIcon(ui->btnLayerStyle, iconDir + QStringLiteral("fx.svg"));
    applyToolbarIcon(ui->btnLayerMask, iconDir + QStringLiteral("mask.svg"));
    applyToolbarIcon(ui->btnAdjustment, iconDir + QStringLiteral("adjustment.svg"));
    applyToolbarIcon(ui->btnNewGroup, iconDir + QStringLiteral("group.svg"));
    applyToolbarIcon(ui->btnNew, iconDir + QStringLiteral("new-layer.svg"));
    applyToolbarIcon(ui->btnDelete, iconDir + QStringLiteral("delete.svg"));

    // —— 类型筛选行 ——
    // PS 用图标表示可筛选的图层类型，本工程原先是"像素/调整/T/形/智"文字按钮，
    // 跨字体渲染不一致，改为图标。顺序与 PS 一致：像素 → 调整 → 文字 → 形状 → 智能对象。
    // 【诚实标注】本项目目前只有像素层，其余四类都是筛选占位（见 toolTip 与 docs/features.md）。
    applyToolbarIcon(ui->filterPixel, iconDir + QStringLiteral("filter-pixel.svg"));
    applyToolbarIcon(ui->filterAdjust, iconDir + QStringLiteral("filter-adjust.svg"));
    applyToolbarIcon(ui->filterType, iconDir + QStringLiteral("filter-type.svg"));
    applyToolbarIcon(ui->filterShape, iconDir + QStringLiteral("filter-shape.svg"));
    applyToolbarIcon(ui->filterSmart, iconDir + QStringLiteral("filter-smart.svg"));

    // —— 锁定行 ——
    // 原先误用"魔棒/画笔/移动/裁剪"等**工具**图标占位，语义完全不对；
    // 改为 PS 的四把锁：锁定透明像素 / 图像像素 / 位置 / 全部。
    applyToolbarIcon(ui->lockTransparent, iconDir + QStringLiteral("lock-transparent.svg"));
    applyToolbarIcon(ui->lockImage, iconDir + QStringLiteral("lock-image.svg"));
    applyToolbarIcon(ui->lockPosition, iconDir + QStringLiteral("lock-position.svg"));
    applyToolbarIcon(ui->lockAll, iconDir + QStringLiteral("lock-all.svg"));

    // GIMP：new_action / delete_action → "layers-new" / "layers-delete"
    connect(ui->btnNew, &QToolButton::clicked, this, &LayerTreePanel::onNewItem);
    connect(ui->btnDelete, &QToolButton::clicked, this, &LayerTreePanel::onDeleteItem);
    connect(ui->itemList, &QListWidget::itemSelectionChanged,
            this, &LayerTreePanel::onListSelectionChanged);
    connect(ui->itemList, &QListWidget::itemChanged,
            this, &LayerTreePanel::onItemChanged);

    // —— 缩略图防抖 ——
    // 单次触发：连发多次 contentChanged（画笔拖动）只会重算一次
    m_thumbTimer = new QTimer(this);
    m_thumbTimer->setSingleShot(true);
    m_thumbTimer->setInterval(kThumbnailDebounceMs);
    connect(m_thumbTimer, &QTimer::timeout, this, &LayerTreePanel::onThumbnailTimer);

    // —— 不透明度滑条的「拖动预览 / 松手提交」两段语义 ——
    // 拖动中只改右侧百分比文字（预览），松手才写入 domain。
    // 好处：① 一次拖动只产生一次状态变更（撤销栈不会被滑条淹没，为 Phase 6 铺路）；
    //       ② 与 PS/GIMP 行为一致（松开才生效）。
    connect(ui->opacitySlider, &QSlider::valueChanged,
            this, &LayerTreePanel::onOpacityValueChanged);
    connect(ui->opacitySlider, &QSlider::sliderReleased,
            this, &LayerTreePanel::onOpacityCommitted);

    connect(ui->fillSlider, &QSlider::valueChanged, this, [this](int value) {
        ui->fillValueLabel->setText(QStringLiteral("%1%").arg(value));
        // 填充尚未进 domain，仅同步 UI 显示
    });
}

LayerTreePanel::~LayerTreePanel()
{
    delete ui;
}

void LayerTreePanel::onDocumentChanged()
{
    // 订阅策略：让「像素变了」不再触发列表重建。
    // 早先 documentChanged 直连 refreshFromDocument，导致每画一笔就 clear() 重建整表，
    // 选中项/编辑态/滚动位置全部丢失（代码里用 blockSignals 打的补丁正是这个症状）。
    if (Ps::ImageDocument *doc = document()) {
        connect(doc, &Ps::ImageDocument::structureChanged,
                this, &LayerTreePanel::refreshFromDocument);
        connect(doc, &Ps::ImageDocument::activeLayerChanged,
                this, &LayerTreePanel::onActiveLayerChanged);
        connect(doc, &Ps::ImageDocument::layerPropertiesChanged,
                this, &LayerTreePanel::onLayerPropertiesChanged);
        // 像素改动 → 只安排防抖刷新缩略图，绝不重建列表。
        // 只订阅 pixelsChanged：contentChanged 是汇总信号，结构/属性变化也会发它，
        // 而那时上面几个槽刚刷过 → 缩略图会被白算一遍。
        connect(doc, &Ps::ImageDocument::pixelsChanged,
                this, &LayerTreePanel::scheduleThumbnailRefresh);
    }
    refreshFromDocument();
}

void LayerTreePanel::refreshFromDocument()
{
    const QSignalBlocker blocker(ui->itemList);
    ui->itemList->clear();

    Ps::ImageDocument *doc = document();
    if (!doc) {
        setOptionsEnabled(false);
        syncActiveRowAndOptions();
        return;
    }

    setOptionsEnabled(true);

    const int count = doc->layers().count();
    for (int stackIndex = count - 1; stackIndex >= 0; --stackIndex) {
        Ps::Layer *layer = doc->layers().layerAt(stackIndex);
        if (!layer)
            continue;
        appendRowForLayer(stackIndex, *layer);
    }

    syncActiveRowAndOptions();
}

void LayerTreePanel::onActiveLayerChanged(int index)
{
    Q_UNUSED(index)
    // 只更新选中行 + 选项控件，不重建列表
    syncActiveRowAndOptions();
}

void LayerTreePanel::onLayerPropertiesChanged(int stackIndex)
{
    Ps::ImageDocument *doc = document();
    if (!doc)
        return;

    // 只重建/更新受影响的那一行 —— 对应 GIMP 容器视图的增量 notify
    QListWidgetItem *item = itemForStackIndex(stackIndex);
    Ps::Layer *layer = doc->layers().layerAt(stackIndex);
    if (!item || !layer)
        return;

    const QSignalBlocker blocker(ui->itemList);
    // 勾选态与本行文字都要跟 domain 对齐（改名可能来自别处）
    const Qt::CheckState check = layer->isVisible() ? Qt::Checked : Qt::Unchecked;
    if (item->checkState() != check)
        item->setCheckState(check);
    if (item->text() != layer->name())
        item->setText(layer->name());

    // 若改的正是活动层，选项区也要跟着变
    if (stackIndex == doc->activeLayerIndex())
        syncActiveRowAndOptions();
}

void LayerTreePanel::onListSelectionChanged()
{
    Ps::ImageDocument *doc = document();
    if (!doc || !ui->itemList->currentItem())
        return;

    // 走语义化 setter（domain 收口），UI 不再直接操作 LayerStack
    doc->setActiveLayerIndex(stackIndexFromRow(doc, ui->itemList->currentRow()));
}

void LayerTreePanel::onItemChanged(QListWidgetItem *item)
{
    Ps::ImageDocument *doc = document();
    if (!doc || !item)
        return;

    const int stackIndex = item->data(Qt::UserRole).toInt();
    Ps::Layer *layer = doc->layers().layerAt(stackIndex);
    if (!layer)
        return;

    // 只判断「与 domain 是否不同」；相等则 no-op，避免信号回环
    const bool visible = item->checkState() == Qt::Checked;
    if (layer->isVisible() != visible)
        doc->setLayerVisible(stackIndex, visible); // 内部自动广播 layerPropertiesChanged

    if (layer->name() != item->text())
        doc->setLayerName(stackIndex, item->text());
}

void LayerTreePanel::onOpacityValueChanged(int value)
{
    // 拖动中：只更新数字（预览），不写 domain
    ui->opacityValueLabel->setText(QStringLiteral("%1%").arg(value));

    // 键盘方向键/点击轨道不触发 sliderReleased，此时 isSliderDown() 为 false，
    // 直接提交，保证键盘操作也能生效。
    if (!ui->opacitySlider->isSliderDown())
        commitOpacity(value);
}

void LayerTreePanel::onOpacityCommitted()
{
    commitOpacity(ui->opacitySlider->value());
}

void LayerTreePanel::commitOpacity(int value)
{
    Ps::ImageDocument *doc = document();
    if (!doc)
        return;
    const int index = doc->activeLayerIndex();
    if (index < 0)
        return;

    const Ps::Layer *layer = doc->layers().layerAt(index);
    if (!layer)
        return;

    // 与 domain 相同则不提交，避免拖回原点仍推入一次历史（Phase 6 需要）
    const qreal target = value / 100.0;
    if (std::abs(layer->opacity() - target) < 1e-6)
        return;

    doc->setLayerOpacity(index, target);
}

void LayerTreePanel::onNewItem()
{
    if (Ps::ImageDocument *doc = document())
        doc->addTransparentLayer();
}

void LayerTreePanel::onDeleteItem()
{
    if (Ps::ImageDocument *doc = document())
        doc->removeLayer(doc->activeLayerIndex());
}

// —— 私有工具 ——

void LayerTreePanel::appendRowForLayer(int stackIndex, Ps::Layer &layer)
{
    auto *item = new QListWidgetItem(layer.name(), ui->itemList);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEditable
                   | Qt::ItemIsSelectable);
    item->setCheckState(layer.isVisible() ? Qt::Checked : Qt::Unchecked);
    // 行 ↔ 栈下标映射存在 UserRole：行序会变，不能用行号当身份
    item->setData(Qt::UserRole, stackIndex);
    // 缩略图：该层像素 + 透明棋盘格（对齐 PS 图层面板最左列）
    item->setIcon(QIcon(QPixmap::fromImage(makeLayerThumbnail(layer.pixels()))));
}

QListWidgetItem *LayerTreePanel::itemForStackIndex(int stackIndex) const
{
    if (!ui || stackIndex < 0)
        return nullptr;
    for (int row = 0; row < ui->itemList->count(); ++row) {
        QListWidgetItem *item = ui->itemList->item(row);
        if (item && item->data(Qt::UserRole).toInt() == stackIndex)
            return item;
    }
    return nullptr;
}

void LayerTreePanel::syncActiveRowAndOptions()
{
    Ps::ImageDocument *doc = document();
    if (!doc) {
        setOptionsEnabled(false);
        return;
    }

    const QSignalBlocker listBlocker(ui->itemList);
    const int activeRow = rowFromStackIndex(doc, doc->activeLayerIndex());
    if (activeRow >= 0 && ui->itemList->currentRow() != activeRow)
        ui->itemList->setCurrentRow(activeRow);

    Ps::Layer *layer = doc->activeLayer();
    const QSignalBlocker sliderBlocker(ui->opacitySlider);
    if (layer) {
        const int percent = qRound(layer->opacity() * 100.0);
        ui->opacitySlider->setValue(percent);
        ui->opacityValueLabel->setText(QStringLiteral("%1%").arg(percent));
    }
}

void LayerTreePanel::setOptionsEnabled(bool enabled)
{
    ui->opacitySlider->setEnabled(enabled);
    ui->fillSlider->setEnabled(enabled);
    ui->blendModeCombo->setEnabled(enabled);
}

// —— 缩略图刷新 ——

void LayerTreePanel::refreshRowThumbnail(int stackIndex, const Ps::Layer *layer)
{
    Ps::ImageDocument *doc = document();
    if (!doc)
        return;
    if (!layer)
        layer = doc->layers().layerAt(stackIndex);
    if (!layer)
        return;

    QListWidgetItem *item = itemForStackIndex(stackIndex);
    if (!item)
        return;

    // 只换图标，不碰文字/勾选/选中态 —— 否则会打断用户正在进行的改名或选择
    item->setIcon(QIcon(QPixmap::fromImage(makeLayerThumbnail(layer->pixels()))));
}

void LayerTreePanel::scheduleThumbnailRefresh()
{
    if (m_thumbTimer)
        m_thumbTimer->start(); // 已在计时则重新计时，实现防抖
}

void LayerTreePanel::onThumbnailTimer()
{
    Ps::ImageDocument *doc = document();
    if (!doc)
        return;
    // 画笔/橡皮只动活动层，故只重算那一行；其他层的缩略图仍然有效
    const int active = doc->activeLayerIndex();
    if (active >= 0)
        refreshRowThumbnail(active);
}
