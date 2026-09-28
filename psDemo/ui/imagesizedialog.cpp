#include "imagesizedialog.h"
#include "ui_imagesizedialog.h"

#include "domain/imagedocument.h"

#include <QIntValidator>
#include <QPixmap>
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
    const double kb = bytes / 1024.0;
    return QStringLiteral("%1K").arg(kb, 0, 'f', 0);
}

} // namespace

ImageSizeDialog::ImageSizeDialog(Ps::ImageDocument *document, const QImage &preview,
                                 QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ImageSizeDialog)
    , m_document(document)
    , m_previewSource(preview)
{
    ui->setupUi(this);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);

    connect(ui->okButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(ui->resetButton, &QPushButton::clicked, this, &ImageSizeDialog::resetToOriginal);
    connect(ui->widthEdit, &QLineEdit::editingFinished, this, &ImageSizeDialog::onWidthEdited);
    connect(ui->heightEdit, &QLineEdit::editingFinished, this, &ImageSizeDialog::onHeightEdited);
    connect(ui->widthUnitCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ImageSizeDialog::onUnitChanged);
    connect(ui->heightUnitCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ImageSizeDialog::syncHeightUnitFromWidth);
    connect(ui->resEdit, &QLineEdit::editingFinished, this, &ImageSizeDialog::updateInfoLabels);
    connect(ui->lockAspectButton, &QToolButton::toggled, this, [this](bool) {
        if (ui->lockAspectButton->isChecked())
            onWidthEdited();
    });

    ui->resEdit->setValidator(new QIntValidator(1, 2400, this));
    loadFromDocument();
}

ImageSizeDialog::~ImageSizeDialog()
{
    delete ui;
}

void ImageSizeDialog::loadFromDocument()
{
    if (!m_document)
        return;
    m_origW = m_document->width();
    m_origH = m_document->height();
    m_aspect = m_origH > 0 ? double(m_origW) / double(m_origH) : 1.0;
    resetToOriginal();
    updatePreview();
}

void ImageSizeDialog::resetToOriginal()
{
    m_blockDim = true;
    ui->widthUnitCombo->setCurrentIndex(Pixels);
    ui->heightUnitCombo->setCurrentIndex(Pixels);
    ui->resEdit->setText(QStringLiteral("72"));
    ui->resUnitCombo->setCurrentIndex(0);
    ui->resampleCheck->setChecked(true);
    setDimEditsFromPixels(m_origW, m_origH);
    m_blockDim = false;
    updateInfoLabels();
}

void ImageSizeDialog::updatePreview()
{
    if (m_previewSource.isNull()) {
        ui->previewLabel->setText(tr("无预览"));
        return;
    }
    const QPixmap pm = QPixmap::fromImage(
        m_previewSource.scaled(ui->previewFrame->size() - QSize(24, 24),
                               Qt::KeepAspectRatio, Qt::SmoothTransformation));
    ui->previewLabel->setPixmap(pm);
}

void ImageSizeDialog::updateInfoLabels()
{
    const QSize px = resultPixelSize();
    const int layers = m_document ? m_document->layers().count() : 1;
    const qint64 now = approxBytes(px.width(), px.height(), layers);
    const qint64 was = approxBytes(m_origW, m_origH, layers);
    ui->sizeInfoLabel->setText(
        tr("图像大小: %1（之前为 %2）").arg(formatBytes(now), formatBytes(was)));
    ui->pixelDimLabel->setText(
        tr("尺寸: %1 像素 × %2 像素").arg(px.width()).arg(px.height()));
}

QSize ImageSizeDialog::resultPixelSize() const
{
    const QSizeF p = dimPixels();
    return QSize(qMax(1, qRound(p.width())), qMax(1, qRound(p.height())));
}

bool ImageSizeDialog::resampleEnabled() const
{
    return ui->resampleCheck->isChecked();
}

double ImageSizeDialog::resolutionPpi() const
{
    bool ok = false;
    double v = ui->resEdit->text().trimmed().toDouble(&ok);
    if (!ok || v <= 0.0)
        v = 72.0;
    if (ui->resUnitCombo->currentIndex() == 1) // 像素/厘米 → PPI
        v *= 2.54;
    return v;
}

QSizeF ImageSizeDialog::dimPixels() const
{
    const DimUnit unit = DimUnit(ui->widthUnitCombo->currentIndex());
    const double ppi = resolutionPpi();
    bool okW = false;
    bool okH = false;
    const double w = ui->widthEdit->text().trimmed().toDouble(&okW);
    const double h = ui->heightEdit->text().trimmed().toDouble(&okH);
    return QSizeF(toPixels(okW ? w : 1.0, unit, ppi),
                  toPixels(okH ? h : 1.0, unit, ppi));
}

void ImageSizeDialog::setDimEditsFromPixels(double wPx, double hPx)
{
    const DimUnit unit = DimUnit(ui->widthUnitCombo->currentIndex());
    const double ppi = resolutionPpi();
    ui->widthEdit->setText(formatDim(fromPixels(wPx, unit, ppi), unit));
    ui->heightEdit->setText(formatDim(fromPixels(hPx, unit, ppi), unit));
}

void ImageSizeDialog::onWidthEdited()
{
    if (m_blockDim)
        return;
    if (ui->lockAspectButton->isChecked() && m_aspect > 0.0) {
        const double wPx = dimPixels().width();
        m_blockDim = true;
        const DimUnit unit = DimUnit(ui->heightUnitCombo->currentIndex());
        ui->heightEdit->setText(formatDim(fromPixels(wPx / m_aspect, unit, resolutionPpi()), unit));
        m_blockDim = false;
    }
    updateInfoLabels();
}

void ImageSizeDialog::onHeightEdited()
{
    if (m_blockDim)
        return;
    if (ui->lockAspectButton->isChecked() && m_aspect > 0.0) {
        const double hPx = dimPixels().height();
        m_blockDim = true;
        const DimUnit unit = DimUnit(ui->widthUnitCombo->currentIndex());
        ui->widthEdit->setText(formatDim(fromPixels(hPx * m_aspect, unit, resolutionPpi()), unit));
        m_blockDim = false;
    }
    updateInfoLabels();
}

void ImageSizeDialog::onUnitChanged()
{
    syncHeightUnitFromWidth();
    const QSize px = resultPixelSize();
    m_blockDim = true;
    setDimEditsFromPixels(px.width(), px.height());
    m_blockDim = false;
    updateInfoLabels();
}

void ImageSizeDialog::syncHeightUnitFromWidth()
{
    ui->heightUnitCombo->blockSignals(true);
    ui->heightUnitCombo->setCurrentIndex(ui->widthUnitCombo->currentIndex());
    ui->heightUnitCombo->blockSignals(false);
}

double ImageSizeDialog::toPixels(double value, DimUnit unit, double ppi)
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

double ImageSizeDialog::fromPixels(double px, DimUnit unit, double ppi)
{
    if (ppi <= 0.0)
        ppi = 72.0;
    switch (unit) {
    case Inches:
        return px / ppi;
    case Centimeters:
        return px / ppi * 2.54;
    case Millimeters:
        return px / ppi * 25.4;
    case Pixels:
    default:
        return px;
    }
}

QString ImageSizeDialog::formatDim(double value, DimUnit unit)
{
    if (unit == Pixels)
        return QString::number(qMax(1, qRound(value)));
    if (unit == Millimeters)
        return QString::number(value, 'f', 0);
    return QString::number(value, 'f', 1);
}
