/**
 * projectio.cpp — ProjectIo::save/load 与 v1 混合模式兼容（io 层）。
 */
#include "projectio.h"

#include "domain/blendmode.h"
#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "domain/selection.h"

#include <QBuffer>
#include <QDataStream>
#include <QFile>
#include <QImage>
#include <QSaveFile>

namespace Ps {
namespace {

/**
 * v1 文件头里存的混合模式序（仅用于读旧工程，勿再扩展）。
 * 顺序：正常、正片叠底、滤色、叠加、柔光、强光、变暗、变亮、差值、排除。
 */
enum class LegacyBlendModeV1 {
    Normal = 0,
    Multiply,
    Screen,
    Overlay,
    SoftLight,
    HardLight,
    Darken,
    Lighten,
    Difference,
    Exclusion,
};

inline constexpr int kLegacyBlendModeV1Count =
    static_cast<int>(LegacyBlendModeV1::Exclusion) + 1;

/** v1 混合序 → 当前 `BlendMode`；非法值返回 false。 */
bool mapLegacyV1Blend(int legacy, BlendMode *out)
{
    if (!out || legacy < 0 || legacy >= kLegacyBlendModeV1Count)
        return false;

    switch (static_cast<LegacyBlendModeV1>(legacy)) {
    case LegacyBlendModeV1::Normal:
        *out = BlendMode::Normal;
        return true;
    case LegacyBlendModeV1::Multiply:
        *out = BlendMode::Multiply;
        return true;
    case LegacyBlendModeV1::Screen:
        *out = BlendMode::Screen;
        return true;
    case LegacyBlendModeV1::Overlay:
        *out = BlendMode::Overlay;
        return true;
    case LegacyBlendModeV1::SoftLight:
        *out = BlendMode::SoftLight;
        return true;
    case LegacyBlendModeV1::HardLight:
        *out = BlendMode::HardLight;
        return true;
    case LegacyBlendModeV1::Darken:
        *out = BlendMode::Darken;
        return true;
    case LegacyBlendModeV1::Lighten:
        *out = BlendMode::Lighten;
        return true;
    case LegacyBlendModeV1::Difference:
        *out = BlendMode::Difference;
        return true;
    case LegacyBlendModeV1::Exclusion:
        *out = BlendMode::Exclusion;
        return true;
    }
    return false;
}

bool isSupportedProjectVersion(ProjectFileVersion version)
{
    switch (version) {
    case ProjectFileVersion::V1:
    case ProjectFileVersion::V2:
        return true;
    }
    return false;
}

QByteArray imageToPngBytes(const QImage &image)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    // 灰度 mask 原样；图层预乘转 ARGB32 再存，读回后 TileBuffer 再转预乘
    QImage out = image;
    if (out.format() != QImage::Format_Grayscale8)
        out = out.convertToFormat(QImage::Format_ARGB32);
    if (!out.save(&buffer, "PNG"))
        return {};
    return bytes;
}

QImage pngBytesToImage(const QByteArray &bytes)
{
    QImage image;
    if (!image.loadFromData(bytes, "PNG"))
        return {};
    return image;
}

bool writeBytes(QDataStream &out, const QByteArray &bytes)
{
    if (bytes.size() > 512 * 1024 * 1024) // 单块硬上限，防坏文件
        return false;
    out << quint32(bytes.size());
    if (out.writeRawData(bytes.constData(), bytes.size()) != bytes.size())
        return false;
    return out.status() == QDataStream::Ok;
}

bool readBytes(QDataStream &in, QByteArray *bytes)
{
    quint32 size = 0;
    in >> size;
    if (in.status() != QDataStream::Ok || size > 512u * 1024u * 1024u)
        return false;
    bytes->resize(int(size));
    if (size == 0)
        return true;
    if (in.readRawData(bytes->data(), int(size)) != int(size))
        return false;
    return in.status() == QDataStream::Ok;
}

} // namespace

bool ProjectIo::save(const ImageDocument &doc, const QString &filePath,
                     QString *errorMessage)
{
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorMessage)
            *errorMessage = QObject::tr("无法写入：%1").arg(file.errorString());
        return false;
    }

    QDataStream out(&file);
    out.setVersion(QDataStream::Qt_6_0);
    out << kMagic << static_cast<quint32>(kCurrentProjectVersion);
    out << qint32(doc.width()) << qint32(doc.height());
    out << qint32(doc.activeLayerIndex());
    out << qint32(doc.layers().count());

    for (int i = 0; i < doc.layers().count(); ++i) {
        const Layer *layer = doc.layers().layerAt(i);
        if (!layer) {
            if (errorMessage)
                *errorMessage = QObject::tr("图层 %1 无效").arg(i);
            return false;
        }
        out << layer->name();
        out << quint8(layer->isVisible() ? 1 : 0);
        out << double(layer->opacity());
        out << qint32(static_cast<int>(layer->blendMode()));
        out << qint32(layer->offsetX()) << qint32(layer->offsetY());

        const QByteArray png = imageToPngBytes(layer->materialize());
        if (png.isEmpty()) {
            if (errorMessage)
                *errorMessage = QObject::tr("图层「%1」像素编码失败").arg(layer->name());
            return false;
        }
        if (!writeBytes(out, png)) {
            if (errorMessage)
                *errorMessage = QObject::tr("写入图层像素失败");
            return false;
        }
    }

    // 选区：空则长度 0（对照 XCF 仅在非空时写 selection channel）
    QByteArray selPng;
    if (!doc.selection().isEmpty())
        selPng = imageToPngBytes(doc.selection().mask());
    if (!writeBytes(out, selPng)) {
        if (errorMessage)
            *errorMessage = QObject::tr("写入选区失败");
        return false;
    }

    if (out.status() != QDataStream::Ok) {
        if (errorMessage)
            *errorMessage = QObject::tr("序列化失败");
        return false;
    }
    if (!file.commit()) {
        if (errorMessage)
            *errorMessage = QObject::tr("无法提交文件：%1").arg(file.errorString());
        return false;
    }
    return true;
}

std::unique_ptr<ImageDocument> ProjectIo::load(const QString &filePath,
                                               QString *errorMessage)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage)
            *errorMessage = QObject::tr("无法打开：%1").arg(file.errorString());
        return nullptr;
    }

    QDataStream in(&file);
    in.setVersion(QDataStream::Qt_6_0);
    quint32 magic = 0;
    quint32 versionRaw = 0;
    in >> magic >> versionRaw;
    if (magic != kMagic) {
        if (errorMessage)
            *errorMessage = QObject::tr("不是 PhotoshopLite 工程文件（魔数不匹配）");
        return nullptr;
    }
    const auto fileVersion = static_cast<ProjectFileVersion>(versionRaw);
    if (!isSupportedProjectVersion(fileVersion)) {
        if (errorMessage)
            *errorMessage = QObject::tr("不支持的工程版本：%1（当前为 %2）")
                                .arg(versionRaw)
                                .arg(static_cast<quint32>(kCurrentProjectVersion));
        return nullptr;
    }
    const bool legacyV1Blend = (fileVersion == ProjectFileVersion::V1);

    qint32 width = 0;
    qint32 height = 0;
    qint32 activeIndex = 0;
    qint32 layerCount = 0;
    in >> width >> height >> activeIndex >> layerCount;
    if (in.status() != QDataStream::Ok
        || width <= 0 || height <= 0 || width > 32768 || height > 32768
        || layerCount <= 0 || layerCount > 4096) {
        if (errorMessage)
            *errorMessage = QObject::tr("工程文件头损坏");
        return nullptr;
    }

    auto doc = std::make_unique<ImageDocument>(width, height);
    ImageDocument::HistorySuppress suppress(*doc);

    for (int i = 0; i < layerCount; ++i) {
        QString name;
        quint8 visible = 1;
        double opacity = 1.0;
        qint32 blend = 0;
        qint32 ox = 0;
        qint32 oy = 0;
        in >> name >> visible >> opacity >> blend >> ox >> oy;
        QByteArray png;
        if (!readBytes(in, &png) || in.status() != QDataStream::Ok) {
            if (errorMessage)
                *errorMessage = QObject::tr("读取图层 %1 失败").arg(i);
            return nullptr;
        }
        const QImage pixels = pngBytesToImage(png);
        if (pixels.isNull()) {
            if (errorMessage)
                *errorMessage = QObject::tr("图层 %1 像素解码失败").arg(i);
            return nullptr;
        }

        auto layer = std::make_unique<Layer>(name.isEmpty()
                                                 ? QObject::tr("图层 %1").arg(i + 1)
                                                 : name,
                                             pixels);
        layer->setVisible(visible != 0);
        layer->setOpacity(qreal(opacity));
        // v1：先映射旧 10 种序；v2：整数即当前枚举。越界 = 坏文件，拒绝静默退化。
        BlendMode mode = BlendMode::Normal;
        if (legacyV1Blend) {
            if (!mapLegacyV1Blend(blend, &mode)) {
                if (errorMessage)
                    *errorMessage = QObject::tr("图层 %1 的混合模式无效：%2").arg(i).arg(blend);
                return nullptr;
            }
        } else {
            if (!isValidBlendMode(blend)) {
                if (errorMessage)
                    *errorMessage = QObject::tr("图层 %1 的混合模式无效：%2").arg(i).arg(blend);
                return nullptr;
            }
            mode = static_cast<BlendMode>(blend);
        }
        layer->setBlendMode(mode);
        layer->setOffsetSilent(ox, oy);
        doc->addLayer(std::move(layer));
    }

    QByteArray selPng;
    if (!readBytes(in, &selPng) || in.status() != QDataStream::Ok) {
        if (errorMessage)
            *errorMessage = QObject::tr("读取选区失败");
        return nullptr;
    }
    if (!selPng.isEmpty()) {
        const QImage mask = pngBytesToImage(selPng);
        if (!mask.isNull())
            doc->replaceSelectionMask(mask);
    }

    if (activeIndex < 0 || activeIndex >= doc->layers().count())
        activeIndex = doc->layers().count() - 1;
    doc->setActiveLayerIndex(activeIndex);
    doc->setFilePath(filePath);
    doc->clearDirty();
    return doc;
}

} // namespace Ps
