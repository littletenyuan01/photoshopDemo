/**
 * memstats.cpp — 内存口径采集与 PSLITE_MEM 日志。
 */
#include "memstats.h"

#include "tilecache.h"
#include "app/historystack.h"
#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "engine/projection.h"

#include <QByteArray>
#include <cstdio>

namespace Ps {

namespace {

qint64 g_peakBytes = 0;

bool memLogEnabled()
{
    return qgetenv("PSLITE_MEM") == "1";
}

} // namespace

MemSnapshot captureMemSnapshot(const ImageDocument *doc, const Projection *projection,
                               const HistoryStack *history)
{
    MemSnapshot s;
    if (doc) {
        for (int i = 0; i < doc->layers().count(); ++i) {
            const Layer *layer = doc->layers().layerAt(i);
            if (!layer)
                continue;
            s.tilesBytes += layer->tiles().residentBytes();
            s.tileCount += layer->tiles().allocatedTileCount();
        }
    }
    if (history)
        s.undoBytes = qint64(history->historyBytes()); // A26：含 redo
    if (projection && !projection->isNull()) {
        const QImage &img = projection->image();
        s.projectionBytes = qint64(img.sizeInBytes());
    }
    s.swapBytes = TileCache::instance().stats().swapBytes;
    const qint64 total = s.tilesBytes + s.undoBytes + s.projectionBytes + s.swapBytes;
    if (total > g_peakBytes)
        g_peakBytes = total;
    s.peakBytes = g_peakBytes;
    return s;
}

void maybeLogMem(const MemSnapshot &snap)
{
    if (!memLogEnabled())
        return;
    const double mb = 1024.0 * 1024.0;
    std::printf("[mem] tiles=%.2fMB (n=%d) undo=%.2fMB projection=%.2fMB swap=%.2fMB peak=%.2fMB\n",
                snap.tilesBytes / mb, snap.tileCount, snap.undoBytes / mb,
                snap.projectionBytes / mb, snap.swapBytes / mb, snap.peakBytes / mb);
    std::fflush(stdout);
}

qint64 memPeakBytes()
{
    return g_peakBytes;
}

void resetMemPeakForTest()
{
    g_peakBytes = 0;
}

} // namespace Ps
