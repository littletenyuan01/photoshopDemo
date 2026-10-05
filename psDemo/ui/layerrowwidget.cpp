/**
 * layerrowwidget.cpp — 单个图层条目实现。
 */
#include "layerrowwidget.h"
#include "ui_layerrowwidget.h"

#include "domain/layer.h"
#include "ui/layerstylerowwidget.h"

#include <QEvent>
#include <QIcon>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPixmap>
#include <QSignalBlocker>
#include <QString>

LayerRowWidget::LayerRowWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::LayerRowWidget)
{
    ui->setupUi(this);
    ui->fxLabel->setVisible(false);
    ui->btnExpand->setVisible(false);
    ui->stylesHost->setVisible(false);
    ui->nameLabel->setTextInteractionFlags(Qt::NoTextInteraction);
    ui->thumbLabel->installEventFilter(this);
    ui->maskThumbLabel->installEventFilter(this);
    ui->nameLabel->installEventFilter(this);

    connect(ui->btnVisible, &QToolButton::toggled, this, [this](bool on) {
        updateEyeIcon(on);
        emit visibilityToggled(on);
    });
    connect(ui->btnExpand, &QToolButton::toggled, this, [this](bool on) {
        setExpanded(on);
    });
    applyHeight();
}

LayerRowWidget::~LayerRowWidget()
{
    delete ui;
}

void LayerRowWidget::syncFromLayer(const Ps::Layer &layer)
{
    {
        const QSignalBlocker blocker(ui->btnVisible);
        ui->btnVisible->setChecked(layer.isVisible());
        updateEyeIcon(layer.isVisible());
    }
    m_layerName = layer.name();
    if (layer.isLinkedLayer()) {
        const QString mark = layer.isLinkBroken()
                                 ? QObject::tr("（破链）")
                                 : QObject::tr("（链接）");
        ui->nameLabel->setText(m_layerName + mark);
        ui->nameLabel->setToolTip(layer.linkPath());
    } else {
        ui->nameLabel->setText(m_layerName);
        ui->nameLabel->setToolTip(QString());
    }

    const bool hasStyles = !layer.styles().isEmpty();
    const int styleCount = layer.styles().count();
    const int existingCount = qMax(0, ui->stylesLayout->count() - 1);

    if (hasStyles != m_hasStyles || styleCount != existingCount) {
        m_hasStyles = hasStyles;
        updateExpandChrome(m_hasStyles);
        rebuildStyleRows(layer);
        ui->stylesHost->setVisible(m_hasStyles && m_expanded);
        applyHeight();
        return;
    }

    if (hasStyles)
        updateStyleRowsInPlace(layer);
}

void LayerRowWidget::setThumbnail(const QImage &thumb)
{
    ui->thumbLabel->setPixmap(QPixmap::fromImage(thumb));
}

void LayerRowWidget::setMaskThumbnail(const QImage &thumb)
{
    if (thumb.isNull()) {
        ui->maskThumbLabel->clear();
        ui->maskThumbLabel->setVisible(false);
        return;
    }
    ui->maskThumbLabel->setPixmap(QPixmap::fromImage(thumb));
    ui->maskThumbLabel->setVisible(true);
}

void LayerRowWidget::setEditTarget(int target)
{
    const char *active =
        "QLabel { border: 2px solid #5a9fd4; background: #2a2a2a; }";
    const char *idle =
        "QLabel { border: 1px solid #666; background: transparent; }";
    ui->thumbLabel->setStyleSheet(target == 0 ? active : idle);
    if (ui->maskThumbLabel->isVisible())
        ui->maskThumbLabel->setStyleSheet(target == 1 ? active : idle);
    else
        ui->maskThumbLabel->setStyleSheet(idle);
}

void LayerRowWidget::setExpanded(bool on)
{
    if (m_expanded == on && ui->stylesHost->isHidden() == !(on && m_hasStyles)) {
        updateExpandChrome(m_hasStyles);
        return;
    }
    m_expanded = on;
    {
        const QSignalBlocker blocker(ui->btnExpand);
        ui->btnExpand->setChecked(on);
    }
    updateExpandChrome(m_hasStyles);
    ui->stylesHost->setVisible(m_hasStyles && m_expanded);
    emit expandChanged(m_expanded);
    applyHeight();
}

void LayerRowWidget::beginRename()
{
    if (m_renameEdit)
        return;

    m_renameEdit = new QLineEdit(m_layerName.isEmpty() ? ui->nameLabel->text() : m_layerName,
                                 ui->headerHost);
    m_renameEdit->setGeometry(ui->nameLabel->geometry());
    m_renameEdit->show();
    m_renameEdit->setFocus(Qt::OtherFocusReason);
    m_renameEdit->selectAll();
    ui->nameLabel->setVisible(false);

    connect(m_renameEdit, &QLineEdit::editingFinished, this, &LayerRowWidget::finishRename);
}

void LayerRowWidget::finishRename()
{
    if (!m_renameEdit)
        return;
    const QString text = m_renameEdit->text().trimmed();
    m_renameEdit->deleteLater();
    m_renameEdit = nullptr;
    ui->nameLabel->setVisible(true);
    if (!text.isEmpty() && text != m_layerName) {
        m_layerName = text;
        ui->nameLabel->setText(text);
        emit nameCommitted(text);
    }
}

int LayerRowWidget::stylesBlockHeight() const
{
    if (!m_hasStyles || !m_expanded)
        return 0;
    // 组头 20 + 每效果行 22 + 底边距 2（与 .ui 一致）
    const int styleCount = qMax(0, ui->stylesLayout->count() - 1);
    return 20 + styleCount * 22 + 2;
}

void LayerRowWidget::applyHeight()
{
    const int h = ui->headerHost->minimumHeight() + stylesBlockHeight();
    setFixedHeight(h);
    updateGeometry();
    emit heightChanged();
}

QSize LayerRowWidget::sizeHint() const
{
    // 勿用 stylesHost->isVisible()：挂到 QListWidget 前祖先未显示时恒为 false，
    // 会把展开行高算成仅 header → 效果文字全部挤叠。
    return QSize(280, ui->headerHost->minimumHeight() + stylesBlockHeight());
}

QSize LayerRowWidget::minimumSizeHint() const
{
    return sizeHint();
}

void LayerRowWidget::mousePressEvent(QMouseEvent *event)
{
    emit rowPressed();
    QWidget::mousePressEvent(event);
}

bool LayerRowWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress
        && (watched == ui->thumbLabel || watched == ui->maskThumbLabel
            || watched == ui->nameLabel)) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() != Qt::LeftButton)
            return false;
        emit rowPressed();
        if (watched == ui->thumbLabel) {
            if (mouse->modifiers().testFlag(Qt::ControlModifier))
                emit thumbnailCtrlClicked(mouse->modifiers());
            else
                emit layerThumbClicked();
        }
        if (watched == ui->maskThumbLabel) {
            if (mouse->modifiers().testFlag(Qt::ControlModifier))
                emit maskCtrlClicked(mouse->modifiers());
            else if (mouse->modifiers().testFlag(Qt::AltModifier))
                emit maskAltClicked();
            else
                emit maskThumbClicked();
        }
        return true; // 勿再冒泡到 mousePressEvent，避免 rowPressed 双发
    }
    if (watched == ui->nameLabel && event->type() == QEvent::MouseButtonDblClick) {
        beginRename();
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void LayerRowWidget::clearStyleRows()
{
    while (ui->stylesLayout->count() > 1) {
        QLayoutItem *item = ui->stylesLayout->takeAt(1);
        if (!item)
            break;
        if (QWidget *w = item->widget())
            delete w;
        delete item;
    }
}

void LayerRowWidget::rebuildStyleRows(const Ps::Layer &layer)
{
    clearStyleRows();
    for (int i = 0; i < layer.styles().count(); ++i) {
        const Ps::LayerStyleEffect &fx = layer.styles().at(i);
        auto *row = new LayerStyleRowWidget(ui->stylesHost);
        row->setStyleIndex(i);
        row->setTitle(fx.title());
        row->setEffectVisible(fx.isEnabled());
        connect(row, &LayerStyleRowWidget::visibilityToggled,
                this, &LayerRowWidget::styleVisibilityToggled);
        ui->stylesLayout->addWidget(row);
    }
}

void LayerRowWidget::updateStyleRowsInPlace(const Ps::Layer &layer)
{
    for (int i = 0; i < layer.styles().count(); ++i) {
        QLayoutItem *item = ui->stylesLayout->itemAt(i + 1);
        auto *row = item ? qobject_cast<LayerStyleRowWidget *>(item->widget()) : nullptr;
        if (!row)
            continue;
        const Ps::LayerStyleEffect &fx = layer.styles().at(i);
        row->setStyleIndex(i);
        row->setTitle(fx.title());
        row->setEffectVisible(fx.isEnabled());
    }
}

void LayerRowWidget::updateExpandChrome(bool hasStyles)
{
    ui->fxLabel->setVisible(hasStyles);
    ui->btnExpand->setVisible(hasStyles);
    if (!hasStyles)
        return;
    ui->btnExpand->setIcon(QIcon(m_expanded
                                     ? QStringLiteral(":/icons/layers/chevron-up.svg")
                                     : QStringLiteral(":/icons/layers/chevron-down.svg")));
    ui->btnExpand->setToolTip(m_expanded ? tr("折叠图层样式") : tr("展开图层样式"));
}

void LayerRowWidget::updateEyeIcon(bool visible)
{
    ui->btnVisible->setIcon(QIcon(visible
                                      ? QStringLiteral(":/icons/layers/eye.svg")
                                      : QStringLiteral(":/icons/layers/eye-off.svg")));
}
