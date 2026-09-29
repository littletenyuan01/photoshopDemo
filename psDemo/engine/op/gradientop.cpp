#include "gradientop.h"

#include "paintclip.h"
#include "domain/tilebuffer.h"
#include "engine/premul.h"

#include <QRandomGenerator>
#include <QtGlobal>
#include <QtMath>

namespace Ps {

namespace {

QColor lerpColor(const QColor &a, const QColor &b, qreal t)
{
    t = qBound(0.0, t, 1.0);
    return QColor(
        qRound(a.red() + (b.red() - a.red()) * t),
        qRound(a.green() + (b.green() - a.green()) * t),
        qRound(a.blue() + (b.blue() - a.blue()) * t),
        qRound(a.alpha() + (b.alpha() - a.alpha()) * t));
}

qreal applyGimpOffset(qreal rat, qreal offset01)
{
    if (rat < offset01)
        return 0.0;
    if (offset01 >= 1.0 - 1e-9)
        return (rat >= 1.0) ? 1.0 : 0.0;
    return (rat - offset01) / (1.0 - offset01);
}

qreal factorLinear(qreal dist, qreal vx, qreal vy, qreal offset01, qreal lx, qreal ly)
{
    if (dist <= 0.0)
        return 0.0;
    const qreal rat = (vx * lx + vy * ly) / dist;
    if (rat < 0.0)
        return rat / (1.0 - offset01);
    return applyGimpOffset(rat, offset01);
}

qreal factorBilinear(qreal dist, qreal vx, qreal vy, qreal offset01, qreal lx, qreal ly)
{
    if (dist <= 0.0)
        return 0.0;
    const qreal rat = qAbs((vx * lx + vy * ly) / dist);
    return applyGimpOffset(rat, offset01);
}

qreal factorRadial(qreal dist, qreal offset01, qreal lx, qreal ly)
{
    if (dist <= 0.0)
        return 0.0;
    const qreal rat = qSqrt(lx * lx + ly * ly) / dist;
    return applyGimpOffset(rat, offset01);
}

qreal factorSquare(qreal dist, qreal offset01, qreal lx, qreal ly)
{
    if (dist <= 0.0)
        return 0.0;
    const qreal rat = qMax(qAbs(lx), qAbs(ly)) / dist;
    return applyGimpOffset(rat, offset01);
}

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

/** 除以 255 的舍入式（与 Qt 混合内部同式），用于预乘 SourceOver。 */
inline int div255(int v)
{
    return (v + 128) * 257 >> 16;
}

/** 预乘 ARGB32 的 SourceOver：out = src + dst × (1 − srcA)。 */
inline QRgb sourceOverPremul(QRgb src, QRgb dst)
{
    const int sa = qAlpha(src);
    if (sa == 255)
        return src;
    if (sa == 0)
        return dst;
    const int ia = 255 - sa;
    return qRgba(qMin(qRed(src) + div255(qRed(dst) * ia), 255),
                 qMin(qGreen(src) + div255(qGreen(dst) * ia), 255),
                 qMin(qBlue(src) + div255(qBlue(dst) * ia), 255),
                 qMin(sa + div255(qAlpha(dst) * ia), 255));
}

} // namespace

bool GradientOp::prepare(OpContext &ctx)
{
    if (!BufferOp::prepare(ctx))
        return false;

    const TileBuffer &tiles = *ctx.tiles;
    if (tiles.width() <= 0 || tiles.height() <= 0)
        return false;

    m_opacityClamped = qBound(0.0, m_opacity, 1.0);
    m_offsetClamped = qBound(0, m_offsetPercent, 100);
    if (m_opacityClamped <= 0.0)
        return false;

    m_window = OpPaintClip::operationWindow(ctx.clip, ctx.roi, tiles.width(), tiles.height());
    if (m_window.isEmpty())
        return false;

    const qreal dx = m_end.x() - m_start.x();
    const qreal dy = m_end.y() - m_start.y();
    m_dist = 0.0;
    m_vx = 0.0;
    m_vy = 0.0;

    switch (m_type) {
    case GradientType::Radial:
        m_dist = qSqrt(dx * dx + dy * dy);
        break;
    case GradientType::Diamond:
        m_dist = qMax(qAbs(dx), qAbs(dy));
        break;
    case GradientType::Linear:
    case GradientType::Reflected:
    case GradientType::Angle:
        m_dist = qSqrt(dx * dx + dy * dy);
        if (m_dist > 0.0) {
            m_vx = dx / m_dist;
            m_vy = dy / m_dist;
        }
        break;
    }

    return true;
}

QRect GradientOp::process(OpContext &ctx)
{
    TileBuffer &tiles = *ctx.tiles;
    const PaintSelectionClip &clip = ctx.clip;

    const QColor c0 = m_reverse ? m_bg : m_fg;
    const QColor c1 = m_reverse ? m_fg : m_bg;
    const qreal offset01 = m_offsetClamped / 100.0;
    QRandomGenerator *rng = m_dither ? QRandomGenerator::global() : nullptr;

    bool any = false;
    int minX = m_window.right() + 1;
    int minY = m_window.bottom() + 1;
    int maxX = -1;
    int maxY = -1;

    tiles.forEachTileInRect(m_window, true, [&](int, int, QImage &tile, const QRect &bounds) {
        const QRect area = m_window.intersected(bounds);
        if (area.isEmpty())
            return;

        for (int ly = area.top(); ly <= area.bottom(); ++ly) {
            QRgb *line = reinterpret_cast<QRgb *>(tile.scanLine(ly - bounds.y()));
            for (int lx = area.left(); lx <= area.right(); ++lx) {
                if (!OpPaintClip::layerPixelSelected(clip, lx, ly))
                    continue;

                const qreal gx = (lx + 0.5) - m_start.x();
                const qreal gy = (ly + 0.5) - m_start.y();

                qreal factor = 0.0;
                switch (m_type) {
                case GradientType::Linear:
                    factor = factorLinear(m_dist, m_vx, m_vy, offset01, gx, gy);
                    break;
                case GradientType::Radial:
                    factor = factorRadial(m_dist, offset01, gx, gy);
                    break;
                case GradientType::Angle:
                    factor = factorConicalAsym(m_dist, m_vx, m_vy, m_offsetClamped, gx, gy);
                    break;
                case GradientType::Reflected:
                    factor = factorBilinear(m_dist, m_vx, m_vy, offset01, gx, gy);
                    break;
                case GradientType::Diamond:
                    factor = factorSquare(m_dist, offset01, gx, gy);
                    break;
                }
                factor = qBound(0.0, factor, 1.0);

                QColor col = lerpColor(c0, c1, factor);
                if (rng) {
                    const qreal n = (rng->generateDouble() - 0.5) / 256.0;
                    col.setRedF(qBound(0.0, col.redF() + n, 1.0));
                    col.setGreenF(qBound(0.0, col.greenF() + n, 1.0));
                    col.setBlueF(qBound(0.0, col.blueF() + n, 1.0));
                }
                col.setAlpha(qRound(col.alpha() * m_opacityClamped));

                QRgb &dst = line[lx - bounds.x()];
                dst = sourceOverPremul(Premul::toPremultipliedRgb(col), dst);

                any = true;
                minX = qMin(minX, lx);
                minY = qMin(minY, ly);
                maxX = qMax(maxX, lx);
                maxY = qMax(maxY, ly);
            }
        }
    });

    if (!any)
        return {};
    return QRect(QPoint(minX, minY), QPoint(maxX, maxY));
}

} // namespace Ps
