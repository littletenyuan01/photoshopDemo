/**
 * layerstyledialog.cpp — 图层样式对话框：只接线 + 同步草稿数据。
 * 界面树见 layerstyledialog.ui（效果列表 + 参数表单）。
 */
#include "layerstyledialog.h"
#include "ui_layerstyledialog.h"

#include "domain/layer.h"

#include <QColorDialog>
#include <QListWidgetItem>
#include <QSignalBlocker>

namespace {

/** 与 effectList 行序一一对应（改 .ui 项顺序时须同步）。 */
constexpr Ps::LayerStyleKind kRowKinds[] = {
    Ps::LayerStyleKind::DropShadow,
    Ps::LayerStyleKind::InnerShadow,
    Ps::LayerStyleKind::OuterGlow,
    Ps::LayerStyleKind::InnerGlow,
    Ps::LayerStyleKind::Stroke,
    Ps::LayerStyleKind::ColorOverlay,
};

constexpr int kRowCount = int(sizeof(kRowKinds) / sizeof(kRowKinds[0]));

} // namespace

LayerStyleDialog::LayerStyleDialog(const Ps::Layer *layer, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::LayerStyleDialog)
{
    ui->setupUi(this);

    // Qt6 uic 对 list item 的 checkState 枚举会生成 Qt::Qt::Unchecked；故默认勾选态在代码里初始化。
    {
        QSignalBlocker block(ui->effectList);
        for (int row = 0; row < ui->effectList->count(); ++row) {
            if (QListWidgetItem *item = ui->effectList->item(row))
                item->setCheckState(Qt::Unchecked);
        }
    }

    for (Ps::LayerStyleKind kind : kRowKinds)
        m_draft.insert(Ps::LayerStyleEffect::kindId(kind),
                       Ps::LayerStyleEffect::makeDefault(kind));

    if (layer) {
        QSignalBlocker block(ui->effectList);
        for (const Ps::LayerStyleEffect &e : layer->styles().snapshot()) {
            m_draft.insert(Ps::LayerStyleEffect::kindId(e.kind()), e);
            for (int row = 0; row < kRowCount; ++row) {
                if (kRowKinds[row] != e.kind())
                    continue;
                if (QListWidgetItem *item = ui->effectList->item(row))
                    item->setCheckState(e.isEnabled() ? Qt::Checked : Qt::Unchecked);
            }
        }
    }

    connect(ui->effectList, &QListWidget::currentRowChanged,
            this, &LayerStyleDialog::onEffectRowChanged);
    connect(ui->effectList, &QListWidget::itemChanged,
            this, &LayerStyleDialog::onEffectItemChanged);
    connect(ui->btnColor, &QPushButton::clicked,
            this, &LayerStyleDialog::onPickColor);

    auto bindSlider = [this](QSlider *slider, QLabel *valueLabel) {
        connect(slider, &QSlider::valueChanged, this, [this, valueLabel](int v) {
            valueLabel->setText(QString::number(v));
            onParamEdited();
        });
    };
    bindSlider(ui->sliderSize, ui->valueSize);
    bindSlider(ui->sliderDistance, ui->valueDistance);
    bindSlider(ui->sliderAngle, ui->valueAngle);
    bindSlider(ui->sliderSpread, ui->valueSpread);
    bindSlider(ui->sliderOpacity, ui->valueOpacity);

    int initialRow = 0;
    for (int row = 0; row < kRowCount; ++row) {
        if (ui->effectList->item(row)
            && ui->effectList->item(row)->checkState() == Qt::Checked) {
            initialRow = row;
            break;
        }
    }
    ui->effectList->setCurrentRow(initialRow);
    selectKind(kindAtRow(initialRow));
}

LayerStyleDialog::~LayerStyleDialog()
{
    delete ui;
}

Ps::LayerStyleKind LayerStyleDialog::kindAtRow(int row) const
{
    if (row < 0 || row >= kRowCount)
        return Ps::LayerStyleKind::DropShadow;
    return kRowKinds[row];
}

Ps::LayerStyleEffect &LayerStyleDialog::draft(Ps::LayerStyleKind kind)
{
    const int id = Ps::LayerStyleEffect::kindId(kind);
    if (!m_draft.contains(id))
        m_draft.insert(id, Ps::LayerStyleEffect::makeDefault(kind));
    return m_draft[id];
}

void LayerStyleDialog::onEffectRowChanged(int row)
{
    if (row < 0)
        return;
    selectKind(kindAtRow(row));
}

void LayerStyleDialog::onEffectItemChanged(QListWidgetItem *item)
{
    if (!item || m_block)
        return;
    const int row = ui->effectList->row(item);
    draft(kindAtRow(row)).setEnabled(item->checkState() == Qt::Checked);
    if (kindAtRow(row) == m_current)
        updateParamEnabled();
}

void LayerStyleDialog::onPickColor()
{
    const QColor c = QColorDialog::getColor(draft(m_current).color(), this,
                                            tr("样式颜色"));
    if (!c.isValid())
        return;
    draft(m_current).setColor(c);
    refreshColorButton();
}

void LayerStyleDialog::onParamEdited()
{
    if (m_block)
        return;
    saveUiToCurrent();
}

void LayerStyleDialog::selectKind(Ps::LayerStyleKind kind)
{
    saveUiToCurrent();
    m_current = kind;
    loadCurrentToUi();
    updateParamEnabled();
}

void LayerStyleDialog::saveUiToCurrent()
{
    if (m_block)
        return;
    Ps::LayerStyleEffect &e = draft(m_current);
    e.setSize(ui->sliderSize->value());
    e.setDistance(ui->sliderDistance->value());
    e.setAngle(ui->sliderAngle->value());
    e.setSpread(ui->sliderSpread->value());
    e.setOpacity(ui->sliderOpacity->value() / 100.0);
    for (int row = 0; row < kRowCount; ++row) {
        if (kRowKinds[row] != m_current)
            continue;
        if (QListWidgetItem *item = ui->effectList->item(row))
            e.setEnabled(item->checkState() == Qt::Checked);
    }
}

void LayerStyleDialog::loadCurrentToUi()
{
    m_block = true;
    const Ps::LayerStyleEffect &e = draft(m_current);
    ui->currentLabel->setText(tr("当前编辑：%1").arg(e.title()));
    ui->sliderSize->setValue(int(e.size()));
    ui->sliderDistance->setValue(int(e.distance()));
    ui->sliderAngle->setValue(int(e.angle()));
    ui->sliderSpread->setValue(int(e.spread()));
    ui->sliderOpacity->setValue(int(e.opacity() * 100.0 + 0.5));
    ui->valueSize->setText(QString::number(ui->sliderSize->value()));
    ui->valueDistance->setText(QString::number(ui->sliderDistance->value()));
    ui->valueAngle->setText(QString::number(ui->sliderAngle->value()));
    ui->valueSpread->setText(QString::number(ui->sliderSpread->value()));
    ui->valueOpacity->setText(QString::number(ui->sliderOpacity->value()));
    refreshColorButton();
    m_block = false;
}

void LayerStyleDialog::refreshColorButton()
{
    const QColor c = draft(m_current).color();
    ui->btnColor->setStyleSheet(
        QStringLiteral("background-color: %1; border: 1px solid #222;").arg(c.name()));
}

void LayerStyleDialog::updateParamEnabled()
{
    const Ps::LayerStyleEffect &e = draft(m_current);
    bool on = false;
    for (int row = 0; row < kRowCount; ++row) {
        if (kRowKinds[row] != m_current)
            continue;
        if (QListWidgetItem *item = ui->effectList->item(row))
            on = (item->checkState() == Qt::Checked);
    }
    ui->paramHost->setEnabled(on);
    ui->sliderSize->setEnabled(on && e.kind() != Ps::LayerStyleKind::ColorOverlay);
    ui->valueSize->setEnabled(on && e.kind() != Ps::LayerStyleKind::ColorOverlay);
    ui->sliderDistance->setEnabled(on && e.usesOffset());
    ui->valueDistance->setEnabled(on && e.usesOffset());
    ui->sliderAngle->setEnabled(on && e.usesOffset());
    ui->valueAngle->setEnabled(on && e.usesOffset());
    const bool spreadOn = on && e.kind() != Ps::LayerStyleKind::ColorOverlay
                          && e.kind() != Ps::LayerStyleKind::Stroke;
    ui->sliderSpread->setEnabled(spreadOn);
    ui->valueSpread->setEnabled(spreadOn);
}

QVector<Ps::LayerStyleEffect> LayerStyleDialog::resultEffects() const
{
    const_cast<LayerStyleDialog *>(this)->saveUiToCurrent();

    QVector<Ps::LayerStyleEffect> out;
    for (int row = 0; row < kRowCount; ++row) {
        const Ps::LayerStyleKind kind = kRowKinds[row];
        Ps::LayerStyleEffect e = m_draft.value(Ps::LayerStyleEffect::kindId(kind),
                                               Ps::LayerStyleEffect::makeDefault(kind));
        if (QListWidgetItem *item = ui->effectList->item(row))
            e.setEnabled(item->checkState() == Qt::Checked);
        if (e.isEnabled())
            out.append(e);
    }
    return out;
}
