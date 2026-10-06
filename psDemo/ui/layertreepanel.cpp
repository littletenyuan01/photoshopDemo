/**
 * layertreepanel.cpp — 图层树面板实现（ui 层）。
 */
#include "layertreepanel.h"
#include "ui_layertreepanel.h"

#include "domain/blendmode.h"
#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "domain/layerstylestack.h"
#include "domain/selection.h"
#include "ui/layerrowwidget.h"
#include "ui/layerstyledialog.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QListWidgetItem>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QSignalBlocker>
#include <QTimer>

#include <cmath>

namespace {

constexpr int kRoleStackIndex = Qt::UserRole;
constexpr int kThumbnailDebounceMs = 250;

int stackIndexOfItem(const QListWidgetItem *item)
{
    if (!item)
        return -1;
    return item->data(kRoleStackIndex).toInt();
}

} // namespace

LayerTreePanel::LayerTreePanel(QWidget *parent)
    : ItemTreePanel(parent)
    , ui(new Ui::LayerTreePanel)
{
    ui->setupUi(this);

    connect(ui->btnNew, &QToolButton::clicked, this, &LayerTreePanel::onBtnNewClicked);
    connect(ui->btnDelete, &QToolButton::clicked, this, &LayerTreePanel::onBtnDeleteClicked);
    connect(ui->btnLayerStyle, &QToolButton::clicked, this, &LayerTreePanel::onBtnLayerStyleClicked);
    connect(ui->btnLayerMask, &QToolButton::clicked, this, &LayerTreePanel::onBtnLayerMaskClicked);
    connect(ui->btnAdjustment, &QToolButton::clicked, this, &LayerTreePanel::onBtnAdjustmentClicked);
    connect(ui->itemList, &QListWidget::itemSelectionChanged,
            this, &LayerTreePanel::onListSelectionChanged);

    // 图层拖放重排：行 widget 发起 QDrag，列表 viewport 接放
    ui->itemList->setDragDropMode(QAbstractItemView::DropOnly);
    ui->itemList->setDefaultDropAction(Qt::MoveAction);
    ui->itemList->setDropIndicatorShown(true);
    ui->itemList->setAcceptDrops(true);
    ui->itemList->viewport()->setAcceptDrops(true);
    ui->itemList->viewport()->installEventFilter(this);

    m_thumbTimer = new QTimer(this);
    m_thumbTimer->setSingleShot(true);
    m_thumbTimer->setInterval(kThumbnailDebounceMs);
    connect(m_thumbTimer, &QTimer::timeout, this, &LayerTreePanel::onThumbnailTimer);

    connect(ui->opacitySlider, &QSlider::valueChanged,
            this, &LayerTreePanel::onOpacityValueChanged);
    connect(ui->opacitySlider, &QSlider::sliderReleased,
            this, &LayerTreePanel::onOpacityCommitted);
    setupBlendModeCombo();
    connect(ui->blendModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &LayerTreePanel::onBlendModeChanged);
    connect(ui->blendModeCombo, QOverload<int>::of(&QComboBox::highlighted),
            this, &LayerTreePanel::onBlendModeHighlighted);
    if (QAbstractItemView *view = ui->blendModeCombo->view()) {
        view->installEventFilter(this);
        if (view->window())
            view->window()->installEventFilter(this);
    }

    connect(ui->fillSlider, &QSlider::valueChanged, this, [this](int value) {
        ui->fillValueLabel->setText(QStringLiteral("%1%").arg(value));
    });

    buildLayerContextMenu();
    connect(ui->itemList, &QWidget::customContextMenuRequested,
            this, &LayerTreePanel::onLayerContextMenu);
    connect(ui->actionCtxNewLayer, &QAction::triggered,
            this, &LayerTreePanel::onBtnNewClicked);
    connect(ui->actionCtxDuplicateLayer, &QAction::triggered, this, [this]() {
        Ps::ImageDocument *d = document();
        if (!d || d->activeLayerIndex() < 0)
            return;
        d->duplicateLayer(d->activeLayerIndex());
    });
    connect(ui->actionCtxDeleteLayer, &QAction::triggered,
            this, &LayerTreePanel::onBtnDeleteClicked);
    connect(ui->actionCtxRenameLayer, &QAction::triggered, this, [this]() {
        if (LayerRowWidget *row = rowWidgetForStackIndex(
                document() ? document()->activeLayerIndex() : -1))
            row->beginRename();
    });
    connect(ui->actionCtxToggleVisible, &QAction::triggered, this, [this]() {
        Ps::ImageDocument *d = document();
        if (!d)
            return;
        const int i = d->activeLayerIndex();
        Ps::Layer *L = d->activeLayer();
        if (!L)
            return;
        d->setLayerVisible(i, !L->isVisible());
    });
    connect(ui->actionCtxCopyLayerStyle, &QAction::triggered,
            this, &LayerTreePanel::onCopyLayerStyle);
    connect(ui->actionCtxPasteLayerStyle, &QAction::triggered,
            this, &LayerTreePanel::onPasteLayerStyle);
}

LayerTreePanel::~LayerTreePanel()
{
    delete ui;
}

void LayerTreePanel::onDocumentChanged()
{
    if (Ps::ImageDocument *doc = document()) {
        connect(doc, &Ps::ImageDocument::structureChanged, this, [this]() {
            refreshFromDocument();
        });
        connect(doc, &Ps::ImageDocument::activeLayerChanged, this, [this](int index) {
            onActiveLayerChanged(index);
        });
        connect(doc, &Ps::ImageDocument::layerPropertiesChanged, this, [this](int index) {
            onLayerPropertiesChanged(index);
        });
        connect(doc, &Ps::ImageDocument::pixelsChanged, this, [this](const QRect &) {
            scheduleThumbnailRefresh();
        });
        connect(doc, &Ps::ImageDocument::selectionChanged, this, [this]() {
            if (!m_settingAlphaSelect) {
                m_alphaSelectSourceLayer = -1;
                m_maskSelectSourceLayer = -1;
            }
        });
        connect(doc, &Ps::ImageDocument::editingTargetChanged, this, [this]() {
            Ps::ImageDocument *d = document();
            if (!d)
                return;
            for (int i = 0; i < d->layers().count(); ++i)
                refreshRowThumbnail(i);
        });
    }
    m_alphaSelectSourceLayer = -1;
    m_maskSelectSourceLayer = -1;
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
    if (ui->itemList->count() > 0)
        ui->itemList->scrollToItem(ui->itemList->item(0));
}

void LayerTreePanel::onActiveLayerChanged(int index)
{
    Q_UNUSED(index)
    syncActiveRowAndOptions();
}

void LayerTreePanel::onLayerPropertiesChanged(int stackIndex)
{
    Ps::ImageDocument *doc = document();
    if (!doc)
        return;

    LayerRowWidget *row = rowWidgetForStackIndex(stackIndex);
    Ps::Layer *layer = doc->layers().layerAt(stackIndex);
    if (!row || !layer)
        return;

    // 属性变了只刷行内容；缩略图走 pixelsChanged 防抖，展开态由用户/建行决定
    row->syncFromLayer(*layer);

    applyLayerThumbnail(row, *layer);

    if (stackIndex == doc->activeLayerIndex())
        syncActiveRowAndOptions();
}

void LayerTreePanel::onListSelectionChanged()
{
    Ps::ImageDocument *doc = document();
    if (!doc || !ui->itemList->currentItem())
        return;

    const int stackIndex = stackIndexOfItem(ui->itemList->currentItem());
    if (stackIndex >= 0)
        doc->setActiveLayerIndex(stackIndex);
}

void LayerTreePanel::onOpacityValueChanged(int value)
{
    ui->opacityValueLabel->setText(QStringLiteral("%1%").arg(value));
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

    const qreal target = value / 100.0;
    if (std::abs(layer->opacity() - target) < 1e-6)
        return;

    doc->setLayerOpacity(index, target);
}

void LayerTreePanel::setupBlendModeCombo()
{
    QComboBox *combo = ui->blendModeCombo;
    const QSignalBlocker blocker(combo);
    Q_ASSERT(combo->count() == Ps::kBlendModeCount);
    for (int i = 0; i < Ps::kBlendModeCount; ++i)
        combo->setItemData(i, i);

    combo->insertSeparator(23);
    combo->insertSeparator(19);
    combo->insertSeparator(12);
    combo->insertSeparator(7);
    combo->insertSeparator(2);
}

int LayerTreePanel::comboIndexForBlendMode(Ps::BlendMode mode) const
{
    const int want = static_cast<int>(mode);
    for (int i = 0; i < ui->blendModeCombo->count(); ++i) {
        const QVariant data = ui->blendModeCombo->itemData(i);
        if (data.isValid() && data.toInt() == want)
            return i;
    }
    return 0;
}

bool LayerTreePanel::blendModeAtComboIndex(int index, Ps::BlendMode *out) const
{
    if (!out || index < 0 || index >= ui->blendModeCombo->count())
        return false;
    const QVariant data = ui->blendModeCombo->itemData(index);
    if (!data.isValid())
        return false;
    const int modeInt = data.toInt();
    if (!Ps::isValidBlendMode(modeInt))
        return false;
    *out = static_cast<Ps::BlendMode>(modeInt);
    return true;
}

void LayerTreePanel::beginBlendModePreview()
{
    if (m_blendPreviewActive)
        return;
    Ps::ImageDocument *doc = document();
    Ps::Layer *layer = doc ? doc->activeLayer() : nullptr;
    if (!layer)
        return;
    m_blendPreviewActive = true;
    m_blendPreviewOriginal = layer->blendMode();
    m_blendPreviewLayerIndex = doc->activeLayerIndex();
}

void LayerTreePanel::endBlendModePreview()
{
    if (!m_blendPreviewActive)
        return;
    const int layerIndex = m_blendPreviewLayerIndex;
    const Ps::BlendMode original = m_blendPreviewOriginal;
    m_blendPreviewActive = false;
    m_blendPreviewLayerIndex = -1;

    Ps::BlendMode mode = original;
    blendModeAtComboIndex(ui->blendModeCombo->currentIndex(), &mode);

    Ps::ImageDocument *doc = document();
    if (!doc || layerIndex < 0)
        return;
    const Ps::Layer *layer = doc->layers().layerAt(layerIndex);
    if (!layer)
        return;
    if (layer->blendMode() == mode) {
        if (mode != original) {
            doc->setLayerBlendMode(layerIndex, original, false);
            doc->setLayerBlendMode(layerIndex, mode, true);
        }
        return;
    }
    if (mode == original)
        doc->setLayerBlendMode(layerIndex, mode, false);
    else {
        doc->setLayerBlendMode(layerIndex, original, false);
        doc->setLayerBlendMode(layerIndex, mode, true);
    }
}

void LayerTreePanel::applyBlendModeToActiveLayer(Ps::BlendMode mode)
{
    Ps::ImageDocument *doc = document();
    if (!doc)
        return;
    const int layerIndex = (m_blendPreviewActive && m_blendPreviewLayerIndex >= 0)
                               ? m_blendPreviewLayerIndex
                               : doc->activeLayerIndex();
    if (layerIndex < 0)
        return;
    const Ps::Layer *layer = doc->layers().layerAt(layerIndex);
    if (!layer || layer->blendMode() == mode)
        return;
    doc->setLayerBlendMode(layerIndex, mode, !m_blendPreviewActive);
}

void LayerTreePanel::onBlendModeHighlighted(int index)
{
    Ps::BlendMode mode = Ps::BlendMode::Normal;
    if (!blendModeAtComboIndex(index, &mode))
        return;
    beginBlendModePreview();
    if (QAbstractItemView *view = ui->blendModeCombo->view()) {
        view->installEventFilter(this);
        if (QWidget *win = view->window())
            win->installEventFilter(this);
    }
    applyBlendModeToActiveLayer(mode);
}

void LayerTreePanel::onBlendModeChanged(int index)
{
    Ps::BlendMode mode = Ps::BlendMode::Normal;
    if (!blendModeAtComboIndex(index, &mode))
        return;
    if (m_blendPreviewActive) {
        endBlendModePreview();
        return;
    }
    applyBlendModeToActiveLayer(mode);
}

void LayerTreePanel::onBtnNewClicked()
{
    onNewItem();
}

void LayerTreePanel::onBtnDeleteClicked()
{
    onDeleteItem();
}

void LayerTreePanel::onBtnLayerStyleClicked()
{
    Ps::ImageDocument *doc = document();
    if (!doc || !doc->activeLayer())
        return;

    LayerStyleDialog dlg(doc->activeLayer(), this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    doc->replaceActiveLayerStyles(dlg.resultEffects());
}

void LayerTreePanel::onBtnLayerMaskClicked()
{
    Ps::ImageDocument *doc = document();
    if (!doc)
        return;
    const int index = doc->activeLayerIndex();
    Ps::Layer *layer = doc->activeLayer();
    if (!layer || index < 0)
        return;

    // 已有蒙版 → 删除；否则：有选区则「显示选区」，无选区则「显示全部」
    if (layer->hasMask()) {
        doc->removeLayerMask(index);
        return;
    }
    const auto init = doc->selection().isEmpty()
                          ? Ps::ImageDocument::LayerMaskInit::RevealAll
                          : Ps::ImageDocument::LayerMaskInit::RevealSelection;
    if (doc->addLayerMask(index, init))
        doc->setEditingLayerMask(true);
}

void LayerTreePanel::onBtnAdjustmentClicked()
{
    Ps::ImageDocument *doc = document();
    if (!doc)
        return;
    if (doc->addBrightnessContrastAdjustmentLayer() < 0)
        return;
}

void LayerTreePanel::onNewItem()
{
    Ps::ImageDocument *doc = document();
    if (!doc)
        return;

    const int index = doc->addTransparentLayer();
    if (index < 0)
        return;

    refreshFromDocument();
}

void LayerTreePanel::onDeleteItem()
{
    Ps::ImageDocument *doc = document();
    if (!doc)
        return;
    if (doc->removeLayer(doc->activeLayerIndex()))
        refreshFromDocument();
}

bool LayerTreePanel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == ui->itemList->viewport() && handleLayerListDrag(event))
        return true;

    if (m_blendPreviewActive && event->type() == QEvent::Hide) {
        QAbstractItemView *view = ui->blendModeCombo->view();
        if (watched == view || (view && watched == view->window()))
            endBlendModePreview();
    }
    return ItemTreePanel::eventFilter(watched, event);
}

bool LayerTreePanel::handleLayerListDrag(QEvent *event)
{
    const auto mimeOk = [](const QMimeData *mime) {
        return mime && mime->hasFormat(QString::fromLatin1(LayerRowWidget::kLayerDragMime));
    };

    switch (event->type()) {
    case QEvent::DragEnter: {
        auto *e = static_cast<QDragEnterEvent *>(event);
        if (!mimeOk(e->mimeData()))
            return false;
        e->acceptProposedAction();
        return true;
    }
    case QEvent::DragMove: {
        auto *e = static_cast<QDragMoveEvent *>(event);
        if (!mimeOk(e->mimeData()))
            return false;
        e->acceptProposedAction();
        return true;
    }
    case QEvent::Drop: {
        auto *e = static_cast<QDropEvent *>(event);
        if (!mimeOk(e->mimeData()))
            return false;

        Ps::ImageDocument *doc = document();
        if (!doc || doc->layers().count() <= 1) {
            e->ignore();
            return true;
        }

        bool ok = false;
        const int fromStack = e->mimeData()
                                  ->data(QString::fromLatin1(LayerRowWidget::kLayerDragMime))
                                  .toInt(&ok);
        const int n = doc->layers().count();
        if (!ok || fromStack < 0 || fromStack >= n) {
            e->ignore();
            return true;
        }

        // 视觉行 0 = 栈顶；对照 GIMP：BEFORE/AFTER → insert-before 下标
        const QPoint pos = e->position().toPoint();
        int insertBeforeVis = ui->itemList->count(); // 默认插到列表底（栈底）
        if (QListWidgetItem *hit = ui->itemList->itemAt(pos)) {
            const int row = ui->itemList->row(hit);
            const QRect r = ui->itemList->visualItemRect(hit);
            insertBeforeVis = (pos.y() < r.center().y()) ? row : (row + 1);
        }

        const int fromVis = n - 1 - fromStack;
        int toVis = insertBeforeVis;
        if (fromVis < toVis)
            --toVis; // 移除源行后目标下移（对照 GIMP src<dest 时 dest--）
        if (toVis < 0 || toVis >= n || fromVis == toVis) {
            e->acceptProposedAction();
            return true;
        }

        const int toStack = n - 1 - toVis;
        doc->moveLayer(fromStack, toStack); // structureChanged → 列表重建 + 画布刷新
        e->acceptProposedAction();
        return true;
    }
    default:
        return false;
    }
}

bool LayerTreePanel::applyAlphaToSelection(int stackIndex, Qt::KeyboardModifiers mods)
{
    Ps::ImageDocument *doc = document();
    if (!doc || stackIndex < 0 || stackIndex >= doc->layers().count())
        return false;

    const bool shift = mods.testFlag(Qt::ShiftModifier);
    const bool alt = mods.testFlag(Qt::AltModifier);

    if (!shift && !alt
        && !doc->selection().isEmpty()
        && m_alphaSelectSourceLayer == stackIndex) {
        m_settingAlphaSelect = true;
        doc->clearSelection();
        m_settingAlphaSelect = false;
        m_alphaSelectSourceLayer = -1;
        doc->setActiveLayerIndex(stackIndex);
        return true;
    }

    Ps::ChannelOp op = Ps::ChannelOp::Replace;
    if (shift && alt)
        op = Ps::ChannelOp::Intersect;
    else if (shift)
        op = Ps::ChannelOp::Add;
    else if (alt)
        op = Ps::ChannelOp::Subtract;

    m_settingAlphaSelect = true;
    doc->selectLayerAlpha(stackIndex, op);
    m_settingAlphaSelect = false;
    m_alphaSelectSourceLayer = (op == Ps::ChannelOp::Replace) ? stackIndex : -1;
    m_maskSelectSourceLayer = -1;
    doc->setActiveLayerIndex(stackIndex);
    return true;
}

bool LayerTreePanel::applyMaskToSelection(int stackIndex, Qt::KeyboardModifiers mods)
{
    Ps::ImageDocument *doc = document();
    if (!doc || stackIndex < 0 || stackIndex >= doc->layers().count())
        return false;
    Ps::Layer *layer = doc->layers().layerAt(stackIndex);
    if (!layer || !layer->hasMask())
        return false;

    const bool shift = mods.testFlag(Qt::ShiftModifier);
    const bool alt = mods.testFlag(Qt::AltModifier);

    if (!shift && !alt
        && !doc->selection().isEmpty()
        && m_maskSelectSourceLayer == stackIndex) {
        m_settingAlphaSelect = true;
        doc->clearSelection();
        m_settingAlphaSelect = false;
        m_maskSelectSourceLayer = -1;
        doc->setActiveLayerIndex(stackIndex);
        return true;
    }

    Ps::ChannelOp op = Ps::ChannelOp::Replace;
    if (shift && alt)
        op = Ps::ChannelOp::Intersect;
    else if (shift)
        op = Ps::ChannelOp::Add;
    else if (alt)
        op = Ps::ChannelOp::Subtract;

    m_settingAlphaSelect = true;
    doc->selectLayerMask(stackIndex, op);
    m_settingAlphaSelect = false;
    m_maskSelectSourceLayer = (op == Ps::ChannelOp::Replace) ? stackIndex : -1;
    m_alphaSelectSourceLayer = -1;
    doc->setActiveLayerIndex(stackIndex);
    return true;
}

void LayerTreePanel::buildLayerContextMenu()
{
    m_layerContextMenu = new QMenu(this);
    m_layerContextMenu->addAction(ui->actionCtxNewLayer);
    m_layerContextMenu->addAction(ui->actionCtxNewGroup);
    m_layerContextMenu->addAction(ui->actionCtxDuplicateLayer);
    m_layerContextMenu->addAction(ui->actionCtxDeleteLayer);
    m_layerContextMenu->addSeparator();
    m_layerContextMenu->addAction(ui->actionCtxQuickExportPng);
    m_layerContextMenu->addAction(ui->actionCtxExportAs);
    m_layerContextMenu->addSeparator();
    m_layerContextMenu->addAction(ui->actionCtxMergeDown);
    m_layerContextMenu->addAction(ui->actionCtxMergeVisible);
    m_layerContextMenu->addAction(ui->actionCtxFlattenImage);
    m_layerContextMenu->addSeparator();
    m_layerContextMenu->addAction(ui->actionCtxLockLayer);
    m_layerContextMenu->addAction(ui->actionCtxRenameLayer);
    m_layerContextMenu->addAction(ui->actionCtxToggleVisible);
    m_layerContextMenu->addSeparator();
    m_layerContextMenu->addAction(ui->actionCtxBlendingOptions);
    m_layerContextMenu->addAction(ui->actionCtxCopyLayerStyle);
    m_layerContextMenu->addAction(ui->actionCtxPasteLayerStyle);
    m_layerContextMenu->addSeparator();
    m_layerContextMenu->addAction(ui->actionCtxNewGroupFromLayers);
    m_layerContextMenu->addAction(ui->actionCtxFrameFromLayer);
    m_layerContextMenu->addSeparator();
    m_layerContextMenu->addAction(ui->actionCtxNewArtboard);
    m_layerContextMenu->addAction(ui->actionCtxArtboardFromLayers);
    m_layerContextMenu->addSeparator();
    m_layerContextMenu->addAction(ui->actionCtxConvertSmartObject);
    m_layerContextMenu->addAction(ui->actionCtxMaskAllObjects);
    m_layerContextMenu->addSeparator();
    m_layerContextMenu->addAction(ui->actionCtxClippingMask);
    m_layerContextMenu->addAction(ui->actionCtxCopyCss);
    m_layerContextMenu->addAction(ui->actionCtxCopySvg);
    m_layerContextMenu->addSeparator();
    QMenu *colorMenu = m_layerContextMenu->addMenu(tr("颜色"));
    colorMenu->addAction(ui->actionCtxColorNone);
    colorMenu->addAction(ui->actionCtxColorRed);
    colorMenu->addAction(ui->actionCtxColorOrange);
    colorMenu->addAction(ui->actionCtxColorYellow);
    colorMenu->addAction(ui->actionCtxColorGreen);
    colorMenu->addAction(ui->actionCtxColorBlue);
    colorMenu->addAction(ui->actionCtxColorViolet);
    colorMenu->addAction(ui->actionCtxColorGray);
}

void LayerTreePanel::onLayerContextMenu(const QPoint &pos)
{
    Ps::ImageDocument *doc = document();
    if (!doc || !m_layerContextMenu)
        return;

    QListWidgetItem *item = ui->itemList->itemAt(pos);
    if (item) {
        const int stackIndex = stackIndexOfItem(item);
        if (stackIndex >= 0)
            doc->setActiveLayerIndex(stackIndex);
    }

    syncLayerContextMenuState();
    m_layerContextMenu->exec(ui->itemList->viewport()->mapToGlobal(pos));
}

void LayerTreePanel::syncLayerContextMenuState()
{
    Ps::ImageDocument *doc = document();
    Ps::Layer *layer = doc ? doc->activeLayer() : nullptr;
    const bool hasLayer = layer != nullptr;
    const bool canDelete = hasLayer && doc->layers().count() > 1;
    const bool hasStyles = hasLayer && !layer->styles().isEmpty();

    ui->actionCtxDuplicateLayer->setEnabled(hasLayer);
    ui->actionCtxDeleteLayer->setEnabled(canDelete);
    ui->actionCtxRenameLayer->setEnabled(hasLayer);
    ui->actionCtxToggleVisible->setEnabled(hasLayer);
    ui->actionCtxCopyLayerStyle->setEnabled(hasStyles);
    ui->actionCtxPasteLayerStyle->setEnabled(
        hasLayer && !Ps::LayerStyleClipboard::isEmpty());
    if (layer) {
        ui->actionCtxToggleVisible->setText(
            layer->isVisible() ? tr("隐藏图层") : tr("显示图层"));
    }
}

void LayerTreePanel::onCopyLayerStyle()
{
    Ps::Layer *layer = document() ? document()->activeLayer() : nullptr;
    if (!layer || layer->styles().isEmpty())
        return;
    Ps::LayerStyleClipboard::set(layer->styles().snapshot());
}

void LayerTreePanel::onPasteLayerStyle()
{
    Ps::ImageDocument *doc = document();
    if (!doc || Ps::LayerStyleClipboard::isEmpty())
        return;
    doc->replaceActiveLayerStyles(Ps::LayerStyleClipboard::snapshot());
}

// —— 行 widget ——

void LayerTreePanel::appendRowForLayer(int stackIndex, Ps::Layer &layer)
{
    auto *item = new QListWidgetItem(ui->itemList);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDropEnabled);
    item->setData(kRoleStackIndex, stackIndex);

    auto *row = new LayerRowWidget(ui->itemList);
    row->setStackIndex(stackIndex);

    // 先接线再 sync，这样 heightChanged 能落到 sizeHint
    connect(row, &LayerRowWidget::rowPressed, this, [this, item]() {
        ui->itemList->setCurrentItem(item);
    });
    connect(row, &LayerRowWidget::visibilityToggled, this, [this, stackIndex](bool on) {
        if (Ps::ImageDocument *doc = document())
            doc->setLayerVisible(stackIndex, on);
    });
    connect(row, &LayerRowWidget::nameCommitted, this, [this, stackIndex](const QString &name) {
        if (Ps::ImageDocument *doc = document())
            doc->setLayerName(stackIndex, name);
    });
    connect(row, &LayerRowWidget::expandChanged, this, [this, stackIndex](bool on) {
        m_stylesExpanded.insert(stackIndex, on);
    });
    connect(row, &LayerRowWidget::styleVisibilityToggled, this,
            [this, stackIndex](int styleIndex, bool on) {
                if (Ps::ImageDocument *doc = document())
                    doc->setLayerStyleEnabled(stackIndex, styleIndex, on);
            });
    connect(row, &LayerRowWidget::heightChanged, this, [this, item, row]() {
        syncItemSize(item, row);
    });
    connect(row, &LayerRowWidget::thumbnailCtrlClicked, this,
            [this, stackIndex](Qt::KeyboardModifiers mods) {
                applyAlphaToSelection(stackIndex, mods);
            });
    connect(row, &LayerRowWidget::layerThumbClicked, this, [this, stackIndex]() {
        Ps::ImageDocument *doc = document();
        if (!doc)
            return;
        doc->setActiveLayerIndex(stackIndex);
        doc->setEditingLayerMask(false);
    });
    connect(row, &LayerRowWidget::maskCtrlClicked, this,
            [this, stackIndex](Qt::KeyboardModifiers mods) {
                applyMaskToSelection(stackIndex, mods);
            });
    connect(row, &LayerRowWidget::maskAltClicked, this, [this, stackIndex]() {
        Ps::ImageDocument *doc = document();
        if (!doc)
            return;
        Ps::Layer *layer = doc->layers().layerAt(stackIndex);
        if (!layer || !layer->mask() || layer->mask()->isNull())
            return;
        doc->setLayerMaskEnabled(stackIndex, !layer->mask()->isEnabled());
    });
    connect(row, &LayerRowWidget::maskThumbClicked, this, [this, stackIndex]() {
        Ps::ImageDocument *doc = document();
        if (!doc)
            return;
        doc->setActiveLayerIndex(stackIndex);
        doc->setEditingLayerMask(true);
    });

    row->syncFromLayer(layer);
    applyLayerThumbnail(row, layer);
    if (!layer.styles().isEmpty())
        row->setExpanded(m_stylesExpanded.value(stackIndex, true));

    ui->itemList->setItemWidget(item, row);
    syncItemSize(item, row);
}

void LayerTreePanel::syncItemSize(QListWidgetItem *item, LayerRowWidget *row)
{
    if (!item || !row)
        return;
    item->setSizeHint(row->sizeHint());
}

void LayerTreePanel::applyLayerThumbnail(LayerRowWidget *row, const Ps::Layer &layer)
{
    if (!row)
        return;
    const QImage thumbSrc = layer.hasPixelData()
                                ? layer.materialize()
                                : QImage(64, 64, QImage::Format_ARGB32_Premultiplied);
    QImage forThumb = thumbSrc;
    if (!layer.hasPixelData())
        forThumb.fill(Qt::transparent);
    row->setThumbnail(makeLayerThumbnail(forThumb));

    if (layer.hasMask() && layer.mask() && !layer.mask()->isNull()) {
        QImage gray = layer.mask()->image();
        if (gray.format() != QImage::Format_ARGB32_Premultiplied)
            gray = gray.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        QImage maskThumb = makeLayerThumbnail(gray);
        if (!layer.mask()->isEnabled() && !maskThumb.isNull()) {
            QPainter painter(&maskThumb);
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setPen(QPen(QColor(220, 70, 70), 2));
            painter.drawLine(3, 3, maskThumb.width() - 4, maskThumb.height() - 4);
        }
        row->setMaskThumbnail(maskThumb);
    } else {
        row->setMaskThumbnail(QImage());
    }

    Ps::ImageDocument *doc = document();
    const bool editingMask = doc && doc->activeLayerIndex() >= 0
                             && doc->layers().layerAt(doc->activeLayerIndex()) == &layer
                             && doc->isEditingLayerMask();
    row->setEditTarget(editingMask ? 1 : 0);
}

LayerRowWidget *LayerTreePanel::rowWidgetForStackIndex(int stackIndex) const
{
    QListWidgetItem *item = itemForStackIndex(stackIndex);
    if (!item)
        return nullptr;
    return qobject_cast<LayerRowWidget *>(ui->itemList->itemWidget(item));
}

QListWidgetItem *LayerTreePanel::itemForStackIndex(int stackIndex) const
{
    if (!ui || stackIndex < 0)
        return nullptr;
    for (int row = 0; row < ui->itemList->count(); ++row) {
        QListWidgetItem *item = ui->itemList->item(row);
        if (item && stackIndexOfItem(item) == stackIndex)
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
    if (QListWidgetItem *layerItem = itemForStackIndex(doc->activeLayerIndex())) {
        if (ui->itemList->currentItem() != layerItem)
            ui->itemList->setCurrentItem(layerItem);
    }

    Ps::Layer *layer = doc->activeLayer();
    const QSignalBlocker sliderBlocker(ui->opacitySlider);
    const QSignalBlocker blendBlocker(ui->blendModeCombo);
    if (layer) {
        const int percent = qRound(layer->opacity() * 100.0);
        ui->opacitySlider->setValue(percent);
        ui->opacityValueLabel->setText(QStringLiteral("%1%").arg(percent));
        if (!m_blendPreviewActive)
            ui->blendModeCombo->setCurrentIndex(comboIndexForBlendMode(layer->blendMode()));
    }
}

void LayerTreePanel::setOptionsEnabled(bool enabled)
{
    ui->opacitySlider->setEnabled(enabled);
    ui->fillSlider->setEnabled(enabled);
    ui->blendModeCombo->setEnabled(enabled);
}

void LayerTreePanel::refreshRowThumbnail(int stackIndex, const Ps::Layer *layer)
{
    Ps::ImageDocument *doc = document();
    if (!doc)
        return;
    if (!layer)
        layer = doc->layers().layerAt(stackIndex);
    LayerRowWidget *row = rowWidgetForStackIndex(stackIndex);
    if (!layer || !row)
        return;
    applyLayerThumbnail(row, *layer);
}

void LayerTreePanel::scheduleThumbnailRefresh()
{
    // 对照 GIMP preview_freeze：拖层中不重算缩略图
    Ps::ImageDocument *doc = document();
    if (doc && doc->isPreviewFrozen())
        return;
    if (m_thumbTimer)
        m_thumbTimer->start();
}

void LayerTreePanel::onThumbnailTimer()
{
    Ps::ImageDocument *doc = document();
    if (!doc)
        return;
    const int active = doc->activeLayerIndex();
    if (active >= 0)
        refreshRowThumbnail(active);
}
