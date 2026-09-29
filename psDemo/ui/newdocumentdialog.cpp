#include "newdocumentdialog.h"
#include "ui_newdocumentdialog.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QDoubleValidator>
#include <QEvent>
#include <QFrame>
#include <QIntValidator>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStyle>
#include <QToolButton>

#include <QtMath>

namespace {

constexpr double kInchesPerCm = 1.0 / 2.54;
constexpr double kInchesPerMm = 1.0 / 25.4;

double parsePositiveDouble(const QLineEdit *edit, double fallback)
{
    bool ok = false;
    const double v = edit->text().trimmed().toDouble(&ok);
    return (ok && v > 0.0) ? v : fallback;
}

} // namespace

double NewDocumentDialog::toPixels(double value, DimUnit unit, double ppi)
{
    if (ppi <= 0.0)
        ppi = 72.0;
    switch (unit) {
    case Pixels:       return value;
    case Inches:       return value * ppi;
    case Centimeters:  return value * kInchesPerCm * ppi;
    case Millimeters:  return value * kInchesPerMm * ppi;
    }
    return value;
}

double NewDocumentDialog::fromPixels(double px, DimUnit unit, double ppi)
{
    if (ppi <= 0.0)
        ppi = 72.0;
    switch (unit) {
    case Pixels:       return px;
    case Inches:       return px / ppi;
    case Centimeters:  return px / ppi / kInchesPerCm;
    case Millimeters:  return px / ppi / kInchesPerMm;
    }
    return px;
}

QString NewDocumentDialog::formatDim(double value, DimUnit unit)
{
    switch (unit) {
    case Pixels:
    case Millimeters:
        return QString::number(qMax(1, qRound(value)));
    case Centimeters:
        return QString::number(qMax(0.1, value), 'f', 1);
    case Inches:
        return QString::number(qMax(0.001, value), 'f', 3);
    }
    return QString::number(value);
}

NewDocumentDialog::NewDocumentDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::NewDocumentDialog)
{
    ui->setupUi(this);
    // 布局 / 图标 / 对齐均在 newdocumentdialog.ui 中配置
    m_dimUnit = static_cast<DimUnit>(ui->widthUnitCombo->currentIndex());
    syncDimValidators();

    ui->resEdit->setValidator(new QIntValidator(1, 2400, this));

    auto *orientGroup = new QButtonGroup(this);
    orientGroup->setExclusive(true);
    orientGroup->addButton(ui->orientPortrait);
    orientGroup->addButton(ui->orientLandscape);

    connect(ui->createButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(ui->closeButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(ui->bannerClose, &QToolButton::clicked, ui->banner, &QWidget::hide);

    ui->presetClipboard->installEventFilter(this);
    ui->presetA4->installEventFilter(this);
    ui->presetDefault->installEventFilter(this);

    connect(ui->widthUnitCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &NewDocumentDialog::onDimUnitChanged);
    connect(ui->resEdit, &QLineEdit::editingFinished,
            this, &NewDocumentDialog::onResolutionEdited);
    connect(ui->resUnitCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &NewDocumentDialog::onResolutionEdited);

    connect(ui->orientPortrait, &QToolButton::clicked, this, [this]() {
        const QSizeF px = dimPixels();
        if (px.width() > px.height())
            setDimEditsFromPixels(px.height(), px.width());
        updateOrientationButtons();
    });
    connect(ui->orientLandscape, &QToolButton::clicked, this, [this]() {
        const QSizeF px = dimPixels();
        if (px.height() > px.width())
            setDimEditsFromPixels(px.height(), px.width());
        updateOrientationButtons();
    });
    connect(ui->widthEdit, &QLineEdit::editingFinished, this, &NewDocumentDialog::updateOrientationButtons);
    connect(ui->heightEdit, &QLineEdit::editingFinished, this, &NewDocumentDialog::updateOrientationButtons);
}

void NewDocumentDialog::syncDimValidators()
{
    if (m_dimUnit == Pixels || m_dimUnit == Millimeters) {
        auto *v = new QIntValidator(1, 30000, this);
        ui->widthEdit->setValidator(v);
        ui->heightEdit->setValidator(v);
        return;
    }
    const int decimals = (m_dimUnit == Centimeters) ? 1 : 3;
    auto *v = new QDoubleValidator(0.001, 100000.0, decimals, this);
    v->setNotation(QDoubleValidator::StandardNotation);
    ui->widthEdit->setValidator(v);
    ui->heightEdit->setValidator(v);
}

double NewDocumentDialog::resolutionPpi() const
{
    const double raw = parsePositiveDouble(ui->resEdit, 72.0);
    if (static_cast<ResUnit>(ui->resUnitCombo->currentIndex()) == ResUnit::PixelsPerCentimeter)
        return raw * 2.54;
    return raw;
}

QSizeF NewDocumentDialog::dimPixels() const
{
    const double ppi = resolutionPpi();
    const double w = toPixels(parsePositiveDouble(ui->widthEdit, 1920.0), m_dimUnit, ppi);
    const double h = toPixels(parsePositiveDouble(ui->heightEdit, 1080.0), m_dimUnit, ppi);
    return {w, h};
}

void NewDocumentDialog::setDimEditsFromPixels(double widthPx, double heightPx)
{
    const double ppi = resolutionPpi();
    const QSignalBlocker bw(ui->widthEdit);
    const QSignalBlocker bh(ui->heightEdit);
    ui->widthEdit->setText(formatDim(fromPixels(widthPx, m_dimUnit, ppi), m_dimUnit));
    ui->heightEdit->setText(formatDim(fromPixels(heightPx, m_dimUnit, ppi), m_dimUnit));
}

void NewDocumentDialog::onDimUnitChanged(int newIndex)
{
    if (newIndex < 0 || newIndex > Millimeters)
        return;

    const DimUnit newUnit = static_cast<DimUnit>(newIndex);
    if (newUnit == m_dimUnit)
        return;

    const QSizeF px = dimPixels();
    m_dimUnit = newUnit;
    syncDimValidators();
    setDimEditsFromPixels(px.width(), px.height());
    updateOrientationButtons();
}

void NewDocumentDialog::onResolutionEdited()
{
    updateOrientationButtons();
}

NewDocumentDialog::~NewDocumentDialog()
{
    delete ui;
}

QSize NewDocumentDialog::documentSize() const
{
    const QSizeF px = dimPixels();
    return {qMax(1, qRound(px.width())), qMax(1, qRound(px.height()))};
}

QString NewDocumentDialog::documentName() const
{
    return ui->docNameEdit->text().trimmed();
}

void NewDocumentDialog::applyPreset(int widthPx, int heightPx, int ppi, DimUnit unit, const QString &title)
{
    // 先同步分辨率与单位，再按像素写入宽高，避免「像素对、ppi 还是 72」导致毫米对不上卡片
    {
        const QSignalBlocker bUnit(ui->widthUnitCombo);
        const QSignalBlocker bRes(ui->resEdit);
        const QSignalBlocker bResUnit(ui->resUnitCombo);
        ui->resEdit->setText(QString::number(qMax(1, ppi)));
        ui->resUnitCombo->setCurrentIndex(static_cast<int>(ResUnit::PixelsPerInch));
        m_dimUnit = unit;
        ui->widthUnitCombo->setCurrentIndex(static_cast<int>(unit));
        syncDimValidators();
    }
    setDimEditsFromPixels(widthPx, heightPx);
    if (!title.isEmpty())
        ui->docNameEdit->setText(title);
    updateOrientationButtons();

    // 选中态用动态属性，边框样式在 newdocumentdialog.ui 的 QSS 中
    auto markSelected = [](QFrame *frame, bool on) {
        frame->setProperty("selected", on);
        frame->style()->unpolish(frame);
        frame->style()->polish(frame);
        frame->update();
    };
    markSelected(ui->presetClipboard, widthPx == 1920 && ppi == 72);
    markSelected(ui->presetA4, widthPx == 2480 && ppi == 300);
    markSelected(ui->presetDefault, widthPx == 800 && ppi == 72);
}

void NewDocumentDialog::updateOrientationButtons()
{
    const QSizeF px = dimPixels();
    const bool landscape = px.width() >= px.height();
    ui->orientLandscape->setChecked(landscape);
    ui->orientPortrait->setChecked(!landscape);
}

bool NewDocumentDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        if (watched == ui->presetClipboard) {
            // 1920×1080 @ 72 ppi（像素）
            applyPreset(1920, 1080, 72, Pixels, QStringLiteral("未标题-1"));
            return true;
        }
        if (watched == ui->presetA4) {
            // 210×297 mm @ 300 ppi → 2480×3508 px
            applyPreset(2480, 3508, 300, Millimeters, QStringLiteral("A4"));
            return true;
        }
        if (watched == ui->presetDefault) {
            applyPreset(800, 600, 72, Pixels, QStringLiteral("未标题-1"));
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}
