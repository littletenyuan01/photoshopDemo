/**
 * t_regionraster.cpp — 层复合栅格「区域化」vs「整层后裁剪」逐像素一致。
 *
 * 生产合成按 64x64 块推进、每块只对那块跑滤镜（Layer::compositeRasterRegion）；
 * 这里用「分块投影」的结果对照「图路径整层 ensureCompositeRaster」的结果，
 * 确保区域化滤镜与整层滤镜完全等价（点滤镜逐像素 → 应当位等价）。
 */
#include "domain/filternode.h"
#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "engine/compositor.h"
#include "engine/op/opsinit.h"
#include "engine/projection.h"
#include "testutil.h"

using namespace Ps;
using PsTests::Reporter;

int tRegionRaster()
{
    Reporter r;

    const int W = 1200, H = 900;
    auto doc = ImageDocument::createBlank(W, H, Qt::white);
    doc->placeImageAsLayer(PsTests::makeLayerImage(W, H, 1, 255), QStringLiteral("L1"));
    doc->placeImageAsLayer(PsTests::makeLayerImage(W, H, 2, 200), QStringLiteral("L2"));

    // 给 L2 挂 3 个点滤镜 → 它会走 compositeRasterRegion 的区域路径
    Layer *l2 = doc->layers().layerAt(2);
    FilterNode n1(OpName::BrightnessContrast);
    n1.setBrightness(0.25);
    n1.setContrast(-0.2);
    FilterNode n2(OpName::Levels);
    FilterNode n3(OpName::HueSaturation);
    n3.setSaturation(35);
    l2->filters().append(n1);
    l2->filters().append(n2);
    l2->filters().append(n3);
    l2->setBlendMode(BlendMode::Multiply);

    // 参考：图路径对带滤镜层走**整层** ensureCompositeRaster
    const QImage ref = Compositor::compositeViaGraph(*doc, QRect(0, 0, W, H));

    // 分块：投影每块一个 64x64 区域 → 区域化滤镜
    Projection p;
    p.bind(doc.get());
    p.setPriorityRect(QRect(0, 0, W, H));
    doc->markDirty();
    p.sync();
    while (p.hasPendingChunks())
        p.processPendingChunks(0);

    const long long d = PsTests::diffCount(ref, p.image());
    r.check(d == 0, QStringLiteral("分块(区域化滤镜) vs 整层栅格 逐像素一致（diffs=%1）").arg(d));
    return r.fails;
}
