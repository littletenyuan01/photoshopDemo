/**
 * t_offset.cpp — 位移（几何算子）：语义 oracle + **分块投影 == 整幅** + 全幅求值护栏。
 *
 * 守的契约（见 engine/op/offsetfilterop.h 与 docs/engine/roi.md §5）：
 *  ① wrap（默认）：`out(x,y) = in((x-dx) mod w, (y-dy) mod h)`，与朴素取模逐像素一致；
 *  ② 非 wrap：移出即丢、空出透明，与朴素实现逐像素一致；
 *  ③ **分块投影 == 整幅合成**（diffs=0）—— 这是几何算子最要紧的护栏：它必须整幅求值，
 *     否则会在 64×64 块界错位（这正是本项目给几何算子单开一条整层路径的原因）；
 *  ④ dx=dy=0 → 不改变画面；⑤ 存读往返保住 dx/dy/wrap。
 */
#include "domain/filternode.h"
#include "domain/filterstack.h"
#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "engine/compositor.h"
#include "engine/filtereval.h"
#include "engine/op/opname.h"
#include "engine/projection.h"
#include "io/projectio.h"
#include "testutil.h"

#include <QImage>

using namespace Ps;
using PsTests::Reporter;

namespace {

/** 朴素位移（逐像素取模 / 越界透明）—— 位移算子的**语义 oracle**。 */
QImage naiveOffset(const QImage &src, int dx, int dy, bool wrap)
{
    const int w = src.width();
    const int h = src.height();
    QImage out(src.size(), src.format());
    if (wrap)
        out.fill(Qt::transparent);
    else
        out.fill(Qt::transparent);

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int sx = x - dx;
            int sy = y - dy;
            if (wrap) {
                sx %= w;
                if (sx < 0)
                    sx += w;
                sy %= h;
                if (sy < 0)
                    sy += h;
            } else if (sx < 0 || sx >= w || sy < 0 || sy >= h) {
                continue;
            }
            out.setPixel(x, y, src.pixel(sx, sy));
        }
    }
    return out;
}

/** 分块投影全排空后的图像。 */
QImage chunkedProjection(ImageDocument *doc)
{
    Projection p;
    p.bind(doc);
    p.setPriorityRect(QRect(0, 0, doc->width(), doc->height()));
    doc->markDirty();
    p.sync();
    while (p.hasPendingChunks())
        p.processPendingChunks(0);
    return p.image();
}

} // namespace

int tOffset()
{
    Reporter r;

    // —— ①② wrap / 非 wrap：语义 oracle + 往返 ——
    struct Case {
        int dx, dy;
        bool wrap;
    };
    const Case cases[] = {
        {7, 5, true}, {7, 5, false}, {-13, -9, true}, {-13, -9, false},
        {40, 0, true}, {0, -70, true}, {200, 130, true}, // 超过尺寸：取模后应等价
    };
    for (const Case &c : cases) {
        const int W = 40, H = 30;
        const QImage src = PsTests::makeLayerImage(W, H, 17, 255);
        QImage got = src;
        FilterNode node(OpName::Offset);
        node.setOffsetDx(c.dx);
        node.setOffsetDy(c.dy);
        node.setOffsetWrap(c.wrap);
        FilterEval::applyNode(got, node);
        const QImage ref = naiveOffset(src, c.dx, c.dy, c.wrap);
        const long long d = PsTests::diffCount(got, ref);
        r.check(d == 0, QStringLiteral("位移 dx=%1 dy=%2 wrap=%3：== 朴素取模（diffs=%4）")
                            .arg(c.dx).arg(c.dy).arg(c.wrap ? 1 : 0).arg(d));
    }

    // —— ④ dx=dy=0 不改画面 ——
    {
        const QImage src = PsTests::makeLayerImage(24, 20, 3, 255);
        QImage got = src;
        FilterNode node(OpName::Offset);
        node.setOffsetDx(0);
        node.setOffsetDy(0);
        FilterEval::applyNode(got, node);
        r.check(PsTests::diffCount(src, got) == 0,
                QStringLiteral("位移 0,0：画面不变（diffs=%1）").arg(PsTests::diffCount(src, got)));
    }

    // —— ③ 分块投影 == 整幅合成（几何算子若被逐块求值，这里必然出现块界错位） ——
    {
        const int W = 256, H = 192;
        auto doc = ImageDocument::createBlank(W, H, Qt::white);
        doc->placeImageAsLayer(PsTests::makeLayerImage(200, 160, 23, 255), QStringLiteral("off"));
        Layer *layer = doc->layers().layerAt(1);
        if (!layer) {
            r.check(false, QStringLiteral("有活动像素层"));
            return r.fails;
        }
        layer->setOffset(20, 10);
        FilterNode node(OpName::Offset);
        node.setOffsetDx(37); // 刻意非 64 的整数倍，跨块界
        node.setOffsetDy(-21);
        node.setOffsetWrap(true);
        layer->filters().append(node);
        layer->invalidateCompositeRaster();
        r.check(layer->filters().hasGeometryOp(),
                QStringLiteral("滤镜栈被识别为「含几何算子」"));

        const QImage whole = Compositor::composite(*doc);
        const QImage chunked = chunkedProjection(doc.get());
        const long long dc = PsTests::diffCount(whole, chunked);
        r.check(dc == 0, QStringLiteral("位移：分块投影 == 整幅合成，无块界错位（diffs=%1）").arg(dc));

        // 位移确实改变了画面（否则上面那条可能是「两边都没动」的假通过）
        auto plain = ImageDocument::createBlank(W, H, Qt::white);
        plain->placeImageAsLayer(PsTests::makeLayerImage(200, 160, 23, 255), QStringLiteral("off"));
        plain->layers().layerAt(1)->setOffset(20, 10);
        const QImage before = Compositor::composite(*plain);
        r.check(PsTests::diffCount(before, whole) > 0,
                QStringLiteral("位移确实改变了画面（diffs=%1）").arg(PsTests::diffCount(before, whole)));

        // —— ⑤ 存读往返 ——
        const QString path = PsTests::tempFilePath(QStringLiteral("offset.pslite"));
        QString err;
        const bool saved = ProjectIo::save(*doc, path, &err);
        auto back = saved ? ProjectIo::load(path, &err) : nullptr;
        if (back && back->layers().count() > 1
            && back->layers().layerAt(1)->filters().count() > 0) {
            const FilterNode &n = back->layers().layerAt(1)->filters().at(0);
            r.check(n.op() == OpName::Offset && n.offsetDx() == 37 && n.offsetDy() == -21
                        && n.offsetWrap(),
                    QStringLiteral("往返保住位移 dx/dy/wrap（实际 %1,%2 wrap=%3）")
                        .arg(n.offsetDx()).arg(n.offsetDy()).arg(n.offsetWrap() ? 1 : 0));
        } else {
            r.check(false, QStringLiteral("重新打开含位移的 .pslite"));
        }
    }

    return r.fails;
}
