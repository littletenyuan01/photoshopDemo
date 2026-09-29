#include "rasterio.h"

#include "domain/imagedocument.h"
#include "engine/compositor.h"

#include <QImage>
#include <QImageWriter>
#include <QPainter>
#include <QFileInfo>

namespace Ps {

bool RasterIo::exportFile(const ImageDocument &doc, const QString &filePath,
                          QString *errorMessage)
{
    if (filePath.isEmpty() || doc.width() <= 0 || doc.height() <= 0) {
        if (errorMessage)
            *errorMessage = QObject::tr("没有可导出的文档。");
        return false;
    }

    QImage premul = Compositor::composite(doc);
    if (premul.isNull()) {
        if (errorMessage)
            *errorMessage = QObject::tr("合成失败，无法导出。");
        return false;
    }

    const QString suffix = QFileInfo(filePath).suffix().toLower();
    QImage out;
    const char *format = nullptr;

    if (suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg")) {
        format = "JPEG";
        // JPEG 无 alpha：铺白底再叠合成结果（对照扁平导出）
        QImage rgb(premul.size(), QImage::Format_RGB32);
        rgb.fill(Qt::white);
        QPainter painter(&rgb);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        painter.drawImage(0, 0, premul);
        painter.end();
        out = rgb;
    } else {
        format = "PNG";
        // 预乘 → 直通 ARGB，PNG 才能正确存透明
        out = premul.convertToFormat(QImage::Format_ARGB32);
    }

    QImageWriter writer(filePath, format);
    if (format && qstrcmp(format, "JPEG") == 0)
        writer.setQuality(92);
    if (!writer.write(out)) {
        if (errorMessage) {
            const QString detail = writer.errorString();
            *errorMessage = detail.isEmpty()
                                ? QObject::tr("无法写入：%1").arg(filePath)
                                : detail;
        }
        return false;
    }
    return true;
}

} // namespace Ps
