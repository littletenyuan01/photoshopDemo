/**
 * freetransformop.cpp — freetransformop.h 实现（engine/op 层）。
 *
 * 逆映射：目标像素 → 源坐标 → nearest / bilinear / bicubic 采样 → 写回。
 * 双三次用 Keys 立方（a=-0.5，即 Catmull-Rom），对照常见图像重采样与 PS「两次立方」。
 */
#include "freetransformop.h"

#include "paintclip.h"
#include "tilepatch.h"
#include "domain/tilebuffer.h"

#include <QPolygonF>
#include <QTransform>
#include <QtMath>

namespace Ps {

namespace {

/** 钳到 [0, maxInclusive]。 */
inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/** Keys 立方核（a = -0.5 → Catmull-Rom）；t 为到采样点的距离。 */
inline qreal cubicWeight(qreal t)
{
    constexpr qreal a = -0.5;
    t = qAbs(t);
    if (t < 1.0) {
        // (a+2)t^3 - (a+3)t^2 + 1
        return ((a + 2.0) * t - (a + 3.0)) * t * t + 1.0;
    }
    if (t < 2.0) {
        // a t^3 - 5a t^2 + 8a t - 4a
        return (((a * t - 5.0 * a) * t) + 8.0 * a) * t - 4.0 * a;
    }
    return 0.0;
}

/** 读源像素（预乘 ARGB）；越界透明。 */
inline QRgb pixelAt(const QImage &src, int x, int y)
{
    if (x < 0 || y < 0 || x >= src.width() || y >= src.height())
        return 0;
    return reinterpret_cast<const QRgb *>(src.constScanLine(y))[x];
}

/** 解预乘到 float rgba [0,1]。 */
inline void unpremul(QRgb p, qreal *r, qreal *g, qreal *b, qreal *a)
{
    const int ai = qAlpha(p);
    *a = ai / 255.0;
    if (ai <= 0) {
        *r = *g = *b = 0.0;
        return;
    }
    *r = qRed(p) / 255.0;
    *g = qGreen(p) / 255.0;
    *b = qBlue(p) / 255.0;
    // 预乘 → 直通色
    *r /= *a;
    *g /= *a;
    *b /= *a;
}

inline QRgb premulFromFloat(qreal r, qreal g, qreal b, qreal a)
{
    a = qBound(0.0, a, 1.0);
    if (a <= 1e-8)
        return 0;
    r = qBound(0.0, r, 1.0);
    g = qBound(0.0, g, 1.0);
    b = qBound(0.0, b, 1.0);
    const int ai = int(a * 255.0 + 0.5);
    const int ri = int(r * a * 255.0 + 0.5);
    const int gi = int(g * a * 255.0 + 0.5);
    const int bi = int(b * a * 255.0 + 0.5);
    return qRgba(ri, gi, bi, ai);
}

QRgb sampleNearest(const QImage &src, qreal sx, qreal sy)
{
    const int x = clampi(int(qFloor(sx)), 0, src.width() - 1);
    const int y = clampi(int(qFloor(sy)), 0, src.height() - 1);
    return pixelAt(src, x, y);
}

QRgb sampleBilinear(const QImage &src, qreal sx, qreal sy)
{
    const int w = src.width();
    const int h = src.height();
    if (w <= 0 || h <= 0)
        return 0;

    const qreal x = sx - 0.5;
    const qreal y = sy - 0.5;
    const int x0 = int(qFloor(x));
    const int y0 = int(qFloor(y));
    const qreal fx = x - x0;
    const qreal fy = y - y0;

    qreal r = 0, g = 0, b = 0, a = 0;
    for (int j = 0; j < 2; ++j) {
        const qreal wy = (j == 0) ? (1.0 - fy) : fy;
        const int yy = clampi(y0 + j, 0, h - 1);
        for (int i = 0; i < 2; ++i) {
            const qreal wx = (i == 0) ? (1.0 - fx) : fx;
            const int xx = clampi(x0 + i, 0, w - 1);
            qreal pr, pg, pb, pa;
            unpremul(pixelAt(src, xx, yy), &pr, &pg, &pb, &pa);
            const qreal wgt = wx * wy;
            r += pr * wgt;
            g += pg * wgt;
            b += pb * wgt;
            a += pa * wgt;
        }
    }
    return premulFromFloat(r, g, b, a);
}

/** 双三次：4×4 邻域 × Keys 立方核。 */
QRgb sampleBicubic(const QImage &src, qreal sx, qreal sy)
{
    const int w = src.width();
    const int h = src.height();
    if (w <= 0 || h <= 0)
        return 0;

    // 像素中心采样
    const qreal x = sx - 0.5;
    const qreal y = sy - 0.5;
    const int x0 = int(qFloor(x));
    const int y0 = int(qFloor(y));
    const qreal fx = x - x0;
    const qreal fy = y - y0;

    qreal r = 0, g = 0, b = 0, a = 0;
    for (int j = -1; j <= 2; ++j) {
        const qreal wy = cubicWeight(fy - j);
        if (wy == 0.0)
            continue;
        const int yy = clampi(y0 + j, 0, h - 1);
        for (int i = -1; i <= 2; ++i) {
            const qreal wx = cubicWeight(fx - i);
            if (wx == 0.0)
                continue;
            const int xx = clampi(x0 + i, 0, w - 1);
            qreal pr, pg, pb, pa;
            unpremul(pixelAt(src, xx, yy), &pr, &pg, &pb, &pa);
            const qreal wgt = wx * wy;
            r += pr * wgt;
            g += pg * wgt;
            b += pb * wgt;
            a += pa * wgt;
        }
    }
    return premulFromFloat(r, g, b, a);
}

QRgb sample(const QImage &src, qreal sx, qreal sy, TransformInterpolation interp)
{
    switch (interp) {
    case TransformInterpolation::Nearest:
        return sampleNearest(src, sx, sy);
    case TransformInterpolation::Bilinear:
        return sampleBilinear(src, sx, sy);
    case TransformInterpolation::Bicubic:
    default:
        return sampleBicubic(src, sx, sy);
    }
}

} // namespace

bool FreeTransformOp::prepare(OpContext &ctx)
{
    if (!BufferOp::prepare(ctx))
        return false;
    if (m_sourceRect.isEmpty() && m_sourcePixels.isNull())
        return false;
    if (!ctx.tiles || ctx.tiles->width() <= 0 || ctx.tiles->height() <= 0)
        return false;
    return true;
}

QRect FreeTransformOp::process(OpContext &ctx)
{
    TileBuffer &tiles = *ctx.tiles;
    const QRect layerRect(0, 0, tiles.width(), tiles.height());

    QImage src = m_sourcePixels;
    QRect srcRect = m_sourceRect;
    if (src.isNull()) {
        srcRect = m_sourceRect.intersected(layerRect);
        if (srcRect.isEmpty())
            return {};
        src = TilePatch::extract(tiles, srcRect);
    } else if (srcRect.isEmpty()) {
        srcRect = QRect(QPoint(0, 0), src.size());
    }

    if (src.format() != QImage::Format_ARGB32_Premultiplied)
        src = src.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    QPolygonF destPoly;
    destPoly << m_dest[0] << m_dest[1] << m_dest[2] << m_dest[3];
    const QRect destBounds = destPoly.boundingRect().toAlignedRect().adjusted(-1, -1, 1, 1)
                                 .intersected(layerRect);

    QRect dirty = destBounds;
    if (m_clearSource)
        dirty = dirty.united(srcRect.intersected(layerRect));
    dirty = dirty.intersected(layerRect);
    if (dirty.isEmpty())
        return {};

    if (OpPaintClip::clipActive(ctx.clip)) {
        dirty = dirty.intersected(
            OpPaintClip::selectionRectInLayer(ctx.clip, layerRect.width(), layerRect.height()));
        if (dirty.isEmpty())
            return {};
    }

    // 源矩形四角（像素空间）→ 目标四边形；再取逆：目标 → 源
    QPolygonF srcPoly;
    srcPoly << QPointF(0, 0)
            << QPointF(src.width(), 0)
            << QPointF(src.width(), src.height())
            << QPointF(0, src.height());

    QTransform forward;
    if (!QTransform::quadToQuad(srcPoly, destPoly, forward)) {
        // 退化：按包围盒仿射
        forward.reset();
        const QRectF db = destPoly.boundingRect();
        forward.translate(db.left(), db.top());
        if (src.width() > 0 && src.height() > 0)
            forward.scale(db.width() / qreal(src.width()), db.height() / qreal(src.height()));
    }

    bool invertible = false;
    const QTransform inverse = forward.inverted(&invertible);
    if (!invertible)
        return {};

    QImage patch(dirty.size(), QImage::Format_ARGB32_Premultiplied);
    patch.fill(0);

    // 源已提起时不必再清；确认提交路径 clearSource=true，并集脏区已含源矩形
    if (m_clearSource) {
        // patch 已填 0；源矩形落在 dirty 内的部分保持透明即可
    }

    const QPoint dirtyOrigin = dirty.topLeft();
    for (int y = 0; y < dirty.height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(patch.scanLine(y));
        const int layerY = dirtyOrigin.y() + y;
        for (int x = 0; x < dirty.width(); ++x) {
            const int layerX = dirtyOrigin.x() + x;
            if (!OpPaintClip::layerPixelSelected(ctx.clip, layerX, layerY))
                continue;

            // 像素中心
            const QPointF destPt(layerX + 0.5, layerY + 0.5);
            // 快速剔除：不在目标四边形附近可跳过（仍依赖逆映射后的源范围）
            const QPointF srcPt = inverse.map(destPt);
            if (srcPt.x() < -1.0 || srcPt.y() < -1.0
                || srcPt.x() > src.width() + 1.0 || srcPt.y() > src.height() + 1.0)
                continue;

            // 可选：点在目标多边形内才画（避免透视外溢糊边）
            // 用包围盒+逆映射已够；透视外区域逆映射可能仍落在源内，用 contains 更干净
            if (!destPoly.containsPoint(destPt, Qt::OddEvenFill))
                continue;

            line[x] = sample(src, srcPt.x(), srcPt.y(), m_interpolation);
        }
    }

    TilePatch::blit(tiles, dirty, patch);
    return dirty;
}

} // namespace Ps
