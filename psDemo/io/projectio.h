/**
 * projectio.h — `.pslite` 工程文件读写（io 层）。
 *
 * 对照 GIMP XCF 的最小子集；与 ImageDocument / Layer / Selection 配合。
 */
#ifndef PROJECTIO_H
#define PROJECTIO_H

#include <QString>
#include <QtGlobal>
#include <memory>

namespace Ps {

class ImageDocument;

/**
 * 保存时写入的格式号，Demo 固定为 1。
 * 旧文件头里可能仍是 2/3/4（开发期涨号留下的），打开时按当时字段读，再保存即为 1。
 */
inline constexpr quint32 kProjectFormatVersion = 1;

/**
 * 工程文件读写（io 层）。
 *
 * 【对照 GIMP】`app/xcf/xcf-save.c` / `xcf-load.c` 的瘦身版。
 *
 * 格式：单文件 `.pslite`（魔数 PSLT + QDataStream）
 * - 魔数、格式版本（固定 1）、文档宽高、活动层
 * - 每层：名 / 显隐 / 不透明度 / 混合 / offset + PNG 像素
 *   + 样式栈 + 可选蒙版（enabled / linked + 灰度 PNG）
 * - 选区：灰度 PNG（空选区则长度 0）
 */
class ProjectIo
{
public:
    static constexpr quint32 kMagic = 0x50534C54; // 'PSLT'

    /** 写入工程文件；失败时 errorMessage 非空。 */
    static bool save(const ImageDocument &doc, const QString &filePath,
                     QString *errorMessage = nullptr);

    /** 读取工程文件；失败返回 nullptr。 */
    static std::unique_ptr<ImageDocument> load(const QString &filePath,
                                               QString *errorMessage = nullptr);

private:
    ProjectIo() = delete;
};

} // namespace Ps

#endif // PROJECTIO_H
