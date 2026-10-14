/**
 * psdio.cpp — PsdIo::save / load（io 层）。
 *
 * 手写大端读写；导出：预乘 ARGB → 直通 RGBA 平面；导入：直通 → 预乘。
 */
#include "psdio.h"

#include "domain/blendmode.h"
#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "engine/compositor.h"

#include <QDataStream>
#include <QDebug>
#include <QFile>
#include <QImage>
#include <QSaveFile>
#include <QtEndian>

#include <cstring>
#include <limits>

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
    Rle = 1,
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

bool readRaw(QDataStream &in, void *data, int len)
{
    return in.readRawData(reinterpret_cast<char *>(data), len) == len;
}

bool readU8(QDataStream &in, quint8 *v)
{
    return readRaw(in, v, 1);
}

bool readU16(QDataStream &in, quint16 *v)
{
    quint16 be = 0;
    if (!readRaw(in, &be, 2))
        return false;
    *v = qFromBigEndian(be);
    return true;
}

bool readU32(QDataStream &in, quint32 *v)
{
    quint32 be = 0;
    if (!readRaw(in, &be, 4))
        return false;
    *v = qFromBigEndian(be);
    return true;
}

bool readI16(QDataStream &in, qint16 *v)
{
    quint16 u = 0;
    if (!readU16(in, &u))
        return false;
    *v = qint16(u);
    return true;
}

bool readI32(QDataStream &in, qint32 *v)
{
    quint32 u = 0;
    if (!readU32(in, &u))
        return false;
    *v = qint32(u);
    return true;
}

bool skipBytes(QDataStream &in, int n)
{
    if (n <= 0)
        return true;
    return in.skipRawData(n) == n;
}

/** PackBits（PSD RLE）解到恰好 dstLen 字节。 */
bool decodePackBits(const char *src, int srcLen, char *dst, int dstLen)
{
    int si = 0;
    int di = 0;
    while (di < dstLen) {
        if (si >= srcLen)
            return false;
        const qint8 n = qint8(src[si++]);
        if (n >= 0) {
            const int count = int(n) + 1;
            if (si + count > srcLen || di + count > dstLen)
                return false;
            std::memcpy(dst + di, src + si, size_t(count));
            si += count;
            di += count;
        } else if (n != -128) {
            const int count = 1 - int(n);
            if (si >= srcLen || di + count > dstLen)
                return false;
            const char b = src[si++];
            std::memset(dst + di, int(uchar(b)), size_t(count));
            di += count;
        }
    }
    return true;
}

/**
 * 读一层某一通道的像素平面（含 2 字节 compression 头）。
 * @param expectedBytes 未压缩平面字节数（w*h）；0 表示空层，只消耗 compression。
 */
bool readChannelPlane(QDataStream &in, int expectedBytes, int height, QByteArray *out)
{
    quint16 comp = 0;
    if (!readU16(in, &comp))
        return false;

    if (expectedBytes <= 0) {
        out->clear();
        return true;
    }

    out->resize(expectedBytes);
    if (comp == static_cast<quint16>(PsdCompression::Raw)) {
        return readRaw(in, out->data(), expectedBytes);
    }
    if (comp != static_cast<quint16>(PsdCompression::Rle)) {
        // Zip 等：本子集不支持
        return false;
    }
    if (height <= 0)
        return false;

    QVector<quint16> rowLens(height);
    int compressedTotal = 0;
    for (int y = 0; y < height; ++y) {
        if (!readU16(in, &rowLens[y]))
            return false;
        compressedTotal += int(rowLens[y]);
    }
    QByteArray compressed(compressedTotal, Qt::Uninitialized);
    if (compressedTotal > 0 && !readRaw(in, compressed.data(), compressedTotal))
        return false;

    const int width = expectedBytes / height;
    if (width * height != expectedBytes)
        return false;

    int off = 0;
    for (int y = 0; y < height; ++y) {
        const int len = int(rowLens[y]);
        if (off + len > compressedTotal)
            return false;
        if (!decodePackBits(compressed.constData() + off, len,
                            out->data() + y * width, width))
            return false;
        off += len;
    }
    return true;
}

/** PSD 混合四字符码：PS 的 27 种模式全表（Adobe PSD File Format 的 "Blend mode key"，
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

BlendMode blendModeFromPsdKey(const char key[4])
{
    const QByteArray k = QByteArray(key, 4);
    if (k == "norm") return BlendMode::Normal;
    if (k == "diss") return BlendMode::Dissolve;
    if (k == "dark") return BlendMode::Darken;
    if (k == "mul ") return BlendMode::Multiply;
    if (k == "idiv") return BlendMode::ColorBurn;
    if (k == "lbrn") return BlendMode::LinearBurn;
    if (k == "dkCl") return BlendMode::DarkerColor;
    if (k == "lite") return BlendMode::Lighten;
    if (k == "scrn") return BlendMode::Screen;
    if (k == "div ") return BlendMode::ColorDodge;
    if (k == "lddg") return BlendMode::LinearDodge;
    if (k == "lgCl") return BlendMode::LighterColor;
    if (k == "over") return BlendMode::Overlay;
    if (k == "sLit") return BlendMode::SoftLight;
    if (k == "hLit") return BlendMode::HardLight;
    if (k == "vLit") return BlendMode::VividLight;
    if (k == "lLit") return BlendMode::LinearLight;
    if (k == "pLit") return BlendMode::PinLight;
    if (k == "hMix") return BlendMode::HardMix;
    if (k == "diff") return BlendMode::Difference;
    if (k == "smud") return BlendMode::Exclusion;
    if (k == "fsub") return BlendMode::Subtract;
    if (k == "fdiv") return BlendMode::Divide;
    if (k == "hue ") return BlendMode::Hue;
    if (k == "sat ") return BlendMode::Saturation;
    if (k == "colr") return BlendMode::Color;
    if (k == "lum ") return BlendMode::Luminosity;
    return BlendMode::Normal;
}

/** Pascal 字符串，填充到 4 字节对齐（含长度字节）。 */
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

/** 读 Pascal 名（含长度字节的 4 对齐）；@p maxRemain 为剩余 extra 字节上限。 */
bool readPascalName(QDataStream &in, int maxRemain, QString *name, int *consumed)
{
    if (maxRemain < 1)
        return false;
    quint8 len = 0;
    if (!readU8(in, &len))
        return false;
    const int total = 1 + int(len);
    const int padded = (total + 3) & ~3;
    if (padded > maxRemain)
        return false;
    QByteArray utf8(int(len), Qt::Uninitialized);
    if (len > 0 && !readRaw(in, utf8.data(), int(len)))
        return false;
    if (!skipBytes(in, padded - total))
        return false;
    *name = QString::fromUtf8(utf8);
    *consumed = padded;
    return true;
}

/** 预乘 ARGB → 直通 RGBA 平面（层矩形内）。 */
struct LayerPlanes {
    QRect rect;     ///< 文档坐标层边界
    QByteArray a;   ///< Alpha 平面（直通，非预乘）
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

namespace {

struct PsdChannelDesc {
    qint16 id = 0;
    quint32 dataLength = 0; ///< 含 2 字节 compression
};

struct PsdLayerRecord {
    qint32 top = 0;
    qint32 left = 0;
    qint32 bottom = 0;
    qint32 right = 0;
    QVector<PsdChannelDesc> channels;
    BlendMode blend = BlendMode::Normal;
    quint8 opacity = 255;
    quint8 flags = 0;
    QString name;
    bool skip = false; ///< 组/空层/无 RGB：不入栈
    QImage pixels;     ///< 预乘 ARGB；skip 时为空
};

QImage planesToPremulImage(int w, int h,
                           const QByteArray &a, const QByteArray &r,
                           const QByteArray &g, const QByteArray &b)
{
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    const int n = w * h;
    const bool hasA = a.size() >= n;
    const bool hasR = r.size() >= n;
    const bool hasG = g.size() >= n;
    const bool hasB = b.size() >= n;
    for (int y = 0; y < h; ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const int i = y * w + x;
            const int aa = hasA ? int(uchar(a[i])) : 255;
            const int rr = hasR ? int(uchar(r[i])) : 0;
            const int gg = hasG ? int(uchar(g[i])) : 0;
            const int bb = hasB ? int(uchar(b[i])) : 0;
            if (aa == 0)
                line[x] = qRgba(0, 0, 0, 0);
            else if (aa == 255)
                line[x] = qRgba(rr, gg, bb, 255);
            else
                line[x] = qRgba((rr * aa + 127) / 255,
                                (gg * aa + 127) / 255,
                                (bb * aa + 127) / 255,
                                aa);
        }
    }
    return img;
}

/** 读合并图像段（无图层时的回退）；失败返回空图。 */
QImage readMergedRgbFallback(QDataStream &in, int w, int h, int channelCount)
{
    if (w <= 0 || h <= 0 || channelCount < 3)
        return {};
    quint16 comp = 0;
    if (!readU16(in, &comp))
        return {};

    const int planeBytes = w * h;
    QByteArray r(planeBytes, 0), g(planeBytes, 0), b(planeBytes, 0);

    auto readOnePlane = [&](QByteArray *plane) -> bool {
        if (comp == static_cast<quint16>(PsdCompression::Raw))
            return readRaw(in, plane->data(), planeBytes);
        if (comp != static_cast<quint16>(PsdCompression::Rle))
            return false;
        // 合并图 RLE：先全部通道的行长度表，再数据——此处简化只支持 Raw
        return false;
    };

    if (comp == static_cast<quint16>(PsdCompression::Raw)) {
        if (!readOnePlane(&r) || !readOnePlane(&g) || !readOnePlane(&b))
            return {};
        // 多余通道（含 alpha）跳过
        for (int c = 3; c < channelCount; ++c) {
            if (!skipBytes(in, planeBytes))
                break;
        }
        return planesToPremulImage(w, h, QByteArray(), r, g, b);
    }

    // RLE 合并图：跳过行长度表 + 压缩数据太易错；无图层时再报错
    Q_UNUSED(readOnePlane);
    return {};
}

} // namespace

std::unique_ptr<ImageDocument> PsdIo::load(const QString &filePath,
                                           QString *errorMessage)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage)
            *errorMessage = QObject::tr("无法打开：%1").arg(file.errorString());
        return nullptr;
    }

    QDataStream in(&file);

    char sig[4];
    if (!readRaw(in, sig, 4) || std::memcmp(sig, "8BPS", 4) != 0) {
        if (errorMessage)
            *errorMessage = QObject::tr("不是有效的 PSD 文件（缺少 8BPS）");
        return nullptr;
    }

    quint16 version = 0;
    if (!readU16(in, &version) || version != static_cast<quint16>(PsdFileVersion::Photoshop)) {
        if (errorMessage)
            *errorMessage = QObject::tr("仅支持 PSD（版本 1），不支持 PSB");
        return nullptr;
    }

    if (!skipBytes(in, 6)) {
        if (errorMessage)
            *errorMessage = QObject::tr("文件头截断");
        return nullptr;
    }

    quint16 channels = 0, depth = 0, colorMode = 0;
    quint32 height = 0, width = 0;
    if (!readU16(in, &channels) || !readU32(in, &height) || !readU32(in, &width)
        || !readU16(in, &depth) || !readU16(in, &colorMode)) {
        if (errorMessage)
            *errorMessage = QObject::tr("文件头截断");
        return nullptr;
    }

    if (colorMode != static_cast<quint16>(PsdColorMode::Rgb) || depth != kPsdBitDepth) {
        if (errorMessage)
            *errorMessage = QObject::tr("仅支持 RGB 8-bit PSD（当前 mode=%1 depth=%2）")
                                .arg(colorMode)
                                .arg(depth);
        return nullptr;
    }
    if (width == 0 || height == 0 || width > 30000 || height > 30000) {
        if (errorMessage)
            *errorMessage = QObject::tr("画布尺寸无效或过大");
        return nullptr;
    }

    quint32 colorModeLen = 0;
    if (!readU32(in, &colorModeLen) || !skipBytes(in, int(colorModeLen))) {
        if (errorMessage)
            *errorMessage = QObject::tr("无法跳过 Color Mode Data");
        return nullptr;
    }
    quint32 resourcesLen = 0;
    if (!readU32(in, &resourcesLen) || !skipBytes(in, int(resourcesLen))) {
        if (errorMessage)
            *errorMessage = QObject::tr("无法跳过 Image Resources");
        return nullptr;
    }

    quint32 layerMaskSectionLen = 0;
    if (!readU32(in, &layerMaskSectionLen)) {
        if (errorMessage)
            *errorMessage = QObject::tr("无法读取图层段长度");
        return nullptr;
    }

    QVector<PsdLayerRecord> records;

    if (layerMaskSectionLen > 0) {
        const qint64 sectionEnd = file.pos() + qint64(layerMaskSectionLen);

        quint32 layerInfoLen = 0;
        if (!readU32(in, &layerInfoLen)) {
            if (errorMessage)
                *errorMessage = QObject::tr("无法读取 Layer Info 长度");
            return nullptr;
        }

        if (layerInfoLen > 0) {
            qint16 layerCountSigned = 0;
            if (!readI16(in, &layerCountSigned)) {
                if (errorMessage)
                    *errorMessage = QObject::tr("无法读取图层数");
                return nullptr;
            }
            const int layerCount = qAbs(int(layerCountSigned));
            if (layerCount > 2048) {
                if (errorMessage)
                    *errorMessage = QObject::tr("图层数异常：%1").arg(layerCount);
                return nullptr;
            }

            records.resize(layerCount);
            for (int i = 0; i < layerCount; ++i) {
                PsdLayerRecord &rec = records[i];
                if (!readI32(in, &rec.top) || !readI32(in, &rec.left)
                    || !readI32(in, &rec.bottom) || !readI32(in, &rec.right)) {
                    if (errorMessage)
                        *errorMessage = QObject::tr("图层 %1 边界截断").arg(i);
                    return nullptr;
                }
                quint16 chCount = 0;
                if (!readU16(in, &chCount) || chCount > 64) {
                    if (errorMessage)
                        *errorMessage = QObject::tr("图层 %1 通道数无效").arg(i);
                    return nullptr;
                }
                rec.channels.resize(int(chCount));
                for (int c = 0; c < int(chCount); ++c) {
                    if (!readI16(in, &rec.channels[c].id)
                        || !readU32(in, &rec.channels[c].dataLength)) {
                        if (errorMessage)
                            *errorMessage = QObject::tr("图层 %1 通道表截断").arg(i);
                        return nullptr;
                    }
                }

                char bim[4], blendKey[4];
                if (!readRaw(in, bim, 4) || std::memcmp(bim, "8BIM", 4) != 0
                    || !readRaw(in, blendKey, 4)) {
                    if (errorMessage)
                        *errorMessage = QObject::tr("图层 %1 混合签名无效").arg(i);
                    return nullptr;
                }
                if (std::memcmp(blendKey, "pass", 4) == 0)
                    rec.skip = true;
                rec.blend = blendModeFromPsdKey(blendKey);

                quint8 clipping = 0, filler = 0;
                if (!readU8(in, &rec.opacity) || !readU8(in, &clipping)
                    || !readU8(in, &rec.flags) || !readU8(in, &filler)) {
                    if (errorMessage)
                        *errorMessage = QObject::tr("图层 %1 属性截断").arg(i);
                    return nullptr;
                }
                Q_UNUSED(clipping);
                Q_UNUSED(filler);

                quint32 extraSize = 0;
                if (!readU32(in, &extraSize)) {
                    if (errorMessage)
                        *errorMessage = QObject::tr("图层 %1 extra 长度截断").arg(i);
                    return nullptr;
                }
                const qint64 extraEnd = file.pos() + qint64(extraSize);

                quint32 maskLen = 0;
                if (!readU32(in, &maskLen) || !skipBytes(in, int(maskLen))) {
                    if (errorMessage)
                        *errorMessage = QObject::tr("图层 %1 蒙版段失败").arg(i);
                    return nullptr;
                }
                quint32 blendRangeLen = 0;
                if (!readU32(in, &blendRangeLen) || !skipBytes(in, int(blendRangeLen))) {
                    if (errorMessage)
                        *errorMessage = QObject::tr("图层 %1 混合范围段失败").arg(i);
                    return nullptr;
                }

                const int nameRemain = int(extraEnd - file.pos());
                int nameConsumed = 0;
                if (nameRemain > 0) {
                    if (!readPascalName(in, nameRemain, &rec.name, &nameConsumed)) {
                        if (errorMessage)
                            *errorMessage = QObject::tr("图层 %1 名称截断").arg(i);
                        return nullptr;
                    }
                }
                if (rec.name.isEmpty())
                    rec.name = QObject::tr("图层 %1").arg(i + 1);

                if (!skipBytes(in, int(extraEnd - file.pos()))) {
                    if (errorMessage)
                        *errorMessage = QObject::tr("图层 %1 extra 尾部跳过失败").arg(i);
                    return nullptr;
                }
            }

            for (int i = 0; i < records.size(); ++i) {
                PsdLayerRecord &rec = records[i];
                const qint64 lw64 = qint64(rec.right) - qint64(rec.left);
                const qint64 lh64 = qint64(rec.bottom) - qint64(rec.top);
                // A16：层矩形须非负、不超过画布上限，且 w*h 不溢出 int（防 OOM / UB）
                if (lw64 < 0 || lh64 < 0 || lw64 > 30000 || lh64 > 30000
                    || lw64 * lh64 > qint64(std::numeric_limits<int>::max())) {
                    if (errorMessage)
                        *errorMessage = QObject::tr("图层 %1 矩形无效或过大（%2×%3）")
                                            .arg(i)
                                            .arg(lw64)
                                            .arg(lh64);
                    return nullptr;
                }
                const int lw = int(lw64);
                const int lh = int(lh64);
                const int planeBytes = lw * lh;

                QByteArray a, r, g, b;
                bool gotR = false, gotG = false, gotB = false;

                for (const PsdChannelDesc &ch : rec.channels) {
                    QByteArray plane;
                    const qint64 chStart = file.pos();
                    const bool okPlane = (!rec.skip && planeBytes > 0)
                                            ? readChannelPlane(in, planeBytes, lh, &plane)
                                            : false;
                    if (!okPlane) {
                        // 空层 / 跳过层：按声明长度消耗
                        if (!file.seek(chStart + qint64(ch.dataLength))) {
                            if (errorMessage)
                                *errorMessage = QObject::tr("图层 %1 通道跳过失败").arg(i);
                            return nullptr;
                        }
                        continue;
                    }
                    const qint64 chEndDeclared = chStart + qint64(ch.dataLength);
                    if (file.pos() < chEndDeclared)
                        skipBytes(in, int(chEndDeclared - file.pos()));
                    else if (file.pos() > chEndDeclared)
                        file.seek(chEndDeclared);

                    switch (ch.id) {
                    case static_cast<qint16>(PsdChannelId::Transparency):
                        a = plane;
                        break;
                    case static_cast<qint16>(PsdChannelId::Red):
                        r = plane;
                        gotR = true;
                        break;
                    case static_cast<qint16>(PsdChannelId::Green):
                        g = plane;
                        gotG = true;
                        break;
                    case static_cast<qint16>(PsdChannelId::Blue):
                        b = plane;
                        gotB = true;
                        break;
                    default:
                        break;
                    }
                }

                if (rec.skip || lw <= 0 || lh <= 0 || !(gotR && gotG && gotB)) {
                    rec.skip = true;
                    continue;
                }
                rec.pixels = planesToPremulImage(lw, lh, a, r, g, b);
            }
        }

        if (file.pos() < sectionEnd)
            skipBytes(in, int(sectionEnd - file.pos()));
        else if (file.pos() > sectionEnd)
            file.seek(sectionEnd);
    }

    auto doc = std::make_unique<ImageDocument>(int(width), int(height));
    {
        ImageDocument::HistorySuppress suppress(*doc);
        int added = 0;
        for (const PsdLayerRecord &rec : records) {
            if (rec.skip || rec.pixels.isNull())
                continue;
            auto layer = std::make_unique<Layer>(rec.name, rec.pixels);
            layer->setOffsetSilent(rec.left, rec.top);
            layer->setOpacity(rec.opacity / 255.0);
            layer->setBlendMode(rec.blend);
            layer->setVisible((rec.flags & static_cast<quint8>(PsdLayerFlags::Invisible)) == 0);
            doc->addLayer(std::move(layer));
            ++added;
        }

        if (added == 0) {
            // 无图层：尝试合并图像作单背景层
            QImage merged = readMergedRgbFallback(in, int(width), int(height), int(channels));
            if (merged.isNull()) {
                if (errorMessage)
                    *errorMessage = QObject::tr("PSD 中没有可导入的像素图层");
                return nullptr;
            }
            auto layer = std::make_unique<Layer>(QObject::tr("背景"), merged);
            doc->addLayer(std::move(layer));
            added = 1;
        }

        doc->setActiveLayerIndex(added - 1);
    }

    doc->setFilePath(filePath);
    doc->clearDirty();
    return doc;
}

} // namespace Ps

