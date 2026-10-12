/**
 * psdio.h — PSD 子集导入 / 导出（io 层）。
 *
 * 只处理 PS 可互通的 RGB 8-bit 分层文件；完整可编辑请用 `.pslite`。
 */
#ifndef PSDIO_H
#define PSDIO_H

#include <QString>
#include <memory>

namespace Ps {

class ImageDocument;

/**
 * PSD 子集 IO（io 层）。
 *
 * 【对照】GIMP `plug-ins/file-psd` 的极简版：RGB 8-bit 分层，不是完整 PSD 工程。
 *
 * **导出已写**：画布尺寸、图层（名/可见/不透明度/混合/位置/RGBA）、合成预览。
 * **导入已读**：同上子集；通道压缩支持 Raw(0) 与 RLE(1)。
 * **未做**：选区、图层组、蒙版、调整层、样式、路径、文字、CMYK、PSB(v2)…
 *
 * 用途：菜单「打开 → .psd」/「存储为 → .psd」互通；完整再编辑请用 `.pslite`。
 */
class PsdIo
{
public:
    static bool save(const ImageDocument &doc, const QString &filePath,
                     QString *errorMessage = nullptr);

    /**
     * 读入常见分层 PSD → ImageDocument（像素层栈）。
     * 失败返回 nullptr；部分层无法解析时尽量跳过并仍返回可读层。
     */
    static std::unique_ptr<ImageDocument> load(const QString &filePath,
                                               QString *errorMessage = nullptr);

private:
    PsdIo() = delete;
};

} // namespace Ps

#endif // PSDIO_H
