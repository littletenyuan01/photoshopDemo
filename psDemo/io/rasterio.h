/**
 * rasterio.h — 扁平栅格读入 / 导出 PNG/JPEG/WebP（io 层）。
 *
 * 读入尊重 EXIF 方向；导出合成当前文档后写出，不改工程路径、不消脏。
 * 「存储为 Web」走 WebExportOptions（质量 / 压缩 / alpha / 缩放）。
 */
#ifndef RASTERIO_H
#define RASTERIO_H

#include <QByteArray>
#include <QImage>
#include <QString>

namespace Ps {

class ImageDocument;

/** 面向 Web 交付的扁平导出格式（对照 PS Save for Web 精简）。 */
enum class WebExportFormat {
    Png = 0,
    Jpeg = 1,
    WebP = 2,
};

/**
 * 「存储为 Web」编码参数。
 * JPEG 始终铺白底；PNG/WebP 可由 preserveAlpha 决定是否保留透明。
 */
struct WebExportOptions {
    WebExportFormat format = WebExportFormat::Jpeg;
    int quality = 80;          ///< JPEG / WebP 有损：0–100
    int pngCompression = 6;    ///< PNG：0–9（传给 QImageWriter::setCompression）
    bool preserveAlpha = true; ///< PNG / WebP；JPEG 忽略
    bool webpLossless = false; ///< WebP 无损时忽略 quality
    int scalePercent = 100;    ///< 1–100；100 = 原尺寸
};

/**
 * 扁平栅格 IO（io 层）。
 *
 * - readFile：打开常见位图（PNG/JPEG/…），尊重 EXIF。
 * - exportFile：合成当前文档 → PNG（透明）或 JPEG（白底，质量 92）。
 * - encode / exportWithOptions：「存储为 Web」可调参数路径。
 */
class RasterIo
{
public:
    /**
     * 读位图；失败返回空图，errorMessage 可选填原因。
     * 对照各处原先各自写 QImageReader + setAutoTransform。
     */
    static QImage readFile(const QString &filePath,
                           QString *errorMessage = nullptr);

    /**
     * 合成并导出；按扩展名选 PNG（透明）或 JPEG（白底，质量 92）。
     * @return 成功 true；失败时 errorMessage 非空。
     */
    static bool exportFile(const ImageDocument &doc, const QString &filePath,
                           QString *errorMessage = nullptr);

    /** 本机 Qt 是否能写出该格式（WebP 依赖 imageformats 插件）。 */
    static bool formatSupported(WebExportFormat format);

    /** 默认文件后缀（不含点）：png / jpg / webp。 */
    static QString formatSuffix(WebExportFormat format);

    /** 编码器四字符名：PNG / JPEG / WEBP。 */
    static const char *formatName(WebExportFormat format);

    /**
     * 把预乘合成图做成可编码的扁平图（缩放、白底、直通 alpha）。
     * @param premulComposite 通常来自 Compositor::composite
     */
    static QImage prepareFlatImage(const QImage &premulComposite,
                                   const WebExportOptions &opt);

    /**
     * 把扁平图编码到内存（用于体积预估与优化预览 round-trip）。
     * @param flat 已由 prepareFlatImage 处理的图
     */
    static bool encodeBytes(const QImage &flat, const WebExportOptions &opt,
                            QByteArray *out, QString *errorMessage = nullptr);

    /** 把 encodeBytes 的结果写入磁盘。 */
    static bool writeEncoded(const QByteArray &bytes, const QString &filePath,
                             QString *errorMessage = nullptr);

    /**
     * 合成文档并按选项导出到路径。
     * 扩展名可与 format 不一致；以 options.format 为准。
     */
    static bool exportWithOptions(const ImageDocument &doc, const QString &filePath,
                                  const WebExportOptions &opt,
                                  QString *errorMessage = nullptr);

private:
    RasterIo() = delete;
};

} // namespace Ps

#endif // RASTERIO_H
