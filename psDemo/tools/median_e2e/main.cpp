/**
 * median_e2e — 中值端到端计时（含上传+kernel+回读 vs 生产 CPU）。
 * 一次性程序，不参与常驻用例（gpu-plan.md §8 / T1.12–T1.14）。
 * 源码在 psDemo/tools/median_e2e/（勿放 .build-verify，否则干净 clone 无法构建）。
 *
 * 必须调用 Ps::opsInit()，否则 Release 下面积算子未注册会静默空跑。
 */
#include "domain/filternode.h"
#include "domain/filterstack.h"
#include "engine/backend/cpurasterbackend.h"
#include "engine/backend/gpurasterbackend.h"
#include "engine/op/opname.h"
#include "engine/op/opsinit.h"

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QRgb>

#include <cstdio>
#include <memory>

using namespace Ps;

static QImage makeSrc(int w, int h, int seed)
{
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < h; ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const int v = (x * 17 + y * 31 + seed * 13) & 255;
            row[x] = qRgba(v, (v * 3) & 255, (v * 5) & 255, 255);
        }
    }
    return img;
}

static qint64 timeApply(RasterBackend &backend, const QImage &src, const FilterStack &stack,
                        int warmups, int runs, int *outDiffs)
{
    {
        QImage before = src;
        QImage img = src;
        if (backend.applyStack(img, stack, src.rect(), src.rect(), nullptr) != ApplyStackResult::Ok)
            return -1;
        int d = 0;
        for (int y = 0; y < img.height(); ++y) {
            const QRgb *a = reinterpret_cast<const QRgb *>(before.constScanLine(y));
            const QRgb *b = reinterpret_cast<const QRgb *>(img.constScanLine(y));
            for (int x = 0; x < img.width(); ++x)
                if (a[x] != b[x])
                    ++d;
        }
        if (outDiffs)
            *outDiffs = d;
        if (d == 0)
            return -2;
    }

    for (int i = 0; i < warmups; ++i) {
        QImage img = src;
        if (backend.applyStack(img, stack, src.rect(), src.rect(), nullptr) != ApplyStackResult::Ok)
            return -1;
    }
    QElapsedTimer t;
    t.start();
    for (int i = 0; i < runs; ++i) {
        QImage img = src;
        if (backend.applyStack(img, stack, src.rect(), src.rect(), nullptr) != ApplyStackResult::Ok)
            return -1;
    }
    return t.elapsed();
}

/** T2.7 对齐：warmup=2；与 custom_e2e 同 seed/runs 档。 */
static void benchSize(int w, int h, int radius, CpuRasterBackend &cpu, GpuRasterBackend &gpu)
{
    FilterStack stack;
    stack.append(FilterNode(OpName::MedianBlur));
    stack.at(0).setMedianRadius(radius);
    const QImage src = makeSrc(w, h, 7);
    const int px = w * h;
    const int runs = (px >= 1600 * 1200) ? 3 : ((px >= 500000) ? 4 : 5);
    constexpr int kWarmups = 2;

    int cpuDiffs = 0, gpuDiffs = 0;
    const qint64 cpuMs = timeApply(cpu, src, stack, kWarmups, runs, &cpuDiffs);
    const qint64 gpuMs = timeApply(gpu, src, stack, kWarmups, runs, &gpuDiffs);
    if (cpuMs == -2 || gpuMs == -2) {
        std::printf("%dx%d r=%d NO-OP (cpuDiffs=%d gpuDiffs=%d) — 漏 opsInit？\n", w, h, radius,
                    cpuDiffs, gpuDiffs);
        return;
    }
    if (cpuMs < 0 || gpuMs < 0) {
        std::printf("%dx%d r=%d FAILED (cpu=%lld gpu=%lld)\n", w, h, radius,
                    static_cast<long long>(cpuMs), static_cast<long long>(gpuMs));
        return;
    }
    const double cpuPer = double(cpuMs) / runs;
    const double gpuPer = double(gpuMs) / runs;
    const double speedup = cpuPer / gpuPer;
    const double cpuNs = cpuPer * 1e6 / px;
    const double gpuNs = gpuPer * 1e6 / px;
    std::printf("%5dx%-5d r=%d | CPU %7.1f ms (%5.1f ns/px) | GPU %7.1f ms (%5.1f ns/px) | "
                "%.2fx  (changed %d/%d)\n",
                w, h, radius, cpuPer, cpuNs, gpuPer, gpuNs, speedup, cpuDiffs, px);
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    Ps::opsInit();

    CpuRasterBackend cpu;
    auto gpu = GpuRasterBackend::tryCreate();
    if (!gpu) {
        std::printf("GpuRasterBackend::tryCreate failed — abort\n");
        return 1;
    }
    std::printf("backend=%s  (T2.7: warmup=2; mean of N runs; GPU = upload+kernel+readback)\n",
                qPrintable(gpu->id()));
    std::printf("--------------------------------------------------------------------\n");
    benchSize(512, 512, 3, cpu, *gpu);
    benchSize(1024, 768, 3, cpu, *gpu);
    benchSize(1600, 1200, 3, cpu, *gpu);
    benchSize(512, 512, 1, cpu, *gpu);
    benchSize(512, 512, 2, cpu, *gpu);
    std::printf("--------------------------------------------------------------------\n");
    std::printf("ref: production CPU table uses 187 ns/px @ 512^2 r=3\n");
    return 0;
}
