/**
 * t_gpucustom.cpp — 有 GPU 才跑的自定滤镜一致性门控（gpu-plan.md §7.4 / T2.4）。
 *
 * 门控：PSLITE_RASTER=gpu 且调度实际返回非 cpu。不满足则跳过（不算失败）。
 * 容差：目标 bit-exact；护栏逐通道 ≤1。
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

FilterNode makeSharpenNode()
{
    // 与 t_morefilters 自定 oracle 同核
    const int w[9] = {0, -1, 0, -1, 5, -1, 0, -1, 0};
    FilterNode node(OpName::CustomFilter);
    for (int i = 0; i < 9; ++i)
        node.setCustomWeight(i, w[i]);
    node.setCustomScale(1);
    node.setCustomOffset(0);
    return node;
}

} // namespace

int tGpuCustom()
{
    Reporter r;
    EnvGuard env("PSLITE_RASTER", "gpu");
    auto &sched = RasterScheduler::instance();
    sched.resetForTest();

    const QImage src = PsTests::makeLayerImage(400, 300, 23, 255);
    FilterStack stack;
    stack.append(makeSharpenNode());

    QImage viaCpu = src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    CpuRasterBackend cpu;
    if (cpu.applyStack(viaCpu, stack, src.rect(), src.rect(), nullptr) != ApplyStackResult::Ok) {
        r.check(false, QStringLiteral("CPU 自定参考失败"));
        return r.fails;
    }

    QImage viaSched = src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    ApplyStackResult outR = ApplyStackResult::Ok;
    const QString id =
        sched.runStack(viaSched, stack, src.rect(), src.rect(), nullptr, &outR);

    if (id == QLatin1String("cpu") || outR != ApplyStackResult::Ok) {
        std::fprintf(stderr,
                     "[gpucustom] SKIP: no usable GPU backend (id=%s outR=%d)\n",
                     qPrintable(id), int(outR));
        return 0;
    }

    r.check(id.startsWith(QLatin1String("gpu")),
            QStringLiteral("门控通过 → backend=%1").arg(id));
    const int worst = maxChannelDiff(viaSched, viaCpu);
    r.check(worst <= 1, QStringLiteral("GPU 自定 vs CPU 逐通道差 ≤1（实际 %1）").arg(worst));

    sched.resetForTest();
    return r.fails;
}
