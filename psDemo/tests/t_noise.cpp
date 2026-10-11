/**
 * t_noise.cpp — 添加杂色：确定性、位置稳定性、均匀/高斯、单色。
 *
 * 守的契约（这个算子的**设计核心就是确定性**）：
 *  ① 同参数重复求值 → **逐像素相同**（噪声是 `(x,y,seed,通道)` 的纯函数，不是 rand()）；
 *  ② 换种子 → 图案不同；
 *  ③ **位置稳定**：请求一块区域得到的结果 == 请求整层再裁同一块。
 *     这条是「整层求值」路由（`FilterStack::hasPositionalOp`）的直接后果；
 *     若被逐块求值，每块的 (0,0) 都不同，噪声会在块界错位/重复，③ 必红；
 *  ④ 单色 = 三通道加**同一个**扰动；非单色 = 三通道独立；
 *  ⑤ 数量 0 = 恒等；全透明像素不受影响（不该把不可见像素变成彩色脏点）；
 *  ⑥ 参数（含种子）存读往返保真。
 */
#include "domain/filternode.h"
#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "engine/filtereval.h"
#include "engine/op/opname.h"
#include "io/projectio.h"
#include "testutil.h"

#include <QImage>

using namespace Ps;
using PsTests::Reporter;

namespace {

/** 纯色层（RGB 全 v），用来直接看「加了多少」。 */
QImage solidImage(int w, int h, int v, int alpha)
{
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < h; ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < w; ++x)
            line[x] = qRgba(v * alpha / 255, v * alpha / 255, v * alpha / 255, alpha);
    }
    return img;
}

} // namespace

int tNoise()
{
    Reporter r;

    const int W = 64, H = 48;
    const QImage src = PsTests::makeLayerImage(W, H, 11, 255);

    FilterNode node(OpName::AddNoise);
    node.setNoiseAmount(20);
    node.setNoiseSeed(12345u);

    // —— ① 确定性：同参数两次求值逐像素相同 ——
    {
        QImage a = src;
        QImage b = src;
        FilterEval::applyNode(a, node);
        FilterEval::applyNode(b, node);
        r.check(PsTests::diffCount(a, b) == 0,
                QStringLiteral("同参数重复求值 == 逐像素相同（diffs=%1）")
                    .arg(PsTests::diffCount(a, b)));
        r.check(PsTests::diffCount(src, a) > 0, QStringLiteral("确实加了噪声（画面变了）"));
    }

    // —— ② 换种子 → 图案不同 ——
    {
        QImage a = src;
        FilterEval::applyNode(a, node);
        FilterNode other = node;
        other.setNoiseSeed(999u);
        QImage b = src;
        FilterEval::applyNode(b, other);
        r.check(PsTests::diffCount(a, b) > 0, QStringLiteral("换种子后图案不同"));
    }

    // —— ⑤ 数量 0 = 恒等；全透明像素不受影响 ——
    {
        FilterNode zero(OpName::AddNoise);
        zero.setNoiseAmount(0);
        QImage a = src;
        FilterEval::applyNode(a, zero);
        r.check(PsTests::diffCount(src, a) == 0, QStringLiteral("数量 0 → 画面不变"));

        QImage transparent(W, H, QImage::Format_ARGB32_Premultiplied);
        transparent.fill(Qt::transparent);
        QImage t2 = transparent;
        FilterEval::applyNode(t2, node);
        r.check(PsTests::diffCount(transparent, t2) == 0,
                QStringLiteral("全透明像素保持全透明（不造出彩色脏点）"));
    }

    // —— ④ 单色 vs 非单色 ——
    {
        const QImage gray = solidImage(W, H, 128, 255);

        FilterNode mono(OpName::AddNoise);
        mono.setNoiseAmount(12);
        mono.setNoiseMonochromatic(true);
        QImage m = gray;
        FilterEval::applyNode(m, mono);
        bool allEqual = true;
        for (int y = 0; y < H && allEqual; ++y) {
            const QRgb *line = reinterpret_cast<const QRgb *>(m.constScanLine(y));
            for (int x = 0; x < W; ++x) {
                const QRgb p = qUnpremultiply(line[x]);
                if (qRed(p) != qGreen(p) || qGreen(p) != qBlue(p)) {
                    allEqual = false;
                    break;
                }
            }
        }
        r.check(allEqual, QStringLiteral("单色：三通道加上同一个扰动（结果仍为灰）"));

        FilterNode color(OpName::AddNoise);
        color.setNoiseAmount(12);
        color.setNoiseMonochromatic(false);
        QImage c = gray;
        FilterEval::applyNode(c, color);
        bool anyDiff = false;
        for (int y = 0; y < H && !anyDiff; ++y) {
            const QRgb *line = reinterpret_cast<const QRgb *>(c.constScanLine(y));
            for (int x = 0; x < W; ++x) {
                const QRgb p = qUnpremultiply(line[x]);
                if (qRed(p) != qGreen(p) || qGreen(p) != qBlue(p)) {
                    anyDiff = true;
                    break;
                }
            }
        }
        r.check(anyDiff, QStringLiteral("非单色：三通道各自独立（出现彩噪）"));
    }

    // —— ③ 位置稳定 + 整层路由判据 ——
    {
        const int DW = 320, DH = 240;
        auto doc = ImageDocument::createBlank(DW, DH, Qt::white);
        doc->placeImageAsLayer(PsTests::makeLayerImage(DW, DH, 17, 255), QStringLiteral("noisy"));
        Layer *layer = doc->layers().layerAt(1);
        if (!layer) {
            r.check(false, QStringLiteral("有活动像素层"));
            return r.fails;
        }
        FilterNode n(OpName::AddNoise);
        n.setNoiseAmount(25);
        n.setNoiseSeed(777u);
        doc->addLayerFilter(n, /*pushUndo=*/false);

        r.check(layer->filters().hasPositionalOp(),
                QStringLiteral("栈被识别为「含位置相关算子」（据此走整层求值）"));

        const QRect region(50, 40, 120, 90);
        const QRect whole(QPoint(0, 0), QSize(DW, DH));
        const Layer::CompositeRegion reg = layer->compositeRasterRegion(region);
        const Layer::CompositeRegion all = layer->compositeRasterRegion(whole);

        // 「位置稳定」在这一层的**可测形式**就是：请求任意区域都返回**同一张整层图**
        // （`docX/docY` 标明它在文档里的原点）。若哪天有人把位置相关算子也改成逐块求值，
        // 请求 region 就会得到一块「以 region 左上角为 (0,0)」的噪声 → 这两条立刻红。
        r.check(reg.image.size() == QSize(DW, DH) && all.image.size() == QSize(DW, DH),
                QStringLiteral("位置相关算子走整层：请求小区域也返回整层图（%1×%2，而非裁好的 %3×%4）")
                    .arg(reg.image.width()).arg(reg.image.height())
                    .arg(region.width()).arg(region.height()));
        r.check(reg.docX == all.docX && reg.docY == all.docY,
                QStringLiteral("两种请求的文档原点一致（噪声锚点不随请求漂移）"));
        const long long d = PsTests::diffCount(reg.image, all.image);
        r.check(d == 0, QStringLiteral("请求区域 == 请求整层，逐像素一致（diffs=%1）").arg(d));

        // 再钉一层：整层图上的噪声 == 对「层内容直接施加同一节点」的结果
        // （证明走整层路径时的坐标口径确实是「层内坐标」，没被平移过）
        {
            QImage direct = layer->materialize();
            FilterEval::applyNode(direct, n);
            // ensureCompositeRaster 可能带 originDx/originDy 偏移，按原点取出对应块再比
            const QRect inAll(QPoint(0, 0), direct.size());
            const QImage got = reg.image.copy(inAll);
            r.check(PsTests::diffCount(got, direct) == 0,
                    QStringLiteral("整层渲染 == 直接对层内容加噪声（diffs=%1）")
                        .arg(PsTests::diffCount(got, direct)));
        }

        // 对照：纯点滤镜**不**该被判成位置相关（否则会白白整层重算）
        {
            auto plain = ImageDocument::createBlank(64, 64, Qt::white);
            plain->placeImageAsLayer(PsTests::makeLayerImage(64, 64, 3, 255), QStringLiteral("p"));
            Layer *pl = plain->layers().layerAt(1);
            FilterNode inv(OpName::Invert);
            plain->addLayerFilter(inv, /*pushUndo=*/false);
            r.check(pl && !pl->filters().hasPositionalOp(),
                    QStringLiteral("纯点滤镜（反相）不被判成位置相关（不白白整层重算）"));
        }

        // —— ⑥ 往返（含种子）——
        const QString path = PsTests::tempFilePath(QStringLiteral("noise.pslite"));
        QString err;
        const bool saved = ProjectIo::save(*doc, path, &err);
        auto back = saved ? ProjectIo::load(path, &err) : nullptr;
        if (back && back->layers().count() > 1
            && back->layers().layerAt(1)->filters().count() > 0) {
            const FilterNode &bn = back->layers().layerAt(1)->filters().at(0);
            r.check(bn.op() == OpName::AddNoise && bn.noiseAmount() == 25 && bn.noiseSeed() == 777u
                        && bn.noiseMonochromatic(),
                    QStringLiteral("往返保住 数量/单色/种子"));
        } else {
            r.check(false, QStringLiteral("重新打开含添加杂色的 .pslite"));
        }
    }

    // —— 高斯分布也要确定且与均匀不同 ——
    {
        FilterNode uni(OpName::AddNoise);
        uni.setNoiseAmount(15);
        uni.setNoiseGaussian(false);
        FilterNode gau = uni;
        gau.setNoiseGaussian(true);
        QImage a = src, b = src;
        FilterEval::applyNode(a, uni);
        FilterEval::applyNode(b, gau);
        r.check(PsTests::diffCount(a, b) > 0, QStringLiteral("高斯与均匀分布结果不同"));
        QImage b2 = src;
        FilterEval::applyNode(b2, gau);
        r.check(PsTests::diffCount(b, b2) == 0, QStringLiteral("高斯分布同样可复现"));
    }

    return r.fails;
}
