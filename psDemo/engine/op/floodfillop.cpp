#include "floodfillop.h"

#include "paintclip.h"
#include "domain/tilebuffer.h"
#include "engine/premul.h"

#include <QPainter>
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

/** 只物化 rect 覆盖的瓦片（未分配瓦片视为透明），而不是 materialize 整层。 */
QImage materializeWindow(const TileBuffer &tiles, const QRect &rect)
{
    QImage out(rect.width(), rect.height(), QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);

    // painter 必须在 return 前结束作用域（QImage COW：提前 return 会让它悬在拷贝上）
    {
        QPainter p(&out);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        tiles.forEachAllocatedTile([&](int, int, const QImage &tile, const QRect &bounds) {
            const QRect area = bounds.intersected(rect);
            if (area.isEmpty())
                return;
            p.drawImage(area.topLeft() - rect.topLeft(),
                        tile,
                        area.translated(-bounds.topLeft()));
        });
    }
    return out;
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

    // 传播类算子：窗口不按选区外接框截断（见头文件注释）
    m_window = OpPaintClip::roiWindow(ctx.roi, w, h);
    if (!m_window.contains(m_seed))
        return false;

    m_tol = qBound(0, m_tolerance, 255);
    m_fillPx = Premul::toPremultipliedRgb(m_fillColor);

    m_work = materializeWindow(tiles, m_window);
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

    // 掩码与层像素都按 scanLine 走（旧实现用 QImage::pixel()，BFS 里每次带边界检查）
    auto maskAt = [&](int x, int y) -> uchar & {
        return m_region.scanLine(y)[x];
    };
    auto matches = [&](const QRgb *line, int x) {
        return similarToSeed(line[x], m_seedR, m_seedG, m_seedB, m_seedA, m_tol);
    };
    auto workLine = [&](int y) {
        return reinterpret_cast<const QRgb *>(m_work.constScanLine(y));
    };

    if (!m_contiguous) {
        for (int y = 0; y < h; ++y) {
            const QRgb *src = workLine(y);
            for (int x = 0; x < w; ++x) {
                if (matches(src, x))
                    maskAt(x, y) = 255;
            }
        }
    } else {
        const QPoint seedLocal = m_seed - m_window.topLeft();
        QQueue<QPoint> queue;
        queue.enqueue(seedLocal);
        maskAt(seedLocal.x(), seedLocal.y()) = 1;

        while (!queue.isEmpty()) {
            const QPoint p = queue.dequeue();
            if (!matches(workLine(p.y()), p.x()))
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
                if (matches(workLine(n.y()), n.x()))
                    queue.enqueue(n);
            }
        }
    }

    // 按选区裁：洪泛在整层上算完，命中像素才受选区约束（对照 apply_buffer + mask）
    if (OpPaintClip::clipActive(clip)) {
        for (int y = 0; y < h; ++y) {
            uchar *mask = m_region.scanLine(y);
            for (int x = 0; x < w; ++x) {
                if (mask[x] == 0)
                    continue;
                if (!OpPaintClip::layerPixelSelected(clip, m_window.x() + x, m_window.y() + y))
                    mask[x] = 0;
            }
        }
    }

    // 命中范围（窗口局部坐标）→ 层内坐标
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
    if (maxX < 0)
        return {};

    const QRect dirty = QRect(QPoint(minX, minY), QPoint(maxX, maxY)).translated(m_window.topLeft());

    // 只写命中框覆盖的瓦片：不再 setFromImage（那会 reset 全部瓦片再整层重新切块）
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
    // 实例会被 OpRunner 常驻复用，临时图必须在这里释放，不能留到下次
    m_work = QImage();
    m_region = QImage();
}

} // namespace Ps
