/**
 * t_phase1a.cpp — phase1-audit 高危项复现与回归（先测再改）。
 *
 * A1：自由变换 + 非矩形选区抹掉外接框内选区外像素
 * A2：扩层类像素 undo 不带蒙版 ⇒ 撤销后蒙版变全白
 * A5：并行合成前钉住/预加载瓦片，换页开启时与串行逐像素一致
 * A6：向左/上拖直线须能绘制
 * A7：Path::read 对超大 nA 立即失败且不巨量分配
 * A3：平移图层须失效文档坐标区域滤镜缓存
 * A8：撤销裁剪后选区 mask 须与文档同尺寸
 * A9：分段 blend + 调整层面算子须与整幅 composite 一致
 */
#include "domain/filternode.h"
#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "domain/layermask.h"
#include "domain/path.h"
#include "domain/selection.h"
#include "domain/tilebuffer.h"
#include "engine/cache/swapstore.h"
#include "engine/cache/tilecache.h"
#include "engine/compositor.h"
#include "engine/op/opname.h"
#include "engine/paintengine.h"
#include "engine/paintselectionclip.h"
#include "engine/painttypes.h"
#include "testutil.h"
#include "ui/canvassizedialog.h"

#include <QBuffer>
#include <QColor>
#include <QComboBox>
#include <QDataStream>
#include <QImage>
#include <QIODevice>
#include <QLineEdit>
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

/** A3：translate 后 regionFilter 缓存失效且世代前进。 */
int checkA3TranslateInvalidatesRegionFilter(Reporter &r)
{
    auto doc = ImageDocument::createBlank(64, 64, Qt::white);
    Layer *layer = doc->activeLayer();
    r.check(layer != nullptr, QStringLiteral("A3：有活动层"));
    if (!layer)
        return r.fails;

    const QRect bounds = layer->boundsInDocument();
    QImage fake(bounds.size(), QImage::Format_ARGB32_Premultiplied);
    fake.fill(qRgba(10, 20, 30, 255));
    layer->setRegionFilterResult(bounds, fake);
    r.check(layer->regionFilterCovers(bounds), QStringLiteral("A3：写入后缓存覆盖"));

    const quint32 genBefore = layer->contentGeneration();
    doc->translateLayer(0, 10, 0, /*emitContent=*/false);
    r.check(layer->contentGeneration() > genBefore,
            QStringLiteral("A3：平移后 contentGeneration 前进"));
    r.check(!layer->regionFilterCovers(bounds),
            QStringLiteral("A3：平移后旧文档矩形不再命中缓存"));
    r.check(!layer->regionFilterCovers(layer->boundsInDocument()),
            QStringLiteral("A3：平移后区域滤镜缓存已清空"));
    return r.fails;
}

/**
 * A9：先 blend [0,1) 再 [1,end) 到子矩形；调整层为面积算子时须与整幅 composite 一致。
 * （子矩形迫使 BoxBlur 走 srcWant!=area 分支。）
 */
int checkA9AdjAreaBlendSeed(Reporter &r)
{
    constexpr int W = 64, H = 64;
    auto doc = ImageDocument::createBlank(W, H, Qt::white);
    doc->placeImageAsLayer(PsTests::makeLayerImage(W, H, 3, 255), QStringLiteral("L1"));
    const int adj = doc->addAdjustmentLayer(OpName::BoxBlur);
    r.check(adj == 2, QStringLiteral("A9：调整层在 index 2（实际 %1）").arg(adj));
    Layer *adjLayer = doc->layers().layerAt(adj);
    r.check(adjLayer && adjLayer->isAdjustmentLayer() && adjLayer->filters().count() == 1,
            QStringLiteral("A9：调整层就绪"));
    if (adjLayer && adjLayer->filters().count() == 1)
        adjLayer->filters().at(0).setBlurRadius(6);

    const QRect area(20, 20, 24, 24); // 小于画布 ⇒ blur pad 使 srcWant != area
    QImage live(W, H, QImage::Format_ARGB32_Premultiplied);
    live.fill(Qt::transparent);
    r.check(Compositor::blendLayerRange(live, *doc, area, 0, 1, -1),
            QStringLiteral("A9：第一段 blend [0,1)"));
    r.check(Compositor::blendLayerRange(live, *doc, area, 1, doc->layers().count(), -1),
            QStringLiteral("A9：第二段 blend [1,end)"));

    const QImage full = Compositor::composite(*doc);
    const long long diffs = PsTests::diffCount(live.copy(area), full.copy(area));
    r.check(diffs == 0,
            QStringLiteral("A9：分段 blend 与整幅 composite 在 area 内一致（diffs=%1）")
                .arg(diffs));
    return r.fails;
}

/** A8：裁剪后 undo，选区 mask 尺寸须等于文档尺寸。 */
int checkA8SelectionSizeAfterCropUndo(Reporter &r)
{
    auto doc = ImageDocument::createBlank(400, 400, Qt::white);
    doc->selectRectangle(QRect(0, 0, 400, 400), ChannelOp::Replace);
    r.check(doc->selection().mask().size() == QSize(400, 400),
            QStringLiteral("A8：裁剪前选区 400×400"));

    doc->cropTo(QRect(0, 0, 200, 200));
    r.check(doc->width() == 200 && doc->height() == 200, QStringLiteral("A8：已裁到 200"));
    r.check(doc->selection().mask().size() == QSize(200, 200),
            QStringLiteral("A8：裁剪后选区随文档变 200"));

    doc->undo();
    r.check(doc->width() == 400 && doc->height() == 400, QStringLiteral("A8：undo 后文档 400"));
    r.check(doc->selection().mask().size() == QSize(doc->width(), doc->height()),
            QStringLiteral("A8：undo 后选区与文档同尺寸（mask=%1×%2 doc=%3×%4）")
                .arg(doc->selection().mask().width())
                .arg(doc->selection().mask().height())
                .arg(doc->width())
                .arg(doc->height()));
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

/** A11：置入的小层在 scaleImage 后应按文档系数缩放，而非拉成整幅画布。 */
int checkA11ScaleImagePreservesLayerExtent(Reporter &r)
{
    auto doc = ImageDocument::createBlank(200, 200, Qt::white);
    QImage patch(100, 100, QImage::Format_ARGB32_Premultiplied);
    patch.fill(qRgba(20, 40, 60, 255));
    const int idx = doc->placeImageAsLayer(patch, QStringLiteral("P"));
    Layer *layer = doc->layers().layerAt(idx);
    r.check(layer && layer->width() == 100 && layer->height() == 100,
            QStringLiteral("A11：置入层 100×100"));
    if (!layer)
        return r.fails;
    const int ox = layer->offsetX();
    const int oy = layer->offsetY();

    doc->scaleImage(400, 400);
    r.check(layer->width() == 200 && layer->height() == 200,
            QStringLiteral("A11：放大 2× 后层 extent 200×200（实际 %1×%2）")
                .arg(layer->width())
                .arg(layer->height()));
    r.check(layer->offsetX() == ox * 2 && layer->offsetY() == oy * 2,
            QStringLiteral("A11：offset 按同一系数缩放"));
    return r.fails;
}

/** A12：删掉正在编辑蒙版的层后，编辑蒙版标志须清除。 */
int checkA12RemoveLayerClearsEditingMask(Reporter &r)
{
    auto doc = ImageDocument::createBlank(64, 64, Qt::white);
    doc->placeImageAsLayer(PsTests::makeLayerImage(64, 64, 3, 255), QStringLiteral("L"));
    r.check(doc->addLayerMask(1), QStringLiteral("A12：加蒙版"));
    doc->setActiveLayerIndex(1);
    r.check(doc->setEditingLayerMask(true), QStringLiteral("A12：进入编辑蒙版"));
    r.check(doc->isEditingLayerMask(), QStringLiteral("A12：标志已开"));
    r.check(doc->removeLayer(1), QStringLiteral("A12：删除该层"));
    r.check(!doc->isEditingLayerMask(), QStringLiteral("A12：删层后编辑蒙版标志已清"));
    return r.fails;
}

/** A14：魔棒连续模式不得把「已访未命中」像素算进选区。 */
int checkA14FloodSelectVisitedNotSelected(Reporter &r)
{
    constexpr int W = 32, H = 32;
    auto doc = ImageDocument::createBlank(W, H, Qt::white);
    // 中心 8×8 黑块；种子在黑块内，邻居白边会被 BFS 探测但不匹配
    Layer *layer = doc->activeLayer();
    QImage img = layer->materialize();
    for (int y = 12; y < 20; ++y)
        for (int x = 12; x < 20; ++x)
            img.setPixel(x, y, qRgba(0, 0, 0, 255));
    layer->replaceFromImage(img);

    doc->selectFlood(QPoint(16, 16), /*tolerance=*/0, /*contiguous=*/true,
                     /*sampleMerged=*/false, ChannelOp::Replace);
    // 紧贴黑块外的白像素不得被选中
    r.check(!doc->selection().isSelected(11, 16),
            QStringLiteral("A14：黑块左邻白像素未入选"));
    r.check(!doc->selection().isSelected(20, 16),
            QStringLiteral("A14：黑块右邻白像素未入选"));
    r.check(doc->selection().isSelected(16, 16),
            QStringLiteral("A14：种子黑像素已选中"));
    return r.fails;
}

/** A20：高度单位独立参与换算（宽=像素、高=英寸）。 */
int checkA20CanvasSizeHeightUnit(Reporter &r)
{
    auto doc = ImageDocument::createBlank(100, 100, Qt::white);
    CanvasSizeDialog dlg(doc.get(), Qt::black, Qt::white);
    auto *wEdit = dlg.findChild<QLineEdit *>(QStringLiteral("widthEdit"));
    auto *hEdit = dlg.findChild<QLineEdit *>(QStringLiteral("heightEdit"));
    auto *wUnit = dlg.findChild<QComboBox *>(QStringLiteral("widthUnitCombo"));
    auto *hUnit = dlg.findChild<QComboBox *>(QStringLiteral("heightUnitCombo"));
    r.check(wEdit && hEdit && wUnit && hUnit, QStringLiteral("A20：对话框控件就绪"));
    if (!wEdit || !hEdit || !wUnit || !hUnit)
        return r.fails;

    wUnit->blockSignals(true);
    hUnit->blockSignals(true);
    wUnit->setCurrentIndex(0); // Pixels
    hUnit->setCurrentIndex(1); // Inches
    wUnit->blockSignals(false);
    hUnit->blockSignals(false);
    wEdit->setText(QStringLiteral("100"));
    hEdit->setText(QStringLiteral("2"));
    // 默认 PPI 72 ⇒ 2 英寸 = 144 像素
    const QSize sz = dlg.resultPixelSize();
    r.check(sz.width() == 100 && sz.height() == 144,
            QStringLiteral("A20：高 2 英寸@72ppi → 144px（实际 %1×%2）")
                .arg(sz.width())
                .arg(sz.height()));
    return r.fails;
}

int tPhase1A()
{
    Reporter r;
    checkA1FreeTransformClip(r);
    checkA2MaskSurvivesExpandUndo(r);
    checkA3TranslateInvalidatesRegionFilter(r);
    checkA5ParallelSharedRead(r);
    checkA6LineUpLeft(r);
    checkA7PathReadCap(r);
    checkA8SelectionSizeAfterCropUndo(r);
    checkA9AdjAreaBlendSeed(r);
    checkA11ScaleImagePreservesLayerExtent(r);
    checkA12RemoveLayerClearsEditingMask(r);
    checkA14FloodSelectVisitedNotSelected(r);
    checkA20CanvasSizeHeightUnit(r);
    return r.fails;
}
