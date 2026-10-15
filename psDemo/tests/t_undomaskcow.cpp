/**
 * t_undomaskcow.cpp — T3：蒙版引用快照的 COW（stampMaskDab / fill）+ 撤销往返。
 */
#include "app/historystack.h"
#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "domain/layermask.h"
#include "engine/paintengine.h"
#include "engine/painttypes.h"
#include "testutil.h"

#include <QElapsedTimer>
#include <QImage>

using namespace Ps;
using PsTests::Reporter;

namespace {

long long grayDiffCount(const QImage &a, const QImage &b)
{
    if (a.size() != b.size() || a.format() != b.format())
        return -1;
    long long d = 0;
    for (int y = 0; y < a.height(); ++y) {
        const uchar *pa = a.constScanLine(y);
        const uchar *pb = b.constScanLine(y);
        for (int x = 0; x < a.width(); ++x) {
            if (pa[x] != pb[x])
                ++d;
        }
    }
    return d;
}

} // namespace

int tUndoMaskCow()
{
    Reporter r;

    // ① stampMaskDab：共享快照后写，快照像素不变（COW detach）
    {
        LayerMask mask(64, 64, 255);
        // 左半涂黑，便于比对
        {
            QImage &g = mask.image();
            for (int y = 0; y < 64; ++y) {
                uchar *line = g.scanLine(y);
                for (int x = 0; x < 32; ++x)
                    line[x] = 0;
            }
        }
        const QImage before = mask.image().copy(); // 深拷贝作 oracle
        QImage snap = mask.image();                 // 浅共享
        const qint64 keyBefore = snap.cacheKey();

        PaintEngine::stampMaskDab(mask.image(), QPointF(48, 48), 10.0, 0, PaintMode::Paint);
        r.check(grayDiffCount(snap, before) == 0,
                QStringLiteral("stampMaskDab：快照像素未改"));
        r.check(grayDiffCount(mask.image(), before) != 0,
                QStringLiteral("stampMaskDab：活蒙版已改"));
        r.check(snap.cacheKey() != mask.image().cacheKey() || snap.cacheKey() != keyBefore
                    || grayDiffCount(mask.image(), before) != 0,
                QStringLiteral("stampMaskDab：已与快照分离（cacheKey 或内容）"));
        r.check(snap.cacheKey() != mask.image().cacheKey(),
                QStringLiteral("stampMaskDab：cacheKey 已分离"));
    }

    // ② LayerMask::fill：共享快照后 fill，快照像素不变
    {
        LayerMask mask(64, 64, 200);
        const QImage before = mask.image().copy();
        QImage snap = mask.image();
        mask.fill(0);
        r.check(grayDiffCount(snap, before) == 0, QStringLiteral("fill：快照像素未改"));
        r.check(mask.image().constScanLine(0)[0] == 0, QStringLiteral("fill：活蒙版已改"));
        r.check(snap.cacheKey() != mask.image().cacheKey(),
                QStringLiteral("fill：cacheKey 已分离"));
    }

    // ③ 文档路径：push 像素撤销（捕获蒙版）→ stampMaskDab 写 → undo 后与原快照一致
    {
        auto doc = ImageDocument::createBlank(64, 64, Qt::white);
        Layer *layer = doc->activeLayer();
        r.check(layer != nullptr, QStringLiteral("有背景层"));
        if (!layer)
            return r.fails;

        r.check(doc->addLayerMask(0), QStringLiteral("已加蒙版"));
        LayerMask *mask = layer->mask();
        r.check(mask != nullptr, QStringLiteral("有蒙版对象"));
        if (!mask)
            return r.fails;

        // 画十字图案
        {
            QImage &g = mask->image();
            for (int y = 0; y < 64; ++y) {
                uchar *line = g.scanLine(y);
                for (int x = 0; x < 64; ++x)
                    line[x] = (x == 32 || y == 32) ? 0 : 255;
            }
        }
        const QImage expected = mask->image().copy();

        doc->pushLayerPixelsUndo(0, QStringLiteral("蒙版笔"));
        PaintEngine::stampMaskDab(mask->image(), QPointF(16, 16), 12.0, 0, PaintMode::Paint);
        layer->invalidateContentBounds();
        layer->invalidateCompositeRaster();
        doc->markDirty(QRect(0, 0, 64, 64));

        r.check(grayDiffCount(mask->image(), expected) != 0,
                QStringLiteral("写后蒙版已变"));

        doc->undo();
        r.check(layer->hasMask() && layer->mask(), QStringLiteral("undo 后仍有蒙版"));
        if (!layer->mask())
            return r.fails;
        const long long d = grayDiffCount(layer->mask()->image(), expected);
        r.check(d == 0, QStringLiteral("undo 后蒙版与原快照逐字节相同（diffs=%1）").arg(d));
    }

    // ④ fill 写路径同样能被撤销回滚
    {
        auto doc = ImageDocument::createBlank(48, 48, Qt::white);
        Layer *layer = doc->activeLayer();
        doc->addLayerMask(0);
        LayerMask *mask = layer->mask();
        mask->fill(180);
        const QImage expected = mask->image().copy();

        doc->pushLayerPixelsUndo(0, QStringLiteral("蒙版填充"));
        mask->fill(0);
        doc->markDirty(QRect(0, 0, 48, 48));
        doc->undo();
        r.check(grayDiffCount(layer->mask()->image(), expected) == 0,
                QStringLiteral("fill 路径 undo 后蒙版逐字节相同"));
    }

    // ⑤ 带蒙版 200 步跳转耗时（info；正式对照写入 T6 / pending-dev）
    {
        auto doc = ImageDocument::createBlank(512, 512, Qt::white);
        Layer *layer = doc->activeLayer();
        doc->addLayerMask(0);
        LayerMask *mask = layer->mask();
        for (int i = 0; i < 200; ++i) {
            doc->pushLayerPixelsUndo(0, QStringLiteral("m%1").arg(i));
            PaintEngine::stampMaskDab(mask->image(), QPointF(40 + (i % 20) * 8, 40 + (i / 20) * 8),
                                      6.0, quint8(i % 200), PaintMode::Paint);
            doc->markDirty(QRect(0, 0, 64, 64));
        }
        QElapsedTimer clock;
        clock.start();
        const bool ok = doc->history().jumpToStep(0, *doc);
        const qint64 ms = clock.elapsed();
        r.check(ok, QStringLiteral("200 步跳到 0 成功"));
        r.info(QStringLiteral("带蒙版 512² 层 200 步 jumpToStep(0) 耗时=%1 ms").arg(ms));
    }

    return r.fails;
}
