/**
 * t_psdio.cpp — PSD 子集导出→导入往返：层属性 + 合成像素。
 */
#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "engine/compositor.h"
#include "io/psdio.h"
#include "testutil.h"

using namespace Ps;
using PsTests::Reporter;

int tPsdIo()
{
    Reporter r;

    const int W = 96, H = 64;
    auto doc = ImageDocument::createBlank(W, H, Qt::white);
    {
        ImageDocument::HistorySuppress suppress(*doc);
        // 再叠一层半透明、偏移、Multiply
        QImage overlay = PsTests::makeLayerImage(40, 30, 3, 200);
        auto layer = std::make_unique<Layer>(QStringLiteral("overlay"), overlay);
        layer->setOffsetSilent(12, 8);
        layer->setOpacity(0.6);
        layer->setBlendMode(BlendMode::Multiply);
        doc->addLayer(std::move(layer));
        doc->setActiveLayerIndex(1);
    }

    const QImage before = Compositor::composite(*doc);
    const QString path = PsTests::tempFilePath(QStringLiteral("roundtrip.psd"));

    QString err;
    r.check(PsdIo::save(*doc, path, &err),
            QStringLiteral("导出 PSD（%1）").arg(err.isEmpty() ? QStringLiteral("ok") : err));

    auto back = PsdIo::load(path, &err);
    r.check(back != nullptr,
            QStringLiteral("导入 PSD（%1）").arg(err.isEmpty() ? QStringLiteral("ok") : err));
    if (!back)
        return r.fails;

    r.check(back->width() == W && back->height() == H,
            QStringLiteral("画布尺寸 %1×%2").arg(back->width()).arg(back->height()));
    r.check(back->layers().count() == doc->layers().count(),
            QStringLiteral("层数一致（%1）").arg(back->layers().count()));

    if (back->layers().count() >= 2) {
        const Layer *A = doc->layers().layerAt(1);
        const Layer *B = back->layers().layerAt(1);
        r.check(B->name() == A->name(), QStringLiteral("层名「%1」").arg(B->name()));
        r.check(B->offsetX() == A->offsetX() && B->offsetY() == A->offsetY(),
                QStringLiteral("offset (%1,%2)")
                    .arg(B->offsetX())
                    .arg(B->offsetY()));
        r.check(B->blendMode() == A->blendMode(), QStringLiteral("混合模式"));
        r.check(qAbs(B->opacity() - A->opacity()) < 0.01,
                QStringLiteral("不透明度 %1").arg(B->opacity()));
        r.check(B->isVisible() == A->isVisible(), QStringLiteral("可见性"));
    }

    const QImage after = Compositor::composite(*back);
    // 解预乘↔预乘有量化，允许少量差异
    const long long d = PsTests::diffCount(before, after);
    const int maxDiff = PsTests::maxChannelDiff(before, after);
    r.check(d == 0 || maxDiff <= 1,
            QStringLiteral("合成往返 diffs=%1 maxCh=%2").arg(d).arg(maxDiff));

    // 非法路径
    auto bad = PsdIo::load(QStringLiteral("no-such-file.psd"), &err);
    r.check(bad == nullptr && !err.isEmpty(), QStringLiteral("缺文件失败有原因"));

    return r.fails;
}
