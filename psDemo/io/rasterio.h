/**
 * rasterio.h — 扁平栅格读入 / 导出 PNG/JPEG（io 层）。
 *
 * 读入尊重 EXIF 方向；导出合成当前文档后写出，不改工程路径、不消脏。
 */
#ifndef RASTERIO_H
#define RASTERIO_H

#include <QImage>
#include <QString>

namespace Ps {

class ImageDocument;

/**
 * 扁平栅格 IO（io 层）。
 *
 * - readFile：打开常见位图（PNG/JPEG/…），尊重 EXIF。
 * - exportFile：合成当前文档 → PNG（透明）或 JPEG（白底）。
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
     * 合成并导出；按扩展名选 PNG（透明）或 JPEG（白底）。
     * @return 成功 true；失败时 errorMessage 非空。
     */
    static bool exportFile(const ImageDocument &doc, const QString &filePath,
                           QString *errorMessage = nullptr);

private:
    RasterIo() = delete;
};

} // namespace Ps

#endif // RASTERIO_H
