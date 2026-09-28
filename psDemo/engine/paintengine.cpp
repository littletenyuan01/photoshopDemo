#include "paintengine.h"

#include "domain/selection.h"
#include "domain/tilebuffer.h"

#include <QPainter>
#include <QQueue>
#include <QRandomGenerator>
#include <QVector>
#include <QtMath>

namespace Ps {

namespace {

bool clipActive(const PaintSelectionClip &clip)
{
    // 对照 gimp_item_mask_intersect：空选区 → 不遮罩绘制
    // 用 bounds()（内部走 cache）判断，避免仅依赖 isEmpty 的歧义
    return clip.selection && !clip.selection->bounds().isEmpty();
}

bool layerPixelSelected(const PaintSelectionClip &clip, int layerX, int layerY)
{
    if (!clipActive(clip))
        return true;
    return clip.selection->isSelected(layerX + clip.layerOffsetX,
                                      layerY + clip.layerOffsetY);
}

/**
 * 把 after 中「选区外」像素恢复为 before（对照 paint 后 apply selection mask）。
 * image 像素 (x,y) 对应层坐标 (originInLayer.x()+x, originInLayer.y()+y)。
 */
void restoreOutsideSelection(QImage &after,
                             const QImage &before,
                             const QPoint &originInLayer,
                             const PaintSelectionClip &clip)
{
    if (!clipActive(clip) || after.isNull() || before.isNull())
        return;
    if (after.size() != before.size())
        return;

    const int w = after.width();
    const int h = after.height();
    for (int y = 0; y < h; ++y) {
        QRgb *dst = reinterpret_cast<QRgb *>(after.scanLine(y));
        const QRgb *src = reinterpret_cast<const QRgb *>(before.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            if (!layerPixelSelected(clip, originInLayer.x() + x, originInLayer.y() + y))
                dst[x] = src[x];
        }
    }
}

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
                          qreal spacing,
                          const PaintSelectionClip &clip)
{
    const QPointF delta = to - from;
    const qreal len = qSqrt(delta.x() * delta.x() + delta.y() * delta.y());
    const qreal step = qMax(0.5, radius * 2.0 * qBound(0.05, spacing, 1.0));

    if (len < 1e-6) {
        PaintEngine::stampDab(target, to, radius, color, mode, hardness, clip);
        return to;
    }

    qreal d = 0.0;
    QPointF last = from;
    while (d <= len) {
        const qreal t = d / len;
        last = from + delta * t;
        PaintEngine::stampDab(target, last, radius, color, mode, hardness, clip);
        d += step;
    }
    if ((last - to).manhattanLength() > 0.5) {
        PaintEngine::stampDab(target, to, radius, color, mode, hardness, clip);
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
                           qreal hardness,
                           PaintSelectionClip clip)
{
    if (!clipActive(clip)) {
        stampDabOnImage(target, center, radius, color, mode, hardness);
        return;
    }
    const QImage before = target;
    stampDabOnImage(target, center, radius, color, mode, hardness);
    restoreOutsideSelection(target, before, QPoint(0, 0), clip);
}

void PaintEngine::stampDab(TileBuffer &tiles,
                           const QPointF &center,
                           qreal radius,
                           const QColor &color,
                           Mode mode,
                           qreal hardness,
                           PaintSelectionClip clip)
{
    // 【功能】在懒分配瓦片上盖一笔圆形 dab；只 ensure 笔触碰到的格
    if (tiles.width() <= 0 || tiles.height() <= 0 || radius <= 0.0)
        return;

    const QRect dabRect = dabBounds(center, radius);
    // allocateMissing=true：第一次画到的透明格在此分配
    tiles.forEachTileInRect(dabRect, true, [&](int, int, QImage &tile, const QRect &bounds) {
        // 图像坐标 → 本瓦片局部坐标再画，避免越界写邻格
        const QPointF local(center.x() - bounds.x(), center.y() - bounds.y());
        if (!clipActive(clip)) {
            stampDabOnImage(tile, local, radius, color, mode, hardness);
            return;
        }
        const QImage before = tile;
        stampDabOnImage(tile, local, radius, color, mode, hardness);
        restoreOutsideSelection(tile, before, bounds.topLeft(), clip);
    });
}

QPointF PaintEngine::strokeSegment(QImage &target,
                                   const QPointF &from,
                                   const QPointF &to,
                                   qreal radius,
                                   const QColor &color,
                                   Mode mode,
                                   qreal hardness,
                                   qreal spacing,
                                   PaintSelectionClip clip)
{
    return strokeSegmentImpl(target, from, to, radius, color, mode, hardness, spacing, clip);
}

QPointF PaintEngine::strokeSegment(TileBuffer &tiles,
                                   const QPointF &from,
                                   const QPointF &to,
                                   qreal radius,
                                   const QColor &color,
                                   Mode mode,
                                   qreal hardness,
                                   qreal spacing,
                                   PaintSelectionClip clip)
{
    return strokeSegmentImpl(tiles, from, to, radius, color, mode, hardness, spacing, clip);
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
                             bool contiguous,
                             PaintSelectionClip clip)
{
    // 【功能】油漆桶：种子相似色区域写入填充色；有选区时最终 ∩ mask
    // 【对照 GIMP】contiguous-region 得区域 → 与 selection 相交 → apply_buffer
    const int w = tiles.width();
    const int h = tiles.height();
    if (w <= 0 || h <= 0)
        return {};
    if (seed.x() < 0 || seed.y() < 0 || seed.x() >= w || seed.y() >= h)
        return {};
    // 种子在选区外：不填充
    if (!layerPixelSelected(clip, seed.x(), seed.y()))
        return {};

    const int tol = qBound(0, tolerance, 255);
    const QRgb fillPx = toPremultipliedRgb(fillColor);

    QImage img = tiles.materialize();
    if (img.format() != QImage::Format_ARGB32_Premultiplied)
        img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    const QImage before = img; // 选区外最终要恢复成填充前的像素

    const QRgb seedPx = img.pixel(seed.x(), seed.y());
    if (seedPx == fillPx)
        return {}; // 种子已是目标色，无需再填

    int seedR = 0, seedG = 0, seedB = 0, seedA = 0;
    unpremultiplyRgb(seedPx, &seedR, &seedG, &seedB, &seedA);

    // 先算「拟填充区域」mask（忽略选区）；透明层上会覆盖整层透明连通域。
    // 然后再与 Selection 求交（对照 GIMP apply 前 ∩ mask），避免只靠 BFS 内联判断漏裁。
    QImage region(w, h, QImage::Format_Grayscale8);
    region.fill(0);

    auto markAt = [&](int x, int y) {
        region.scanLine(y)[x] = 255;
    };

    auto matchesColor = [&](int x, int y) {
        return similarToSeed(img.pixel(x, y), seedR, seedG, seedB, seedA, tol);
    };

    if (!contiguous) {
        for (int y = 0; y < h; ++y) {
            uchar *line = region.scanLine(y);
            for (int x = 0; x < w; ++x) {
                if (matchesColor(x, y))
                    line[x] = 255;
            }
        }
    } else {
        QVector<quint8> visited(w * h, 0);
        QQueue<QPoint> queue;
        queue.enqueue(seed);
        visited[seed.y() * w + seed.x()] = 1;

        while (!queue.isEmpty()) {
            const QPoint p = queue.dequeue();
            if (!matchesColor(p.x(), p.y()))
                continue;
            markAt(p.x(), p.y());

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
                if (matchesColor(n.x(), n.y()))
                    queue.enqueue(n);
            }
        }
    }

    // ∩ 选区：非选中位置从 region 清掉
    if (clipActive(clip)) {
        for (int y = 0; y < h; ++y) {
            uchar *line = region.scanLine(y);
            for (int x = 0; x < w; ++x) {
                if (line[x] == 0)
                    continue;
                if (!layerPixelSelected(clip, x, y))
                    line[x] = 0;
            }
        }
    }

    int minX = w, minY = h, maxX = -1, maxY = -1;
    bool any = false;
    for (int y = 0; y < h; ++y) {
        const uchar *line = region.constScanLine(y);
        QRgb *pix = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            if (line[x] == 0)
                continue;
            pix[x] = fillPx;
            any = true;
            minX = qMin(minX, x);
            minY = qMin(minY, y);
            maxX = qMax(maxX, x);
            maxY = qMax(maxY, y);
        }
    }

    if (!any)
        return {};

    // 双保险：选区外强制恢复填充前像素（与笔刷 dab 的 restoreOutsideSelection 同思路）
    restoreOutsideSelection(img, before, QPoint(0, 0), clip);

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
                                 bool dither,
                                 PaintSelectionClip clip)
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

    // 有选区时只扫选区外接框 ∩ 层（对照 mask_intersect 缩小工作区）
    int x0 = 0, y0 = 0, x1 = w - 1, y1 = h - 1;
    if (clipActive(clip)) {
        const QRect selDoc = clip.selection->bounds();
        const QRect selLayer = selDoc.translated(-clip.layerOffsetX, -clip.layerOffsetY)
                                   .intersected(QRect(0, 0, w, h));
        if (selLayer.isEmpty())
            return {};
        x0 = selLayer.left();
        y0 = selLayer.top();
        x1 = selLayer.right();
        y1 = selLayer.bottom();
    }

    bool any = false;
    for (int y = y0; y <= y1; ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(overlay.scanLine(y));
        for (int x = x0; x <= x1; ++x) {
            if (!layerPixelSelected(clip, x, y))
                continue;

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
            any = true;
        }
    }

    if (!any)
        return {};

    // SourceOver 叠回图层（对照 apply_buffer + paint opacity）
    {
        QPainter painter(&base);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        if (clipActive(clip))
            painter.setClipRect(QRect(QPoint(x0, y0), QPoint(x1, y1)));
        painter.drawImage(0, 0, overlay);
    }

    tiles.setFromImage(base);
    return QRect(QPoint(x0, y0), QPoint(x1, y1));
}

} // namespace Ps
