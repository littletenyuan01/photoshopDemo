/**
 * t_projectionvalid.cpp — A31/A32/A33：投影块有效位与图路径预览口径。
 *
 * A32：adoptImage 仅 covered 内块有效；processPending 补齐后与整幅合成一致。
 * A31：文档改尺寸后 processPendingChunks 先 ensureBuffer，结果尺寸=新尺寸。
 * A33：有 compositePreview 时 compositeViaGraph == Compositor::composite。
 */
#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "domain/path.h"
#include "engine/compositor.h"
#include "engine/projection.h"
#include "testutil.h"
#include "tools/pathanchortool.h"
#include "tools/tool.h"
#include "tools/toolid.h"

#include <QPainter>

using namespace Ps;
using PsTests::Reporter;

namespace {

void flushProjection(Projection &p, ImageDocument *doc)
{
    doc->markDirty();
    p.setPriorityRect(QRect(0, 0, doc->width(), doc->height()));
    p.sync();
    while (p.hasPendingChunks())
        p.processPendingChunks(0);
}

} // namespace

int tProjectionValid()
{
    Reporter r;

    // —— A32：半幅 adopt 后仍有无效块，idle 补齐后与合成一致 ——
    {
        constexpr int W = 128, H = 128; // 2×2 块（kChunkSize=64）
        auto doc = ImageDocument::createBlank(W, H, Qt::white);
        QImage paint(W, H, QImage::Format_ARGB32_Premultiplied);
        paint.fill(qRgba(200, 40, 40, 255));
        {
            QPainter qp(&paint);
            qp.fillRect(W / 2, 0, W / 2, H, QColor(40, 40, 200));
        }
        doc->placeImageAsLayer(paint, QStringLiteral("split"));

        Projection proj;
        proj.bind(doc.get());
        flushProjection(proj, doc.get());
        const QImage ref = Compositor::composite(*doc);
        r.check(PsTests::diffCount(ref, proj.image()) == 0,
                QStringLiteral("A32：满幅 sync 后投影==合成"));

        QImage live = ref.copy();
        {
            QPainter qp(&live);
            qp.setCompositionMode(QPainter::CompositionMode_Source);
            qp.fillRect(W / 2, 0, W / 2, H, QColor(0, 255, 0)); // 右半垃圾
        }
        const QRect left(0, 0, W / 2, H);
        r.check(proj.adoptImage(live, left), QStringLiteral("A32：adopt 左半成功"));
        r.check(proj.hasPendingChunks(), QStringLiteral("A32：右半块仍无效"));
        r.check(proj.invalidChunkCount() >= 2,
                QStringLiteral("A32：至少 2 个无效块（实际 %1）")
                    .arg(proj.invalidChunkCount()));

        r.check(proj.image().pixel(16, 64) == ref.pixel(16, 64),
                QStringLiteral("A32：adopt 后左半保持可信"));
        r.check(qGreen(proj.image().pixel(W - 16, 64)) > 200,
                QStringLiteral("A32：adopt 后右半仍是垃圾（待重算）"));

        while (proj.hasPendingChunks())
            proj.processPendingChunks(0);
        r.check(!proj.hasPendingChunks(), QStringLiteral("A32：idle 后无待办"));
        const long long d = PsTests::diffCount(ref, proj.image());
        r.check(d == 0, QStringLiteral("A32：补齐后投影==合成（diffs=%1）").arg(d));
    }

    // —— A31：改文档尺寸后仅 processPending 也能对齐缓冲并合成 ——
    {
        auto doc = ImageDocument::createBlank(64, 64, Qt::white);
        Projection proj;
        proj.bind(doc.get());
        flushProjection(proj, doc.get());
        r.check(proj.image().size() == QSize(64, 64), QStringLiteral("A31：初始 64×64"));

        doc->resizeCanvas(128, 96, 0, 0, Qt::black);
        // 不调 sync：只走 idle。processPending 内 ensureBuffer 按新尺寸重建网格。
        proj.setPriorityRect(QRect(0, 0, 128, 96));
        int guard = 0;
        while (guard++ < 64) {
            proj.processPendingChunks(0);
            if (proj.image().size() == QSize(128, 96) && !proj.hasPendingChunks())
                break;
        }
        r.check(proj.image().width() == 128 && proj.image().height() == 96,
                QStringLiteral("A31：processPending 后尺寸=%1×%2（期望 128×96）")
                    .arg(proj.image().width())
                    .arg(proj.image().height()));
        r.check(!proj.hasPendingChunks(), QStringLiteral("A31：新尺寸无待办块"));

        const QImage ref2 = Compositor::composite(*doc);
        const long long d2 = PsTests::diffCount(ref2, proj.image());
        r.check(d2 == 0,
                QStringLiteral("A31：新尺寸投影==合成（diffs=%1）").arg(d2));
    }

    // —— A33：compositePreview 时图路径与直接合成一致 ——
    {
        constexpr int W = 64, H = 64;
        auto doc = ImageDocument::createBlank(W, H, Qt::white);
        Layer *layer = doc->activeLayer();
        r.check(layer != nullptr, QStringLiteral("A33：有活动层"));
        if (!layer)
            return r.fails;

        QImage hole = layer->materialize();
        {
            QPainter qp(&hole);
            qp.setCompositionMode(QPainter::CompositionMode_Source);
            qp.fillRect(16, 16, 32, 32, Qt::transparent);
        }
        layer->replaceFromImage(hole);
        QImage preview(32, 32, QImage::Format_ARGB32_Premultiplied);
        preview.fill(qRgba(40, 200, 40, 255));
        layer->setCompositePreview(preview, 16, 16);

        const QImage viaDirect = Compositor::composite(*doc);
        const QImage viaGraph =
            Compositor::compositeViaGraph(*doc, QRect(0, 0, W, H));
        const long long dg = PsTests::diffCount(viaDirect, viaGraph);
        r.check(dg == 0,
                QStringLiteral("A33：有 preview 时 graph==composite（diffs=%1）")
                    .arg(dg));
        r.check(qGreen(viaDirect.pixel(24, 24)) > 150,
                QStringLiteral("A33：预览区可见绿色"));
    }

    // —— A36：删路径 / 隐藏后 overlay 不画幽灵 ——
    {
        struct StubVp : ViewPort {
            void zoomAt(const QPointF &, qreal) override {}
            void panBy(const QPointF &) override {}
            void requestRepaint() override {}
        } vp;

        auto doc = ImageDocument::createBlank(80, 80, Qt::white);
        Path path;
        path.name = QStringLiteral("p");
        SubPath sp;
        sp.closed = true;
        for (const QPointF &pt : {QPointF(10, 10), QPointF(50, 10), QPointF(50, 50), QPointF(10, 50)}) {
            PathAnchor a;
            a.point = a.inHandle = a.outHandle = pt;
            sp.anchors.append(a);
        }
        path.subPaths.append(sp);
        doc->addPath(path);
        doc->setActivePathIndex(0);

        ToolContext ctx;
        ctx.document = doc.get();
        ctx.viewZoom = 1.0;

        PathAnchorTool tool(Ps::ToolId::AddAnchorPoint);
        ToolEvent press;
        press.imagePos = QPointF(30, 10);
        press.widgetPos = press.imagePos;
        press.button = Qt::LeftButton;
        press.buttons = Qt::LeftButton;
        tool.mousePress(press, ctx, vp); // loadActive

        auto paintOverlay = [&]() {
            QImage img(80, 80, QImage::Format_ARGB32_Premultiplied);
            img.fill(0);
            QPainter qp(&img);
            tool.drawOverlay(qp, ctx);
            qp.end();
            int lit = 0;
            for (int y = 0; y < 80; ++y) {
                const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
                for (int x = 0; x < 80; ++x) {
                    if (qAlpha(line[x]) > 0)
                        ++lit;
                }
            }
            return lit;
        };

        const int litBefore = paintOverlay();
        r.check(litBefore > 0, QStringLiteral("A36：有路径时 overlay 有像素（%1）").arg(litBefore));

        doc->setPathVisible(0, false);
        r.check(paintOverlay() == 0, QStringLiteral("A36：隐藏后不画"));

        doc->setPathVisible(0, true);
        r.check(paintOverlay() > 0, QStringLiteral("A36：再显示可画"));

        doc->removePath(0);
        r.check(paintOverlay() == 0, QStringLiteral("A36：删除后无幽灵像素"));
    }

    return r.fails;
}
