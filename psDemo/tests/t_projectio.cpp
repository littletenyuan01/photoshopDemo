/**
 * t_projectio.cpp — .pslite 存读往返：层结构 / 调整层参数 / 像素 全保真。
 *
 * 除了逐字段比对，最后用「整幅合成逐像素一致」做端到端判据 ——
 * 它同时覆盖像素数据、滤镜链、调整层参数、蒙版、层属性与 offset。
 */
#include "domain/filternode.h"
#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "engine/compositor.h"
#include "io/projectio.h"
#include "testutil.h"

using namespace Ps;
using PsTests::Reporter;

int tProjectIo()
{
    Reporter r;

    const int W = 320, H = 240;
    auto doc = ImageDocument::createBlank(W, H, Qt::white);
    doc->placeImageAsLayer(PsTests::makeLayerImage(W, H, 1, 255), QStringLiteral("bg"));
    doc->addBrightnessContrastFilter(0.2, 0.1); // 像素层滤镜

    // 调整层 1：亮度/对比度（非默认值）
    const int a1 = doc->addAdjustmentLayer(OpName::BrightnessContrast);
    {
        FilterNode n = doc->layers().layerAt(a1)->filters().at(0);
        n.setBrightness(0.42);
        n.setContrast(-0.17);
        doc->setLayerFilterNode(a1, 0, n, false);
    }
    // 调整层 2：色阶（含容易被量程夹住的 gamma）
    const int a2 = doc->addAdjustmentLayer(OpName::Levels);
    {
        FilterNode n = doc->layers().layerAt(a2)->filters().at(0);
        n.setLevelsBlack(137.0);
        n.setLevelsWhite(168.0);
        n.setLevelsGamma(4.79);
        doc->setLayerFilterNode(a2, 0, n, false);
    }
    // 层属性 + 蒙版 + offset
    doc->setLayerOpacity(1, 0.6);
    doc->setLayerBlendMode(1, BlendMode::Multiply);
    doc->translateLayer(1, 7, -5, /*emitContent=*/false);
    doc->addLayerMask(1, ImageDocument::LayerMaskInit::HideAll);

    const QImage before = Compositor::composite(*doc);

    const QString path = PsTests::tempFilePath(QStringLiteral("roundtrip.pslite"));
    QString err;
    r.check(ProjectIo::save(*doc, path, &err),
            QStringLiteral("保存 .pslite（%1）").arg(err.isEmpty() ? QStringLiteral("ok") : err));
    auto back = ProjectIo::load(path, &err);
    r.check(back != nullptr,
            QStringLiteral("重新打开 .pslite（%1）").arg(err.isEmpty() ? QStringLiteral("ok") : err));
    if (!back)
        return r.fails;

    r.check(back->layers().count() == doc->layers().count(),
            QStringLiteral("层数一致（%1）").arg(back->layers().count()));

    int fieldBad = 0;
    for (int i = 0; i < doc->layers().count() && i < back->layers().count(); ++i) {
        Layer *A = doc->layers().layerAt(i);
        Layer *B = back->layers().layerAt(i);
        if (A->isAdjustmentLayer() != B->isAdjustmentLayer())
            ++fieldBad;
        if (A->opacity() != B->opacity() || A->blendMode() != B->blendMode()
            || A->offsetX() != B->offsetX() || A->offsetY() != B->offsetY()
            || A->hasMask() != B->hasMask())
            ++fieldBad;
        if (A->filters().count() != B->filters().count()) {
            ++fieldBad;
            continue;
        }
        for (int k = 0; k < A->filters().count(); ++k) {
            const FilterNode &x = A->filters().at(k);
            const FilterNode &y = B->filters().at(k);
            if (x.op() != y.op() || x.isEnabled() != y.isEnabled()
                || x.brightness() != y.brightness() || x.contrast() != y.contrast()
                || x.levelsBlack() != y.levelsBlack() || x.levelsWhite() != y.levelsWhite()
                || x.levelsGamma() != y.levelsGamma() || x.saturation() != y.saturation())
                ++fieldBad;
        }
    }
    r.check(fieldBad == 0,
            QStringLiteral("层 kind / 属性 / 滤镜参数逐项一致（不一致 %1 项）").arg(fieldBad));

    const QImage after = Compositor::composite(*back);
    const long long d = PsTests::diffCount(before, after);
    r.check(d == 0, QStringLiteral("存读往返后整幅合成逐像素一致（diffs=%1）").arg(d));
    return r.fails;
}
