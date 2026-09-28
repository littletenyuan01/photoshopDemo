#include "paintengine.h"

#include "domain/tilebuffer.h"

#include <QPainter>
#include <QQueue>
#include <QRandomGenerator>
#include <QVector>
#include <QtMath>

namespace Ps {

namespace {

QRect dabBounds(const QPointF &center, qreal radius)
{
    const int rCeil = qCeil(radius) + 1;
    return QRect(qFloor(center.x()) - rCeil,
                 qFloor(center.y()) - rCeil,
                 rCeil * 2 + 1,
                 rCeil * 2 + 1);
}

void stampDabOnImage(QImage &target,
                     const QPointF &center,
                     qreal radius,
                     const QColor &color,
                     PaintEngine::Mode mode,
                     qreal hardness)
{
    if (target.isNull() || radius <= 0.0)
        return;

    hardness = qBound(0.0, hardness, 1.0);

    const QRect clip = dabBounds(center, radius).intersected(target.rect());
    if (clip.isEmpty())
        return;

    QPainter painter(&target);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setClipRect(clip);

    if (mode == PaintEngine::Mode::Erase)
        painter.setCompositionMode(QPainter::CompositionMode_DestinationOut);
    else
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);

    QColor core = (mode == PaintEngine::Mode::Erase) ? QColor(255, 255, 255) : color;
    QColor edge = core;
    core.setAlpha(255);
    edge.setAlpha(0);

    QRadialGradient grad(center, radius);
    const qreal stop = qBound(0.05, hardness, 0.98);
    grad.setColorAt(0.0, core);
    grad.setColorAt(stop, core);
    grad.setColorAt(1.0, edge);

    painter.setPen(Qt::NoPen);
    painter.setBrush(grad);
    painter.drawEllipse(center, radius, radius);
}

template <typename Target>
QPointF strokeSegmentImpl(Target &target,
                          const QPointF &from,
                          const QPointF &to,
                          qreal radius,
                          const QColor &color,
                          PaintEngine::Mode mode,
                          qreal hardness,
                          qreal spacing)
{
    const QPointF delta = to - from;
    const qreal len = qSqrt(delta.x() * delta.x() + delta.y() * delta.y());
    const qreal step = qMax(0.5, radius * 2.0 * qBound(0.05, spacing, 1.0));

    if (len < 1e-6) {
        PaintEngine::stampDab(target, to, radius, color, mode, hardness);
        return to;
    }

    qreal d = 0.0;
    QPointF last = from;
    while (d <= len) {
        const qreal t = d / len;
        last = from + delta * t;
        PaintEngine::stampDab(target, last, radius, color, mode, hardness);
        d += step;
    }
    if ((last - to).manhattanLength() > 0.5) {
        PaintEngine::stampDab(target, to, radius, color, mode, hardness);
        last = to;
    }
    return last;
}

} // namespace

void PaintEngine::stampDab(QImage &target,
                           const QPointF &center,
                           qreal radius,
                           const QColor &color,
                           Mode mode,
                           qreal hardness)
{
    stampDabOnImage(target, center, radius, color, mode, hardness);
}

void PaintEngine::stampDab(TileBuffer &tiles,
                           const QPointF &center,
                           qreal radius,
                           const QColor &color,
                           Mode mode,
                           qreal hardness)
{
    // 【功能】在懒分配瓦片上盖一笔圆形 dab；只 ensure 笔触碰到的格
    if (tiles.width() <= 0 || tiles.height() <= 0 || radius <= 0.0)
        return;

    const QRect dabRect = dabBounds(center, radius);
    // allocateMissing=true：第一次画到的透明格在此分配
    tiles.forEachTileInRect(dabRect, true, [&](int, int, QImage &tile, const QRect &bounds) {
        // 图像坐标 → 本瓦片局部坐标再画，避免越界写邻格
        const QPointF local(center.x() - bounds.x(), center.y() - bounds.y());
        stampDabOnImage(tile, local, radius, color, mode, hardness);
    });
}

QPointF PaintEngine::strokeSegment(QImage &target,
                                   const QPointF &from,
                                   const QPointF &to,
                                   qreal radius,
                                   const QColor &color,
                                   Mode mode,
                                   qreal hardness,
                                   qreal spacing)
{
    return strokeSegmentImpl(target, from, to, radius, color, mode, hardness, spacing);
}

QPointF PaintEngine::strokeSegment(TileBuffer &tiles,
                                   const QPointF &from,
                                   const QPointF &to,
                                   qreal radius,
                                   const QColor &color,
                                   Mode mode,
                                   qreal hardness,
                                   qreal spacing)
{
    return strokeSegmentImpl(tiles, from, to, radius, color, mode, hardness, spacing);
}

namespace {

/** 把 QColor 转成预乘 ARGB 像素。 */
QRgb toPremultipliedRgb(const QColor &color)
{
    const int a = qBound(0, color.alpha(), 255);
    const int r = (color.red() * a + 127) / 255;
    const int g = (color.green() * a + 127) / 255;
    const int b = (color.blue() * a + 127) / 255;
    return qRgba(r, g, b, a);
}

/** 预乘像素还原成直通 R/G/B/A（0..255），供容差比较。 */
void unpremultiplyRgb(QRgb px, int *r, int *g, int *b, int *a)
{
    *a = qAlpha(px);
    if (*a <= 0) {
        *r = *g = *b = 0;
        return;
    }
    if (*a >= 255) {
        *r = qRed(px);
        *g = qGreen(px);
        *b = qBlue(px);
        return;
    }
    *r = qBound(0, (qRed(px) * 255 + *a / 2) / *a, 255);
    *g = qBound(0, (qGreen(px) * 255 + *a / 2) / *a, 255);
    *b = qBound(0, (qBlue(px) * 255 + *a / 2) / *a, 255);
}

/**
 * 相似色判定（瘦身版 GIMP COMPOSITE + fill-transparent）。
 * 对照 `gimppickable-contiguous-region.cc` 的 pixel_difference：
 * - 种子全透明：只比 alpha（允许填透明区）
 * - 否则：比直通 RGB 的 max|Δ|，且不把全透明像素算进连通域
 * 本项目不做 antialias 软边、不做对角邻接、不做 HSV/LCh 准则。
 */
bool similarToSeed(QRgb candidate, int seedR, int seedG, int seedB, int seedA, int tol)
{
    int r, g, b, a;
    unpremultiplyRgb(candidate, &r, &g, &b, &a);

    if (seedA == 0) {
        // GIMP：select_transparent 且种子 alpha==0 → 只比 alpha
        return qAbs(a - seedA) <= tol;
    }

    // GIMP：!select_transparent 时跳过全透明像素
    if (a == 0)
        return false;

    const int maxDiff = qMax(qAbs(r - seedR), qMax(qAbs(g - seedG), qAbs(b - seedB)));
    return maxDiff <= tol;
}

} // namespace

QRect PaintEngine::floodFill(TileBuffer &tiles,
                             const QPoint &seed,
                             const QColor &fillColor,
                             int tolerance,
                             bool contiguous)
{
    // 【功能】油漆桶：种子相似色区域写入填充色
    // 【对照 GIMP 两步】
    //   1) gimppickable-contiguous-region：by_seed（连续）/ by_color（本项目「非连续」近似）
    //   2) gimpdrawable-bucket-fill：建 fill buffer → apply_buffer
    // 本项目合并为一次物化+写入；无选区相交、无 sample-merged、无对角邻接、无抗锯齿软边。
    const int w = tiles.width();
    const int h = tiles.height();
    if (w <= 0 || h <= 0)
        return {};
    if (seed.x() < 0 || seed.y() < 0 || seed.x() >= w || seed.y() >= h)
        return {};

    const int tol = qBound(0, tolerance, 255);
    const QRgb fillPx = toPremultipliedRgb(fillColor);

    QImage img = tiles.materialize();
    if (img.format() != QImage::Format_ARGB32_Premultiplied)
        img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    const QRgb seedPx = img.pixel(seed.x(), seed.y());
    if (seedPx == fillPx)
        return {}; // 种子已是目标色，无需再填

    int seedR = 0, seedG = 0, seedB = 0, seedA = 0;
    unpremultiplyRgb(seedPx, &seedR, &seedG, &seedB, &seedA);

    int minX = seed.x();
    int minY = seed.y();
    int maxX = seed.x();
    int maxY = seed.y();
    bool any = false;

    auto paintAt = [&](int x, int y) {
        img.setPixel(x, y, fillPx);
        any = true;
        minX = qMin(minX, x);
        minY = qMin(minY, y);
        maxX = qMax(maxX, x);
        maxY = qMax(maxY, y);
    };

    auto matches = [&](int x, int y) {
        return similarToSeed(img.pixel(x, y), seedR, seedG, seedB, seedA, tol);
    };

    if (!contiguous) {
        // 近似 GIMP by_color：整层所有与种子相似的像素（桶工具在 GIMP 默认走 by_seed）
        for (int y = 0; y < h; ++y) {
            QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
            for (int x = 0; x < w; ++x) {
                if (!similarToSeed(line[x], seedR, seedG, seedB, seedA, tol))
                    continue;
                line[x] = fillPx;
                any = true;
                minX = qMin(minX, x);
                minY = qMin(minY, y);
                maxX = qMax(maxX, x);
                maxY = qMax(maxY, y);
            }
        }
    } else {
        // 连续：4-邻接 BFS ≈ GIMP by_seed（diagonal-neighbors 默认关）
        QVector<quint8> visited(w * h, 0);
        QQueue<QPoint> queue;
        queue.enqueue(seed);
        visited[seed.y() * w + seed.x()] = 1;

        while (!queue.isEmpty()) {
            const QPoint p = queue.dequeue();
            if (!matches(p.x(), p.y()))
                continue;
            paintAt(p.x(), p.y());

            const QPoint nbs[] = {
                QPoint(p.x() + 1, p.y()),
                QPoint(p.x() - 1, p.y()),
                QPoint(p.x(), p.y() + 1),
                QPoint(p.x(), p.y() - 1),
            };
            for (const QPoint &n : nbs) {
                if (n.x() < 0 || n.y() < 0 || n.x() >= w || n.y() >= h)
                    continue;
                const int idx = n.y() * w + n.x();
                if (visited[idx])
                    continue;
                visited[idx] = 1;
                if (matches(n.x(), n.y()))
                    queue.enqueue(n);
            }
        }
    }

    if (!any)
        return {};

    tiles.setFromImage(img);
    return QRect(QPoint(minX, minY), QPoint(maxX, maxY));
}

namespace {

/** t∈[0,1] 线性插值（直通色）。 */
QColor lerpColor(const QColor &a, const QColor &b, qreal t)
{
    t = qBound(0.0, t, 1.0);
    return QColor(
        qRound(a.red() + (b.red() - a.red()) * t),
        qRound(a.green() + (b.green() - a.green()) * t),
        qRound(a.blue() + (b.blue() - a.blue()) * t),
        qRound(a.alpha() + (b.alpha() - a.alpha()) * t));
}

/**
 * GIMP offset 语义：把 [offset,1] 重映射到 [0,1]（见 gradient_calc_*_factor）。
 * @param offset01 offset/100，已夹到 [0,1)
 */
qreal applyGimpOffset(qreal rat, qreal offset01)
{
    if (rat < offset01)
        return 0.0;
    if (offset01 >= 1.0 - 1e-9)
        return (rat >= 1.0) ? 1.0 : 0.0;
    return (rat - offset01) / (1.0 - offset01);
}

/** ≈ gradient_calc_linear_factor（相对起点的局部坐标 lx,ly）。 */
qreal factorLinear(qreal dist, qreal vx, qreal vy, qreal offset01, qreal lx, qreal ly)
{
    if (dist <= 0.0)
        return 0.0;
    const qreal rat = (vx * lx + vy * ly) / dist;
    if (rat < 0.0)
        return rat / (1.0 - offset01); // REPEAT_NONE 后会 clamp
    return applyGimpOffset(rat, offset01);
}

/** ≈ gradient_calc_bilinear_factor（对称）。 */
qreal factorBilinear(qreal dist, qreal vx, qreal vy, qreal offset01, qreal lx, qreal ly)
{
    if (dist <= 0.0)
        return 0.0;
    const qreal rat = qAbs((vx * lx + vy * ly) / dist);
    return applyGimpOffset(rat, offset01);
}

/** ≈ gradient_calc_radial_factor。 */
qreal factorRadial(qreal dist, qreal offset01, qreal lx, qreal ly)
{
    if (dist <= 0.0)
        return 0.0;
    const qreal rat = qSqrt(lx * lx + ly * ly) / dist;
    return applyGimpOffset(rat, offset01);
}

/** ≈ gradient_calc_square_factor（轴对齐方距 = PS「菱形」在 GIMP 的对应 shape）。 */
qreal factorSquare(qreal dist, qreal offset01, qreal lx, qreal ly)
{
    if (dist <= 0.0)
        return 0.0;
    const qreal rat = qMax(qAbs(lx), qAbs(ly)) / dist;
    return applyGimpOffset(rat, offset01);
}

/** ≈ gradient_calc_conical_asym_factor（角度渐变）；offset 以幂次进入。 */
qreal factorConicalAsym(qreal dist, qreal vx, qreal vy, qreal offsetPercent, qreal lx, qreal ly)
{
    if (dist <= 0.0)
        return 0.0;
    if (qAbs(lx) < 1e-12 && qAbs(ly) < 1e-12)
        return 0.5;

    qreal ang0 = qAtan2(vx, vy) + acos(-1.0);
    qreal ang1 = qAtan2(lx, ly) + acos(-1.0);
    qreal ang = ang1 - ang0;
    if (ang < 0.0)
        ang += 2.0 * acos(-1.0);
    qreal rat = ang / (2.0 * acos(-1.0));
    rat = qPow(rat, (offsetPercent / 10.0) + 1.0);
    return qBound(0.0, rat, 1.0);
}

} // namespace

QRect PaintEngine::applyGradient(TileBuffer &tiles,
                                 const QPointF &start,
                                 const QPointF &end,
                                 const QColor &foreground,
                                 const QColor &background,
                                 GradientType type,
                                 qreal opacity,
                                 int offsetPercent,
                                 bool reverse,
                                 bool dither)
{
    // 【功能】拖拽起止写前景→背景渐变（对照 gimp_drawable_gradient + gimp:gradient）
    const int w = tiles.width();
    const int h = tiles.height();
    if (w <= 0 || h <= 0)
        return {};

    opacity = qBound(0.0, opacity, 1.0);
    // GIMP offset 属性范围 0..100；UI 若仍给负值则当 0
    offsetPercent = qBound(0, offsetPercent, 100);
    if (opacity <= 0.0)
        return {};

    // reverse ≈ paint options 的 gradient-reverse（对调两端色）
    const QColor c0 = reverse ? background : foreground;
    const QColor c1 = reverse ? foreground : background;

    const qreal dx = end.x() - start.x();
    const qreal dy = end.y() - start.y();
    qreal dist = 0.0;
    qreal vx = 0.0;
    qreal vy = 0.0;
    const qreal offset01 = offsetPercent / 100.0;

    switch (type) {
    case GradientType::Radial:
        dist = qSqrt(dx * dx + dy * dy);
        break;
    case GradientType::Diamond: // SQUARE
        dist = qMax(qAbs(dx), qAbs(dy));
        break;
    case GradientType::Linear:
    case GradientType::Reflected:
    case GradientType::Angle:
        dist = qSqrt(dx * dx + dy * dy);
        if (dist > 0.0) {
            vx = dx / dist;
            vy = dy / dist;
        }
        break;
    }

    QImage base = tiles.materialize();
    if (base.format() != QImage::Format_ARGB32_Premultiplied)
        base = base.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    QImage overlay(w, h, QImage::Format_ARGB32_Premultiplied);
    overlay.fill(Qt::transparent);

    QRandomGenerator *rng = dither ? QRandomGenerator::global() : nullptr;

    for (int y = 0; y < h; ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(overlay.scanLine(y));
        for (int x = 0; x < w; ++x) {
            // 像素中心（gimpoperationgradient：x+=0.5,y+=0.5）
            const qreal lx = (x + 0.5) - start.x();
            const qreal ly = (y + 0.5) - start.y();

            qreal factor = 0.0;
            switch (type) {
            case GradientType::Linear:
                factor = factorLinear(dist, vx, vy, offset01, lx, ly);
                break;
            case GradientType::Radial:
                factor = factorRadial(dist, offset01, lx, ly);
                break;
            case GradientType::Angle:
                factor = factorConicalAsym(dist, vx, vy, offsetPercent, lx, ly);
                break;
            case GradientType::Reflected:
                factor = factorBilinear(dist, vx, vy, offset01, lx, ly);
                break;
            case GradientType::Diamond:
                factor = factorSquare(dist, offset01, lx, ly);
                break;
            }
            // REPEAT_NONE：采样前 clamp（同 GIMP cache 路径）
            factor = qBound(0.0, factor, 1.0);

            QColor col = lerpColor(c0, c1, factor);
            if (rng) {
                // 近似 gradient_dither_pixel：约 ±0.5/256 的量化噪声
                const qreal n = (rng->generateDouble() - 0.5) / 256.0;
                col.setRedF(qBound(0.0, col.redF() + n, 1.0));
                col.setGreenF(qBound(0.0, col.greenF() + n, 1.0));
                col.setBlueF(qBound(0.0, col.blueF() + n, 1.0));
            }
            col.setAlpha(qRound(col.alpha() * opacity));
            line[x] = toPremultipliedRgb(col);
        }
    }

    // SourceOver 叠回图层（对照 apply_buffer + paint opacity）
    {
        QPainter painter(&base);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        painter.drawImage(0, 0, overlay);
    }

    tiles.setFromImage(base);
    return QRect(0, 0, w, h);
}

} // namespace Ps
