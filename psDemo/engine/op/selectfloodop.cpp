/**
 * selectfloodop.cpp — SelectFloodOp 实现（engine/op 层）。
 *
 * 洪泛逻辑对齐 FloodFillOp（相似色 + BFS / 全图扫）；目标是 Selection 而非瓦片。
 */
#include "selectfloodop.h"

#include "domain/selection.h"
#include "engine/premul.h"

#include <QQueue>
#include <QtGlobal>

namespace Ps {

namespace {

bool similarToSeed(QRgb candidate, int seedR, int seedG, int seedB, int seedA, int tol)
{
    int r, g, b, a;
    Premul::unpremultiplyRgb(candidate, &r, &g, &b, &a);

    if (seedA == 0)
        return qAbs(a - seedA) <= tol;
    if (a == 0)
        return false;

    const int maxDiff = qMax(qAbs(r - seedR), qMax(qAbs(g - seedG), qAbs(b - seedB)));
    return maxDiff <= tol;
}

} // namespace

bool SelectFloodOp::prepare(OpContext &ctx)
{
    if (!ctx.selection || ctx.selection->mask().isNull())
        return false;
    if (m_sample.isNull())
        return false;

    QImage sample = m_sample;
    if (sample.format() != QImage::Format_ARGB32_Premultiplied)
        sample = sample.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    const int w = ctx.selection->width();
    const int h = ctx.selection->height();
    if (sample.width() != w || sample.height() != h)
        return false;
    if (m_seed.x() < 0 || m_seed.y() < 0 || m_seed.x() >= w || m_seed.y() >= h)
        return false;

    m_sample = sample;
    m_tol = qBound(0, m_tolerance, 255);

    const QRgb seedPx = reinterpret_cast<const QRgb *>(m_sample.constScanLine(m_seed.y()))[m_seed.x()];
    Premul::unpremultiplyRgb(seedPx, &m_seedR, &m_seedG, &m_seedB, &m_seedA);

    m_region = QImage(w, h, QImage::Format_Grayscale8);
    m_region.fill(0);
    return true;
}

QRect SelectFloodOp::process(OpContext &ctx)
{
    const int w = m_region.width();
    const int h = m_region.height();

    auto maskAt = [&](int x, int y) -> uchar & {
        return m_region.scanLine(y)[x];
    };
    auto matches = [&](const QRgb *line, int x) {
        return similarToSeed(line[x], m_seedR, m_seedG, m_seedB, m_seedA, m_tol);
    };
    auto lineAt = [&](int y) {
        return reinterpret_cast<const QRgb *>(m_sample.constScanLine(y));
    };

    if (!m_contiguous) {
        // 对照 by_color：整图相似色
        for (int y = 0; y < h; ++y) {
            const QRgb *src = lineAt(y);
            for (int x = 0; x < w; ++x) {
                if (matches(src, x))
                    maskAt(x, y) = 255;
            }
        }
    } else {
        QQueue<QPoint> queue;
        queue.enqueue(m_seed);
        maskAt(m_seed.x(), m_seed.y()) = 1;

        while (!queue.isEmpty()) {
            const QPoint p = queue.dequeue();
            if (!matches(lineAt(p.y()), p.x()))
                continue;
            maskAt(p.x(), p.y()) = 255;

            const QPoint nbs[] = {
                QPoint(p.x() + 1, p.y()),
                QPoint(p.x() - 1, p.y()),
                QPoint(p.x(), p.y() + 1),
                QPoint(p.x(), p.y() - 1),
            };
            for (const QPoint &n : nbs) {
                if (n.x() < 0 || n.y() < 0 || n.x() >= w || n.y() >= h)
                    continue;
                if (maskAt(n.x(), n.y()) != 0)
                    continue;
                maskAt(n.x(), n.y()) = 1;
                if (matches(lineAt(n.y()), n.x()))
                    queue.enqueue(n);
            }
        }
    }

    int minX = w, minY = h, maxX = -1, maxY = -1;
    for (int y = 0; y < h; ++y) {
        const uchar *mask = m_region.constScanLine(y);
        for (int x = 0; x < w; ++x) {
            if (mask[x] != 255)
                continue;
            minX = qMin(minX, x);
            minY = qMin(minY, y);
            maxX = qMax(maxX, x);
            maxY = qMax(maxY, y);
        }
    }
    if (maxX < 0) {
        if (m_op == ChannelOp::Replace)
            ctx.selection->clear();
        return {};
    }

    const QRect area(QPoint(minX, minY), QPoint(maxX, maxY));
    ctx.selection->combineShapeMask(m_region, m_op, area);
    return area;
}

void SelectFloodOp::finish(OpContext &ctx)
{
    Q_UNUSED(ctx)
    m_sample = QImage();
    m_region = QImage();
}

} // namespace Ps
