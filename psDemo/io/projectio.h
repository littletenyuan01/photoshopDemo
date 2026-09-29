#ifndef PROJECTIO_H
#define PROJECTIO_H

#include <QString>
#include <QtGlobal>
#include <memory>

namespace Ps {

class ImageDocument;

/**
 * `.pslite` 文件格式版本（写入文件头的整数 = 枚举值）。
 *
 * - V1：初版；混合模式 10 种、旧枚举序
 * - V2：`BlendMode` 扩到 PS 27 种且重排；存当前枚举整数
 *
 * 读：接受 V1（映射混合）与 V2；保存始终写 `kCurrentProjectVersion`。
 */
enum class ProjectFileVersion : quint32 {
    V1 = 1,
    V2 = 2,
};

/** 当前写入用的工程版本。 */
inline constexpr ProjectFileVersion kCurrentProjectVersion = ProjectFileVersion::V2;

/**
 * 工程文件读写（io 层）。
 *
 * 【对照 GIMP】`app/xcf/xcf-save.c` / `xcf-load.c` 的瘦身版：
 * GIMP 用 XCF 持久化图层/选区/路径等；本项目先做最小可编辑子集。
 *
 * 格式：单文件 `.pslite`（魔数 PSLT + QDataStream）
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
