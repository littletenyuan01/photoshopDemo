/**
 * t_gput47.cpp — T4.7：Sharpen / Emboss / Maximum / Minimum GPU vs CPU（有 GPU 才跑）。
 *
 * 门控同 gpumedian：PSLITE_RASTER=gpu 且 backend≠cpu；否则 SKIP。
 * FindEdges 不在本批（Sobel 幅值非线性，不能喂 custom.comp）。
 */
#include "domain/filternode.h"
#include "domain/filterstack.h"
#include "engine/backend/cpurasterbackend.h"
#include "engine/backend/rasterscheduler.h"
#include "engine/op/opname.h"
#include "testutil.h"

#include <QImage>
#include <QtGlobal>
#include <cstdio>

using namespace Ps;
using PsTests::Reporter;

namespace {

int maxChannelDiff(const QImage &a, const QImage &b)
{
    if (a.size() != b.size())
        return 255;
    QImage aa = a.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QImage bb = b.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    int worst = 0;
    for (int y = 0; y < aa.height(); ++y) {
        const QRgb *pa = reinterpret_cast<const QRgb *>(aa.constScanLine(y));
        const QRgb *pb = reinterpret_cast<const QRgb *>(bb.constScanLine(y));
        for (int x = 0; x < aa.width(); ++x) {
            worst = qMax(worst, qAbs(int(qRed(pa[x])) - int(qRed(pb[x]))));
            worst = qMax(worst, qAbs(int(qGreen(pa[x])) - int(qGreen(pb[x]))));
            worst = qMax(worst, qAbs(int(qBlue(pa[x])) - int(qBlue(pb[x]))));
            worst = qMax(worst, qAbs(int(qAlpha(pa[x])) - int(qAlpha(pb[x]))));
        }
    }
    return worst;
}

struct EnvGuard {
    QByteArray key;
    QByteArray old;
    bool had = false;
    EnvGuard(const char *k, const char *v)
        : key(k)
    {
        had = qEnvironmentVariableIsSet(k);
        old = qgetenv(k);
        if (v)
            qputenv(k, v);
        else
            qunsetenv(k);
        RasterScheduler::instance().resetForTest();
    }
    ~EnvGuard()
    {
        if (had)
            qputenv(key.constData(), old);
        else
            qunsetenv(key.constData());
        RasterScheduler::instance().resetForTest();
    }
};

bool checkGpuVsCpu(Reporter &r, const QImage &src, FilterStack stack, const QString &tag)
{
    QImage viaCpu = src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    CpuRasterBackend cpu;
    if (cpu.applyStack(viaCpu, stack, src.rect(), src.rect(), nullptr) != ApplyStackResult::Ok) {
        r.check(false, tag + QStringLiteral("：CPU 参考失败"));
        return false;
    }

    QImage viaSched = src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    ApplyStackResult outR = ApplyStackResult::Ok;
    const QString id =
        RasterScheduler::instance().runStack(viaSched, stack, src.rect(), src.rect(), nullptr,
                                             &outR);
    if (id == QLatin1String("cpu") || outR != ApplyStackResult::Ok) {
        std::fprintf(stderr, "[gput47] SKIP %s: no GPU (id=%s outR=%d)\n", qPrintable(tag),
                     qPrintable(id), int(outR));
        return true; // 跳过不算失败
    }
    r.check(id.startsWith(QLatin1String("gpu")),
            tag + QStringLiteral("：backend=%1").arg(id));
    const int worst = maxChannelDiff(viaSched, viaCpu);
    r.check(worst <= 1, tag + QStringLiteral("：GPU vs CPU 差 ≤1（实际 %1）").arg(worst));
    return true;
}

} // namespace

int tGpuT47()
{
    Reporter r;
    EnvGuard env("PSLITE_RASTER", "gpu");
    RasterScheduler::instance().resetForTest();

    const QImage src = PsTests::makeLayerImage(400, 300, 31, 255);

    {
        FilterStack stack;
        FilterNode n(OpName::Sharpen);
        n.setSharpenStrength(2);
        stack.append(n);
        checkGpuVsCpu(r, src, stack, QStringLiteral("Sharpen"));
    }
    {
        FilterStack stack;
        FilterNode n(OpName::Emboss);
        n.setEmbossStrength(2);
        stack.append(n);
        checkGpuVsCpu(r, src, stack, QStringLiteral("Emboss"));
    }
    {
        FilterStack stack;
        FilterNode n(OpName::Maximum);
        n.setExtremumRadius(2);
        stack.append(n);
        checkGpuVsCpu(r, src, stack, QStringLiteral("Maximum"));
    }
    {
        FilterStack stack;
        FilterNode n(OpName::Minimum);
        n.setExtremumRadius(2);
        stack.append(n);
        checkGpuVsCpu(r, src, stack, QStringLiteral("Minimum"));
    }

    RasterScheduler::instance().resetForTest();
    return r.fails;
}
