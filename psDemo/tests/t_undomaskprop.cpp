/**
 * t_undomaskprop.cpp — O4：属性类蒙版引用快照 + 按是否共享记账。
 */
#include "app/historystack.h"
#include "app/undoitem.h"
#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "domain/layermask.h"
#include "engine/paintengine.h"
#include "engine/painttypes.h"
#include "testutil.h"

#include <QObject>

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

int tUndoMaskProp()
{
    Reporter r;

    // —— T1：capture 浅拷贝；改显隐 undo 后蒙版逐字节一致 ——
    {
        auto doc = ImageDocument::createBlank(128, 128, Qt::white);
        Layer *layer = doc->activeLayer();
        r.check(layer != nullptr, QStringLiteral("有背景层"));
        if (!layer)
            return r.fails;

        doc->addLayerMask(0);
        LayerMask *mask = layer->mask();
        r.check(mask != nullptr, QStringLiteral("已加蒙版"));
        mask->fill(200);
        {
            QImage &g = mask->image();
            for (int y = 0; y < 32; ++y) {
                uchar *line = g.scanLine(y);
                for (int x = 0; x < 32; ++x)
                    line[x] = 40;
            }
        }
        const QImage before = mask->image().copy();

        doc->setLayerVisible(0, false);
        auto *prop = dynamic_cast<LayerPropUndo *>(doc->history().topUndoItem());
        r.check(prop != nullptr, QStringLiteral("栈顶是 LayerPropUndo"));
        r.check(prop && prop->maskSharedWith(layer->mask()->image()),
                QStringLiteral("属性撤销条目与图层蒙版 cacheKey 相同（共享）"));

        doc->setLayerVisible(0, true); // 再一条属性撤销
        doc->undo();                   // 回到可见=false
        doc->undo();                   // 回到可见=true，蒙版应还原
        r.check(layer->isVisible(), QStringLiteral("undo 后可见"));
        r.check(layer->hasMask() && layer->mask() != nullptr, QStringLiteral("undo 后仍有蒙版"));
        r.check(grayDiffCount(layer->mask()->image(), before) == 0,
                QStringLiteral("undo 后蒙版与原快照逐字节相同"));
    }

    // —— T2①：2048² 连改 100 次显隐，不 trim，增量 < 1MB ——
    {
        auto doc = ImageDocument::createBlank(2048, 2048, Qt::white);
        Layer *layer = doc->activeLayer();
        doc->addLayerMask(0);
        layer->mask()->fill(180);

        const quint64 bytes0 = doc->history().undoBytes();
        int trimmed = 0;
        QObject::connect(&doc->history(), &HistoryStack::historyTrimmed,
                         [&](int n) { trimmed += n; });

        bool vis = true;
        for (int i = 0; i < 100; ++i) {
            vis = !vis;
            doc->setLayerVisible(0, vis);
        }
        const quint64 delta = doc->history().undoBytes() - bytes0;
        r.check(trimmed == 0,
                QStringLiteral("100 次显隐 historyTrimmed==0（实际 %1）").arg(trimmed));
        r.check(delta < 1024ull * 1024ull,
                QStringLiteral("100 次显隐 undoBytes 增量=%1 < 1MB").arg(delta));
        r.info(QStringLiteral("O4 T2① 100 次显隐 undoBytes 增量=%1 trimmed=%2")
                   .arg(delta)
                   .arg(trimmed));
    }

    // —— T2②：LayerPropUndo detach 后 byteSize 下界 ——
    {
        auto doc = ImageDocument::createBlank(2048, 2048, Qt::white);
        Layer *layer = doc->activeLayer();
        doc->addLayerMask(0);
        layer->mask()->fill(220);

        doc->setLayerVisible(0, false);
        auto *prop = dynamic_cast<LayerPropUndo *>(doc->history().topUndoItem());
        r.check(prop != nullptr, QStringLiteral("detach 场景有 LayerPropUndo"));
        const quint64 sharedBytes = prop ? prop->byteSize() : 0;

        const QRect loc = PaintEngine::stampMaskDab(
            layer->mask()->image(), QPointF(64, 64), 12.0, 0, PaintMode::Paint);
        doc->markDirty(loc.translated(layer->offsetX(), layer->offsetY()));

        prop = dynamic_cast<LayerPropUndo *>(doc->history().topUndoItem());
        r.check(prop != nullptr && !prop->maskSharedWith(layer->mask()->image()),
                QStringLiteral("写蒙版后不再共享"));
        const quint64 detachedBytes = prop ? prop->byteSize() : 0;
        constexpr quint64 kMaskFloor = quint64(4194304 * 0.75);
        r.check(detachedBytes >= kMaskFloor,
                QStringLiteral("detach 后 byteSize=%1 ≥ %2（共享时=%3）")
                    .arg(detachedBytes)
                    .arg(kMaskFloor)
                    .arg(sharedBytes));
        r.info(QStringLiteral("O4 T2② LayerPropUndo detach byteSize=%1（共享时 %2）")
                   .arg(detachedBytes)
                   .arg(sharedBytes));
    }

    // —— T2③：抬高上限后蒙版 50 笔 LayerPixelsUndo 下界 ——
    {
        auto doc = ImageDocument::createBlank(2048, 2048, Qt::white);
        Layer *layer = doc->activeLayer();
        doc->addLayerMask(0);
        LayerMask *mask = layer->mask();
        mask->fill(200);

        doc->history().setByteLimit(2ull << 30);
        const quint64 bytes0 = doc->history().undoBytes();
        for (int i = 0; i < 50; ++i) {
            doc->pushLayerPixelsUndo(0, QStringLiteral("m%1").arg(i));
            const QRect loc = PaintEngine::stampMaskDab(
                mask->image(), QPointF(40 + (i % 10) * 20, 40 + (i / 10) * 20), 8.0,
                quint8(i % 200), PaintMode::Paint);
            doc->markDirty(loc.translated(layer->offsetX(), layer->offsetY()));
        }
        const quint64 delta = doc->history().undoBytes() - bytes0;
        constexpr quint64 kFloor = quint64(50.0 * 4194304.0 * 0.75);
        r.check(delta >= kFloor,
                QStringLiteral("50 笔蒙版 undoBytes 增量=%1 ≥ %2").arg(delta).arg(kFloor));
        r.info(QStringLiteral("O4 T2③ 50 笔蒙版 undoBytes 增量=%1（下界 %2）")
                   .arg(delta)
                   .arg(kFloor));
    }

    return r.fails;
}
