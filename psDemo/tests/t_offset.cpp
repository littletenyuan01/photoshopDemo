/**
 * t_offset.cpp — 位移：语义 oracle + **分块投影 == 整幅** + 输入需求形状 + 往返。
 *
 * 守的契约（见 engine/op/offsetfilterop.h 与 docs/engine/roi.md §5）：
 *  ① 语义：`out(x,y) = in(x-dx, y-dy)`，**移出即丢、空出透明**（对照 PS「位移」的
 *     「未定义区域 = 设置为背景/透明」一档）。*不做环绕* —— 环绕是多矩形依赖，
 *     会让整条 ROI 机制退化成整层求值（理由见 offsetfilterop.cpp 顶部）；
 *  ② **分块投影 == 整幅合成**（diffs=0）—— 最要紧的护栏：位移走的是**逐块**路径
 *     （它的输入需求是「输出区 ∪ 平移后的输出区」，仍是一个矩形），需求算错就会在块界错位；
 *  ③ `requiredInputs` 的形状（逐块路径全靠它）；
 *  ④ dx=dy=0 → 不改变画面；⑤ 存读往返保住 dx/dy。
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

/** 朴素位移（`out(x,y) = in(x-dx,y-dy)`，越界留透明）—— 位移算子的**语义 oracle**。 */
QImage naiveOffset(const QImage &src, int dx, int dy)
{
    const int w = src.width();
    const int h = src.height();
    QImage out(src.size(), src.format());
    out.fill(Qt::transparent);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int sx = x - dx;
            const int sy = y - dy;
            if (sx < 0 || sx >= w || sy < 0 || sy >= h)
                continue; // 移出即丢
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

    // —— ① 语义 oracle（含正负、单轴、远大于尺寸的偏移） ——
    {
        const int dxs[] = {7, -13, 40, 0, 200, -70};
        const int dys[] = {5, -9, 0, -70, 130, 0};
        for (int i = 0; i < 6; ++i) {
            const int W = 40, H = 30;
            const QImage src = PsTests::makeLayerImage(W, H, 17, 255);
            QImage got = src;
            FilterNode node(OpName::Offset);
            node.setOffsetDx(dxs[i]);
            node.setOffsetDy(dys[i]);
            FilterEval::applyNode(got, node);
            const QImage ref = naiveOffset(src, dxs[i], dys[i]);
            const long long d = PsTests::diffCount(got, ref);
            r.check(d == 0, QStringLiteral("位移 dx=%1 dy=%2：== 朴素（diffs=%3）")
                                .arg(dxs[i]).arg(dys[i]).arg(d));
        }
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

    // —— ③ 输入需求的形状（逐块路径全靠它；写错就是块界错位） ——
    {
        FilterNode o(OpName::Offset);
        o.setOffsetDx(10);
        o.setOffsetDy(-6);
        const QRect want(100, 100, 64, 64);
        // want=(100,100,64,64)；dx=10 → x 并上 [90,153]；dy=-6 → y 并上 [106,169]
        r.check(o.requiredInputs(want) == QRect(90, 100, 74, 70),
                QStringLiteral("需求 = 输出区 ∪ 平移后的输出区，仍是单矩形（实际 %1,%2 %3×%4）")
                    .arg(o.requiredInputs(want).left()).arg(o.requiredInputs(want).top())
                    .arg(o.requiredInputs(want).width()).arg(o.requiredInputs(want).height()));
        o.setOffsetDx(0);
        o.setOffsetDy(0);
        r.check(o.requiredInputs(want) == want,
                QStringLiteral("dx=dy=0：需求就是输出区本身（退化为点算子）"));
    }

    // —— ② 分块投影 == 整幅合成（逐块路径的护栏；两轴都刻意取非 64 的整数倍以跨块界） ——
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
        node.setOffsetDx(37);
        node.setOffsetDy(-21);
        layer->filters().append(node);
        layer->invalidateCompositeRaster();
        r.check(layer->filters().containsOffset(),
                QStringLiteral("滤镜栈被识别为「含位移」"));

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

        // 位移 + 面积算子同栈：需求要**先算位移的平移、再叠加半径**，逐块路径同样不能错位
        layer->filters().append(FilterNode(OpName::MedianBlur));
        layer->filters().at(1).setMedianRadius(3);
        layer->invalidateCompositeRaster();
        const long long dcm = PsTests::diffCount(Compositor::composite(*doc),
                                                 chunkedProjection(doc.get()));
        r.check(dcm == 0, QStringLiteral("位移+中值同栈：分块投影 == 整幅合成（diffs=%1）").arg(dcm));
        layer->filters().removeAt(1);

        // —— ⑤ 存读往返 ——
        const QString path = PsTests::tempFilePath(QStringLiteral("offset.pslite"));
        QString err;
        const bool saved = ProjectIo::save(*doc, path, &err);
        auto back = saved ? ProjectIo::load(path, &err) : nullptr;
        if (back && back->layers().count() > 1
            && back->layers().layerAt(1)->filters().count() > 0) {
            const FilterNode &n = back->layers().layerAt(1)->filters().at(0);
            r.check(n.op() == OpName::Offset && n.offsetDx() == 37 && n.offsetDy() == -21,
                    QStringLiteral("往返保住位移 dx/dy（实际 %1,%2）")
                        .arg(n.offsetDx()).arg(n.offsetDy()));
        } else {
            r.check(false, QStringLiteral("重新打开含位移的 .pslite"));
        }
    }

    return r.fails;
}
