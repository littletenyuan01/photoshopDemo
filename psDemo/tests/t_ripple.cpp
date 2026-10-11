/**
 * t_ripple.cpp — 波纹（几何重映射代表项）：oracle + 整层路径 + 往返。
 */
#include "domain/filternode.h"
#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "engine/compositor.h"
#include "engine/filtereval.h"
#include "engine/op/opname.h"
#include "engine/projection.h"
#include "io/projectio.h"
#include "testutil.h"

#include <QImage>

#include <cmath>

using namespace Ps;
using PsTests::Reporter;

namespace {

QImage naiveRipple(const QImage &src, int A, int L)
{
    const int w = src.width();
    const int h = src.height();
    QImage out(w, h, src.format());
    out.fill(Qt::transparent);
    const double k = 2.0 * 3.14159265358979323846 / double(L);
    for (int y = 0; y < h; ++y) {
        const int dx = int(std::lround(A * std::sin(k * y)));
        for (int x = 0; x < w; ++x) {
            const int sx = x + dx;
            if (sx < 0 || sx >= w)
                continue;
            out.setPixel(x, y, src.pixel(sx, y));
        }
    }
    return out;
}

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

int tRipple()
{
    Reporter r;

    {
        const QImage src = PsTests::makeLayerImage(48, 36, 41, 255);
        const int A = 5, L = 16;
        QImage got = src;
        FilterNode n(OpName::Ripple);
        n.setRippleAmplitude(A);
        n.setRippleWavelength(L);
        FilterEval::applyNode(got, n);
        const QImage ref = naiveRipple(src, A, L);
        const long long d = PsTests::diffCount(got, ref);
        r.check(d == 0, QStringLiteral("波纹 == 朴素正弦位移（diffs=%1）").arg(d));
        r.check(PsTests::diffCount(src, got) > 0, QStringLiteral("波纹确实改变画面"));
    }

    {
        FilterNode n(OpName::Ripple);
        n.setRippleAmplitude(0);
        QImage src = PsTests::makeLayerImage(24, 16, 7, 255);
        QImage got = src;
        FilterEval::applyNode(got, n);
        r.check(PsTests::diffCount(got, src) == 0, QStringLiteral("振幅 0 ≈ 恒等"));
    }

    {
        FilterNode n(OpName::Ripple);
        n.setRippleAmplitude(6);
        const QRect want(40, 40, 64, 64);
        r.check(n.requiredInputs(want) == want.adjusted(-6, 0, 6, 0),
                QStringLiteral("波纹输入需求只向左右扩振幅"));
    }

    {
        auto doc = ImageDocument::createBlank(256, 192, Qt::white);
        doc->placeImageAsLayer(PsTests::makeLayerImage(200, 160, 59, 220), QStringLiteral("rip"));
        doc->layers().layerAt(1)->setOffset(20, 10);
        FilterNode n(OpName::Ripple);
        n.setRippleAmplitude(8);
        n.setRippleWavelength(20);
        doc->layers().layerAt(1)->filters().append(n);
        r.check(doc->layers().layerAt(1)->filters().hasPositionalOp(),
                QStringLiteral("波纹走位置相关（整层）路径"));
        const long long dc =
            PsTests::diffCount(Compositor::composite(*doc), chunkedProjection(doc.get()));
        r.check(dc == 0, QStringLiteral("波纹：分块投影 == 整幅（diffs=%1）").arg(dc));
    }

    {
        auto doc = ImageDocument::createBlank(80, 60, Qt::white);
        doc->placeImageAsLayer(PsTests::makeLayerImage(60, 48, 11, 255), QStringLiteral("rip2"));
        FilterNode n(OpName::Ripple);
        n.setRippleAmplitude(7);
        n.setRippleWavelength(18);
        doc->layers().layerAt(1)->filters().append(n);
        const QString path = PsTests::tempFilePath(QStringLiteral("ripple.pslite"));
        QString err;
        const bool saved = ProjectIo::save(*doc, path, &err);
        auto back = saved ? ProjectIo::load(path, &err) : nullptr;
        if (back && back->layers().count() > 1 && back->layers().layerAt(1)->filters().count() > 0) {
            const FilterNode &bn = back->layers().layerAt(1)->filters().at(0);
            r.check(bn.op() == OpName::Ripple && bn.rippleAmplitude() == 7
                        && bn.rippleWavelength() == 18,
                    QStringLiteral("波纹参数往返"));
        } else {
            r.check(false, QStringLiteral("重新打开含波纹的 .pslite"));
        }
    }

    return r.fails;
}
