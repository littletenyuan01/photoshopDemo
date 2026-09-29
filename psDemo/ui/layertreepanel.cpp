#include "layertreepanel.h"
#include "ui_layertreepanel.h"

#include "domain/blendmode.h"
#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "domain/selection.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDebug>
#include <QEvent>
#include <QListWidgetItem>
#include <QMenu>
#include <QMouseEvent>
#include <QSignalBlocker>
#include <QTimer>

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
    // 图标 / iconSize 见 layertreepanel.ui

    // GIMP：new_action / delete_action → "layers-new" / "layers-delete"
    // 接到 private slots（进 moc），避免只靠虚函数/lambda 时个别构建下点了没反应
    connect(ui->btnNew, &QToolButton::clicked, this, &LayerTreePanel::onBtnNewClicked);
    connect(ui->btnDelete, &QToolButton::clicked, this, &LayerTreePanel::onBtnDeleteClicked);
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
    // 先插分隔线并绑 itemData，再 connect —— 避免 setup 过程中误提交
    setupBlendModeCombo();
    connect(ui->blendModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &LayerTreePanel::onBlendModeChanged);
    // 弹出列表里光标滑过某一项 → 画布即时预览（未点选则关闭时还原）
    connect(ui->blendModeCombo, QOverload<int>::of(&QComboBox::highlighted),
            this, &LayerTreePanel::onBlendModeHighlighted);
    if (QAbstractItemView *view = ui->blendModeCombo->view()) {
        view->installEventFilter(this);
        if (view->window())
            view->window()->installEventFilter(this);
    }

    connect(ui->fillSlider, &QSlider::valueChanged, this, [this](int value) {
        ui->fillValueLabel->setText(QStringLiteral("%1%").arg(value));
        // 填充尚未进 domain，仅同步 UI 显示
    });

    // Ctrl+点缩略图：alpha → 选区（对齐 PS；GIMP 同类操作为 Alt+点预览）
    ui->itemList->viewport()->installEventFilter(this);

    // Action 文案/灰显在 .ui；QMenu 壳在此组装（uic 无法在带 layout 窗体里嵌 QMenu）
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
        if (QListWidgetItem *it = ui->itemList->currentItem())
            ui->itemList->editItem(it);
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
    //
    // 结构/活动层等用 lambda 转调：比「虚函数成员指针 + UniqueConnection」更稳，
    // 避免个别构建下 connect 失败后「新建图层点了列表不刷新」。
    // setDocument 会先 disconnect(旧文档→this)，不会叠连。
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
        // 像素改动 → 只安排防抖刷新缩略图，绝不重建列表。
        // 只订阅 pixelsChanged：contentChanged 是汇总信号，结构/属性变化也会发它，
        // 而那时上面几个槽刚刷过 → 缩略图会被白算一遍。
        connect(doc, &Ps::ImageDocument::pixelsChanged, this, [this](const QRect &) {
            scheduleThumbnailRefresh();
        });
        // 选区被别处改掉（矩形选框等）时，打断「同层再点取消」配对
        connect(doc, &Ps::ImageDocument::selectionChanged, this, [this]() {
            if (!m_settingAlphaSelect)
                m_alphaSelectSourceLayer = -1;
        });
    }
    m_alphaSelectSourceLayer = -1;
    refreshFromDocument();
}

void LayerTreePanel::refreshFromDocument()
{
    // 【功能】按 LayerStack 全量重建列表：第 0 行 = 栈顶（最新层）
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
    // 新建层在顶部，滚到顶以免用户以为「没加上」
    if (ui->itemList->count() > 0)
        ui->itemList->scrollToItem(ui->itemList->item(0));
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

void LayerTreePanel::setupBlendModeCombo()
{
    // 文案 / maxVisibleItems / 白底弹出样式都在 layertreepanel.ui（及 ui_layertreepanel.h）。
    // 这里只绑 itemData（枚举序）并插分组分隔线 —— 不要用 combo 下标当模式。
    QComboBox *combo = ui->blendModeCombo;
    const QSignalBlocker blocker(combo);
    Q_ASSERT(combo->count() == Ps::kBlendModeCount);
    for (int i = 0; i < Ps::kBlendModeCount; ++i)
        combo->setItemData(i, i);

    // 从后往前插，前面下标不漂移：溶解后 / 深色后 / 浅色后 / 实色混合后 / 划分后
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

    // 以 combo 当前项为准：点选后已是新模式；Esc 未改下标则回到打开前的模式
    Ps::BlendMode mode = original;
    blendModeAtComboIndex(ui->blendModeCombo->currentIndex(), &mode);

    Ps::ImageDocument *doc = document();
    if (!doc || layerIndex < 0)
        return;
    const Ps::Layer *layer = doc->layers().layerAt(layerIndex);
    if (!layer)
        return;
    if (layer->blendMode() == mode) {
        // 预览态已写到 mode：若相对打开前有变，补一条撤销（先回写 original 再正式提交）
        if (mode != original) {
            doc->setLayerBlendMode(layerIndex, original, false);
            doc->setLayerBlendMode(layerIndex, mode, true);
        }
        return;
    }
    if (mode == original)
        doc->setLayerBlendMode(layerIndex, mode, false); // 还原预览，不记历史
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
    // 悬停预览不记历史；点选提交走 onBlendModeChanged / endBlendModePreview
    doc->setLayerBlendMode(layerIndex, mode, !m_blendPreviewActive);
}

void LayerTreePanel::onBlendModeHighlighted(int index)
{
    // 弹出列表里高亮（鼠标滑过 / 键盘上下）→ 立即改合成预览
    Ps::BlendMode mode = Ps::BlendMode::Normal;
    if (!blendModeAtComboIndex(index, &mode))
        return; // 分隔线：保持上一预览
    beginBlendModePreview();
    // 弹出容器可能晚于 setup 才建成，再挂一次 Hide 监听
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
        // 点选提交：复用 end 逻辑补撤销条目
        // 先把 combo 已是 mode，end 会按 currentIndex 提交
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

void LayerTreePanel::onNewItem()
{
    // 【功能】图层面板底栏「新建图层」：栈顶加透明层；列表第 0 行应为最新层
    Ps::ImageDocument *doc = document();
    if (!doc)
        return;

    const int index = doc->addTransparentLayer();
    if (index < 0)
        return;

    // 不依赖信号：属性面板能显示「图层 N」而列表仍只有「背景」时，
    // 根因就是 structureChanged→refresh 断了；这里强制重建列表。
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
    // 混合模式弹出列表关闭 → 结束悬停预览（Esc / 点空白 / 点选后）
    if (m_blendPreviewActive && event->type() == QEvent::Hide) {
        QAbstractItemView *view = ui->blendModeCombo->view();
        if (watched == view || (view && watched == view->window()))
            endBlendModePreview();
    }

    if (watched == ui->itemList->viewport()
        && event->type() == QEvent::MouseButtonPress) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (tryAlphaToSelectionClick(mouse))
            return true;
    }
    return ItemTreePanel::eventFilter(watched, event);
}

bool LayerTreePanel::tryAlphaToSelectionClick(QMouseEvent *mouse)
{
    // 【功能】Ctrl+点图层缩略图 → 该层非透明像素载入选区；再点同层 → 取消
    // 【对照】GIMP gimp_item_tree_view_item_pre_clicked（Alt+）+ gimp_channel_select_alpha
    //         PS：Ctrl+点图层缩略图
    if (!mouse || mouse->button() != Qt::LeftButton)
        return false;
    if (!mouse->modifiers().testFlag(Qt::ControlModifier))
        return false;

    Ps::ImageDocument *doc = document();
    if (!doc)
        return false;

    QListWidgetItem *item = ui->itemList->itemAt(mouse->pos());
    if (!item)
        return false;

    // 热区放宽：勾选框 + 缩略图整块左侧（style 的 decoration 矩形经常偏/空，导致「点了没反应」）
    const QRect rowRect = ui->itemList->visualItemRect(item);
    const int iconW = ui->itemList->iconSize().width();
    const int hotW = qMax(iconW + 36, 64);
    const QRect hotRect(rowRect.left(), rowRect.top(), hotW, rowRect.height());
    if (!hotRect.contains(mouse->pos()))
        return false;

    const int stackIndex = item->data(Qt::UserRole).toInt();
    if (stackIndex < 0 || stackIndex >= doc->layers().count())
        return false;

    const bool shift = mouse->modifiers().testFlag(Qt::ShiftModifier);
    const bool alt = mouse->modifiers().testFlag(Qt::AltModifier);

    // 纯 Ctrl（无 Shift/Alt）：同一来源层再点 → 取消选区
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
    // 仅 Replace 建立「再点取消」配对；加/减/交不配对
    m_alphaSelectSourceLayer = (op == Ps::ChannelOp::Replace) ? stackIndex : -1;
    doc->setActiveLayerIndex(stackIndex);
    return true;
}

void LayerTreePanel::buildLayerContextMenu()
{
    // 【对照】PS 图层面板右键；条目 Action 在 .ui，此处只排版结构
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
    // 【对照】PS 图层面板右键；GIMP layers-actions + item tree view popup
    Ps::ImageDocument *doc = document();
    if (!doc || !m_layerContextMenu)
        return;

    QListWidgetItem *item = ui->itemList->itemAt(pos);
    if (item) {
        const int stackIndex = item->data(Qt::UserRole).toInt();
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

    ui->actionCtxDuplicateLayer->setEnabled(hasLayer);
    ui->actionCtxDeleteLayer->setEnabled(canDelete);
    ui->actionCtxRenameLayer->setEnabled(hasLayer);
    ui->actionCtxToggleVisible->setEnabled(hasLayer);
    if (layer) {
        ui->actionCtxToggleVisible->setText(
            layer->isVisible() ? tr("隐藏图层") : tr("显示图层"));
    }
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
    // 缩略图：有瓦片则物化；空透明层用小占位避免 materialize 整幅大图
    const QImage thumbSrc = layer.hasPixelData()
                                ? layer.materialize()
                                : QImage(64, 64, QImage::Format_ARGB32_Premultiplied);
    QImage forThumb = thumbSrc;
    if (!layer.hasPixelData())
        forThumb.fill(Qt::transparent);
    item->setIcon(QIcon(QPixmap::fromImage(makeLayerThumbnail(forThumb))));
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
    const QSignalBlocker blendBlocker(ui->blendModeCombo);
    if (layer) {
        const int percent = qRound(layer->opacity() * 100.0);
        ui->opacitySlider->setValue(percent);
        ui->opacityValueLabel->setText(QStringLiteral("%1%").arg(percent));
        // 弹出预览中勿回写 combo，否则高亮项会被拽回
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
    const QImage thumbSrc = layer->hasPixelData()
                                ? layer->materialize()
                                : QImage(64, 64, QImage::Format_ARGB32_Premultiplied);
    QImage forThumb = thumbSrc;
    if (!layer->hasPixelData())
        forThumb.fill(Qt::transparent);
    item->setIcon(QIcon(QPixmap::fromImage(makeLayerThumbnail(forThumb))));
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
