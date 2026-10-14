/**
 * t_tilecache.cpp — 瓦片页缓存：往返 / 预算 / LRU / 失败降级 / COW 复制（page-cache-plan）。
 */
#include "app/historystack.h"
#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "domain/tilebuffer.h"
#include "engine/cache/memstats.h"
#include "engine/cache/swapstore.h"
#include "engine/cache/tilecache.h"
#include "engine/projection.h"
#include "testutil.h"

#include <QElapsedTimer>
#include <QImage>
#include <QtGlobal>

#include <memory>
#include <vector>

using namespace Ps;
using PsTests::Reporter;

namespace {

struct EnvGuard {
    QByteArray key;
    QByteArray old;
    bool had = false;
    EnvGuard(const char *k, const char *v)
        : key(k)
    {
        had = qEnvironmentVariableIsSet(k);
        old = qgetenv(k);
        if (v)
            qputenv(k, v);
        else
            qunsetenv(k);
    }
    ~EnvGuard()
    {
        if (had)
            qputenv(key.constData(), old);
        else
            qunsetenv(key.constData());
    }
};

void paintChecker(TileBuffer &tiles, int seed)
{
    tiles.forEachTileInRect(QRect(0, 0, tiles.width(), tiles.height()), true,
                            [&](int tx, int ty, QImage &tile, const QRect &) {
                                const int v = (tx * 17 + ty * 31 + seed) & 0xff;
                                tile.fill(qRgba(v, 255 - v, (v * 3) & 0xff, 255));
                            });
}

/** 强制把缓冲内全部驻留瓦片换出（需预算已很小 + MemorySwapStore）。 */
void forceEvictAll(TileBuffer &buf)
{
    auto &cache = TileCache::instance();
    // 多次 trim，直到没有驻留或无法再减
    for (int i = 0; i < 10000; ++i) {
        if (buf.residentBytes() == 0)
            break;
        const qint64 before = cache.usedBytes();
        cache.trim();
        if (cache.usedBytes() >= before)
            break;
    }
}

} // namespace

int tTileCache()
{
    Reporter r;
    auto &cache = TileCache::instance();

    // —— 0) 默认关闭：预算 0，行为与无换页一致 ——
    {
        EnvGuard envMb("PSLITE_TILE_CACHE_MB", nullptr);
        EnvGuard envMem("PSLITE_MEM", nullptr);
        cache.resetForTest();
        cache.setBudgetBytes(0);
        TileBuffer tiles(128, 128);
        paintChecker(tiles, 1);
        const QImage a = tiles.materialize();
        r.check(cache.stats().evicted == 0, QStringLiteral("预算0：不淘汰"));
        r.check(tiles.residentBytes() > 0, QStringLiteral("预算0：瓦片驻留内存"));
        r.check(PsTests::diffCount(a, tiles.materialize()) == 0,
                QStringLiteral("预算0：materialize 稳定"));
    }

    // —— 1) 换页往返 bit-exact ——
    {
        EnvGuard envMb("PSLITE_TILE_CACHE_MB", "1");
        cache.resetForTest();
        cache.setBudgetBytes(16 * 1024); // 极小：约 1 瓦片
        auto mem = std::make_unique<MemorySwapStore>();
        MemorySwapStore *memRaw = mem.get();
        cache.setSwapStoreForTest(std::move(mem));

        TileBuffer tiles(256, 256); // 16 瓦片 × 16KB
        paintChecker(tiles, 42);
        const QImage before = tiles.materialize();
        const qint64 beforeBytes = tiles.residentBytes();
        r.check(beforeBytes > cache.budgetBytes(), QStringLiteral("往返：写满后超预算"));

        forceEvictAll(tiles);
        r.check(tiles.residentBytes() < beforeBytes, QStringLiteral("往返：淘汰后驻留下降"));
        r.check(cache.stats().evicted > 0, QStringLiteral("往返：evicted>0"));
        r.check(memRaw->putCount() > 0, QStringLiteral("往返：swap put>0"));

        const QImage after = tiles.materialize(); // 触发读回
        r.check(PsTests::diffCount(before, after) == 0,
                QStringLiteral("往返：换出再读回 bit-exact"));
        r.check(cache.stats().loaded > 0, QStringLiteral("往返：loaded>0"));
    }

    // —— 2) 预算被遵守（+1 瓦片余量）——
    {
        cache.resetForTest();
        const qint64 budget = 8 * 1024 * 1024;
        cache.setBudgetBytes(budget);
        cache.setSwapStoreForTest(std::make_unique<MemorySwapStore>());

        TileBuffer tiles(2048, 1536); // 满画 ≈ 12MB+
        tiles.fill(QColor(10, 20, 30, 255));
        cache.trim();
        const qint64 used = cache.usedBytes();
        const qint64 slack = 64 * 64 * 4; // 一个满格瓦片
        r.check(used <= budget + slack,
                QStringLiteral("预算：used=%1 ≤ budget+1tile=%2")
                    .arg(used)
                    .arg(budget + slack));
    }

    // —— 3) LRU：最久未用先出 ——
    {
        cache.resetForTest();
        cache.setBudgetBytes(16 * 1024); // 只够约 1 块
        cache.setSwapStoreForTest(std::make_unique<MemorySwapStore>());

        TileBuffer tiles(128, 64); // 2 瓦片
        QImage *t0 = tiles.ensureTile(0, 0);
        QImage *t1 = tiles.ensureTile(1, 0);
        r.check(t0 && t1, QStringLiteral("LRU：两块已分配"));
        t0->fill(qRgb(1, 2, 3));
        t1->fill(qRgb(4, 5, 6));
        // touch：先 0 后 1，再碰 1 ⇒ 0 为 LRU
        tiles.tileAt(0, 0);
        tiles.tileAt(1, 0);
        tiles.tileAt(1, 0);

        cache.trim();
        r.check(cache.stats().evicted >= 1, QStringLiteral("LRU：发生过淘汰"));
        r.check(tiles.residentBytes() <= 16 * 1024, QStringLiteral("LRU：驻留 ≤ 1 瓦片"));

        const int loadedBefore = cache.stats().loaded;
        r.check(tiles.tileAt(1, 0) != nullptr, QStringLiteral("LRU：较新块仍可取"));
        r.check(cache.stats().loaded == loadedBefore,
                QStringLiteral("LRU：较新块仍在内存（未 load）"));
        r.check(tiles.tileAt(0, 0) != nullptr, QStringLiteral("LRU：较旧块可读回"));
        r.check(cache.stats().loaded == loadedBefore + 1,
                QStringLiteral("LRU：较旧块从 swap load"));

        const QRgb c0 = tiles.tileAt(0, 0)->pixel(0, 0);
        const QRgb c1 = tiles.tileAt(1, 0)->pixel(0, 0);
        r.check(c0 == qRgb(1, 2, 3) && c1 == qRgb(4, 5, 6),
                QStringLiteral("LRU：淘汰后像素仍正确"));
    }

    // —— 4) 失败降级：FailingSwapStore ——
    {
        cache.resetForTest();
        cache.setBudgetBytes(1024);
        cache.setSwapStoreForTest(std::make_unique<FailingSwapStore>());

        TileBuffer tiles(128, 128);
        paintChecker(tiles, 7);
        const QImage before = tiles.materialize();
        cache.trim();
        r.check(cache.stats().swapFailures >= 1, QStringLiteral("降级：记到 swapFailures"));
        r.check(!cache.swapEnabled(), QStringLiteral("降级：关闭换页"));
        r.check(PsTests::diffCount(before, tiles.materialize()) == 0,
                QStringLiteral("降级：结果仍正确（留内存）"));
    }

    // —— 5) P4 检查点：复制图层内存增量 ≪ 整层 ——
    {
        cache.resetForTest();
        cache.setBudgetBytes(0);
        auto doc = ImageDocument::createBlank(640, 480, Qt::white);
        Layer *src = doc->layers().layerAt(0);
        r.check(src != nullptr, QStringLiteral("COW：有背景层"));
        const qint64 before = src->tiles().residentBytes();
        // 注意：sharePixelsFrom 会 noteResident 再计一份逻辑字节，但 QImage 数据共享。
        // 断言：复制后「新增逻辑记账」可以到 before，但不应触发 materialize 式的独立缓冲
        // —— 用 cacheKey 证明共享。
        const int idx = doc->duplicateLayer(0);
        r.check(idx >= 0, QStringLiteral("COW：duplicateLayer 成功"));
        Layer *dup = doc->layers().layerAt(idx);
        r.check(dup != nullptr, QStringLiteral("COW：副本存在"));

        // 抽一块比 cacheKey
        const QImage *a = src->tiles().tileAt(0, 0);
        const QImage *b = dup->tiles().tileAt(0, 0);
        r.check(a && b, QStringLiteral("COW：双方有瓦片"));
        r.check(a->cacheKey() == b->cacheKey(),
                QStringLiteral("COW：复制后同瓦片 cacheKey 相同（隐式共享）"));
        r.check(before > 0, QStringLiteral("COW：源层有像素 before=%1").arg(before));

        // 写副本应 detach，不再共享
        QImage *bw = dup->tiles().ensureTile(0, 0);
        bw->setPixel(0, 0, qRgb(9, 9, 9));
        r.check(a->cacheKey() != bw->cacheKey(),
                QStringLiteral("COW：写副本后 detach"));
        r.check(a->pixel(0, 0) != qRgb(9, 9, 9),
                QStringLiteral("COW：源像素未改"));
    }

    // —— 6) 派生缓存淘汰后投影仍可重算 ——
    {
        cache.resetForTest();
        cache.setBudgetBytes(1024);
        cache.setSwapStoreForTest(std::make_unique<MemorySwapStore>());

        auto doc = ImageDocument::createBlank(128, 128, QColor(80, 90, 100));
        Projection proj;
        proj.bind(doc.get());
        doc->markDirty();
        const QRect painted = proj.sync();
        r.check(!painted.isEmpty() || !proj.isNull(), QStringLiteral("派生：投影已 sync"));

        Layer *layer = doc->activeLayer();
        layer->setRegionFilterResult(QRect(0, 0, 64, 64),
                                     PsTests::makeLayerImage(64, 64, 2));
        r.check(layer->regionFilterCovers(QRect(0, 0, 32, 32)),
                QStringLiteral("派生：区域滤镜缓存命中"));

        // sync() 可能已把瓦片全换出（used=0）；先触碰读回若干块制造超限，
        // 再 trim，才能走进「先丢派生缓存」分支（A 档 P5）。
        layer->tiles().tileAt(0, 0);
        layer->tiles().tileAt(1, 0);
        cache.setBudgetBytes(1);
        r.check(cache.usedBytes() > cache.budgetBytes(),
                QStringLiteral("派生：trim 前确已超预算"));
        cache.trim(doc.get(), &proj);
        r.check(!layer->regionFilterCovers(QRect(0, 0, 32, 32)),
                QStringLiteral("派生：trim 后区域滤镜缓存已清"));

        doc->markDirty();
        proj.sync();
        r.check(!proj.isNull(), QStringLiteral("派生：投影仍可重算"));
    }

    // —— 7) 记账：TileBuffer::residentBytes 与 TileCache::usedBytes ——
    {
        cache.resetForTest();
        cache.setBudgetBytes(0);
        TileBuffer tiles(64, 64);
        r.check(tiles.residentBytes() == 0 && cache.usedBytes() == 0,
                QStringLiteral("记账：空缓冲为 0"));
        tiles.ensureTile(0, 0);
        r.check(tiles.residentBytes() == 64 * 64 * 4, QStringLiteral("记账：一块 16KB"));
        r.check(cache.usedBytes() == tiles.residentBytes(),
                QStringLiteral("记账：cache.used == buffer.resident"));
        tiles.clearTiles();
        r.check(cache.usedBytes() == 0, QStringLiteral("记账：clear 后 used=0"));
    }

    // —— 8) PSLITE_MEM 快照字段齐全（不强制打印）——
    {
        resetMemPeakForTest();
        auto doc = ImageDocument::createBlank(64, 64, Qt::white);
        Projection proj;
        proj.bind(doc.get());
        doc->markDirty();
        proj.sync();
        const MemSnapshot snap = captureMemSnapshot(doc.get(), &proj, &doc->history());
        r.check(snap.tilesBytes > 0, QStringLiteral("MEM：tilesBytes>0"));
        r.check(snap.projectionBytes > 0, QStringLiteral("MEM：projectionBytes>0"));
        r.check(snap.peakBytes >= snap.tilesBytes, QStringLiteral("MEM：peak≥tiles"));
    }

    // —— 9) P13 性能护栏：换页开启但未触发淘汰时，端到端退化 ≤5% ——
    // 工作负载：1600×1200 实色层 × 反复 materialize（读全瓦片，无 trim）。
    // 粗护栏放宽到 25%（本机噪声可达 ±2×，见 tests.md）；实测值写回 page-cache-plan。
    {
        constexpr int kW = 1600;
        constexpr int kH = 1200;
        constexpr int kRounds = 40; // 拉长时程，压低 ~25ms 级噪声

        auto runOnce = [&](qint64 budgetMb) -> qint64 {
            cache.resetForTest();
            cache.setBudgetBytes(budgetMb * 1024 * 1024);
            if (budgetMb > 0)
                cache.setSwapStoreForTest(std::make_unique<MemorySwapStore>());
            TileBuffer tiles(kW, kH);
            tiles.fill(QColor(40, 80, 120, 255));
            // 预热一轮（建瓦片 / 填色不计入）
            volatile int sink = tiles.materialize().width();
            Q_UNUSED(sink);
            QElapsedTimer t;
            t.start();
            for (int i = 0; i < kRounds; ++i)
                sink += tiles.materialize().width();
            Q_UNUSED(sink);
            r.check(cache.stats().evicted == 0,
                    QStringLiteral("护栏：budget=%1MB 时不应淘汰").arg(budgetMb));
            return t.elapsed();
        };

        const qint64 msOff = runOnce(0);
        const qint64 msOn = runOnce(512); // 远大于 1600×1200×4 ≈ 7.3MB
        const double ratio = msOff > 0 ? (double(msOn) / double(msOff)) : 1.0;
        const double pct = (ratio - 1.0) * 100.0;
        r.info(QStringLiteral("护栏 P13：off=%1ms on(512MB,无淘汰)=%2ms 退化=%3% (目标≤5%)")
                   .arg(msOff)
                   .arg(msOn)
                   .arg(QString::number(pct, 'f', 1)));
        // 粗护栏：退化不超过 25%（防回归把记账/touch 做成热路径灾变）
        r.check(ratio <= 1.25,
                QStringLiteral("护栏：退化 ≤25%（实测 %1%）").arg(QString::number(pct, 'f', 1)));
    }

    cache.resetForTest();
    cache.setBudgetBytes(0);
    return r.fails;
}
