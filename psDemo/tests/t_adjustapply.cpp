/**
 * t_adjustapply.cpp — 调整层参数落盘后，重新打开**仍作用于画面**。
 *
 * 判据：打开后先合成一次；再关掉全部调整层合成一次；两者必须有差异
 * （差异为 0 说明调整没生效，即用户会看到「原始未调整的图」）。
 */
#include "domain/filternode.h"
#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "engine/op/opsinit.h"
#include "engine/projection.h"
#include "io/projectio.h"
#include "testutil.h"

using namespace Ps;
using PsTests::Reporter;

namespace {

/** 只合成视口优先区（比整幅快得多，且足够判定）。 */
QImage renderViewport(ImageDocument *doc, const QRect &vp)
{
    Projection p;
    p.bind(doc);
    p.setPriorityRect(vp);
    doc->markDirty();
    p.sync();
    while (p.hasPendingChunks())
        p.processPendingChunks(0);
    return p.image().copy(vp);
}

} // namespace

int tAdjustApply()
{
    Reporter r;

    const int W = 400, H = 300;
    const QRect vp(0, 0, 200, 200);

    // 1) 造文档：底图 + 非恒等参数的调整层
    auto doc = ImageDocument::createBlank(W, H, Qt::white);
    doc->placeImageAsLayer(PsTests::makeLayerImage(W, H, 1, 255), QStringLiteral("bg"));
    const int adj = doc->addAdjustmentLayer(OpName::BrightnessContrast);
    {
        FilterNode n = doc->layers().layerAt(adj)->filters().at(0);
        n.setBrightness(0.5);
        n.setContrast(0.4);
        doc->setLayerFilterNode(adj, 0, n, false);
    }

    // 2) 落盘 + 重新打开
    const QString path = PsTests::tempFilePath(QStringLiteral("adjustapply.pslite"));
    QString err;
    r.check(ProjectIo::save(*doc, path, &err), QStringLiteral("保存 .pslite"));
    auto back = ProjectIo::load(path, &err);
    r.check(back != nullptr, QStringLiteral("重新打开 .pslite"));
    if (!back)
        return r.fails;

    // 3) 打开后的参数必须还是非默认值
    double loadedBri = 0.0;
    for (int i = 0; i < back->layers().count(); ++i) {
        Layer *L = back->layers().layerAt(i);
        if (L->isAdjustmentLayer() && L->filters().count() > 0)
            loadedBri = L->filters().at(0).brightness();
    }
    r.check(qFuzzyCompare(loadedBri, 0.5) || qAbs(loadedBri - 0.5) < 1e-9,
            QStringLiteral("重开后调整层参数为 0.5（实际 %1）").arg(loadedBri, 0, 'f', 4));

    // 4) 画面必须已经过调整
    const QImage withAdj = renderViewport(back.get(), vp);
    for (int i = 0; i < back->layers().count(); ++i) {
        if (back->layers().layerAt(i)->isAdjustmentLayer())
            back->setLayerFilterEnabled(i, 0, false);
    }
    const QImage noAdj = renderViewport(back.get(), vp);
    const long long d = PsTests::diffCount(withAdj, noAdj);
    r.check(d > 0, QStringLiteral("关掉调整层前后画面有差异（%1/%2 像素）")
                       .arg(d)
                       .arg(vp.width() * vp.height()));
    return r.fails;
}
