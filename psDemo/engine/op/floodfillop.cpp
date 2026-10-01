/**
 * floodfillop.cpp — floodfillop.h 实现（engine/op 层）。
 *
 * BFS/全窗口扫色 + 掩码；栈式 BFS（避免 QQueue 分配）；包围盒随洪泛累计；
 * materialize 用 memcpy（对齐 TilePatch）。
 */
#include "floodfillop.h"

#include "paintclip.h"
#include "tilepatch.h"
#include "domain/tilebuffer.h"
#include "engine/premul.h"

#include <QtGlobal>
#include <vector>

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

bool FloodFillOp::prepare(OpContext &ctx)
{
    if (!BufferOp::prepare(ctx))
        return false;

    const TileBuffer &tiles = *ctx.tiles;
    const PaintSelectionClip &clip = ctx.clip;
    const int w = tiles.width();
    const int h = tiles.height();
    if (w <= 0 || h <= 0)
        return false;
    if (m_seed.x() < 0 || m_seed.y() < 0 || m_seed.x() >= w || m_seed.y() >= h)
        return false;
    if (!OpPaintClip::layerPixelSelected(clip, m_seed.x(), m_seed.y()))
        return false;

    m_window = OpPaintClip::roiWindow(ctx.roi, w, h);
    if (!m_window.contains(m_seed))
        return false;

    m_tol = qBound(0, m_tolerance, 255);
    m_fillPx = Premul::toPremultipliedRgb(m_fillColor);

    m_work = TilePatch::extract(tiles, m_window);
    const QPoint seedLocal = m_seed - m_window.topLeft();
    const QRgb seedPx = reinterpret_cast<const QRgb *>(m_work.constScanLine(seedLocal.y()))[seedLocal.x()];
    if (seedPx == m_fillPx)
        return false;

    Premul::unpremultiplyRgb(seedPx, &m_seedR, &m_seedG, &m_seedB, &m_seedA);

    m_region = QImage(m_window.width(), m_window.height(), QImage::Format_Grayscale8);
    m_region.fill(0);
    return true;
}

QRect FloodFillOp::process(OpContext &ctx)
{
    TileBuffer &tiles = *ctx.tiles;
    const PaintSelectionClip &clip = ctx.clip;
    const int w = m_window.width();
    const int h = m_window.height();

    auto maskAt = [&](int x, int y) -> uchar & {
        return m_region.scanLine(y)[x];
    };
    auto matches = [&](const QRgb *line, int x) {
        return similarToSeed(line[x], m_seedR, m_seedG, m_seedB, m_seedA, m_tol);
    };
    auto workLine = [&](int y) {
        return reinterpret_cast<const QRgb *>(m_work.constScanLine(y));
    };

    int minX = w, minY = h, maxX = -1, maxY = -1;
    auto markHit = [&](int x, int y) {
        maskAt(x, y) = 255;
        minX = qMin(minX, x);
        minY = qMin(minY, y);
        maxX = qMax(maxX, x);
        maxY = qMax(maxY, y);
    };

    if (!m_contiguous) {
        for (int y = 0; y < h; ++y) {
            const QRgb *src = workLine(y);
            for (int x = 0; x < w; ++x) {
                if (matches(src, x))
                    markHit(x, y);
            }
        }
    } else {
        // 栈式 BFS：只入队已匹配像素，避免 QQueue 节点分配 + 二次 matches
        const QPoint seedLocal = m_seed - m_window.topLeft();
        std::vector<QPoint> stack;
        stack.reserve(size_t(w + h) * 4);
        if (matches(workLine(seedLocal.y()), seedLocal.x())) {
            markHit(seedLocal.x(), seedLocal.y());
            stack.push_back(seedLocal);
        }

        while (!stack.empty()) {
            const QPoint p = stack.back();
            stack.pop_back();

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
                if (!matches(workLine(n.y()), n.x())) {
                    maskAt(n.x(), n.y()) = 1; // 已访未命中，防重复探测
                    continue;
                }
                markHit(n.x(), n.y());
                stack.push_back(n);
            }
        }
    }

    // 按选区裁：洪泛在窗口上算完，命中像素才受选区约束
    if (OpPaintClip::clipActive(clip) && maxX >= 0) {
        minX = w;
        minY = h;
        maxX = -1;
        maxY = -1;
        for (int y = 0; y < h; ++y) {
            uchar *mask = m_region.scanLine(y);
            for (int x = 0; x < w; ++x) {
                if (mask[x] != 255)
                    continue;
                if (!OpPaintClip::layerPixelSelected(clip, m_window.x() + x, m_window.y() + y)) {
                    mask[x] = 0;
                    continue;
                }
                minX = qMin(minX, x);
                minY = qMin(minY, y);
                maxX = qMax(maxX, x);
                maxY = qMax(maxY, y);
            }
        }
    }

    if (maxX < 0)
        return {};

    const QRect dirty = QRect(QPoint(minX, minY), QPoint(maxX, maxY)).translated(m_window.topLeft());

    tiles.forEachTileInRect(dirty, true, [&](int, int, QImage &tile, const QRect &bounds) {
        const QRect area = dirty.intersected(bounds);
        if (area.isEmpty())
            return;

        for (int ly = area.top(); ly <= area.bottom(); ++ly) {
            const uchar *mask = m_region.constScanLine(ly - m_window.y());
            QRgb *dst = reinterpret_cast<QRgb *>(tile.scanLine(ly - bounds.y()));
            for (int lx = area.left(); lx <= area.right(); ++lx) {
                if (mask[lx - m_window.x()] == 255)
                    dst[lx - bounds.x()] = m_fillPx;
            }
        }
    });

    return dirty;
}

void FloodFillOp::finish(OpContext &ctx)
{
    Q_UNUSED(ctx);
    m_work = QImage();
    m_region = QImage();
}

} // namespace Ps
