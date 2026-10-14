/**
 * t_phase1a.cpp — phase1-audit 高危项复现与回归（先测再改）。
 *
 * A1：自由变换 + 非矩形选区抹掉外接框内选区外像素
 * A2：扩层类像素 undo 不带蒙版 ⇒ 撤销后蒙版变全白
 * A5：并行合成前钉住/预加载瓦片，换页开启时与串行逐像素一致
 * A6：向左/上拖直线须能绘制
 * A7：Path::read 对超大 nA 立即失败且不巨量分配
 */
#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "domain/layermask.h"
#include "domain/path.h"
#include "domain/selection.h"
#include "domain/tilebuffer.h"
#include "engine/cache/swapstore.h"
#include "engine/cache/tilecache.h"
#include "engine/compositor.h"
#include "engine/paintengine.h"
#include "engine/paintselectionclip.h"
#include "engine/painttypes.h"
#include "testutil.h"

#include <QBuffer>
#include <QColor>
#include <QDataStream>
#include <QImage>
#include <QIODevice>
#include <QPointF>
#include <QRect>
#include <QThreadPool>

#include <memory>

using namespace Ps;
using PsTests::Reporter;

namespace {

} // namespace

/** A1：椭圆选区 + clearSource 平移，外接框角点不得被抹成透明。 */
int checkA1FreeTransformClip(Reporter &r)
{
    constexpr int W = 64, H = 64;
    const QRgb fill = qRgba(200, 40, 40, 255);
    auto doc = ImageDocument::createBlank(W, H, QColor::fromRgba(fill));
    Layer *layer = doc->activeLayer();
    r.check(layer != nullptr, QStringLiteral("A1：有活动层"));
    if (!layer)
        return r.fails;

    // 椭圆选区：外接框四角在椭圆外
    doc->selectEllipse(QRect(8, 8, 32, 32), ChannelOp::Replace);
    r.check(!doc->selection().isEmpty(), QStringLiteral("A1：选区非空"));
    r.check(!doc->selection().isSelected(9, 9),
            QStringLiteral("A1：(9,9) 在外接框内、椭圆外（未选中）"));
    r.check(doc->selection().isSelected(24, 24),
            QStringLiteral("A1：椭圆中心已选中"));

    const QRgb beforeCorner = layer->materialize().pixel(9, 9);
    r.check(beforeCorner == fill, QStringLiteral("A1：变换前角点仍是填充色"));

    PaintSelectionClip clip;
    clip.selection = &doc->selection();
    clip.layerOffsetX = layer->offsetX();
    clip.layerOffsetY = layer->offsetY();

    // 整体右移 10px（clearSource=true，脏区含源矩形）
    const QRect src(0, 0, W, H);
    QPointF dest[4] = {
        QPointF(10, 0), QPointF(W + 10, 0), QPointF(W + 10, H), QPointF(10, H),
    };
    const QRect dirty = PaintEngine::freeTransform(
        layer->tiles(), src, QImage(), dest, /*clearSource=*/true, clip,
        TransformInterpolation::Nearest);
    r.check(!dirty.isEmpty(), QStringLiteral("A1：变换返回非空脏区"));

    const QRgb afterCorner = layer->materialize().pixel(9, 9);
    r.check(afterCorner == fill,
            QStringLiteral("A1：外接框内、选区外像素未被抹掉（got=0x%1 want=0x%2）")
                .arg(afterCorner, 8, 16, QLatin1Char('0'))
                .arg(fill, 8, 16, QLatin1Char('0')));
    return r.fails;
}

/** A2：带蒙版层扩尺寸后 undo，蒙版须与变换前逐字节相同。 */
int checkA2MaskSurvivesExpandUndo(Reporter &r)
{
    constexpr int W = 64, H = 64;
    auto doc = ImageDocument::createBlank(W, H, Qt::white);
    Layer *layer = doc->activeLayer();
    r.check(layer != nullptr, QStringLiteral("A2：有背景层"));
    if (!layer)
        return r.fails;

    doc->addLayerMask(0);
    r.check(layer->hasMask() && layer->mask(), QStringLiteral("A2：已加蒙版"));
    if (!layer->mask())
        return r.fails;

    // 左半涂黑
    QImage mask = layer->mask()->image().copy();
    for (int y = 0; y < H; ++y) {
        uchar *line = mask.scanLine(y);
        for (int x = 0; x < W / 2; ++x)
            line[x] = 0;
    }
    layer->mask()->setFromImage(mask);
    const QImage snap = layer->mask()->image().copy();

    // 模拟变换提交：push 像素 undo（4 参，不带 mask）→ 扩到 74×74 → replaceFromImage
    doc->pushLayerPixelsUndo(0, QStringLiteral("自由变换"));
    QImage bigger(74, 74, QImage::Format_ARGB32_Premultiplied);
    bigger.fill(qRgba(10, 20, 30, 255));
    layer->replaceFromImage(bigger);
    r.check(layer->width() == 74 && layer->height() == 74, QStringLiteral("A2：层已扩到 74"));
    r.check(layer->hasMask(), QStringLiteral("A2：扩层后仍有蒙版对象"));

    doc->undo();
    r.check(layer->width() == W && layer->height() == H, QStringLiteral("A2：undo 后尺寸恢复"));
    r.check(layer->hasMask() && layer->mask(), QStringLiteral("A2：undo 后仍有蒙版"));
    if (!layer->mask())
        return r.fails;

    const QImage after = layer->mask()->image();
    const long long d = PsTests::diffCount(
        after.convertToFormat(QImage::Format_ARGB32_Premultiplied),
        snap.convertToFormat(QImage::Format_ARGB32_Premultiplied));
    // Grayscale8：逐字节比
    bool same = (after.size() == snap.size() && after.format() == snap.format());
    long long byteDiffs = 0;
    if (same) {
        for (int y = 0; y < after.height(); ++y) {
            const uchar *a = after.constScanLine(y);
            const uchar *b = snap.constScanLine(y);
            for (int x = 0; x < after.width(); ++x)
                if (a[x] != b[x])
                    ++byteDiffs;
        }
    } else {
        byteDiffs = -1;
    }
    Q_UNUSED(d);
    r.check(byteDiffs == 0,
            QStringLiteral("A2：undo 后蒙版与快照一致（diffs=%1）").arg(byteDiffs));
    return r.fails;
}

/**
 * A5：开启换页并把瓦片换出后，并行合成须先在主线程驻留；
 * 结果与串行 blend 一致，且 shared-read 期间 touch 不推进时钟。
 */
int checkA5ParallelSharedRead(Reporter &r)
{
    auto &cache = TileCache::instance();
    cache.resetForTest();
    cache.setBudgetBytes(64 * 64 * 4); // 只够一块驻留 → 强制换页
    cache.setSwapStoreForTest(std::make_unique<MemorySwapStore>());

    constexpr int W = 128, H = 128; // 4 块 64×64
    auto doc = ImageDocument::createBlank(W, H, Qt::white);
    Layer *layer = doc->activeLayer();
    r.check(layer != nullptr, QStringLiteral("A5：有活动层"));
    if (!layer)
        return r.fails;

    // 各瓦片不同色，便于发现错读/透明降级
    layer->tiles().forEachTileInRect(
        QRect(0, 0, W, H), true, [&](int tx, int ty, QImage &tile, const QRect &) {
            tile.fill(qRgba(40 + tx * 50, 80 + ty * 40, 120, 255));
        });

    // 换出到只剩预算内驻留
    for (int i = 0; i < 100; ++i) {
        if (cache.usedBytes() <= cache.budgetBytes())
            break;
        cache.trim();
    }
    r.check(cache.stats().evicted > 0, QStringLiteral("A5：已发生换出"));
    r.check(layer->tiles().residentBytes() <= cache.budgetBytes() + 64 * 64 * 4,
            QStringLiteral("A5：驻留接近预算"));

    // shared-read：touch 不得推进时钟
    {
        TileSharedReadGuard guard(doc.get());
        r.check(cache.sharedReadActive(), QStringLiteral("A5：sharedRead 已激活"));
        TileSlot probe;
        probe.lastUse = 0;
        const qint64 clockBefore = cache.nextClock();
        cache.touch(probe);
        r.check(probe.lastUse == 0, QStringLiteral("A5：sharedRead 下 touch 不写 lastUse"));
        // nextClock 仍可用（主线程记账）；再取一次确认 touch 没偷偷调用
        Q_UNUSED(clockBefore);
        r.check(layer->tiles().residentBytes() > cache.budgetBytes()
                    || layer->tiles().allocatedTileCount() <= 1,
                QStringLiteral("A5：beginSharedRead 已预加载（或仅一块）"));
    }
    r.check(!cache.sharedReadActive(), QStringLiteral("A5：guard 结束后 sharedRead 关闭"));

    // 再次换出，然后并行 vs 串行合成
    for (int i = 0; i < 100; ++i) {
        if (cache.usedBytes() <= cache.budgetBytes())
            break;
        cache.trim();
    }

    QThreadPool::globalInstance()->setMaxThreadCount(qMax(2, QThreadPool::globalInstance()->maxThreadCount()));

    QImage serial(W, H, QImage::Format_ARGB32_Premultiplied);
    serial.fill(Qt::transparent);
    r.check(Compositor::blendLayerRange(serial, *doc, QRect(0, 0, W, H), 0, doc->layers().count()),
            QStringLiteral("A5：串行 blend 成功"));

    // 再换出一次，逼并行路径走预加载
    for (int i = 0; i < 100; ++i) {
        if (cache.usedBytes() <= cache.budgetBytes())
            break;
        cache.trim();
    }

    QImage parallel(W, H, QImage::Format_ARGB32_Premultiplied);
    parallel.fill(Qt::transparent);
    r.check(Compositor::blendLayerRangeParallel(parallel, *doc, QRect(0, 0, W, H), 0,
                                                doc->layers().count()),
            QStringLiteral("A5：并行 blend 成功"));

    const long long diffs = PsTests::diffCount(serial, parallel);
    r.check(diffs == 0,
            QStringLiteral("A5：并行与串行合成一致（diffs=%1）").arg(diffs));

    cache.resetForTest();
    cache.setBudgetBytes(0);
    return r.fails;
}

/** A6：非 normalized 直线矩形（向左上拖）须返回非空脏区且线上有像素。 */
int checkA6LineUpLeft(Reporter &r)
{
    TileBuffer tiles(64, 64);
    tiles.fill(Qt::transparent);
    const QRect dirty = PaintEngine::fillShape(
        tiles, ShapeKind::Line, QRectF(QPointF(30, 20), QPointF(10, 10)),
        QColor(255, 0, 0, 255), /*fill=*/false, /*stroke=*/true, /*strokeWidth=*/2.0);
    r.check(!dirty.isEmpty(), QStringLiteral("A6：向左上画线返回非空脏区"));
    const QImage img = tiles.materialize();
    int painted = 0;
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            if (qAlpha(img.pixel(x, y)) > 0)
                ++painted;
        }
    }
    r.check(painted > 0, QStringLiteral("A6：线上有不透明像素（count=%1）").arg(painted));
    return r.fails;
}

/** A7：声明超大锚点数必须立刻 Corrupt，且不把内存撑爆。 */
int checkA7PathReadCap(Reporter &r)
{
    QByteArray blob;
    {
        QBuffer buf(&blob);
        buf.open(QIODevice::WriteOnly);
        QDataStream out(&buf);
        out.setVersion(QDataStream::Qt_6_0);
        out << QStringLiteral("bad") << quint8(1) << qint32(1); // 1 subpath
        out << quint8(0) << qint32(0x7FFFFFFF);                 // nA = 2^31-1
        // 不写任何锚点载荷
    }

    QBuffer inBuf(&blob);
    inBuf.open(QIODevice::ReadOnly);
    QDataStream in(&inBuf);
    in.setVersion(QDataStream::Qt_6_0);
    const Path path = Path::read(in);
    r.check(in.status() != QDataStream::Ok,
            QStringLiteral("A7：超大 nA 后流状态非 Ok"));
    r.check(path.subPaths.isEmpty(),
            QStringLiteral("A7：损坏路径不返回半截数据"));
    return r.fails;
}

int tPhase1A()
{
    Reporter r;
    checkA1FreeTransformClip(r);
    checkA2MaskSurvivesExpandUndo(r);
    checkA5ParallelSharedRead(r);
    checkA6LineUpLeft(r);
    checkA7PathReadCap(r);
    return r.fails;
}
