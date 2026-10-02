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
    ui->nameLabel->installEventFilter(this);

    connect(ui->btnVisible, &QToolButton::toggled, this, [this](bool on) {
        updateEyeIcon(on);
        emit visibilityToggled(on);
    });
    connect(ui->btnExpand, &QToolButton::toggled, this, [this](bool on) {
        setExpanded(on);
    });
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
    ui->nameLabel->setText(layer.name());

    const bool hasStyles = !layer.styles().isEmpty();
    const int styleCount = layer.styles().count();
    const int existingCount = qMax(0, ui->stylesLayout->count() - 1);

    if (hasStyles != m_hasStyles || styleCount != existingCount) {
        m_hasStyles = hasStyles;
        updateExpandChrome(m_hasStyles);
        rebuildStyleRows(layer);
        ui->stylesHost->setVisible(m_hasStyles && m_expanded);
        updateGeometry();
        emit heightChanged();
        return;
    }

    if (hasStyles)
        updateStyleRowsInPlace(layer);
}

void LayerRowWidget::setThumbnail(const QImage &thumb)
{
    ui->thumbLabel->setPixmap(QPixmap::fromImage(thumb));
}

void LayerRowWidget::setExpanded(bool on)
{
    if (m_expanded == on && ui->stylesHost->isVisible() == (on && m_hasStyles)) {
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
    updateGeometry();
    emit expandChanged(m_expanded);
    emit heightChanged();
}

void LayerRowWidget::beginRename()
{
    if (m_renameEdit)
        return;

    m_renameEdit = new QLineEdit(ui->nameLabel->text(), ui->headerHost);
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
    if (!text.isEmpty() && text != ui->nameLabel->text()) {
        ui->nameLabel->setText(text);
        emit nameCommitted(text);
    }
}

QSize LayerRowWidget::sizeHint() const
{
    int h = ui->headerHost->minimumHeight();
    if (m_hasStyles && m_expanded && ui->stylesHost->isVisible()) {
        h += ui->effectsHeaderLabel->sizeHint().height();
        const int styleCount = ui->stylesLayout->count() - 1;
        h += qMax(0, styleCount) * 22 + 2;
    }
    return QSize(280, h);
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
        && (watched == ui->thumbLabel || watched == ui->nameLabel)) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() != Qt::LeftButton)
            return false;
        emit rowPressed();
        if (watched == ui->thumbLabel
            && mouse->modifiers().testFlag(Qt::ControlModifier)) {
            emit thumbnailCtrlClicked(mouse->modifiers());
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
