/**
 * memstats.h — PSLITE_MEM=1 内存口径（engine/cache）。
 *
 * 打印 [mem] tiles/undo/projection/swap/peak；见 docs/page-cache-plan.md §3 A / §5#8。
 */
#ifndef ENGINE_CACHE_MEMSTATS_H
#define ENGINE_CACHE_MEMSTATS_H

#include <QtGlobal>

namespace Ps {

class ImageDocument;
class Projection;
class HistoryStack;

struct MemSnapshot {
    qint64 tilesBytes = 0;
    int tileCount = 0;
    qint64 undoBytes = 0;
    qint64 projectionBytes = 0;
    qint64 swapBytes = 0;
    qint64 peakBytes = 0;
};

/**
 * 汇总文档 + 投影 + 历史 + 瓦片缓存的内存口径。
 * @p projection / @p history 可空（单测只测瓦片时）。
 */
MemSnapshot captureMemSnapshot(const ImageDocument *doc, const Projection *projection,
                               const HistoryStack *history);

/** 若 PSLITE_MEM=1，打印一行并更新进程内峰值。 */
void maybeLogMem(const MemSnapshot &snap);

/** 当前进程内观测到的峰值（tiles+undo+projection+swap）。 */
qint64 memPeakBytes();

/** 测试用：清零峰值。 */
void resetMemPeakForTest();

} // namespace Ps

#endif // ENGINE_CACHE_MEMSTATS_H
