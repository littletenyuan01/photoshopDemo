/**
 * t_composite.cpp — 投影分块「并行」合成 vs 单次整体合成，逐像素一致。
 *
 * 文档刻意混入**调整层**与**带滤镜的层**：
 * 并行化的第一版曾在调整层分支残留 QPainter（同一 paint device 不能被多个 QPainter
 * 同时绘制），导致并行合成访问冲突崩溃 —— 而当时不含调整层的等价性测试完全没暴露。
 * 这个用例就是为了守住那条路径。
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

int tComposite()
{
    Reporter r;

    const int W = 1200, H = 900;
    auto doc = ImageDocument::createBlank(W, H, Qt::white);
    doc->placeImageAsLayer(PsTests::makeLayerImage(W, H, 1, 255), QStringLiteral("L1"));
    doc->placeImageAsLayer(PsTests::makeLayerImage(W, H, 2, 200), QStringLiteral("L2"));
    doc->setLayerBlendMode(2, BlendMode::Multiply);

    // 带滤镜的层（走区域化栅格 + 并行块）
    doc->addBrightnessContrastFilter(0.15, 0.1);

    // 调整层（走 dst.copy(area) → 滤镜 → 写回；正是曾经崩溃的分支）
    const int adj = doc->addAdjustmentLayer(OpName::BrightnessContrast);
    r.check(adj >= 0, QStringLiteral("成功新建调整层"));

    // 参考：单次整体（一次 compositeRegion，单线程）
    const QImage ref = Compositor::composite(*doc);

    bool anyBad = false;
    QString detail;
    for (int round = 0; round < 5 && !anyBad; ++round) {
        Projection p;
        p.bind(doc.get());
        p.setPriorityRect(QRect(0, 0, W, H));
        doc->markDirty();
        p.sync();
        while (p.hasPendingChunks())
            p.processPendingChunks(0);
        const QImage got = p.image();
        const long long d = PsTests::diffCount(ref, got);
        if (d != 0) {
            anyBad = true;
            detail = QStringLiteral("diffs=%1 | 参考 %2x%3 fmt=%4 | 分块 %5x%6 fmt=%7")
                         .arg(d)
                         .arg(ref.width())
                         .arg(ref.height())
                         .arg(int(ref.format()))
                         .arg(got.width())
                         .arg(got.height())
                         .arg(int(got.format()));
        }
    }
    r.check(!anyBad, anyBad ? detail
                            : QStringLiteral("5 轮分块并行合成均与单次整体逐像素一致"));
    return r.fails;
}
