/**
 * projection.cpp — projection.h 实现（engine 层）。
 *
 * sync() 吸入 dirtyRect；视口内限额立即 patch，其余 idle 渐进。
 * 对照 GIMP gimp_projection_add_update_area / chunk_render_* / set_priority_rect。
 *
 * 注意：切勿在 sync 里对「视口优先」使用不限块数且连带扫完全图——大层拖动时
 * 每帧会把旧∪新 bounds 内所有块（含视口外）同步合成，表现为严重卡顿。
 */
#include "projection.h"

#include "compositor.h"
#include "domain/imagedocument.h"
#include "domain/tilebuffer.h"

#include <QtGlobal>

namespace Ps {

namespace {

QRect alignToChunks(const QRect &rect, int chunk, const QRect &bounds)
{
    if (rect.isEmpty() || chunk <= 0)
        return rect.intersected(bounds);

    const int x0 = qMax(bounds.left(), (rect.left() / chunk) * chunk);
    const int y0 = qMax(bounds.top(), (rect.top() / chunk) * chunk);
    const int x1 = qMin(bounds.right(),
                        ((rect.right() + chunk) / chunk) * chunk - 1);
    const int y1 = qMin(bounds.bottom(),
                        ((rect.bottom() + chunk) / chunk) * chunk - 1);
    if (x1 < x0 || y1 < y0)
        return {};
    return QRect(QPoint(x0, y0), QPoint(x1, y1));
}

} // namespace

void Projection::bind(ImageDocument *document)
{
    if (m_document == document)
        return;
    m_document = document;
    invalidate();
}

void Projection::invalidate()
{
    m_buffer = QImage();
    m_chunkValid.clear();
    m_chunkCols = 0;
    m_chunkRows = 0;
}

bool Projection::adoptImage(const QImage &image)
{
    if (image.isNull() || !m_document)
        return false;
    if (image.width() != m_document->width() || image.height() != m_document->height())
        return false;

    if (image.format() == QImage::Format_ARGB32_Premultiplied)
        m_buffer = image;
    else
        m_buffer = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    resetChunkGrid(m_buffer.size());
    m_chunkValid.fill(true);
    return true;
}

void Projection::setPriorityRect(const QRect &docRect)
{
    m_priorityRect = docRect;
}

void Projection::resetChunkGrid(const QSize &pixelSize)
{
    m_chunkCols = (pixelSize.width() + kChunkSize - 1) / kChunkSize;
    m_chunkRows = (pixelSize.height() + kChunkSize - 1) / kChunkSize;
    m_chunkValid = QBitArray(m_chunkCols * m_chunkRows, false);
}

void Projection::invalidateChunks(const QRect &pixelRect)
{
    if (m_chunkValid.isEmpty() || pixelRect.isEmpty())
        return;
    const int c0 = qBound(0, pixelRect.left() / kChunkSize, m_chunkCols - 1);
    const int r0 = qBound(0, pixelRect.top() / kChunkSize, m_chunkRows - 1);
    const int c1 = qBound(0, pixelRect.right() / kChunkSize, m_chunkCols - 1);
    const int r1 = qBound(0, pixelRect.bottom() / kChunkSize, m_chunkRows - 1);
    for (int r = r0; r <= r1; ++r) {
        for (int c = c0; c <= c1; ++c)
            m_chunkValid.clearBit(r * m_chunkCols + c);
    }
}

void Projection::markChunksValid(const QRect &pixelRect)
{
    if (m_chunkValid.isEmpty() || pixelRect.isEmpty())
        return;
    const int c0 = qBound(0, pixelRect.left() / kChunkSize, m_chunkCols - 1);
    const int r0 = qBound(0, pixelRect.top() / kChunkSize, m_chunkRows - 1);
    const int c1 = qBound(0, pixelRect.right() / kChunkSize, m_chunkCols - 1);
    const int r1 = qBound(0, pixelRect.bottom() / kChunkSize, m_chunkRows - 1);
    for (int r = r0; r <= r1; ++r) {
        for (int c = c0; c <= c1; ++c)
            m_chunkValid.setBit(r * m_chunkCols + c);
    }
}

bool Projection::ensureBuffer(const QSize &pixelSize)
{
    static_assert(kChunkSize == TileBuffer::kTileSize,
                  "Projection chunk size should match layer tile size");

    const bool sizeOk = !m_buffer.isNull()
                        && m_buffer.size() == pixelSize
                        && m_buffer.format() == QImage::Format_ARGB32_Premultiplied
                        && m_chunkCols > 0
                        && m_chunkValid.size() == m_chunkCols * m_chunkRows;
    if (sizeOk)
        return true;

    m_buffer = QImage(pixelSize, QImage::Format_ARGB32_Premultiplied);
    m_buffer.fill(Qt::transparent);
    resetChunkGrid(pixelSize);
    return false;
}

QRect Projection::chunkRectAt(int col, int row, const QRect &full) const
{
    return QRect(col * kChunkSize, row * kChunkSize, kChunkSize, kChunkSize)
        .intersected(full);
}

int Projection::invalidChunkCount() const
{
    int n = 0;
    for (int i = 0; i < m_chunkValid.size(); ++i) {
        if (!m_chunkValid.testBit(i))
            ++n;
    }
    return n;
}

bool Projection::hasPendingChunks() const
{
    if (m_chunkValid.isEmpty())
        return false;
    for (int i = 0; i < m_chunkValid.size(); ++i) {
        if (!m_chunkValid.testBit(i))
            return true;
    }
    return false;
}

QRect Projection::recomposeInvalidChunks(int maxChunks, ScanMode mode)
{
    if (!m_document || m_chunkValid.isEmpty() || m_buffer.isNull())
        return {};

    const QRect full(0, 0, m_document->width(), m_document->height());
    if (full.isEmpty())
        return {};

    const QRect priority = m_priorityRect.intersected(full);
    QRect painted;
    int done = 0;

    auto tryChunk = [&](int col, int row) -> bool {
        const int idx = row * m_chunkCols + col;
        if (m_chunkValid.testBit(idx))
            return false;
        const QRect piece = chunkRectAt(col, row, full);
        if (piece.isEmpty()) {
            m_chunkValid.setBit(idx);
            return false;
        }
        if (!Compositor::compositeRegion(m_buffer, *m_document, piece))
            return false;
        m_chunkValid.setBit(idx);
        painted = painted.isNull() ? piece : painted.united(piece);
        ++done;
        return true;
    };

    auto budgetOk = [&]() {
        return maxChunks <= 0 || done < maxChunks;
    };

    auto scanPriority = [&]() {
        if (priority.isEmpty())
            return;
        const int c0 = qBound(0, priority.left() / kChunkSize, m_chunkCols - 1);
        const int r0 = qBound(0, priority.top() / kChunkSize, m_chunkRows - 1);
        const int c1 = qBound(0, priority.right() / kChunkSize, m_chunkCols - 1);
        const int r1 = qBound(0, priority.bottom() / kChunkSize, m_chunkRows - 1);
        for (int row = r0; row <= r1 && budgetOk(); ++row) {
            for (int col = c0; col <= c1 && budgetOk(); ++col) {
                const QRect piece = chunkRectAt(col, row, full);
                if (!piece.intersects(priority))
                    continue;
                tryChunk(col, row);
            }
        }
    };

    auto scanNonPriority = [&]() {
        for (int row = 0; row < m_chunkRows && budgetOk(); ++row) {
            for (int col = 0; col < m_chunkCols && budgetOk(); ++col) {
                const QRect piece = chunkRectAt(col, row, full);
                if (!priority.isEmpty() && piece.intersects(priority))
                    continue;
                tryChunk(col, row);
            }
        }
    };

    switch (mode) {
    case ScanMode::PriorityOnly:
        scanPriority();
        break;
    case ScanMode::NonPriorityOnly:
        scanNonPriority();
        break;
    case ScanMode::PriorityThenRest:
        scanPriority();
        scanNonPriority();
        break;
    }
    return painted;
}

QRect Projection::sync()
{
    if (!m_document) {
        invalidate();
        return {};
    }

    const int w = m_document->width();
    const int h = m_document->height();
    if (w <= 0 || h <= 0) {
        invalidate();
        return {};
    }

    const QRect full(0, 0, w, h);
    const QSize fullSize(w, h);
    QRect dirty = m_document->dirtyRect().intersected(full);

    const bool bufferWasOk = ensureBuffer(fullSize);
    if (!bufferWasOk) {
        dirty = full;
    } else if (dirty.isEmpty()) {
        return {};
    }

    if (!dirty.isEmpty() && dirty != full)
        dirty = alignToChunks(dirty, kChunkSize, full);
    if (!dirty.isEmpty())
        invalidateChunks(dirty);

    m_document->clearDirtyRect();

    // 1) 视口内：限额（对照 GIMP flush 后 chunk 渐进，而非一帧打完视口）
    QRect painted = recomposeInvalidChunks(kSyncPriorityBudget, ScanMode::PriorityOnly);

    // 2) 视口外：小量预算；剩多则 idle
    const int left = invalidChunkCount();
    if (left > 0 && left <= kSyncChunkBudget) {
        painted = painted.united(
            recomposeInvalidChunks(-1, ScanMode::NonPriorityOnly));
    } else if (left > 0) {
        painted = painted.united(
            recomposeInvalidChunks(kSyncChunkBudget, ScanMode::NonPriorityOnly));
    }

    return painted;
}

QRect Projection::processPendingChunks(int maxChunks)
{
    if (!hasPendingChunks())
        return {};
    return recomposeInvalidChunks(maxChunks, ScanMode::PriorityThenRest);
}

QRect Projection::flushPriority()
{
    if (!hasPendingChunks())
        return {};
    return recomposeInvalidChunks(-1, ScanMode::PriorityOnly);
}

} // namespace Ps
