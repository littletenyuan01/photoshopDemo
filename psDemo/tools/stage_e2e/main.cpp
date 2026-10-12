/**
 * stage_e2e — T4.2/T4.3 分段计时（PSLITE_GPU_STAGE=1）。
 * 输出：format / pack / device(upload+kernel+readback) / unpack。
 * T4.3 后解/预乘在 shader；CPU 侧不应再出现大块 unpremul/premul。
 */
#include "domain/filternode.h"
#include "domain/filterstack.h"
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

static QImage makeSrc(int w, int h)
{
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < h; ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const int v = (x * 17 + y * 31 + 7 * 13) & 255;
            row[x] = qRgba(v, (v * 3) & 255, (v * 5) & 255, 255);
        }
    }
    return img;
}

static FilterNode makeMedian(int r)
{
    FilterNode n(OpName::MedianBlur);
    n.setMedianRadius(r);
    return n;
}

static FilterNode makeSharpen()
{
    const int w[9] = {0, -1, 0, -1, 5, -1, 0, -1, 0};
    FilterNode n(OpName::CustomFilter);
    for (int i = 0; i < 9; ++i)
        n.setCustomWeight(i, w[i]);
    n.setCustomScale(1);
    return n;
}

static void runCase(const char *label, int w, int h, const FilterNode &node, GpuRasterBackend &gpu)
{
    FilterStack stack;
    stack.append(node);
    const QImage src = makeSrc(w, h);
    constexpr int kWarm = 2;
    constexpr int kRuns = 4;

    for (int i = 0; i < kWarm; ++i) {
        QImage img = src;
        gpu.applyStack(img, stack, src.rect(), src.rect(), nullptr);
    }

    std::printf("---- %s %dx%d (stage lines = per applyStack; last %d are timed) ----\n", label, w,
                h, kRuns);
    QElapsedTimer wall;
    wall.start();
    for (int i = 0; i < kRuns; ++i) {
        QImage img = src;
        if (gpu.applyStack(img, stack, src.rect(), src.rect(), nullptr) != ApplyStackResult::Ok) {
            std::printf("FAIL\n");
            return;
        }
    }
    std::printf("  wall mean = %.2f ms/call\n\n", double(wall.elapsed()) / kRuns);
}

int main(int argc, char **argv)
{
    qputenv("PSLITE_GPU_STAGE", "1");
    QGuiApplication app(argc, argv);
    Ps::opsInit();

    auto gpu = GpuRasterBackend::tryCreate();
    if (!gpu) {
        std::printf("GpuRasterBackend::tryCreate failed\n");
        return 1;
    }
    std::printf("backend=%s  PSLITE_GPU_STAGE=1\n", qPrintable(gpu->id()));
    std::printf("device = upload + compute + readback + finish (not split further)\n");
    std::printf("cpu   = format + pack + unpack  (premul round-trip in shader, T4.3)\n\n");

    runCase("median r=3", 512, 512, makeMedian(3), *gpu);
    runCase("median r=3", 1600, 1200, makeMedian(3), *gpu);
    runCase("sharpen", 512, 512, makeSharpen(), *gpu);
    runCase("sharpen", 1600, 1200, makeSharpen(), *gpu);
    return 0;
}
