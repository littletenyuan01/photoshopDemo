/**
 * t_undotile.cpp — T2：TileRefSnapshot 交换对称性 + 换页路径。
 */
#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "domain/tilebuffer.h"
#include "engine/cache/swapstore.h"
#include "engine/cache/tilecache.h"
#include "engine/paintengine.h"
#include "engine/painttypes.h"
#include "testutil.h"

using namespace Ps;
using PsTests::Reporter;

int tUndoTile()
{
    Reporter r;
    auto &cache = TileCache::instance();

    // ① 快照 → 写 → 交换 → 再交换
    {
        cache.resetForTest();
        cache.setBudgetBytes(0);
        TileBuffer buf(128, 128);
        buf.fill(QColor(20, 30, 40, 255));
        auto snap = buf.takeRefSnapshot();
        const QImage before = buf.materialize();
        const int allocBefore = buf.allocatedTileCount();
        const qint64 resBefore = buf.residentBytes();

        PaintEngine::stampDab(buf, QPointF(20, 20), 12.0, QColor(255, 0, 0), PaintMode::Paint);
        PaintEngine::stampDab(buf, QPointF(100, 100), 8.0, QColor(0, 255, 0), PaintMode::Paint);
        const QImage afterWrite = buf.materialize();
        r.check(PsTests::diffCount(before, afterWrite) > 0, QStringLiteral("写后与快照前不同"));

        r.check(buf.swapWithRefSnapshot(snap), QStringLiteral("交换①成功"));
        r.check(PsTests::diffCount(buf.materialize(), before) == 0,
                QStringLiteral("交换①后 == 快照前"));
        r.check(buf.allocatedTileCount() == allocBefore,
                QStringLiteral("交换①后 allocated 回到初值"));
        r.check(buf.residentBytes() == resBefore,
                QStringLiteral("交换①后 residentBytes 回到初值"));

        r.check(buf.swapWithRefSnapshot(snap), QStringLiteral("交换②（redo）成功"));
        r.check(PsTests::diffCount(buf.materialize(), afterWrite) == 0,
                QStringLiteral("交换②后 == 写后状态"));
    }

    // ② 操作新建瓦片：交换后回到「未分配」
    {
        cache.resetForTest();
        cache.setBudgetBytes(0);
        TileBuffer buf(128, 128); // 全透明，无瓦片
        r.check(buf.allocatedTileCount() == 0, QStringLiteral("新建前无瓦片"));
        auto snap = buf.takeRefSnapshot();
        PaintEngine::stampDab(buf, QPointF(10, 10), 6.0, QColor(1, 2, 3), PaintMode::Paint);
        r.check(buf.allocatedTileCount() > 0, QStringLiteral("画后有新瓦片"));
        r.check(buf.swapWithRefSnapshot(snap), QStringLiteral("新瓦片场景交换成功"));
        r.check(buf.allocatedTileCount() == 0, QStringLiteral("交换后回到无瓦片"));
        r.check(PsTests::diffCount(buf.materialize(),
                                   QImage(128, 128, QImage::Format_ARGB32_Premultiplied))
                    >= 0,
                QStringLiteral("交换后可 materialize"));
        {
            QImage empty(128, 128, QImage::Format_ARGB32_Premultiplied);
            empty.fill(Qt::transparent);
            r.check(PsTests::diffCount(buf.materialize(), empty) == 0,
                    QStringLiteral("交换后全透明"));
        }
    }

    // ③ 换页开启
    {
        cache.resetForTest();
        cache.setBudgetBytes(64 * 64 * 4); // 仅 1 块
        cache.setSwapStoreForTest(std::make_unique<MemorySwapStore>());
        TileBuffer buf(192, 192); // 9 块
        buf.fill(QColor(11, 22, 33, 255));
        cache.trim(); // 逼换页
        auto snap = buf.takeRefSnapshot();
        const QImage before = buf.materialize();
        PaintEngine::stampDab(buf, QPointF(80, 80), 10.0, Qt::red, PaintMode::Paint);
        const QImage afterWrite = buf.materialize();
        r.check(buf.swapWithRefSnapshot(snap), QStringLiteral("换页：交换①"));
        r.check(PsTests::diffCount(buf.materialize(), before) == 0,
                QStringLiteral("换页：交换① bit-exact"));
        r.check(buf.swapWithRefSnapshot(snap), QStringLiteral("换页：交换②"));
        r.check(PsTests::diffCount(buf.materialize(), afterWrite) == 0,
                QStringLiteral("换页：交换② bit-exact"));
        cache.resetForTest();
    }

    return r.fails;
}
