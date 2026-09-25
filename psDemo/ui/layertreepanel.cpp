#include "layertreepanel.h"
#include "ui_layertreepanel.h"

#include "domain/imagedocument.h"
#include "domain/layer.h"

#include <QSignalBlocker>

LayerTreePanel::LayerTreePanel(QWidget *parent)
    : ItemTreePanel(parent)
    , ui(new Ui::LayerTreePanel)
{
    ui->setupUi(this);
    // 骨架名与 GimpItemTreeView 的 options / tree / button_box 对应
    bindSkeleton(ui->optionsHost, ui->itemList, ui->toolbarHost);

    // GIMP：new_action / delete_action → "layers-new" / "layers-delete"
    connect(ui->btnNew, &QToolButton::clicked, this, &LayerTreePanel::onNewItem);
    connect(ui->btnDelete, &QToolButton::clicked, this, &LayerTreePanel::onDeleteItem);
    connect(ui->itemList, &QListWidget::itemSelectionChanged, this, &LayerTreePanel::onListSelectionChanged);
    connect(ui->itemList, &QListWidget::itemChanged, this, &LayerTreePanel::onItemChanged);
    // 0–100 用滑动条（见 percent-sliders 规则）；GIMP 在 view 内直接改 opacity
    connect(ui->opacitySlider, &QSlider::valueChanged, this, &LayerTreePanel::onOpacityChanged);
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
    if (Ps::ImageDocument *doc = document()) {
        connect(doc, &Ps::ImageDocument::structureChanged, this, &LayerTreePanel::refreshFromDocument);
        connect(doc, &Ps::ImageDocument::activeLayerChanged, this, &LayerTreePanel::refreshFromDocument);
        connect(doc, &Ps::ImageDocument::documentChanged, this, [this]() {
            Ps::ImageDocument *d = document();
            if (!d)
                return;
            Ps::Layer *layer = d->activeLayer();
            if (!layer)
                return;
            const QSignalBlocker blocker(ui->opacitySlider);
            const int percent = qRound(layer->opacity() * 100.0);
            ui->opacitySlider->setValue(percent);
            ui->opacityValueLabel->setText(QStringLiteral("%1%").arg(percent));
        });
    }
    refreshFromDocument();
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

void LayerTreePanel::onListSelectionChanged()
{
    Ps::ImageDocument *doc = document();
    if (!doc || !ui->itemList->currentItem())
        return;

    doc->setActiveLayerIndex(stackIndexFromRow(ui->itemList->currentRow()));

    if (Ps::Layer *layer = doc->activeLayer()) {
        const QSignalBlocker blocker(ui->opacitySlider);
        const int percent = qRound(layer->opacity() * 100.0);
        ui->opacitySlider->setValue(percent);
        ui->opacityValueLabel->setText(QStringLiteral("%1%").arg(percent));
    }
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

    const bool visible = item->checkState() == Qt::Checked;
    if (layer->isVisible() != visible) {
        layer->setVisible(visible);
        doc->notifyLayerVisualChanged();
    }

    if (layer->name() != item->text()) {
        layer->setName(item->text());
        doc->markDirty();
    }
}

void LayerTreePanel::onOpacityChanged(int value)
{
    ui->opacityValueLabel->setText(QStringLiteral("%1%").arg(value));

    Ps::ImageDocument *doc = document();
    if (!doc)
        return;
    Ps::Layer *layer = doc->activeLayer();
    if (!layer)
        return;

    layer->setOpacity(value / 100.0);
    doc->notifyLayerVisualChanged();
}

void LayerTreePanel::refreshFromDocument()
{
    blockUiSignals(true);
    ui->itemList->clear();

    Ps::ImageDocument *doc = document();
    if (!doc) {
        ui->opacitySlider->setEnabled(false);
        ui->fillSlider->setEnabled(false);
        ui->blendModeCombo->setEnabled(false);
        blockUiSignals(false);
        return;
    }

    ui->opacitySlider->setEnabled(true);
    ui->fillSlider->setEnabled(true);
    ui->blendModeCombo->setEnabled(true);

    const int count = doc->layers().count();
    for (int stackIndex = count - 1; stackIndex >= 0; --stackIndex) {
        Ps::Layer *layer = doc->layers().layerAt(stackIndex);
        if (!layer)
            continue;

        auto *item = new QListWidgetItem(layer->name(), ui->itemList);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEditable | Qt::ItemIsSelectable);
        item->setCheckState(layer->isVisible() ? Qt::Checked : Qt::Unchecked);
        item->setData(Qt::UserRole, stackIndex);
    }

    const int activeRow = rowFromStackIndex(doc->activeLayerIndex());
    if (activeRow >= 0)
        ui->itemList->setCurrentRow(activeRow);

    if (Ps::Layer *layer = doc->activeLayer()) {
        const int percent = qRound(layer->opacity() * 100.0);
        ui->opacitySlider->setValue(percent);
        ui->opacityValueLabel->setText(QStringLiteral("%1%").arg(percent));
    }

    blockUiSignals(false);
}

int LayerTreePanel::stackIndexFromRow(int row) const
{
    Ps::ImageDocument *doc = document();
    if (!doc || row < 0)
        return -1;
    return doc->layers().count() - 1 - row;
}

int LayerTreePanel::rowFromStackIndex(int stackIndex) const
{
    Ps::ImageDocument *doc = document();
    if (!doc || stackIndex < 0)
        return -1;
    return doc->layers().count() - 1 - stackIndex;
}

void LayerTreePanel::blockUiSignals(bool block)
{
    ui->itemList->blockSignals(block);
    ui->opacitySlider->blockSignals(block);
    ui->fillSlider->blockSignals(block);
}
