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
#include "engine/cache/memstats.h"
#include "engine/cache/tilecache.h"

#include <QElapsedTimer>
#include <QPair>
#include <QThreadPool>
#include <QtConcurrent>
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

void Projection::invalidateChunkValidity()
{
    if (!m_chunkValid.isEmpty())
        m_chunkValid.fill(false);
}

qint64 Projection::bufferBytes() const
{
    return m_buffer.isNull() ? 0 : qint64(m_buffer.sizeInBytes());
}

bool Projection::adoptImage(const QImage &image, const QRect &coveredDoc)
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
    const QRect full(0, 0, m_document->width(), m_document->height());
    const QRect covered = coveredDoc.isNull() ? full : coveredDoc.intersected(full);
    if (covered.isEmpty()) {
        m_chunkValid.fill(false);
    } else if (covered.contains(full)) {
        m_chunkValid.fill(true);
    } else {
        m_chunkValid.fill(false);
        markChunksValid(covered);
    }
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

QRect Projection::recomposeInvalidChunks(int maxChunks, ScanMode mode, int maxMs)
{
    if (!m_document || m_chunkValid.isEmpty() || m_buffer.isNull())
        return {};

    const QRect full(0, 0, m_document->width(), m_document->height());
    if (full.isEmpty())
        return {};

    QElapsedTimer clock;
    if (maxMs > 0)
        clock.start();

    const QRect priority = m_priorityRect.intersected(full);

    // —— 1) 收集候选 chunk（只收集、不合成；顺序 = 扫描顺序）——
    QVector<QPair<int, int>> cand; // (col, row)
    auto addRange = [&](int c0, int r0, int c1, int r1, bool wantPriority) {
        for (int row = r0; row <= r1; ++row) {
            for (int col = c0; col <= c1; ++col) {
                const int idx = row * m_chunkCols + col;
                if (m_chunkValid.testBit(idx))
                    continue;
                const QRect piece = chunkRectAt(col, row, full);
                if (piece.isEmpty()) {
                    m_chunkValid.setBit(idx);
                    continue;
                }
                const bool inPri = !priority.isEmpty() && piece.intersects(priority);
                if (inPri != wantPriority)
                    continue;
                cand.append(qMakePair(col, row));
            }
        }
    };

    if (mode != ScanMode::NonPriorityOnly && !priority.isEmpty()) {
        const int c0 = qBound(0, priority.left() / kChunkSize, m_chunkCols - 1);
        const int r0 = qBound(0, priority.top() / kChunkSize, m_chunkRows - 1);
        const int c1 = qBound(0, priority.right() / kChunkSize, m_chunkCols - 1);
        const int r1 = qBound(0, priority.bottom() / kChunkSize, m_chunkRows - 1);
        addRange(c0, r0, c1, r1, /*wantPriority=*/true);
    }
    if (mode != ScanMode::PriorityOnly)
        addRange(0, 0, m_chunkCols - 1, m_chunkRows - 1, /*wantPriority=*/false);

    // —— 2) 分批合成：同一时间预算下，并行能把视口更快铺满（单帧上限不变）——
    QRect painted;
    int done = 0;
    const int threads = qMax(1, QThreadPool::globalInstance()->maxThreadCount());
    const int batchCap = qMax(1, threads * 4);

    // 并行前在主线程：钉住并预加载层瓦片（A5）、预热 mutable 缓存、确保投影缓冲独占
    // （否则 worker 首次 scanLine 会各自 detach）。
    TileSharedReadGuard sharedTiles(m_document);
    Compositor::warmLayerCaches(*m_document);
    (void)m_buffer.bits();

    // 并行批次每次 blockingMap 的固定开销（唤醒 + 汇总）约数 ms：按「预算 − 固定开销」
    // 折算块数，否则小预算下一次 batches 就会冲破预算。
    constexpr double kBatchOverheadMs = 4.0;

    for (int start = 0; start < cand.size();) {
        if (maxChunks > 0 && done >= maxChunks)
            break;
        if (maxMs > 0 && clock.elapsed() >= maxMs)
            break;

        int take;
        if (maxMs > 0 && threads > 1) {
            const double usable = double(maxMs) - kBatchOverheadMs;
            take = (usable > 0.0) ? int(usable / m_msPerChunkParallel) : 1;
        } else {
            take = batchCap;
        }
        take = qBound(1, take, batchCap);
        take = qMin(take, int(cand.size()) - start);
        if (maxChunks > 0)
            take = qMin(take, maxChunks - done);
        if (take <= 0)
            break;

        if (take >= 2 && threads > 1) {
            struct PieceJob {
                QRect rect;
                bool ok = false;
            };
            QVector<PieceJob> jobs;
            jobs.reserve(take);
            for (int k = 0; k < take; ++k)
                jobs.append({chunkRectAt(cand[start + k].first, cand[start + k].second, full),
                             false});

            QElapsedTimer batchClock;
            batchClock.start();
            QtConcurrent::blockingMap(jobs, [this](PieceJob &job) {
                job.ok = Compositor::compositeRegion(m_buffer, *m_document, job.rect);
            });
            // 只用「足够大」的批次更新估计：小批次被固定开销主导，会把估计越推越高
            if (take >= threads) {
                const double per = (batchClock.nsecsElapsed() / 1e6) / take;
                m_msPerChunkParallel =
                    qBound(0.05, 0.7 * m_msPerChunkParallel + 0.3 * per, 2.0);
            }

            for (const PieceJob &job : jobs) {
                if (!job.ok)
                    continue;
                markChunksValid(job.rect);
                painted = painted.isNull() ? job.rect : painted.united(job.rect);
            }
        } else {
            for (int k = 0; k < take; ++k) {
                const QRect piece =
                    chunkRectAt(cand[start + k].first, cand[start + k].second, full);
                if (!Compositor::compositeRegion(m_buffer, *m_document, piece))
                    continue;
                markChunksValid(piece);
                painted = painted.isNull() ? piece : painted.united(piece);
            }
        }
        done += take;
        start += take;
    }

    return painted;
}

QRect Projection::sync(int maxMs)
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

    QElapsedTimer clock;
    if (maxMs > 0)
        clock.start();

    // 1) 视口内：限额（对照 GIMP flush 后 chunk 渐进，而非一帧打完视口）
    QRect painted = recomposeInvalidChunks(kSyncPriorityBudget, ScanMode::PriorityOnly, maxMs);

    // 2) 视口外：小量预算；剩多则 idle（与阶段 1 **共用**同一时间预算，故总耗时 ≤ maxMs）
    int remain = maxMs;
    if (maxMs > 0)
        remain = maxMs - int(clock.elapsed());
    if (remain > 0 || maxMs <= 0) {
        const int left = invalidChunkCount();
        if (left > 0 && left <= kSyncChunkBudget) {
            painted = painted.united(
                recomposeInvalidChunks(-1, ScanMode::NonPriorityOnly, remain));
        } else if (left > 0) {
            painted = painted.united(
                recomposeInvalidChunks(kSyncChunkBudget, ScanMode::NonPriorityOnly, remain));
        }
    }

    // 静默点：投影同步后再 trim（绝不在 ensureTile 里淘汰）
    TileCache::instance().trim(m_document, this);
    maybeLogMem(captureMemSnapshot(m_document, this,
                                   m_document ? &m_document->history() : nullptr));

    return painted;
}

QRect Projection::processPendingChunks(int maxChunks, int maxMs)
{
    if (!m_document)
        return {};
    // A31：与 sync 一样先对齐缓冲/网格，避免按旧尺寸标有效
    if (!ensureBuffer(QSize(m_document->width(), m_document->height())))
        return {};
    if (!hasPendingChunks())
        return {};
    const QRect painted = recomposeInvalidChunks(maxChunks, ScanMode::PriorityThenRest, maxMs);
    TileCache::instance().trim(m_document, this);
    return painted;
}

QRect Projection::flushPriority(int maxMs)
{
    if (!m_document)
        return {};
    if (!ensureBuffer(QSize(m_document->width(), m_document->height())))
        return {};
    if (!hasPendingChunks())
        return {};
    return recomposeInvalidChunks(-1, ScanMode::PriorityOnly, maxMs);
}

} // namespace Ps
