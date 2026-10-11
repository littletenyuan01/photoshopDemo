/**
 * t_roi.cpp — ROI 通用化**第 ① 步**：契约带 src/out 几何。
 *
 * 守的契约（见 docs/engine/roi.md §5 与 engine/op/filterop.h 的 FilterContext 注释）：
 *  ① **两条真实求值路径**都把几何填成了真值 ——
 *     · 图/直接路径：`FilterEval::applyNode(image, node)` → 几何 = 整张图（本地坐标）；
 *     · 像素层路径：`Layer::compositeRasterRegion(region)` → 几何 = 文档坐标下的
 *       「所需输入区（= 请求区外扩 pad）/ 请求区」；
 *  ② 几何**如实描述**手里那张图：`srcRect` 的尺寸 == 输入图尺寸，
 *     `outRect` 必须被 `srcRect` 包住，且 `outRect` + pad 反推得回 `srcRect`；
 *  ③ 带上几何求值与不传几何（本地坐标）**逐像素一致** —— 第 ① 步不改行为。
 *
 * 这些断言用的是 `setFilterGeometryProbe` 这个**测试探针**：光读代码无法证明「真的传了」，
 * 必须让 `applyNode` 在运行时把实际几何回吐出来对账。
 */
#include "domain/filternode.h"
#include "domain/filterstack.h"
#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "engine/filtereval.h"
#include "engine/op/filterop.h"
#include "engine/op/opname.h"
#include "engine/op/tilepatch.h"
#include "testutil.h"

#include <QImage>
#include <QRect>

using namespace Ps;
using PsTests::Reporter;

namespace {

/** 最近一次求值的几何 + 是否与期望相符（只在用例内单线程使用）。 */
struct GeometryLog {
    QRect src;
    QRect out;
    QSize imageSize;
    int calls = 0;
    int mismatches = 0;
    QRect expectSrc;
    QRect expectOut;

    void reset(const QRect &src, const QRect &out)
    {
        calls = 0;
        mismatches = 0;
        expectSrc = src;
        expectOut = out;
    }
};

GeometryLog g_log;

void onGeometry(const FilterGeometryProbe &g)
{
    g_log.src = g.srcRect;
    g_log.out = g.outRect;
    g_log.imageSize = g.imageSize;
    ++g_log.calls;
    if (g.srcRect != g_log.expectSrc || g.outRect != g_log.expectOut)
        ++g_log.mismatches;
}

} // namespace

int tRoi()
{
    Reporter r;
    setFilterGeometryProbe(&onGeometry);

    const QRect imgRect(0, 0, 64, 48);

    // —— ① 直接路径（无外扩）：本地坐标语义，几何 = 整张图 ——
    //
    // 注意：点滤镜**不注册为 FilterOp**，故走不到 `applyNode` 里挂探针的那一段
    // （与「三段式对无资源算子纯属开销」的取舍一致）。这里用面积算子来验「无外扩时
    // 几何退化为整张图」这条语义，探针在 FilterOp 分支里被调用。
    {
        QImage img = PsTests::makeLayerImage(imgRect.width(), imgRect.height(), 3, 255);
        FilterNode blur(OpName::BoxBlur);
        blur.setBlurRadius(2);
        g_log.reset(imgRect, imgRect);
        FilterEval::applyNode(img, blur);
        r.check(g_log.calls == 1, QStringLiteral("直接路径：探针被调用 1 次（实际 %1）").arg(g_log.calls));
        r.check(g_log.mismatches == 0,
                QStringLiteral("直接路径：几何退化为整张图 %1x%2")
                    .arg(g_log.imageSize.width()).arg(g_log.imageSize.height()));
    }

    // —— ① 图/直接路径（面积算子外扩）：几何退化为整张图（本地坐标语义）——
    //
    // 说明：**图路径的输出仍与输入同尺寸**（它不做「裁回请求区」），那是第 ② 步的范围；
    // 第 ① 步只保证「几何被如实转述」。所以这里只断言几何本身。
    {
        const int W = 64, H = 48, PAD = 3;
        FilterStack stack;
        FilterNode blur(OpName::BoxBlur);
        blur.setBlurRadius(PAD);
        stack.append(blur);

        QImage src = PsTests::makeLayerImage(W, H, 5, 255);
        g_log.reset(QRect(0, 0, W, H), QRect(0, 0, W, H));
        const QImage viaStack = stack.apply(src, nullptr);
        r.check(g_log.calls >= 1, QStringLiteral("图路径：探针被调用（实际 %1 次）").arg(g_log.calls));
        r.check(g_log.mismatches == 0,
                QStringLiteral("图路径（不传几何）：几何 = 整张图 %1x%2 / 输出 %3x%4")
                    .arg(g_log.src.width()).arg(g_log.src.height())
                    .arg(g_log.out.width()).arg(g_log.out.height()));
        r.check(g_log.imageSize == QSize(W, H),
                QStringLiteral("图路径：几何如实描述手里那张图（期望 %1x%2，实际 %3x%4）")
                    .arg(W).arg(H).arg(g_log.imageSize.width()).arg(g_log.imageSize.height()));
        r.check(viaStack.size() == src.size(),
                QStringLiteral("图路径：输出仍与输入同尺寸（第 ② 步才改这条）"));
    }

    // —— ① 像素层路径：文档坐标下的 want + pad ——
    {
        const int DW = 200, DH = 160;
        auto doc = ImageDocument::createBlank(DW, DH, Qt::white);
        doc->placeImageAsLayer(PsTests::makeLayerImage(120, 90, 11, 255), QStringLiteral("L"));
        Layer *layer = doc->layers().layerAt(1);
        if (!layer) {
            r.check(false, QStringLiteral("有活动像素层"));
            setFilterGeometryProbe(nullptr);
            return r.fails;
        }
        layer->setOffset(20, 15);
        FilterNode median(OpName::MedianBlur);
        median.setMedianRadius(2); // pad = 2
        layer->filters().append(median);
        layer->invalidateCompositeRaster();

        // 请求层内 (30,20)-(90,80) 一块 → 文档坐标 +offset；所需输入区外扩 2
        const QRect localWant(30, 20, 60, 60);
        const QRect docWant = localWant.translated(layer->offsetX(), layer->offsetY());
        const QRect layerRect(0, 0, layer->width(), layer->height());
        const QRect srcLocal = localWant.adjusted(-2, -2, 2, 2).intersected(layerRect);
        const QRect docSrc = srcLocal.translated(layer->offsetX(), layer->offsetY());
        // 几何是「输入图自己坐标系」里的两个矩形（输入图 = docSrc 那块）
        const QRect probeSrc(0, 0, srcLocal.width(), srcLocal.height());
        const QRect probeOut = docWant.translated(-docSrc.topLeft());

        g_log.reset(probeSrc, probeOut);
        const Layer::CompositeRegion got = layer->compositeRasterRegion(docWant);
        r.check(g_log.calls >= 1, QStringLiteral("像素层路径：探针被调用（实际 %1 次）").arg(g_log.calls));
        r.check(g_log.mismatches == 0,
                QStringLiteral("像素层路径：几何按输入图本地坐标 = 输入区 %1,%2 %3x%4 / 输出区 %5,%6 %7x%8"
                               "（实际 %9,%10 %11x%12 / %13,%14 %15x%16）")
                    .arg(probeSrc.x()).arg(probeSrc.y()).arg(probeSrc.width()).arg(probeSrc.height())
                    .arg(probeOut.x()).arg(probeOut.y()).arg(probeOut.width()).arg(probeOut.height())
                    .arg(g_log.src.x()).arg(g_log.src.y()).arg(g_log.src.width()).arg(g_log.src.height())
                    .arg(g_log.out.x()).arg(g_log.out.y()).arg(g_log.out.width()).arg(g_log.out.height()));
        r.check(got.image.size() == docWant.size() && got.docX == docWant.left()
                    && got.docY == docWant.top(),
                QStringLiteral("像素层路径：返回图像覆盖请求区且坐标正确"));
        r.check(g_log.imageSize == QSize(srcLocal.width(), srcLocal.height()),
                QStringLiteral("像素层路径：几何如实描述输入图尺寸（期望 %1x%2，实际 %3x%4）")
                    .arg(srcLocal.width()).arg(srcLocal.height())
                    .arg(g_log.imageSize.width()).arg(g_log.imageSize.height()));

        // —— ③ 带几何求值 + 裁回 == 直接对同一块补边图求值后裁回（第 ① 步不改行为） ——
        {
            FilterStack stack;
            stack.replaceAll(layer->filters().snapshot());
            const QImage patch = TilePatch::extract(layer->tiles(), srcLocal);
            const QImage filtered = stack.apply(patch); // 不传几何：本地坐标语义
            const int dx = localWant.left() - srcLocal.left();
            const int dy = localWant.top() - srcLocal.top();
            const QImage ref = filtered.copy(dx, dy, localWant.width(), localWant.height());
            r.check(ref.size() == got.image.size() && PsTests::diffCount(got.image, ref) == 0,
                    QStringLiteral("像素层路径：带几何 + 裁回 == 不带几何求值后裁回（diffs=%1）")
                        .arg(PsTests::diffCount(got.image, ref)));
        }
    }

    setFilterGeometryProbe(nullptr);
    return r.fails;
}
