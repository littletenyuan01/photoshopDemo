#include "psdio.h"

#include "domain/blendmode.h"
#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "engine/compositor.h"

#include <QDataStream>
#include <QDebug>
#include <QImage>
#include <QSaveFile>
#include <QtEndian>

namespace Ps {
namespace {

/** PSD 文件头 / 通道常量（对照 Adobe PSD 规范与 GIMP psd-export）。 */
enum class PsdFileVersion : quint16 {
    Photoshop = 1,
};

enum class PsdColorMode : quint16 {
    Rgb = 3,
};

enum class PsdCompression : quint16 {
    Raw = 0,
};

enum class PsdChannelId : qint16 {
    Transparency = -1,
    Red = 0,
    Green = 1,
    Blue = 2,
};

enum class PsdLayerFlags : quint8 {
    None = 0,
    Invisible = 2, ///< bit1
};

constexpr quint16 kPsdBitDepth = 8;
constexpr quint16 kPsdMergedRgbChannels = 3;
constexpr quint16 kPsdLayerRgbaChannelCount = 4;

void writeRaw(QDataStream &out, const void *data, int len)
{
    out.writeRawData(reinterpret_cast<const char *>(data), len);
}

void writeU8(QDataStream &out, quint8 v) { out << v; }

void writeU16(QDataStream &out, quint16 v)
{
    const quint16 be = qToBigEndian(v);
    writeRaw(out, &be, 2);
}

void writeU32(QDataStream &out, quint32 v)
{
    const quint32 be = qToBigEndian(v);
    writeRaw(out, &be, 4);
}

void writeI16(QDataStream &out, qint16 v) { writeU16(out, quint16(v)); }
void writeI32(QDataStream &out, qint32 v) { writeU32(out, quint32(v)); }

/** Pascal 字符串，填充到 4 字节对齐（含长度字节）。 */
/**
 * PSD 混合四字符码：PS 的 27 种模式全表（Adobe PSD File Format 的 "Blend mode key"，
 * 对照 GIMP `plug-ins/file-psd/psd-export.c` 的 `blend_modes` 表）。
 *
 * 【坑】四字符码里有几个看名字猜不到的：颜色加深=idiv、颜色减淡=div、
 * 线性加深=lbrn、线性减淡=lddg、深色=dkCl、浅色=lgCl、排除=smud。
 * 表尾不加 `default`：新增 BlendMode 时编译器直接告警，避免导出**静默降级成 norm**
 * （那样 PS 里打开会少一个模式，且没有任何提示）。
 */
const char *psdBlendKey(BlendMode mode)
{
    switch (mode) {
    case BlendMode::Normal:      return "norm";
    case BlendMode::Dissolve:    return "diss";
    case BlendMode::Darken:      return "dark";
    case BlendMode::Multiply:    return "mul ";
    case BlendMode::ColorBurn:   return "idiv";
    case BlendMode::LinearBurn:  return "lbrn";
    case BlendMode::DarkerColor: return "dkCl";
    case BlendMode::Lighten:     return "lite";
    case BlendMode::Screen:      return "scrn";
    case BlendMode::ColorDodge:  return "div ";
    case BlendMode::LinearDodge: return "lddg";
    case BlendMode::LighterColor:return "lgCl";
    case BlendMode::Overlay:     return "over";
    case BlendMode::SoftLight:   return "sLit";
    case BlendMode::HardLight:   return "hLit";
    case BlendMode::VividLight:  return "vLit";
    case BlendMode::LinearLight: return "lLit";
    case BlendMode::PinLight:    return "pLit";
    case BlendMode::HardMix:     return "hMix";
    case BlendMode::Difference:  return "diff";
    case BlendMode::Exclusion:   return "smud";
    case BlendMode::Subtract:    return "fsub";
    case BlendMode::Divide:      return "fdiv";
    case BlendMode::Hue:         return "hue ";
    case BlendMode::Saturation:  return "sat ";
    case BlendMode::Color:       return "colr";
    case BlendMode::Luminosity:  return "lum ";
    }
    // 落到这里 = mode 是越界值（枚举被强转坏了）：不能装作没事
    qWarning("psdBlendKey: 未知混合模式 %d，按 norm 导出", int(mode));
    return "norm";
}

void writePascalName(QDataStream &out, const QString &name)
{
    QByteArray utf8 = name.toUtf8();
    if (utf8.size() > 255)
        utf8 = utf8.left(255);
    const int total = 1 + utf8.size();
    const int padded = (total + 3) & ~3;
    writeU8(out, quint8(utf8.size()));
    writeRaw(out, utf8.constData(), utf8.size());
    for (int i = total; i < padded; ++i)
        writeU8(out, 0);
}

/** 预乘 ARGB → 直通 RGBA 平面（层矩形内）。 */
struct LayerPlanes {
    QRect rect; // 文档坐标，right/bottom 为开区间语义用 width/height
    QByteArray a;
    QByteArray r;
    QByteArray g;
    QByteArray b;
};

LayerPlanes extractLayerPlanes(const Layer &layer)
{
    LayerPlanes p;
    const int lw = layer.width();
    const int lh = layer.height();
    p.rect = QRect(layer.offsetX(), layer.offsetY(), lw, lh);
    const int n = lw * lh;
    p.a.resize(n);
    p.r.resize(n);
    p.g.resize(n);
    p.b.resize(n);

    const QImage img = layer.materialize().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < lh; ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < lw; ++x) {
            const int i = y * lw + x;
            const QRgb px = line[x];
            const int aa = qAlpha(px);
            p.a[i] = char(aa);
            if (aa == 0) {
                p.r[i] = p.g[i] = p.b[i] = 0;
            } else if (aa == 255) {
                p.r[i] = char(qRed(px));
                p.g[i] = char(qGreen(px));
                p.b[i] = char(qBlue(px));
            } else {
                p.r[i] = char(qMin(255, (qRed(px) * 255 + aa / 2) / aa));
                p.g[i] = char(qMin(255, (qGreen(px) * 255 + aa / 2) / aa));
                p.b[i] = char(qMin(255, (qBlue(px) * 255 + aa / 2) / aa));
            }
        }
    }
    return p;
}

void writeChannelRaw(QDataStream &out, const QByteArray &plane)
{
    writeU16(out, static_cast<quint16>(PsdCompression::Raw));
    writeRaw(out, plane.constData(), plane.size());
}

QByteArray buildLayerAndMaskSection(const ImageDocument &doc, QString *errorMessage)
{
    QByteArray layerInfo;
    QDataStream li(&layerInfo, QIODevice::WriteOnly);

    const int n = doc.layers().count();
    writeI16(li, qint16(n)); // 正数：自底向顶

    QVector<LayerPlanes> planes;
    planes.reserve(n);

    // 图层记录（栈底 → 栈顶，与 PS 图层板底部向上一致）
    for (int i = 0; i < n; ++i) {
        const Layer *layer = doc.layers().layerAt(i);
        if (!layer) {
            if (errorMessage)
                *errorMessage = QObject::tr("图层 %1 无效").arg(i);
            return {};
        }
        LayerPlanes pl = extractLayerPlanes(*layer);
        planes.append(pl);

        const qint32 top = pl.rect.top();
        const qint32 left = pl.rect.left();
        const qint32 bottom = pl.rect.top() + pl.rect.height();
        const qint32 right = pl.rect.left() + pl.rect.width();
        writeI32(li, top);
        writeI32(li, left);
        writeI32(li, bottom);
        writeI32(li, right);

        writeU16(li, kPsdLayerRgbaChannelCount); // A+R+G+B
        // channel id + length（含 2 字节 compression）
        const quint32 chLen = 2 + quint32(pl.a.size());
        writeI16(li, static_cast<qint16>(PsdChannelId::Transparency));
        writeU32(li, chLen);
        writeI16(li, static_cast<qint16>(PsdChannelId::Red));
        writeU32(li, chLen);
        writeI16(li, static_cast<qint16>(PsdChannelId::Green));
        writeU32(li, chLen);
        writeI16(li, static_cast<qint16>(PsdChannelId::Blue));
        writeU32(li, chLen);

        writeRaw(li, "8BIM", 4);
        writeRaw(li, psdBlendKey(layer->blendMode()), 4);
        writeU8(li, quint8(qBound(0, int(layer->opacity() * 255.0 + 0.5), 255)));
        writeU8(li, 0); // clipping
        writeU8(li, layer->isVisible()
                        ? static_cast<quint8>(PsdLayerFlags::None)
                        : static_cast<quint8>(PsdLayerFlags::Invisible));
        writeU8(li, 0); // filler

        // extra: mask(0) + blending ranges(0) + name
        QByteArray extra;
        {
            QDataStream ex(&extra, QIODevice::WriteOnly);
            writeU32(ex, 0); // mask size
            writeU32(ex, 0); // blending ranges size
            writePascalName(ex, layer->name());
        }
        writeU32(li, quint32(extra.size()));
        writeRaw(li, extra.constData(), extra.size());
    }

    // 通道像素（与图层记录顺序相同）
    for (const LayerPlanes &pl : planes) {
        writeChannelRaw(li, pl.a);
        writeChannelRaw(li, pl.r);
        writeChannelRaw(li, pl.g);
        writeChannelRaw(li, pl.b);
    }

    if (li.status() != QDataStream::Ok) {
        if (errorMessage)
            *errorMessage = QObject::tr("图层段序列化失败");
        return {};
    }

    // Layer info 长度需偶数对齐
    if (layerInfo.size() & 1)
        layerInfo.append('\0');

    QByteArray section;
    QDataStream sec(&section, QIODevice::WriteOnly);
    writeU32(sec, quint32(layerInfo.size()));
    writeRaw(sec, layerInfo.constData(), layerInfo.size());
    writeU32(sec, 0); // global layer mask info length
    return section;
}

QByteArray buildMergedImageData(const ImageDocument &doc)
{
    QImage comp = Compositor::composite(doc).convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const int w = doc.width();
    const int h = doc.height();
    // 合成到白底，写 RGB 三平面（匹配 header channels=3）
    QByteArray r(w * h, 0), g(w * h, 0), b(w * h, 0);
    for (int y = 0; y < h; ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(comp.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            const int i = y * w + x;
            const QRgb px = line[x];
            const int aa = qAlpha(px);
            const int rr = qRed(px);
            const int gg = qGreen(px);
            const int bb = qBlue(px);
            // 白底：out = premul + (255-a)
            r[i] = char(qBound(0, rr + (255 - aa), 255));
            g[i] = char(qBound(0, gg + (255 - aa), 255));
            b[i] = char(qBound(0, bb + (255 - aa), 255));
        }
    }

    QByteArray data;
    QDataStream out(&data, QIODevice::WriteOnly);
    writeU16(out, 0); // raw
    writeRaw(out, r.constData(), r.size());
    writeRaw(out, g.constData(), g.size());
    writeRaw(out, b.constData(), b.size());
    return data;
}

} // namespace

bool PsdIo::save(const ImageDocument &doc, const QString &filePath,
                 QString *errorMessage)
{
    if (doc.width() <= 0 || doc.height() <= 0 || doc.layers().count() <= 0) {
        if (errorMessage)
            *errorMessage = QObject::tr("文档无效，无法导出 PSD");
        return false;
    }
    if (doc.width() > 30000 || doc.height() > 30000) {
        if (errorMessage)
            *errorMessage = QObject::tr("尺寸超出 PSD 限制");
        return false;
    }

    QString layerErr;
    const QByteArray layerSection = buildLayerAndMaskSection(doc, &layerErr);
    if (layerSection.isEmpty()) {
        if (errorMessage)
            *errorMessage = layerErr.isEmpty() ? QObject::tr("图层段为空") : layerErr;
        return false;
    }

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorMessage)
            *errorMessage = QObject::tr("无法写入：%1").arg(file.errorString());
        return false;
    }

    QDataStream out(&file);
    // 手写大端，不依赖 QDataStream 版本

    // —— File Header ——
    writeRaw(out, "8BPS", 4);
    writeU16(out, static_cast<quint16>(PsdFileVersion::Photoshop));
    writeU32(out, 0);
    writeU16(out, 0); // reserved 6 bytes
    writeU16(out, kPsdMergedRgbChannels);
    writeU32(out, quint32(doc.height()));
    writeU32(out, quint32(doc.width()));
    writeU16(out, kPsdBitDepth);
    writeU16(out, static_cast<quint16>(PsdColorMode::Rgb));

    writeU32(out, 0); // color mode data
    writeU32(out, 0); // image resources

    writeU32(out, quint32(layerSection.size()));
    writeRaw(out, layerSection.constData(), layerSection.size());

    const QByteArray merged = buildMergedImageData(doc);
    writeRaw(out, merged.constData(), merged.size());

    if (out.status() != QDataStream::Ok) {
        if (errorMessage)
            *errorMessage = QObject::tr("PSD 写入失败");
        return false;
    }
    if (!file.commit()) {
        if (errorMessage)
            *errorMessage = QObject::tr("无法提交文件：%1").arg(file.errorString());
        return false;
    }
    return true;
}

} // namespace Ps
