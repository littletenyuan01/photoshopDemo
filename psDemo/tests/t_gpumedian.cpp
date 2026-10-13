/**
 * t_gpumedian.cpp — GPU 中值门控 + T5.1 大半径正确性（gpu-plan.md §8 / §17.1）。
 *
 * ① 有 GPU：r=2 走 GPU，与 CPU 逐通道差 ≤1；无 GPU 则跳过（不算失败）。
 * ② **始终跑**：r=5..8 在 PSLITE_RASTER=gpu 下必须走 CPU（shader 只支持 r≤4），
 *    且结果与纯 CPU bit-exact —— 钉住台账 #22（曾静默错像素）。
 */
#include "domain/filternode.h"
#include "domain/filterstack.h"
#include "engine/backend/cpurasterbackend.h"
#include "engine/backend/gpurasterbackend.h"
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

} // namespace

int tGpuMedian()
{
    Reporter r;
    EnvGuard env("PSLITE_RASTER", "gpu");
    auto &sched = RasterScheduler::instance();
    sched.resetForTest();

    // ≥ kMinPixelsForGpu（100k）；ForceGpu 仍守像素门槛
    const QImage src = PsTests::makeLayerImage(400, 300, 17, 255);

    // —— ① 有 GPU 才断言加速路径（无 GPU = 跳过本段，不算失败）——
    {
        FilterStack stack;
        stack.append(FilterNode(OpName::MedianBlur));
        stack.at(0).setMedianRadius(2);

        QImage viaCpu = src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        CpuRasterBackend cpu;
        if (cpu.applyStack(viaCpu, stack, src.rect(), src.rect(), nullptr)
            != ApplyStackResult::Ok) {
            r.check(false, QStringLiteral("CPU 中值参考失败"));
            return r.fails;
        }

        QImage viaSched = src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        ApplyStackResult outR = ApplyStackResult::Ok;
        const QString id =
            sched.runStack(viaSched, stack, src.rect(), src.rect(), nullptr, &outR);

        if (id == QLatin1String("cpu") || outR != ApplyStackResult::Ok) {
            std::fprintf(stderr,
                         "[gpumedian] SKIP GPU gate: no usable GPU (id=%s outR=%d)\n",
                         qPrintable(id), int(outR));
        } else {
            r.check(id.startsWith(QLatin1String("gpu")),
                    QStringLiteral("门控通过 → backend=%1").arg(id));
            const int worst = maxChannelDiff(viaSched, viaCpu);
            r.check(worst <= 1,
                    QStringLiteral("GPU 中值 vs CPU 逐通道差 ≤1（实际 %1）").arg(worst));
        }
    }

    // —— ② T5.1 / 台账 #22：始终跑（不依赖 GPU 是否可用）——
    {
        auto gpu = GpuRasterBackend::tryCreate();
        if (gpu) {
            FilterStack hi;
            hi.append(FilterNode(OpName::MedianBlur));
            hi.at(0).setMedianRadius(5);
            r.check(!gpu->supports(hi),
                    QStringLiteral("supports(中值 r=5) == false（shader 上限 r≤4）"));
            FilterStack lo;
            lo.append(FilterNode(OpName::MedianBlur));
            lo.at(0).setMedianRadius(4);
            r.check(gpu->supports(lo),
                    QStringLiteral("supports(中值 r=4) == true（临界仍可加速）"));
            FilterStack mx;
            mx.append(FilterNode(OpName::Maximum));
            mx.at(0).setExtremumRadius(6);
            r.check(!gpu->supports(mx),
                    QStringLiteral("supports(最大值 r=6) == false"));
        } else {
            r.info(QStringLiteral("无 GpuRasterBackend → 跳过 supports() 直测"));
        }

        for (int radius : {5, 6, 7, 8}) {
            FilterStack hi;
            hi.append(FilterNode(OpName::MedianBlur));
            hi.at(0).setMedianRadius(radius);

            QImage expect = src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            CpuRasterBackend cpuOnly;
            r.check(cpuOnly.applyStack(expect, hi, src.rect(), src.rect(), nullptr)
                        == ApplyStackResult::Ok,
                    QStringLiteral("CPU 参考中值 r=%1").arg(radius));

            QImage got = src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            ApplyStackResult orr = ApplyStackResult::Ok;
            const QString bid =
                sched.runStack(got, hi, src.rect(), src.rect(), nullptr, &orr);
            r.check(orr == ApplyStackResult::Ok && bid == QLatin1String("cpu"),
                    QStringLiteral("r=%1 → 调度走 CPU（backend=%2）").arg(radius).arg(bid));
            r.check(maxChannelDiff(got, expect) == 0,
                    QStringLiteral("r=%1 调度结果 == 纯 CPU（bit-exact）").arg(radius));
        }
    }

    sched.resetForTest();
    return r.fails;
}
