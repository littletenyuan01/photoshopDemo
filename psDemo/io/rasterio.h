#ifndef RASTERIO_H
#define RASTERIO_H

#include <QString>

namespace Ps {

class ImageDocument;

/**
 * 扁平栅格导出（io 层）。
 *
 * 合成当前文档 → PNG（保留透明）或 JPEG（铺白底，无 alpha）。
 * 不改工程路径、不消脏 —— 对照 PS「导出」≠「存储」。
 */
class RasterIo
{
public:
    static bool exportFile(const ImageDocument &doc, const QString &filePath,
                           QString *errorMessage = nullptr);

private:
    RasterIo() = delete;
};

} // namespace Ps

#endif // RASTERIO_H
