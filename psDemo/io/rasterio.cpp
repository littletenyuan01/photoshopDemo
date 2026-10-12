/**
 * rasterio.cpp — RasterIo 读入 / 导出实现（io 层）。
 */
#include "rasterio.h"

#include "domain/imagedocument.h"
#include "engine/compositor.h"

#include <QBuffer>
#include <QImageReader>
#include <QImageWriter>
#include <QPainter>
#include <QFile>
#include <QFileInfo>

namespace Ps {
namespace {

QImage flattenOnWhite(const QImage &premul)
{
    QImage rgb(premul.size(), QImage::Format_RGB32);
    rgb.fill(Qt::white);
    QPainter painter(&rgb);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.drawImage(0, 0, premul);
    painter.end();
    return rgb;
}

QImage scaleIfNeeded(const QImage &src, int scalePercent)
{
    const int pct = qBound(1, scalePercent, 100);
    if (pct >= 100 || src.isNull())
        return src;
    const int w = qMax(1, qRound(src.width() * pct / 100.0));
    const int h = qMax(1, qRound(src.height() * pct / 100.0));
    return src.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

void applyWriterSettings(QImageWriter &writer, const WebExportOptions &opt)
{
    switch (opt.format) {
    case WebExportFormat::Jpeg:
        writer.setQuality(qBound(0, opt.quality, 100));
        break;
    case WebExportFormat::Png:
        writer.setCompression(qBound(0, opt.pngCompression, 9));
        break;
    case WebExportFormat::WebP:
        if (opt.webpLossless) {
            writer.setQuality(100);
            // Qt WebP：quality=100 常作无损；部分版本另认 compression
            writer.setCompression(0);
        } else {
            writer.setQuality(qBound(0, opt.quality, 100));
        }
        break;
    }
}

} // namespace

QImage RasterIo::readFile(const QString &filePath, QString *errorMessage)
{
    if (filePath.isEmpty()) {
        if (errorMessage)
            *errorMessage = QObject::tr("路径为空。");
        return {};
    }

    QImageReader reader(filePath);
    reader.setAutoTransform(true); // 尊重 EXIF 方向
    QImage image = reader.read();
    if (image.isNull() && errorMessage)
        *errorMessage = reader.errorString();
    return image;
}

bool RasterIo::formatSupported(WebExportFormat format)
{
    const QByteArray name = QByteArray(formatName(format)).toLower();
    const QList<QByteArray> list = QImageWriter::supportedImageFormats();
    for (const QByteArray &f : list) {
        if (f.toLower() == name)
            return true;
    }
    // JPEG 插件常只登记 "jpg"
    if (format == WebExportFormat::Jpeg) {
        for (const QByteArray &f : list) {
            const QByteArray lower = f.toLower();
            if (lower == "jpg" || lower == "jpeg")
                return true;
        }
    }
    return false;
}

QString RasterIo::formatSuffix(WebExportFormat format)
{
    switch (format) {
    case WebExportFormat::Png:
        return QStringLiteral("png");
    case WebExportFormat::Jpeg:
        return QStringLiteral("jpg");
    case WebExportFormat::WebP:
        return QStringLiteral("webp");
    }
    return QStringLiteral("png");
}

const char *RasterIo::formatName(WebExportFormat format)
{
    switch (format) {
    case WebExportFormat::Png:
        return "PNG";
    case WebExportFormat::Jpeg:
        return "JPEG";
    case WebExportFormat::WebP:
        return "WEBP";
    }
    return "PNG";
}

QImage RasterIo::prepareFlatImage(const QImage &premulComposite,
                                  const WebExportOptions &opt)
{
    if (premulComposite.isNull())
        return {};

    QImage scaled = scaleIfNeeded(premulComposite, opt.scalePercent);
    if (scaled.isNull())
        return {};

    const bool wantAlpha = opt.preserveAlpha
                           && (opt.format == WebExportFormat::Png
                               || opt.format == WebExportFormat::WebP);

    if (wantAlpha) {
        // 预乘 → 直通 ARGB，编码器才能正确存透明
        return scaled.convertToFormat(QImage::Format_ARGB32);
    }
    return flattenOnWhite(scaled);
}

bool RasterIo::encodeBytes(const QImage &flat, const WebExportOptions &opt,
                           QByteArray *out, QString *errorMessage)
{
    if (!out) {
        if (errorMessage)
            *errorMessage = QObject::tr("内部错误：输出缓冲为空。");
        return false;
    }
    if (flat.isNull()) {
        if (errorMessage)
            *errorMessage = QObject::tr("没有可编码的图像。");
        return false;
    }
    if (!formatSupported(opt.format)) {
        if (errorMessage) {
            *errorMessage = QObject::tr("本机 Qt 不支持写出 %1（缺少 imageformats 插件）。")
                                .arg(QString::fromLatin1(formatName(opt.format)));
        }
        return false;
    }

    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    QImageWriter writer(&buffer, formatName(opt.format));
    applyWriterSettings(writer, opt);
    if (!writer.write(flat)) {
        if (errorMessage) {
            const QString detail = writer.errorString();
            *errorMessage = detail.isEmpty()
                                ? QObject::tr("编码失败（%1）")
                                      .arg(QString::fromLatin1(formatName(opt.format)))
                                : detail;
        }
        return false;
    }
    *out = bytes;
    return true;
}

bool RasterIo::writeEncoded(const QByteArray &bytes, const QString &filePath,
                            QString *errorMessage)
{
    if (filePath.isEmpty()) {
        if (errorMessage)
            *errorMessage = QObject::tr("路径为空。");
        return false;
    }
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage)
            *errorMessage = QObject::tr("无法写入：%1").arg(filePath);
        return false;
    }
    if (file.write(bytes) != bytes.size()) {
        if (errorMessage)
            *errorMessage = QObject::tr("写入不完整：%1").arg(filePath);
        return false;
    }
    return true;
}

bool RasterIo::exportWithOptions(const ImageDocument &doc, const QString &filePath,
                                 const WebExportOptions &opt, QString *errorMessage)
{
    if (filePath.isEmpty() || doc.width() <= 0 || doc.height() <= 0) {
        if (errorMessage)
            *errorMessage = QObject::tr("没有可导出的文档。");
        return false;
    }

    const QImage premul = Compositor::composite(doc);
    if (premul.isNull()) {
        if (errorMessage)
            *errorMessage = QObject::tr("合成失败，无法导出。");
        return false;
    }

    const QImage flat = prepareFlatImage(premul, opt);
    QByteArray bytes;
    if (!encodeBytes(flat, opt, &bytes, errorMessage))
        return false;
    return writeEncoded(bytes, filePath, errorMessage);
}

bool RasterIo::exportFile(const ImageDocument &doc, const QString &filePath,
                          QString *errorMessage)
{
    if (filePath.isEmpty() || doc.width() <= 0 || doc.height() <= 0) {
        if (errorMessage)
            *errorMessage = QObject::tr("没有可导出的文档。");
        return false;
    }

    const QString suffix = QFileInfo(filePath).suffix().toLower();
    WebExportOptions opt;
    if (suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg")) {
        opt.format = WebExportFormat::Jpeg;
        opt.quality = 92;
        opt.preserveAlpha = false;
    } else if (suffix == QLatin1String("webp")) {
        opt.format = WebExportFormat::WebP;
        opt.quality = 90;
        opt.preserveAlpha = true;
    } else {
        opt.format = WebExportFormat::Png;
        opt.pngCompression = 6;
        opt.preserveAlpha = true;
    }
    return exportWithOptions(doc, filePath, opt, errorMessage);
}

} // namespace Ps
