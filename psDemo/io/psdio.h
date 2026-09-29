/**
 * psdio.h — PSD 子集导出（io 层）。
 *
 * 只写 PS 可打开的 RGB 8-bit 分层文件；完整可编辑请用 `.pslite`。
 */
#ifndef PSDIO_H
#define PSDIO_H

#include <QString>

namespace Ps {

class ImageDocument;

/**
 * PSD 子集导出（io 层）。
 *
 * 【对照】GIMP `plug-ins/file-psd/psd-export.c` 的极简版：只写 PS 能打开的
 * RGB 8-bit 分层文件，不是完整 PSD 工程。
 *
 * 已写：画布尺寸、图层（名/可见/不透明度/位置/RGBA 像素）、合成预览图。
 * 未写：选区、图层组、蒙版、调整层、样式、路径、文字层、CMYK…
 *
 * 用途：菜单「存储为 → .psd」互通；完整可再编辑请用 `.pslite`。
 */
class PsdIo
{
public:
    static bool save(const ImageDocument &doc, const QString &filePath,
                     QString *errorMessage = nullptr);

private:
    PsdIo() = delete;
};

} // namespace Ps

#endif // PSDIO_H
