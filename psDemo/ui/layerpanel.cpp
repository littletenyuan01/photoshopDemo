#include "layerpanel.h"
#include "ui_layerpanel.h"

#include "domain/imagedocument.h"
#include "domain/layer.h"

#include <QListWidgetItem>
#include <QSignalBlocker>

LayerPanel::LayerPanel(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::LayerPanel)
{
    ui->setupUi(this); // 控件来自 layerpanel.ui

    connect(ui->btnAdd, &QToolButton::clicked, this, &LayerPanel::onAddLayer);
    connect(ui->btnDelete, &QToolButton::clicked, this, &LayerPanel::onDeleteLayer);
    connect(ui->btnUp, &QToolButton::clicked, this, &LayerPanel::onMoveUp);
    connect(ui->btnDown, &QToolButton::clicked, this, &LayerPanel::onMoveDown);
    connect(ui->layerList, &QListWidget::itemSelectionChanged, this, &LayerPanel::onListSelectionChanged);
    connect(ui->layerList, &QListWidget::itemChanged, this, &LayerPanel::onItemChanged);
    connect(ui->opacitySlider, &QSlider::valueChanged, this, &LayerPanel::onOpacityChanged);
}

LayerPanel::~LayerPanel()
{
    delete ui;
}

void LayerPanel::setDocument(Ps::ImageDocument *document)
{
    if (m_document == document)
        return;

    if (m_document)
        disconnect(m_document, nullptr, this, nullptr);

    m_document = document;

    if (m_document) {
        // 结构/活动层变 → 整表刷新；普通像素变 → 只同步透明度显示
        connect(m_document, &Ps::ImageDocument::structureChanged, this, &LayerPanel::refreshFromDocument);
        connect(m_document, &Ps::ImageDocument::activeLayerChanged, this, &LayerPanel::refreshFromDocument);
        connect(m_document, &Ps::ImageDocument::documentChanged, this, [this]() {
            // 避免画笔高频 documentChanged 时整表重建（画笔尚未接入，预留）
            if (!m_document)
                return;
            Ps::Layer *layer = m_document->activeLayer();
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

void LayerPanel::onAddLayer()
{
    if (!m_document)
        return;
    // 具体层对象由 domain 创建并发信号，面板只触发
    m_document->addTransparentLayer();
}

void LayerPanel::onDeleteLayer()
{
    if (!m_document)
        return;
    m_document->removeLayer(m_document->activeLayerIndex());
}

void LayerPanel::onMoveUp()
{
    if (!m_document)
        return;
    // UI「上移」= 更靠近列表顶部 = LayerStack 下标 +1（更晚合成、压在上面）
    const int from = m_document->activeLayerIndex();
    const int to = from + 1;
    if (to >= m_document->layers().count())
        return;
    m_document->layers().moveLayer(from, to);
    m_document->setActiveLayerIndex(to);
    m_document->notifyStructureChanged();
}

void LayerPanel::onMoveDown()
{
    if (!m_document)
        return;
    // UI「下移」= LayerStack 下标 -1
    const int from = m_document->activeLayerIndex();
    const int to = from - 1;
    if (to < 0)
        return;
    m_document->layers().moveLayer(from, to);
    m_document->setActiveLayerIndex(to);
    m_document->notifyStructureChanged();
}

void LayerPanel::onListSelectionChanged()
{
    if (!m_document || !ui->layerList->currentItem())
        return;

    // 把列表选中映射回栈下标，设为活动层（后续画笔写这一层）
    const int stackIndex = stackIndexFromRow(ui->layerList->currentRow());
    m_document->setActiveLayerIndex(stackIndex);

    Ps::Layer *layer = m_document->activeLayer();
    if (!layer)
        return;
    const QSignalBlocker blocker(ui->opacitySlider);
    const int percent = qRound(layer->opacity() * 100.0);
    ui->opacitySlider->setValue(percent);
    ui->opacityValueLabel->setText(QStringLiteral("%1%").arg(percent));
}

void LayerPanel::onItemChanged(QListWidgetItem *item)
{
    if (!m_document || !item)
        return;

    // UserRole 存的是 LayerStack 下标，不是行号
    const int stackIndex = item->data(Qt::UserRole).toInt();
    Ps::Layer *layer = m_document->layers().layerAt(stackIndex);
    if (!layer)
        return;

    const bool visible = item->checkState() == Qt::Checked;
    if (layer->isVisible() != visible) {
        layer->setVisible(visible);
        m_document->notifyLayerVisualChanged(); // 触发画布重合成
    }

    if (layer->name() != item->text()) {
        layer->setName(item->text());
        // 改名不影响像素；勿发 structureChanged，否则 refresh 会打断就地编辑
        m_document->markDirty();
    }
}

void LayerPanel::onOpacityChanged(int value)
{
    if (!m_document)
        return;
    Ps::Layer *layer = m_document->activeLayer();
    if (!layer)
        return;

    // 滑条 0–100 → 图层 opacity 0–1，合成时乘到该层
    layer->setOpacity(value / 100.0);
    ui->opacityValueLabel->setText(QStringLiteral("%1%").arg(value));
    m_document->notifyLayerVisualChanged();
}

void LayerPanel::refreshFromDocument()
{
    blockUiSignals(true);
    ui->layerList->clear();

    if (!m_document) {
        ui->opacitySlider->setEnabled(false);
        blockUiSignals(false);
        return;
    }

    ui->opacitySlider->setEnabled(true);
    const int count = m_document->layers().count();

    // 倒序插入：先画栈顶到列表第 0 行
    for (int stackIndex = count - 1; stackIndex >= 0; --stackIndex) {
        Ps::Layer *layer = m_document->layers().layerAt(stackIndex);
        if (!layer)
            continue;

        auto *item = new QListWidgetItem(layer->name(), ui->layerList);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEditable | Qt::ItemIsSelectable);
        item->setCheckState(layer->isVisible() ? Qt::Checked : Qt::Unchecked);
        item->setData(Qt::UserRole, stackIndex); // 反向查找栈下标
    }

    const int activeRow = rowFromStackIndex(m_document->activeLayerIndex());
    if (activeRow >= 0)
        ui->layerList->setCurrentRow(activeRow);

    if (Ps::Layer *layer = m_document->activeLayer()) {
        const int percent = qRound(layer->opacity() * 100.0);
        ui->opacitySlider->setValue(percent);
        ui->opacityValueLabel->setText(QStringLiteral("%1%").arg(percent));
    }

    blockUiSignals(false);
}

int LayerPanel::stackIndexFromRow(int row) const
{
    if (!m_document || row < 0)
        return -1;
    // row0 ↔ count-1
    return m_document->layers().count() - 1 - row;
}

int LayerPanel::rowFromStackIndex(int stackIndex) const
{
    if (!m_document || stackIndex < 0)
        return -1;
    return m_document->layers().count() - 1 - stackIndex;
}

void LayerPanel::blockUiSignals(bool block)
{
    ui->layerList->blockSignals(block);
    ui->opacitySlider->blockSignals(block);
}
