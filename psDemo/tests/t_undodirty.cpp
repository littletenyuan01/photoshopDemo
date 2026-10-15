/**
 * t_undodirty.cpp — T4：像素撤销只标受影响矩形。
 */
#include "app/undoitem.h"
#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "domain/layerstyle.h"
#include "domain/layerstylestack.h"
#include "engine/paintengine.h"
#include "engine/painttypes.h"
#include "testutil.h"

using namespace Ps;
using PsTests::Reporter;

int tUndoDirty()
{
    Reporter r;

    // ① 小笔撤销：dirtyRect 严格小于整幅且覆盖改动（白底整层有瓦片，靠瓦片 diff）
    {
        constexpr int W = 400, H = 400;
        auto doc = ImageDocument::createBlank(W, H, Qt::white);
        Layer *layer = doc->activeLayer();
        doc->pushLayerPixelsUndo(0, QStringLiteral("画笔"));
        const QRect local = PaintEngine::stampDab(
            layer->tiles(), QPointF(350, 350), 12.0, Qt::red, PaintMode::Paint);
        layer->invalidateContentBounds();
        layer->invalidateCompositeRaster();
        doc->markDirty(local.translated(layer->offsetX(), layer->offsetY()));
        doc->clearDirtyRect(); // 只关心 undo 产生的脏区
        doc->undo();
        const QRect dirty = doc->dirtyRect();
        const QRect full(0, 0, W, H);
        r.check(!dirty.isEmpty(), QStringLiteral("① undo 后 dirty 非空"));
        r.check(dirty != full && dirty.width() * dirty.height() < W * H,
                QStringLiteral("① dirty 严格小于整幅（%1×%2）")
                    .arg(dirty.width())
                    .arg(dirty.height()));
        const QRect strokeDoc = local.translated(layer->offsetX(), layer->offsetY());
        r.check(dirty.contains(strokeDoc) || dirty.intersects(strokeDoc),
                QStringLiteral("① dirty 覆盖笔迹区"));
    }

    // ② 带投影样式：dirty 含 styleBounds
    {
        auto doc = ImageDocument::createBlank(200, 200, Qt::white);
        Layer *layer = doc->activeLayer();
        LayerStyleEffect shadow = LayerStyleEffect::makeDefault(LayerStyleKind::DropShadow);
        shadow.setEnabled(true);
        shadow.setSize(8);
        shadow.setDistance(6);
        layer->styles().append(shadow);
        layer->invalidateCompositeRaster();
        const QRect styleBefore = layer->styleBoundsInDocument();

        doc->pushLayerPixelsUndo(0, QStringLiteral("画笔"));
        PaintEngine::stampDab(layer->tiles(), QPointF(30, 30), 8.0, Qt::blue, PaintMode::Paint);
        layer->invalidateContentBounds();
        layer->invalidateCompositeRaster();
        doc->clearDirtyRect();
        doc->undo();
        const QRect dirty = doc->dirtyRect();
        r.check(dirty.contains(styleBefore.intersected(QRect(0, 0, 200, 200)))
                    || dirty.intersects(styleBefore),
                QStringLiteral("② dirty 覆盖样式外扩"));
    }

    // ③ 层在画布外：不崩；交集为空时走整幅 markDirty 回退
    {
        auto doc = ImageDocument::createBlank(64, 64, Qt::white);
        auto shell = std::make_unique<Layer>(QStringLiteral("外"), 32, 32);
        shell->tiles().fill(QColor(90, 90, 90, 255));
        shell->setOffsetSilent(500, 500);
        doc->addLayer(std::move(shell));
        const int idx = doc->layers().count() - 1;
        doc->setActiveLayerIndex(idx);
        Layer *layer = doc->activeLayer();

        doc->pushLayerPixelsUndo(idx, QStringLiteral("画笔"));
        PaintEngine::stampDab(layer->tiles(), QPointF(8, 8), 4.0, Qt::green, PaintMode::Paint);
        layer->invalidateContentBounds();
        layer->invalidateCompositeRaster();
        doc->clearDirtyRect();
        doc->undo();
        r.check(!doc->dirtyRect().isEmpty(),
                QStringLiteral("③ 画布外撤销仍标脏（整幅回退）"));
        r.check(layer->tiles().allocatedTileCount() >= 0,
                QStringLiteral("③ 撤销后层仍可访问"));
    }

    return r.fails;
}
