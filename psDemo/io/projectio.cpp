/**
 * projectio.cpp — ProjectIo::save/load（io 层）。
 *
 * 只读写 ProjectFormat::Current；不做旧版兼容。
 * 滤镜参数写入固定 kFilterNodeParamBytes 块：已用字段从前排，尾部预留 0；
 * 后续加 FilterNode 字段只占用预留，勿再无长度追加（用尽再改 ProjectFormat）。
 */
#include "projectio.h"

#include "domain/blendmode.h"
#include "domain/filternode.h"
#include "domain/filterstack.h"
#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "domain/layermask.h"
#include "domain/layerstyle.h"
#include "domain/selection.h"
#include "engine/op/opname.h"
#include "io/rasterio.h"

#include <QBuffer>
#include <QColor>
#include <QDataStream>
#include <QFile>
#include <QImage>
#include <QSaveFile>

namespace Ps {
namespace {

bool writeLayerStyles(QDataStream &out, const LayerStyleStack &styles)
{
    out << qint32(styles.count());
    for (int i = 0; i < styles.count(); ++i) {
        const LayerStyleEffect &e = styles.at(i);
        out << qint32(LayerStyleEffect::kindId(e.kind()));
        out << quint8(e.isEnabled() ? 1 : 0);
        out << double(e.opacity());
        out << qint32(e.color().red()) << qint32(e.color().green())
            << qint32(e.color().blue()) << qint32(e.color().alpha());
        out << double(e.angle()) << double(e.distance())
            << double(e.size()) << double(e.spread());
    }
    return out.status() == QDataStream::Ok;
}

bool readLayerStyles(QDataStream &in, LayerStyleStack *styles)
{
    if (!styles)
        return false;
    styles->clear();
    qint32 count = 0;
    in >> count;
    if (in.status() != QDataStream::Ok || count < 0 || count > 64)
        return false;
    for (int i = 0; i < count; ++i) {
        qint32 kindId = 0;
        quint8 enabled = 1;
        double opacity = 1.0;
        qint32 r = 0, g = 0, b = 0, a = 255;
        double angle = 120.0, distance = 5.0, size = 5.0, spread = 0.0;
        in >> kindId >> enabled >> opacity >> r >> g >> b >> a
           >> angle >> distance >> size >> spread;
        if (in.status() != QDataStream::Ok)
            return false;
        LayerStyleKind kind = LayerStyleKind::DropShadow;
        if (!LayerStyleEffect::kindFromId(kindId, &kind))
            continue;
        LayerStyleEffect e = LayerStyleEffect::makeDefault(kind);
        e.setEnabled(enabled != 0);
        e.setOpacity(opacity);
        e.setColor(QColor(r, g, b, a));
        e.setAngle(angle);
        e.setDistance(distance);
        e.setSize(size);
        e.setSpread(spread);
        styles->append(e);
    }
    return true;
}

/** 每个滤镜节点参数块固定长度；已用字段从前写，尾部预留填 0。用尽前勿改此常量。 */
constexpr int kFilterNodeParamBytes = 512;

void writeFilterNodeParams(QDataStream &p, const FilterNode &n)
{
    p << double(n.brightness()) << double(n.contrast());
    p << double(n.hue()) << double(n.saturation()) << double(n.lightness())
      << double(n.vibrance());
    p << double(n.exposure()) << double(n.exposureOffset())
      << double(n.gammaCorrection());
    p << double(n.levelsBlack()) << double(n.levelsWhite()) << double(n.levelsGamma());
    p << double(n.colorBalanceCR()) << double(n.colorBalanceMG())
      << double(n.colorBalanceYB());
    p << double(n.photoFilterHue()) << double(n.photoFilterDensity());
    p << double(n.bwReds()) << double(n.bwYellows()) << double(n.bwGreens())
      << double(n.bwCyans()) << double(n.bwBlues()) << double(n.bwMagentas());
    p << qint32(n.posterizeLevels()) << qint32(n.threshold());
    p << qint32(n.curveY0()) << qint32(n.curveY1()) << qint32(n.curveY2())
      << qint32(n.curveY3()) << qint32(n.curveY4());
    p << double(n.mixRr()) << double(n.mixRg()) << double(n.mixRb())
      << double(n.mixGr()) << double(n.mixGg()) << double(n.mixGb())
      << double(n.mixBr()) << double(n.mixBg()) << double(n.mixBb());
    p << quint8(n.mixMonochrome() ? 1 : 0);
    p << qint32(n.colorLookupPreset());
    p << quint32(n.gradientMapColorA()) << quint32(n.gradientMapColorB())
      << double(n.gradientMapStrength());
    p << qint32(n.selectiveColorTarget())
      << double(n.selectiveCyan()) << double(n.selectiveMagenta())
      << double(n.selectiveYellow()) << double(n.selectiveBlack());
    // —— 预留：后续新参数追加在此注释之后、勿改已有顺序 ——
}

bool readFilterNodeParams(QDataStream &p, FilterNode *n)
{
    if (!n)
        return false;
    double brightness = 0.0, contrast = 0.0;
    double hue = 0.0, saturation = 0.0, lightness = 0.0, vibrance = 0.0;
    double exposure = 0.0, exposureOffset = 0.0, gammaCorrection = 1.0;
    double levelsBlack = 0.0, levelsWhite = 255.0, levelsGamma = 1.0;
    double cbCR = 0.0, cbMG = 0.0, cbYB = 0.0;
    double photoHue = 35.0, photoDensity = 25.0;
    double bwR = 40, bwY = 60, bwG = 40, bwC = 60, bwB = 20, bwM = 80;
    qint32 posterize = 4, threshold = 128;
    qint32 cy0 = 0, cy1 = 64, cy2 = 128, cy3 = 192, cy4 = 255;
    double mixRr = 100, mixRg = 0, mixRb = 0, mixGr = 0, mixGg = 100, mixGb = 0;
    double mixBr = 0, mixBg = 0, mixBb = 100;
    quint8 mixMono = 0;
    qint32 lutPreset = 0;
    quint32 gradA = 0xff000000, gradB = 0xffffffff;
    double gradStr = 100.0;
    qint32 selTarget = 0;
    double selC = 0, selM = 0, selY = 0, selK = 0;
    p >> brightness >> contrast;
    p >> hue >> saturation >> lightness >> vibrance;
    p >> exposure >> exposureOffset >> gammaCorrection;
    p >> levelsBlack >> levelsWhite >> levelsGamma;
    p >> cbCR >> cbMG >> cbYB;
    p >> photoHue >> photoDensity;
    p >> bwR >> bwY >> bwG >> bwC >> bwB >> bwM;
    p >> posterize >> threshold;
    p >> cy0 >> cy1 >> cy2 >> cy3 >> cy4;
    p >> mixRr >> mixRg >> mixRb >> mixGr >> mixGg >> mixGb >> mixBr >> mixBg >> mixBb;
    p >> mixMono >> lutPreset;
    p >> gradA >> gradB >> gradStr;
    p >> selTarget >> selC >> selM >> selY >> selK;
    if (p.status() != QDataStream::Ok)
        return false;
    n->setBrightness(brightness);
    n->setContrast(contrast);
    n->setHue(hue);
    n->setSaturation(saturation);
    n->setLightness(lightness);
    n->setVibrance(vibrance);
    n->setExposure(exposure);
    n->setExposureOffset(exposureOffset);
    n->setGammaCorrection(gammaCorrection);
    n->setLevelsBlack(levelsBlack);
    n->setLevelsWhite(levelsWhite);
    n->setLevelsGamma(levelsGamma);
    n->setColorBalanceCR(cbCR);
    n->setColorBalanceMG(cbMG);
    n->setColorBalanceYB(cbYB);
    n->setPhotoFilterHue(photoHue);
    n->setPhotoFilterDensity(photoDensity);
    n->setBwReds(bwR);
    n->setBwYellows(bwY);
    n->setBwGreens(bwG);
    n->setBwCyans(bwC);
    n->setBwBlues(bwB);
    n->setBwMagentas(bwM);
    n->setPosterizeLevels(int(posterize));
    n->setThreshold(int(threshold));
    n->setCurveY0(int(cy0));
    n->setCurveY1(int(cy1));
    n->setCurveY2(int(cy2));
    n->setCurveY3(int(cy3));
    n->setCurveY4(int(cy4));
    n->setMixRr(mixRr);
    n->setMixRg(mixRg);
    n->setMixRb(mixRb);
    n->setMixGr(mixGr);
    n->setMixGg(mixGg);
    n->setMixGb(mixGb);
    n->setMixBr(mixBr);
    n->setMixBg(mixBg);
    n->setMixBb(mixBb);
    n->setMixMonochrome(mixMono != 0);
    n->setColorLookupPreset(int(lutPreset));
    n->setGradientMapColorA(gradA);
    n->setGradientMapColorB(gradB);
    n->setGradientMapStrength(gradStr);
    n->setSelectiveColorTarget(int(selTarget));
    n->setSelectiveCyan(selC);
    n->setSelectiveMagenta(selM);
    n->setSelectiveYellow(selY);
    n->setSelectiveBlack(selK);
    return true;
}

bool writeLayerFilters(QDataStream &out, const FilterStack &filters)
{
    out << qint32(filters.count());
    for (int i = 0; i < filters.count(); ++i) {
        const FilterNode &n = filters.at(i);
        out << qint32(static_cast<int>(n.op()));
        out << quint8(n.isEnabled() ? 1 : 0);

        QByteArray blob(kFilterNodeParamBytes, '\0');
        {
            QBuffer buf(&blob);
            if (!buf.open(QIODevice::ReadWrite))
                return false;
            QDataStream p(&buf);
            p.setVersion(out.version());
            p.setByteOrder(out.byteOrder());
            writeFilterNodeParams(p, n);
            if (p.status() != QDataStream::Ok)
                return false;
            if (buf.pos() > kFilterNodeParamBytes)
                return false; // 已用字段超过预留块：加大 kFilterNodeParamBytes 并改 Current
        }
        if (blob.size() != kFilterNodeParamBytes)
            blob.resize(kFilterNodeParamBytes);
        out << blob;
    }
    return out.status() == QDataStream::Ok;
}

bool readLayerFilters(QDataStream &in, FilterStack *filters)
{
    if (!filters)
        return false;
    filters->replaceAll({});
    qint32 count = 0;
    in >> count;
    if (in.status() != QDataStream::Ok || count < 0 || count > 64)
        return false;
    QVector<FilterNode> nodes;
    nodes.reserve(count);
    for (int i = 0; i < count; ++i) {
        qint32 opId = 0;
        quint8 enabled = 1;
        QByteArray blob;
        in >> opId >> enabled >> blob;
        if (in.status() != QDataStream::Ok)
            return false;
        if (blob.size() != kFilterNodeParamBytes)
            return false;
        if (opId < 0 || opId >= int(OpName::Count))
            continue;

        FilterNode n(static_cast<OpName>(opId));
        n.setEnabled(enabled != 0);
        {
            QBuffer buf(&blob);
            if (!buf.open(QIODevice::ReadOnly))
                return false;
            QDataStream p(&buf);
            p.setVersion(in.version());
            p.setByteOrder(in.byteOrder());
            if (!readFilterNodeParams(p, &n))
                return false;
        }
        nodes.append(n);
    }
    filters->replaceAll(nodes);
    return true;
}

QByteArray imageToPngBytes(const QImage &image)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
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
    if (bytes.size() > 512 * 1024 * 1024)
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

std::unique_ptr<ImageDocument> loadDocumentBody(QDataStream &in,
                                                const QString &filePath,
                                                QString *errorMessage)
{
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

        const QString layerName = name.isEmpty()
                                      ? QObject::tr("图层 %1").arg(i + 1)
                                      : name;
        // 空 PNG = 无瓦片壳层（调整层 / 透明新建）
        std::unique_ptr<Layer> layer;
        if (png.isEmpty()) {
            layer = std::make_unique<Layer>(layerName, width, height);
        } else {
            const QImage pixels = pngBytesToImage(png);
            if (pixels.isNull()) {
                if (errorMessage)
                    *errorMessage = QObject::tr("图层 %1 像素解码失败").arg(i);
                return nullptr;
            }
            layer = std::make_unique<Layer>(layerName, pixels);
        }
        layer->setVisible(visible != 0);
        layer->setOpacity(qreal(opacity));

        if (!isValidBlendMode(blend)) {
            if (errorMessage)
                *errorMessage = QObject::tr("图层 %1 的混合模式无效：%2").arg(i).arg(blend);
            return nullptr;
        }
        layer->setBlendMode(static_cast<BlendMode>(blend));
        layer->setOffsetSilent(ox, oy);

        if (!readLayerStyles(in, &layer->styles()) || in.status() != QDataStream::Ok) {
            if (errorMessage)
                *errorMessage = QObject::tr("读取图层 %1 样式失败").arg(i);
            return nullptr;
        }

        quint8 hasMask = 0;
        in >> hasMask;
        if (in.status() != QDataStream::Ok) {
            if (errorMessage)
                *errorMessage = QObject::tr("读取图层 %1 蒙版标志失败").arg(i);
            return nullptr;
        }
        if (hasMask) {
            quint8 enabled = 1;
            quint8 linked = 1;
            in >> enabled >> linked;
            QByteArray maskPng;
            if (!readBytes(in, &maskPng) || in.status() != QDataStream::Ok) {
                if (errorMessage)
                    *errorMessage = QObject::tr("读取图层 %1 蒙版失败").arg(i);
                return nullptr;
            }
            const QImage gray = pngBytesToImage(maskPng);
            if (!gray.isNull()) {
                auto mask = std::make_unique<LayerMask>();
                mask->setFromImage(gray);
                mask->setEnabled(enabled != 0);
                mask->setLinked(linked != 0);
                layer->setMask(std::move(mask));
            }
        }

        QString linkPath;
        in >> linkPath;
        if (in.status() != QDataStream::Ok) {
            if (errorMessage)
                *errorMessage = QObject::tr("读取图层 %1 链接路径失败").arg(i);
            return nullptr;
        }
        if (!linkPath.isEmpty()) {
            layer->setLinkPathSilent(linkPath);
            const QImage fresh = RasterIo::readFile(linkPath);
            if (!fresh.isNull()) {
                const int oxKeep = layer->offsetX();
                const int oyKeep = layer->offsetY();
                layer->replaceFromImage(fresh);
                layer->setOffsetSilent(oxKeep, oyKeep);
            }
        }

        quint8 kindByte = 0;
        in >> kindByte;
        if (in.status() != QDataStream::Ok) {
            if (errorMessage)
                *errorMessage = QObject::tr("读取图层 %1 类型失败").arg(i);
            return nullptr;
        }
        LayerKind kind = LayerKind::Raster;
        if (kindByte == quint8(LayerKind::Adjustment))
            kind = LayerKind::Adjustment;
        layer->setKindSilent(kind);
        if (!readLayerFilters(in, &layer->filters()) || in.status() != QDataStream::Ok) {
            if (errorMessage)
                *errorMessage = QObject::tr("读取图层 %1 滤镜失败").arg(i);
            return nullptr;
        }

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
    out << kMagic << quint32(ProjectFormat::Current);
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

        QByteArray png;
        if (layer->hasPixelData()) {
            png = imageToPngBytes(layer->materialize());
            if (png.isEmpty()) {
                if (errorMessage)
                    *errorMessage = QObject::tr("图层「%1」像素编码失败").arg(layer->name());
                return false;
            }
        }
        if (!writeBytes(out, png)) {
            if (errorMessage)
                *errorMessage = QObject::tr("写入图层像素失败");
            return false;
        }
        if (!writeLayerStyles(out, layer->styles())) {
            if (errorMessage)
                *errorMessage = QObject::tr("写入图层样式失败");
            return false;
        }

        const bool hasMask = layer->hasMask() && layer->mask()
                             && !layer->mask()->isNull();
        out << quint8(hasMask ? 1 : 0);
        if (hasMask) {
            out << quint8(layer->mask()->isEnabled() ? 1 : 0);
            out << quint8(layer->mask()->isLinked() ? 1 : 0);
            const QByteArray maskPng = imageToPngBytes(layer->mask()->image());
            if (maskPng.isEmpty() || !writeBytes(out, maskPng)) {
                if (errorMessage)
                    *errorMessage = QObject::tr("写入图层蒙版失败");
                return false;
            }
        }

        out << layer->linkPath();
        out << quint8(layer->kind());
        if (!writeLayerFilters(out, layer->filters())) {
            if (errorMessage)
                *errorMessage = QObject::tr("写入图层滤镜失败");
            return false;
        }
    }

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
    quint32 formatRaw = 0;
    in >> magic >> formatRaw;
    if (magic != kMagic) {
        if (errorMessage)
            *errorMessage = QObject::tr("不是 PhotoshopLite 工程文件（魔数不匹配）");
        return nullptr;
    }
    if (formatRaw != quint32(ProjectFormat::Current)) {
        if (errorMessage)
            *errorMessage = QObject::tr("工程格式不匹配（当前仅支持 ProjectFormat::Current）");
        return nullptr;
    }

    return loadDocumentBody(in, filePath, errorMessage);
}

} // namespace Ps
