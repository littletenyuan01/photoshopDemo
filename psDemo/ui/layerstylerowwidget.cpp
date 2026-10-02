/**
 * layerstylerowwidget.cpp — 图层样式子行实现。
 */
#include "layerstylerowwidget.h"
#include "ui_layerstylerowwidget.h"

#include <QIcon>
#include <QSignalBlocker>

LayerStyleRowWidget::LayerStyleRowWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::LayerStyleRowWidget)
{
    ui->setupUi(this);
    connect(ui->btnVisible, &QToolButton::toggled, this, [this](bool on) {
        updateEyeIcon(on);
        emit visibilityToggled(m_styleIndex, on);
    });
}

LayerStyleRowWidget::~LayerStyleRowWidget()
{
    delete ui;
}

void LayerStyleRowWidget::setTitle(const QString &title)
{
    ui->nameLabel->setText(title);
}

void LayerStyleRowWidget::setEffectVisible(bool visible)
{
    const QSignalBlocker blocker(ui->btnVisible);
    ui->btnVisible->setChecked(visible);
    updateEyeIcon(visible);
}

void LayerStyleRowWidget::setStyleIndex(int index)
{
    m_styleIndex = index;
}

void LayerStyleRowWidget::updateEyeIcon(bool visible)
{
    ui->btnVisible->setIcon(QIcon(visible
                                      ? QStringLiteral(":/icons/layers/eye.svg")
                                      : QStringLiteral(":/icons/layers/eye-off.svg")));
}
