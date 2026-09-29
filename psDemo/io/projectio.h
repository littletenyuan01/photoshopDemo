#ifndef PROJECTIO_H
#define PROJECTIO_H

#include <QString>
#include <memory>

namespace Ps {

class ImageDocument;

/**
 * 工程文件读写（io 层）。
 *
 * 【对照 GIMP】`app/xcf/xcf-save.c` / `xcf-load.c` 的瘦身版：
 * GIMP 用 XCF 持久化图层/选区/路径等；本项目先做最小可编辑子集。
 *
 * 格式：单文件 `.pslite`（魔数 PSLT + QDataStream v1）
 * - 文档宽高、活动层
 * - 每层：名 / 显隐 / 不透明度 / 混合 / offset + PNG 像素
 * - 选区：灰度 PNG（空选区则长度 0）
 *
 * 不做：图层组、调整层、蒙版、路径、文字层（以后按需加字段版本）。
 */
class ProjectIo
{
public:
    static constexpr quint32 kMagic = 0x50534C54; // 'PSLT'
    static constexpr quint32 kVersion = 1;

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
