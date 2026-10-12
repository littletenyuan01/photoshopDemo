/**
 * saveforwebdialog.cpp —「存储为 Web」对话框实现（ui 层）。
 */
#include "saveforwebdialog.h"
#include "ui_saveforwebdialog.h"

#include <QBuffer>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QMessageBox>
#include <QPixmap>
#include <QTimer>
#include <QShowEvent>

using Ps::RasterIo;
using Ps::WebExportFormat;
using Ps::WebExportOptions;

SaveForWebDialog::SaveForWebDialog(const QImage &premulComposite,
                                   const QString &suggestedPath,
                                   QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::SaveForWebDialog)
    , m_source(premulComposite)
    , m_suggestedPath(suggestedPath)
{
    ui->setupUi(this);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);

    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setSingleShot(true);
    m_refreshTimer->setInterval(120);
    connect(m_refreshTimer, &QTimer::timeout, this, &SaveForWebDialog::refreshPreviewAndSize);

    rebuildFormatList();

    auto linkSliderSpin = [this](QSlider *slider, QSpinBox *spin) {
        connect(slider, &QSlider::valueChanged, this, [this, spin](int v) {
            if (m_blockSync)
                return;
            m_blockSync = true;
            spin->setValue(v);
            m_blockSync = false;
            scheduleRefresh();
        });
        connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this,
                [this, slider](int v) {
                    if (m_blockSync)
                        return;
                    m_blockSync = true;
                    slider->setValue(v);
                    m_blockSync = false;
                    scheduleRefresh();
                });
    };
    linkSliderSpin(ui->qualitySlider, ui->qualitySpin);
    linkSliderSpin(ui->pngCompSlider, ui->pngCompSpin);

    connect(ui->formatCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
                syncControlVisibility();
                scheduleRefresh();
            });
    connect(ui->preserveAlphaCheck, &QCheckBox::toggled, this, [this](bool) {
        scheduleRefresh();
    });
    connect(ui->webpLosslessCheck, &QCheckBox::toggled, this, [this](bool) {
        syncControlVisibility();
        scheduleRefresh();
    });
    connect(ui->scaleSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int) { scheduleRefresh(); });

    connect(ui->saveButton, &QPushButton::clicked, this, &SaveForWebDialog::onSaveClicked);
    connect(ui->cancelButton, &QPushButton::clicked, this, &QDialog::reject);

    syncControlVisibility();
}

SaveForWebDialog::~SaveForWebDialog()
{
    delete ui;
}

void SaveForWebDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    // 首次显示后标签才有真实尺寸，再刷预览
    QTimer::singleShot(0, this, &SaveForWebDialog::refreshPreviewAndSize);
}

void SaveForWebDialog::rebuildFormatList()
{
    ui->formatCombo->clear();
    auto add = [this](WebExportFormat f, const QString &label) {
        if (!RasterIo::formatSupported(f))
            return;
        ui->formatCombo->addItem(label, static_cast<int>(f));
    };
    add(WebExportFormat::Jpeg, QStringLiteral("JPEG"));
    add(WebExportFormat::Png, QStringLiteral("PNG"));
    add(WebExportFormat::WebP, QStringLiteral("WebP"));

    if (ui->formatCombo->count() == 0) {
        ui->saveButton->setEnabled(false);
        ui->sizeLabel->setText(tr("本机无可用图像写出插件"));
    }
}

WebExportFormat SaveForWebDialog::currentFormat() const
{
    const int v = ui->formatCombo->currentData().toInt();
    return static_cast<WebExportFormat>(v);
}

WebExportOptions SaveForWebDialog::options() const
{
    WebExportOptions opt;
    opt.format = currentFormat();
    opt.quality = ui->qualitySpin->value();
    opt.pngCompression = ui->pngCompSpin->value();
    opt.preserveAlpha = ui->preserveAlphaCheck->isChecked();
    opt.webpLossless = ui->webpLosslessCheck->isChecked();
    opt.scalePercent = ui->scaleSpin->value();
    return opt;
}

void SaveForWebDialog::syncControlVisibility()
{
    const WebExportFormat fmt = currentFormat();
    const bool jpeg = fmt == WebExportFormat::Jpeg;
    const bool png = fmt == WebExportFormat::Png;
    const bool webp = fmt == WebExportFormat::WebP;

    ui->qualityLabel->setVisible(!png && !(webp && ui->webpLosslessCheck->isChecked()));
    ui->qualitySlider->setVisible(ui->qualityLabel->isVisible());
    ui->qualitySpin->setVisible(ui->qualityLabel->isVisible());

    ui->pngCompLabel->setVisible(png);
    ui->pngCompSlider->setVisible(png);
    ui->pngCompSpin->setVisible(png);

    ui->preserveAlphaCheck->setVisible(png || webp);
    ui->webpLosslessCheck->setVisible(webp);
}

void SaveForWebDialog::scheduleRefresh()
{
    m_refreshTimer->start();
}

QString SaveForWebDialog::formatBytes(qint64 bytes)
{
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);
    const double kb = bytes / 1024.0;
    if (kb < 1024.0)
        return QStringLiteral("%1 K").arg(kb, 0, 'f', kb >= 100 ? 0 : 1);
    return QStringLiteral("%1 M").arg(kb / 1024.0, 0, 'f', 2);
}

QPixmap SaveForWebDialog::fitPreview(const QImage &img, const QSize &box)
{
    if (img.isNull() || box.width() < 8 || box.height() < 8)
        return {};
    return QPixmap::fromImage(
        img.scaled(box, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void SaveForWebDialog::refreshPreviewAndSize()
{
    if (m_source.isNull() || ui->formatCombo->count() == 0)
        return;

    const WebExportOptions opt = options();
    const QImage flat = RasterIo::prepareFlatImage(m_source, opt);
    if (flat.isNull()) {
        ui->sizeLabel->setText(tr("估计大小: —"));
        return;
    }

    ui->dimLabel->setText(tr("尺寸: %1 × %2 像素").arg(flat.width()).arg(flat.height()));

    // 原稿侧：同缩放、白底，便于与 JPEG 优化结果对比
    WebExportOptions origOpt = opt;
    origOpt.format = WebExportFormat::Png;
    origOpt.preserveAlpha = false;
    const QImage originalView = RasterIo::prepareFlatImage(m_source, origOpt);

    const QSize box(qMax(8, ui->originalLabel->width() - 4),
                    qMax(8, ui->originalLabel->height() - 4));
    ui->originalLabel->setPixmap(fitPreview(originalView, box));

    QByteArray bytes;
    QString err;
    if (!RasterIo::encodeBytes(flat, opt, &bytes, &err)) {
        ui->optimizedLabel->setText(tr("编码失败"));
        ui->sizeLabel->setText(tr("估计大小: —（%1）").arg(err));
        return;
    }

    ui->sizeLabel->setText(tr("估计大小: %1").arg(formatBytes(bytes.size())));

    QBuffer buf(&bytes);
    buf.open(QIODevice::ReadOnly);
    QImageReader reader(&buf, RasterIo::formatName(opt.format));
    QImage decoded = reader.read();
    if (decoded.isNull()) {
        // 无损/部分格式直接显示 flat
        ui->optimizedLabel->setPixmap(fitPreview(flat, box));
    } else {
        ui->optimizedLabel->setPixmap(fitPreview(decoded, box));
    }
}

void SaveForWebDialog::onSaveClicked()
{
    if (ui->formatCombo->count() == 0)
        return;

    const WebExportOptions opt = options();
    const QString suffix = RasterIo::formatSuffix(opt.format);

    QFileInfo info(m_suggestedPath);
    QString dir = info.absolutePath();
    if (dir.isEmpty() || dir == QLatin1String("."))
        dir = QDir::homePath();
    QString base = info.completeBaseName();
    if (base.isEmpty())
        base = QStringLiteral("export");

    QString filter;
    switch (opt.format) {
    case WebExportFormat::Jpeg:
        filter = tr("JPEG 图像 (*.jpg *.jpeg)");
        break;
    case WebExportFormat::Png:
        filter = tr("PNG 图像 (*.png)");
        break;
    case WebExportFormat::WebP:
        filter = tr("WebP 图像 (*.webp)");
        break;
    }

    QString path = QFileDialog::getSaveFileName(
        this, tr("存储为 Web"), dir + QLatin1Char('/') + base + QLatin1Char('.') + suffix,
        filter);
    if (path.isEmpty())
        return;

    if (!path.endsWith(QLatin1Char('.') + suffix, Qt::CaseInsensitive)) {
        // 去掉其它常见后缀再补正确后缀
        if (path.endsWith(QLatin1String(".jpeg"), Qt::CaseInsensitive))
            path.chop(5);
        else if (path.endsWith(QLatin1String(".jpg"), Qt::CaseInsensitive)
                 || path.endsWith(QLatin1String(".png"), Qt::CaseInsensitive)
                 || path.endsWith(QLatin1String(".webp"), Qt::CaseInsensitive))
            path.chop(4);
        path += QLatin1Char('.') + suffix;
    }

    QByteArray bytes;
    QString err;
    const QImage flat = RasterIo::prepareFlatImage(m_source, opt);
    if (!RasterIo::encodeBytes(flat, opt, &bytes, &err)
        || !RasterIo::writeEncoded(bytes, path, &err)) {
        QMessageBox::warning(this, tr("存储失败"), err);
        return;
    }

    m_savePath = path;
    accept();
}
