/**
 * custom_e2e — 自定滤镜端到端（gpu-plan T2.0 / T2.5 / T2.7 / T2.8）。
 *
 * 标准测法（T2.7）：
 *   - 同一 seed=7 图像；warmup=2；runs 按像素档固定；
 *   - CPU ns/px 与 GPU÷CPU **共用同一次** timeApply 的 CPU 均值（不再分表不同 seed）；
 *   - 先建 GPU（避免冷启动夹在两次 CPU 测之间）。
 * 必须 Ps::opsInit()。
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

static constexpr int kSeed = 7;
static constexpr int kWarmups = 2;

static QImage makeSrc(int w, int h)
{
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < h; ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const int v = (x * 17 + y * 31 + kSeed * 13) & 255;
            row[x] = qRgba(v, (v * 3) & 255, (v * 5) & 255, 255);
        }
    }
    return img;
}

static int runsFor(qint64 px)
{
    if (px >= 1600 * 1200)
        return 3;
    if (px >= 500000)
        return 4;
    return 5;
}

static FilterNode makeSharpen()
{
    const int w[9] = {0, -1, 0, -1, 5, -1, 0, -1, 0};
    FilterNode n(OpName::CustomFilter);
    for (int i = 0; i < 9; ++i)
        n.setCustomWeight(i, w[i]);
    n.setCustomScale(1);
    n.setCustomOffset(0);
    return n;
}

/** 稠密核：9 个非零权重（盒模糊近似）；T2.8。 */
static FilterNode makeDenseBox()
{
    FilterNode n(OpName::CustomFilter);
    for (int i = 0; i < 9; ++i)
        n.setCustomWeight(i, 1);
    n.setCustomScale(9);
    n.setCustomOffset(0);
    return n;
}

static FilterNode makeGauss(int radius)
{
    FilterNode n(OpName::GaussianBlur);
    n.setGaussRadius(radius);
    return n;
}

static qint64 timeApply(RasterBackend &backend, const QImage &src, const FilterStack &stack,
                        int warmups, int runs, int *outChanged)
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
        if (outChanged)
            *outChanged = d;
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

static void benchPair(const char *label, int w, int h, const FilterNode &node,
                      CpuRasterBackend &cpu, GpuRasterBackend *gpu)
{
    FilterStack stack;
    stack.append(node);
    const QImage src = makeSrc(w, h);
    const qint64 px = qint64(w) * h;
    const int runs = runsFor(px);

    int cChanged = 0, gChanged = 0;
    const qint64 cMs = timeApply(cpu, src, stack, kWarmups, runs, &cChanged);
    if (cMs < 0) {
        std::printf("  %-18s %4dx%-4d | CPU FAIL code=%lld\n", label, w, h,
                    static_cast<long long>(cMs));
        return;
    }
    const double cMean = double(cMs) / runs;
    const double cNs = cMean * 1e6 / double(px);

    if (!gpu) {
        std::printf("  %-18s %4dx%-4d | CPU %7.1f ms (%5.1f ns/px)  changed=%d\n", label, w, h,
                    cMean, cNs, cChanged);
        return;
    }

    const qint64 gMs = timeApply(*gpu, src, stack, kWarmups, runs, &gChanged);
    if (gMs < 0) {
        std::printf("  %-18s %4dx%-4d | CPU %7.1f ms | GPU FAIL code=%lld\n", label, w, h, cMean,
                    static_cast<long long>(gMs));
        return;
    }
    const double gMean = double(gMs) / runs;
    const double gNs = gMean * 1e6 / double(px);
    std::printf("  %-18s %4dx%-4d | CPU %7.1f ms (%5.1f ns/px) | GPU %7.1f ms (%5.1f ns/px) | "
                "%.2fx  (changed %d/%lld)\n",
                label, w, h, cMean, cNs, gMean, gNs, cMean / gMean, gChanged,
                static_cast<long long>(px));
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    Ps::opsInit();

    CpuRasterBackend cpu;
    auto gpu = GpuRasterBackend::tryCreate();

    std::printf("==== standard method (T2.7): seed=%d warmup=%d; CPU&GPU share same src/runs ====\n",
                kSeed, kWarmups);
    if (gpu)
        std::printf("backend=%s\n", qPrintable(gpu->id()));
    else
        std::printf("GPU unavailable — CPU-only rows\n");

    std::printf("\n---- CustomFilter sharpen (5 nonzero) ----\n");
    benchPair("sharpen", 512, 512, makeSharpen(), cpu, gpu.get());
    benchPair("sharpen", 1024, 768, makeSharpen(), cpu, gpu.get());
    benchPair("sharpen", 1600, 1200, makeSharpen(), cpu, gpu.get());

    std::printf("\n---- CustomFilter dense box (9 ones / scale=9) T2.8 ----\n");
    benchPair("dense-box", 512, 512, makeDenseBox(), cpu, gpu.get());
    benchPair("dense-box", 1024, 768, makeDenseBox(), cpu, gpu.get());
    benchPair("dense-box", 1600, 1200, makeDenseBox(), cpu, gpu.get());

    std::printf("\n---- GaussianBlur r=3 (CPU ns/px only) ----\n");
    benchPair("gauss r=3", 512, 512, makeGauss(3), cpu, nullptr);
    benchPair("gauss r=3", 1024, 768, makeGauss(3), cpu, nullptr);

    return 0;
}
