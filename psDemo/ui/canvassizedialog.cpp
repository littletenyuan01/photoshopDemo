#include "canvassizedialog.h"
#include "ui_canvassizedialog.h"

#include "domain/imagedocument.h"

#include <QButtonGroup>
#include <QIntValidator>
#include <QtMath>

namespace {

qint64 approxBytes(int w, int h, int layersApprox = 1)
{
    return qint64(w) * h * 4 * qMax(1, layersApprox);
}

QString formatBytes(qint64 bytes)
{
    const double mb = bytes / (1024.0 * 1024.0);
    if (mb >= 0.1)
        return QStringLiteral("%1M").arg(mb, 0, 'f', 1);
    return QStringLiteral("%1K").arg(bytes / 1024.0, 0, 'f', 0);
}

} // namespace

CanvasSizeDialog::CanvasSizeDialog(Ps::ImageDocument *document,
                                   const QColor &foreground,
                                   const QColor &background,
                                   QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::CanvasSizeDialog)
    , m_document(document)
    , m_fg(foreground)
    , m_bg(background)
{
    ui->setupUi(this);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);

    m_anchorGroup = new QButtonGroup(this);
    m_anchorGroup->setExclusive(true);
    const QList<QToolButton *> anchors = {
        ui->anchorTL, ui->anchorTC, ui->anchorTR,
        ui->anchorML, ui->anchorMC, ui->anchorMR,
        ui->anchorBL, ui->anchorBC, ui->anchorBR};
    for (int i = 0; i < anchors.size(); ++i)
        m_anchorGroup->addButton(anchors[i], i);

    connect(ui->okButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(ui->resetButton, &QPushButton::clicked, this, &CanvasSizeDialog::resetToOriginal);
    connect(ui->widthEdit, &QLineEdit::editingFinished, this, &CanvasSizeDialog::updateLabels);
    connect(ui->heightEdit, &QLineEdit::editingFinished, this, &CanvasSizeDialog::updateLabels);
    connect(ui->widthUnitCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CanvasSizeDialog::syncHeightUnitFromWidth);
    connect(ui->relativeCheck, &QCheckBox::toggled, this, &CanvasSizeDialog::updateLabels);
    connect(ui->extColorCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CanvasSizeDialog::onExtColorChanged);

    loadFromDocument();
    onExtColorChanged();
}

CanvasSizeDialog::~CanvasSizeDialog()
{
    delete ui;
}

void CanvasSizeDialog::loadFromDocument()
{
    if (!m_document)
        return;
    m_origW = m_document->width();
    m_origH = m_document->height();
    resetToOriginal();
}

void CanvasSizeDialog::resetToOriginal()
{
    ui->widthUnitCombo->setCurrentIndex(Pixels);
    ui->heightUnitCombo->setCurrentIndex(Pixels);
    ui->relativeCheck->setChecked(false);
    ui->widthEdit->setText(QString::number(m_origW));
    ui->heightEdit->setText(QString::number(m_origH));
    ui->anchorMC->setChecked(true);
    ui->extColorCombo->setCurrentIndex(0);
    updateLabels();
    onExtColorChanged();
}

void CanvasSizeDialog::updateLabels()
{
    const int layers = m_document ? m_document->layers().count() : 1;
    ui->currentGroup->setTitle(
        tr("当前大小: %1").arg(formatBytes(approxBytes(m_origW, m_origH, layers))));
    ui->curWidthLabel->setText(tr("%1 像素").arg(m_origW));
    ui->curHeightLabel->setText(tr("%1 像素").arg(m_origH));

    const QSize abs = resultPixelSize();
    ui->newGroup->setTitle(
        tr("新建大小: %1").arg(formatBytes(approxBytes(abs.width(), abs.height(), layers))));
}

void CanvasSizeDialog::onExtColorChanged()
{
    ui->extColorSwatch->setStyleSheet(
        QStringLiteral("QFrame#extColorSwatch { background-color: %1; border: 1px solid #111; }")
            .arg(extensionColor().name(QColor::HexRgb)));
}

void CanvasSizeDialog::syncHeightUnitFromWidth()
{
    ui->heightUnitCombo->blockSignals(true);
    ui->heightUnitCombo->setCurrentIndex(ui->widthUnitCombo->currentIndex());
    ui->heightUnitCombo->blockSignals(false);
    updateLabels();
}

QSizeF CanvasSizeDialog::absolutePixelSize() const
{
    const DimUnit unit = DimUnit(ui->widthUnitCombo->currentIndex());
    bool okW = false;
    bool okH = false;
    double w = ui->widthEdit->text().trimmed().toDouble(&okW);
    double h = ui->heightEdit->text().trimmed().toDouble(&okH);
    if (!okW)
        w = 0;
    if (!okH)
        h = 0;
    w = toPixels(w, unit, m_ppi);
    h = toPixels(h, unit, m_ppi);
    if (ui->relativeCheck->isChecked()) {
        w += m_origW;
        h += m_origH;
    }
    return QSizeF(w, h);
}

QSize CanvasSizeDialog::resultPixelSize() const
{
    const QSizeF p = absolutePixelSize();
    return QSize(qMax(1, qRound(p.width())), qMax(1, qRound(p.height())));
}

int CanvasSizeDialog::anchorRow() const
{
    const int id = m_anchorGroup ? m_anchorGroup->checkedId() : 4;
    return qBound(0, id / 3, 2);
}

int CanvasSizeDialog::anchorCol() const
{
    const int id = m_anchorGroup ? m_anchorGroup->checkedId() : 4;
    return qBound(0, id % 3, 2);
}

QColor CanvasSizeDialog::extensionColor() const
{
    const int index = ui->extColorCombo->currentIndex();
    if (index < 0 || index > static_cast<int>(ExtensionColor::Transparent))
        return m_bg;
    switch (static_cast<ExtensionColor>(index)) {
    case ExtensionColor::Foreground:
        return m_fg;
    case ExtensionColor::White:
        return Qt::white;
    case ExtensionColor::Black:
        return Qt::black;
    case ExtensionColor::Transparent:
        return Qt::transparent;
    case ExtensionColor::Background:
        return m_bg;
    }
    return m_bg;
}

double CanvasSizeDialog::toPixels(double value, DimUnit unit, double ppi)
{
    switch (unit) {
    case Inches:
        return value * ppi;
    case Centimeters:
        return value / 2.54 * ppi;
    case Millimeters:
        return value / 25.4 * ppi;
    case Pixels:
    default:
        return value;
    }
}
