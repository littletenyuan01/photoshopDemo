/**
 * t_webexport.cpp —「存储为 Web」编码路径：质量体积、alpha、缩放、写出往返。
 */
#include "domain/imagedocument.h"
#include "engine/compositor.h"
#include "io/rasterio.h"
#include "testutil.h"

#include <QBuffer>
#include <QImageReader>
#include <QFile>

using namespace Ps;
using PsTests::Reporter;

namespace {

QImage makeAlphaFixture(int w, int h)
{
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    img.fill(qRgba(0, 0, 0, 0));
    for (int y = 0; y < h; ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            if (x < w / 2)
                line[x] = qRgba(200, 40, 40, 255); // 不透明红
            else
                line[x] = qRgba(0, 0, 0, 0); // 全透明
        }
    }
    return img;
}

} // namespace

int tWebExport()
{
    Reporter r;

    r.check(RasterIo::formatSupported(WebExportFormat::Png),
            QStringLiteral("PNG 写出受支持"));
    r.check(RasterIo::formatSupported(WebExportFormat::Jpeg),
            QStringLiteral("JPEG 写出受支持"));
    if (RasterIo::formatSupported(WebExportFormat::WebP))
        r.info(QStringLiteral("本机有 WebP 插件"));
    else
        r.info(QStringLiteral("本机无 WebP 插件（菜单中不出现 WebP，属预期）"));

    const QImage premul = makeAlphaFixture(96, 64);
    WebExportOptions jpegHigh;
    jpegHigh.format = WebExportFormat::Jpeg;
    jpegHigh.quality = 95;
    jpegHigh.preserveAlpha = false;
    jpegHigh.scalePercent = 100;

    WebExportOptions jpegLow = jpegHigh;
    jpegLow.quality = 10;

    const QImage flat = RasterIo::prepareFlatImage(premul, jpegHigh);
    r.check(!flat.isNull() && flat.format() == QImage::Format_RGB32,
            QStringLiteral("JPEG 准备图为 RGB32 白底"));

    QByteArray hiBytes, loBytes;
    QString err;
    r.check(RasterIo::encodeBytes(flat, jpegHigh, &hiBytes, &err),
            QStringLiteral("JPEG q=95 编码（%1）").arg(err));
    r.check(RasterIo::encodeBytes(flat, jpegLow, &loBytes, &err),
            QStringLiteral("JPEG q=10 编码（%1）").arg(err));
    r.check(loBytes.size() > 0 && loBytes.size() < hiBytes.size(),
            QStringLiteral("低质量 JPEG 更小（%1 < %2）")
                .arg(loBytes.size())
                .arg(hiBytes.size()));

    // PNG 保留透明：右半 alpha 应仍接近 0
    WebExportOptions pngKeep;
    pngKeep.format = WebExportFormat::Png;
    pngKeep.pngCompression = 3;
    pngKeep.preserveAlpha = true;
    const QImage flatPng = RasterIo::prepareFlatImage(premul, pngKeep);
    r.check(flatPng.format() == QImage::Format_ARGB32,
            QStringLiteral("PNG 保留透明 → ARGB32"));
    QByteArray pngBytes;
    r.check(RasterIo::encodeBytes(flatPng, pngKeep, &pngBytes, &err),
            QStringLiteral("PNG 编码（%1）").arg(err));
    {
        QBuffer buf(&pngBytes);
        buf.open(QIODevice::ReadOnly);
        QImageReader reader(&buf, "PNG");
        const QImage decoded = reader.read().convertToFormat(QImage::Format_ARGB32);
        r.check(!decoded.isNull(), QStringLiteral("PNG 可解码"));
        if (!decoded.isNull()) {
            const QRgb right = decoded.pixel(decoded.width() - 1, decoded.height() / 2);
            r.check(qAlpha(right) < 8,
                    QStringLiteral("PNG 保留透明：右缘 alpha=%1").arg(qAlpha(right)));
        }
    }

    // PNG 不保留透明：铺白底，右缘应不透明
    WebExportOptions pngFlat = pngKeep;
    pngFlat.preserveAlpha = false;
    const QImage flatOpaque = RasterIo::prepareFlatImage(premul, pngFlat);
    r.check(flatOpaque.format() == QImage::Format_RGB32,
            QStringLiteral("PNG 去透明 → RGB32"));

    // 缩放
    WebExportOptions scaled = jpegHigh;
    scaled.scalePercent = 50;
    const QImage half = RasterIo::prepareFlatImage(premul, scaled);
    r.check(half.width() == 48 && half.height() == 32,
            QStringLiteral("50%% 缩放 → %1×%2").arg(half.width()).arg(half.height()));

    // 端到端：文档 → exportWithOptions → 文件可读
    auto doc = ImageDocument::createBlank(80, 60, Qt::white);
    doc->placeImageAsLayer(PsTests::makeLayerImage(80, 60, 2, 255), QStringLiteral("L"));
    const QString path = PsTests::tempFilePath(QStringLiteral("webexport.jpg"));
    WebExportOptions outOpt;
    outOpt.format = WebExportFormat::Jpeg;
    outOpt.quality = 85;
    r.check(RasterIo::exportWithOptions(*doc, path, outOpt, &err),
            QStringLiteral("exportWithOptions 写盘（%1）").arg(err));
    const QImage round = RasterIo::readFile(path, &err);
    r.check(!round.isNull() && round.width() == 80 && round.height() == 60,
            QStringLiteral("回读 JPEG 尺寸正确（%1）").arg(err));

    // 旧路径 exportFile 仍可用
    const QString pngPath = PsTests::tempFilePath(QStringLiteral("webexport.png"));
    r.check(RasterIo::exportFile(*doc, pngPath, &err),
            QStringLiteral("exportFile PNG（%1）").arg(err));
    r.check(QFile::exists(pngPath) && QFile(pngPath).size() > 0,
            QStringLiteral("exportFile 产出非空文件"));

    // 合成结果与 prepare 同源：不改 .pslite
    const QImage composite = Compositor::composite(*doc);
    r.check(!composite.isNull(), QStringLiteral("合成可用作 Web 导出输入"));

    return r.fails;
}
