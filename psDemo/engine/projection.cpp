/**
 * projection.cpp — projection.h 实现（engine 层）。
 *
 * sync() 读文档 dirtyRect，小脏区走 Compositor::compositeRegion，否则全量重合成。
 */
#include "projection.h"

#include "compositor.h"
#include "domain/imagedocument.h"
#include "domain/tilebuffer.h"

#include <QtGlobal>

namespace Ps {

namespace {

/** 将脏矩形对齐到 chunk 网格并裁剪到 bounds。 */
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
    QRect dirty = m_document->dirtyRect().intersected(full);
    const QSize fullSize(w, h);

    const bool bufferOk = !m_buffer.isNull()
                          && m_buffer.size() == fullSize
                          && m_buffer.format() == QImage::Format_ARGB32_Premultiplied
                          && m_chunkCols > 0
                          && m_chunkValid.size() == m_chunkCols * m_chunkRows;

    if (bufferOk && dirty.isEmpty())
        return {};

    static_assert(kChunkSize == TileBuffer::kTileSize,
                  "Projection chunk size should match layer tile size");

    if (!dirty.isEmpty() && dirty != full)
        dirty = alignToChunks(dirty, kChunkSize, full);

    const bool patch = bufferOk
                       && !dirty.isEmpty()
                       && dirty != full
                       && (qint64(dirty.width()) * dirty.height()
                           < qint64(full.width()) * full.height());

    if (patch) {
        invalidateChunks(dirty);
        if (!Compositor::compositeRegion(m_buffer, *m_document, dirty)) {
            m_buffer = Compositor::composite(*m_document);
            resetChunkGrid(fullSize);
            m_chunkValid.fill(true);
            dirty = full;
        } else {
            markChunksValid(dirty);
        }
    } else {
        m_buffer = Compositor::composite(*m_document);
        resetChunkGrid(fullSize);
        m_chunkValid.fill(true);
        dirty = full;
    }

    m_document->clearDirtyRect();
    return dirty;
}

} // namespace Ps
