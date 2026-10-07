/**
 * projectio.h — `.pslite` 工程文件读写（io 层）。
 *
 * 对照 GIMP XCF 的最小子集；与 ImageDocument / Layer / Selection 配合。
 * 单一 ProjectFormat::Current；滤镜参数用固定字节块+预留，避免每加字段就撑爆旧档。
 * Demo 完成前不保证更早布局的文件可开。
 */
#ifndef PROJECTIO_H
#define PROJECTIO_H

#include <QString>
#include <QtGlobal>
#include <memory>

namespace Ps {

class ImageDocument;

/**
 * 工程磁盘格式标识（写入文件头的 quint32）。
 * 仅 Current；不兼容变更时改此值。滤镜参数块大小见 projectio.cpp `kFilterNodeParamBytes`。
 */
enum class ProjectFormat : quint32 {
    Current = 2, ///< 滤镜节点：op+enabled+固定 512B 参数块（含预留）
};

/**
 * 工程文件读写（io 层）。
 *
 * 【对照 GIMP】`app/xcf/xcf-save.c` / `xcf-load.c` 的瘦身版。
 *
 * 格式：单文件 `.pslite`（魔数 PSLT + QDataStream）
 * - 魔数、ProjectFormat、文档宽高、活动层
 * - 每层：名 / 显隐 / 不透明度 / 混合 / offset + PNG 像素（无瓦片则长度 0）
 *   + 样式栈 + 可选蒙版 + 链接路径 + kind + 滤镜栈
 *   （滤镜：count × {op, enabled, 固定长度参数 blob，尾部预留填 0}）
 * - 选区：灰度 PNG（空选区则长度 0）
 */
class ProjectIo
{
public:
    static constexpr quint32 kMagic = 0x50534C54; // 'PSLT'

    /** 写入工程文件；失败时 errorMessage 非空。 */
    static bool save(const ImageDocument &doc, const QString &filePath,
                     QString *errorMessage = nullptr);

    /** 读取工程文件；失败返回 nullptr。格式须为 ProjectFormat::Current。 */
    static std::unique_ptr<ImageDocument> load(const QString &filePath,
                                               QString *errorMessage = nullptr);

private:
    ProjectIo() = delete;
};

} // namespace Ps

#endif // PROJECTIO_H
