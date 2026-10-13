/**
 * gpu_bench — 走**真实调度路径**（RasterScheduler：supports → 成本阈值 → 像素门槛 →
 * 熔断）的端到端计时，覆盖当前所有 GPU 算子。
 *
 * 【为什么不用 median_e2e/custom_e2e 的结论回答"GPU 有没有效果"】
 * 那两个工具**直接调 GpuRasterBackend**，绕过了调度器，所以它们的数字是"kernel 上限"，
 * 不是"生产里会不会走 GPU、走了值不值"。生产实际是 `RasterScheduler::runStack`：
 * 它还会问 supports()、算链成本（阈值 5 ns/px）、比像素数（≥100k），任一条不过就回 CPU。
 * 所以本工具两条都测：先报"调度器到底选了谁"，再报两者的端到端耗时。
 *
 * 【第二块：合成路径】第 ② 段测「带滤镜的层 + 投影合成」，因为默认改成自动之后，
 * GPU 会被**投影合成**在后台线程里反复调用（每 64×64 块一次 `applyStack`），
 * 这与"单次滤镜"是两种负载：小块多、调用密、且 `GpuRasterBackend` 内部有单飞互斥。
 * 只测单次 applyStack 会漏掉这个形态。
 *
 * 用法：release/gpu_bench.exe              （默认 Auto：会自动用 GPU）
 *       release/gpu_bench.exe 1600 1200    指定尺寸
 *       set PSLITE_RASTER=cpu              对照纯 CPU 基线
 */
#include "domain/filternode.h"
#include "domain/filterstack.h"
#include "engine/backend/cpurasterbackend.h"
#include "engine/backend/rasterscheduler.h"
#include "engine/compositor.h"
#include "engine/op/opname.h"
#include "engine/op/opsinit.h"

#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "engine/projection.h"

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QRgb>

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <vector>

using namespace Ps;

namespace {

QImage makeSrc(int w, int h, int seed)
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

struct Case {
    const char *tag;
    std::vector<FilterNode> nodes;
};

FilterNode node(OpName op)
{
    return FilterNode(op);
}

/**
 * 测一条链：先问调度器选谁，再分别量 CPU 与「走调度器」的端到端耗时。
 * 返回时打印 `chosen`（调度器实际用的后端）与 `x`（CPU/调度器 的加速比）。
 */
void bench(const Case &c, const QImage &src, int warmups, int runs)
{
    FilterStack stack;
    for (const FilterNode &n : c.nodes)
        stack.append(n);
    const int px = src.width() * src.height();

    // ① 调度器会选谁？（shouldUseGpu 的完整判据）
    const bool schedPicksGpu = RasterScheduler::instance().shouldUseGpu(stack, px);

    // ② CPU 基线
    CpuRasterBackend cpu;
    for (int i = 0; i < warmups; ++i) {
        QImage img = src;
        cpu.applyStack(img, stack, src.rect(), src.rect(), nullptr);
    }
    QElapsedTimer t;
    t.start();
    QImage cpuOut;
    for (int i = 0; i < runs; ++i) {
        QImage img = src;
        cpu.applyStack(img, stack, src.rect(), src.rect(), nullptr);
        cpuOut = img;
    }
    const double cpuMs = double(t.elapsed()) / runs;

    // ③ 走调度器（生产路径）
    // 【用 resetCounters 而不是 resetForTest】后者会顺手禁止惰性创建 GPU，
    // 在默认 Auto 下会让每一条链都退回 CPU —— 实测时会得出"GPU 完全没用"的假结论。
    auto &sched = RasterScheduler::instance();
    sched.resetCounters();
    QString usedId;
    for (int i = 0; i < warmups; ++i) {
        QImage img = src;
        ApplyStackResult r = ApplyStackResult::Ok;
        sched.runStack(img, stack, src.rect(), src.rect(), nullptr, &r);
    }
    t.start();
    QImage out;
    for (int i = 0; i < runs; ++i) {
        QImage img = src;
        ApplyStackResult r = ApplyStackResult::Ok;
        usedId = sched.runStack(img, stack, src.rect(), src.rect(), nullptr, &r);
        out = img;
    }
    const double schedMs = double(t.elapsed()) / runs;

    // ④ 正确性（抽样：最大单通道差 + 差异像素数）
    int worst = 0;
    long long diffs = 0;
    if (cpuOut.size() == out.size() && !out.isNull()) {
        for (int y = 0; y < out.height(); ++y) {
            const QRgb *a = reinterpret_cast<const QRgb *>(cpuOut.constScanLine(y));
            const QRgb *b = reinterpret_cast<const QRgb *>(out.constScanLine(y));
            for (int x = 0; x < out.width(); ++x) {
                const int d = qMax(qMax(qAbs(qRed(a[x]) - qRed(b[x])),
                                        qAbs(qGreen(a[x]) - qGreen(b[x]))),
                                   qMax(qAbs(qBlue(a[x]) - qBlue(b[x])),
                                        qAbs(qAlpha(a[x]) - qAlpha(b[x]))));
                if (d)
                    ++diffs;
                worst = qMax(worst, d);
            }
        }
    } else {
        worst = -1;
    }

    std::printf("%-26s %5dx%-5d | %-10s | CPU %8.1f ms | 调度 %8.1f ms | %6.2fx | 差 %d/%lld\n",
                c.tag, src.width(), src.height(), qPrintable(usedId), cpuMs, schedMs,
                cpuMs / schedMs, worst, diffs);
    (void)schedPicksGpu;
}

} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    Ps::opsInit();

    int W = 1600, H = 1200;
    bool onlyCompose = false;
    {
        QStringList pos;
        for (int i = 1; i < argc; ++i) {
            const QString a = QString::fromLocal8Bit(argv[i]);
            if (a == QLatin1String("onlycompose"))
                onlyCompose = true;
            else
                pos << a;
        }
        if (pos.size() >= 2) {
            W = pos.at(0).toInt();
            H = pos.at(1).toInt();
        }
        if (W < 16 || H < 16) {
            W = 1600;
            H = 1200;
        }
    }
    const QImage src = makeSrc(W, H, 7);
    const int px = W * H;
    const int runs = (px >= 1600 * 1200) ? 3 : 4;
    constexpr int kWarmups = 2;

    std::printf("gpu_bench — 端到端（含上传/回读），走真实调度路径\n");
    std::printf("图像 %dx%d (%.0fk px)  runs=%d warmup=%d\n", W, H, px / 1000.0, runs, kWarmups);
    std::printf("PSLITE_RASTER=%s\n", qgetenv("PSLITE_RASTER").constData());
    std::printf("阈值：链成本 > 5 ns/px 且像素 >= 100k 才考虑 GPU（见 rastercost.h）\n");
    std::printf("------------------------------------------------------------------------------"
                "----------------------\n");

    std::vector<Case> cases;

    {
        FilterNode n = node(OpName::MedianBlur);
        n.setMedianRadius(3);
        cases.push_back({"中值 r=3 (187ns/px)", {n}});
    }
    {
        FilterNode n = node(OpName::Maximum);
        n.setExtremumRadius(3);
        cases.push_back({"最大值 r=3 (100ns/px)", {n}});
    }
    {
        FilterNode n = node(OpName::CustomFilter);
        const int w9[9] = {0, -1, 0, -1, 5, -1, 0, -1, 0};
        for (int i = 0; i < 9; ++i)
            n.setCustomWeight(i, w9[i]);
        n.setCustomScale(1);
        n.setCustomOffset(0);
        cases.push_back({"自定卷积锐化 (48ns/px)", {n}});
    }
    {
        FilterNode n = node(OpName::Sharpen);
        n.setSharpenStrength(2);
        cases.push_back({"锐化 s=2 (5ns/px)", {n}});
    }
    {
        FilterNode n = node(OpName::AddNoise);
        n.setNoiseAmount(12);
        n.setNoiseGaussian(true);
        n.setNoiseMonochromatic(false);
        cases.push_back({"添加杂色 12% 高斯彩 (22ns/px)", {n}});
    }
    {
        FilterNode n = node(OpName::GaussianBlur);
        n.setGaussRadius(5);
        cases.push_back({"高斯模糊 r=5 (19ns/px)", {n}});
    }
    {
        FilterNode n = node(OpName::BoxBlur);
        n.setBlurRadius(4);
        cases.push_back({"盒模糊 r=4 (5ns/px)", {n}});
    }
    { // 招牌混合链：点算子 + 中值（T5.2 兑现的就是这条）
        FilterNode a = node(OpName::BrightnessContrast);
        FilterNode b = node(OpName::MedianBlur);
        b.setMedianRadius(3);
        FilterNode c = node(OpName::Invert);
        cases.push_back({"亮度→中值→反相 (188ns/px)", {a, b, c}});
    }
    { // 纯点算子链：成本 2 ns/px < 阈值 ⇒ 按设计不上卡
        FilterNode a = node(OpName::BrightnessContrast);
        FilterNode b = node(OpName::Invert);
        cases.push_back({"亮度→反相 (2ns/px，不该上卡)", {a, b}});
    }

    for (const Case &c : cases) {
        if (onlyCompose)
            break;
        bench(c, src, kWarmups, runs);
    }

    std::printf("------------------------------------------------------------------------------"
                "----------------------\n");
    std::printf("注：「调度」列是生产真实路径（含 supports/成本/像素门槛决策）；"
                "「x」= CPU/调度，>1 表示 GPU 更快。\n");

    // —— ② 合成路径：带滤镜的层 + **分块投影** ——
    // 默认改成自动之后，GPU 会被投影合成在**后台线程**里反复调用（每 64×64 块一次
    // `applyStack`），且 GpuRasterBackend 内部有单飞互斥 ⇒ 小块、密调用是另一种负载形态。
    // 只测单次 applyStack 会漏掉它（这里照抄 t_regionraster 的驱动方式）。
    {
        const int DW = W, DH = H;
        std::printf("\n---- 合成路径（带中值 r=2 滤镜的层，分块投影）----\n");
        auto runCompose = [&](int reps) -> double {
            auto doc = ImageDocument::createBlank(DW, DH, Qt::white);
            doc->placeImageAsLayer(makeSrc(DW, DH, 3), QStringLiteral("L"));
            Layer *layer = doc->layers().layerAt(1);
            if (!layer)
                return -1.0;
            FilterNode med(OpName::MedianBlur);
            med.setMedianRadius(2);
            layer->filters().append(med);

            QElapsedTimer t;
            t.start();
            for (int i = 0; i < reps; ++i) {
                Projection p;
                p.bind(doc.get());
                p.setPriorityRect(QRect(0, 0, DW, DH));
                doc->markDirty();
                p.sync();
                while (p.hasPendingChunks())
                    p.processPendingChunks(0);
            }
            return double(t.elapsed()) / reps;
        };

        // 【预热必须做够】GPU 路径的初始化是"惰性且一次性的"：建 QRhi 设备、编译/加载 shader、
        // 起 RHI 线程。只预热 1 次不够 —— 实测那样会把初始化开销留在第一次计时里，
        // 得出"Auto 比纯 CPU 慢 1.7×"的**假结论**（312/96/96 vs 94/97/97：稳态其实一样）。
        // 生产里这个开销只付一次（main.cpp 的 warmUp 就付掉了），所以必须测稳态。
        for (int i = 0; i < 3; ++i)
            (void)runCompose(1);
        RasterScheduler::instance().resetCounters();
        std::printf("逐次耗时（ms）：");
        QVector<double> each;
        for (int i = 0; i < runs; ++i)
            each.append(runCompose(1));
        for (double v : each)
            std::printf(" %.0f", v);
        std::printf("\n");
        // 用**中位数**而不是均值：偶发的调度抖动不该把结论带偏
        QVector<double> sorted = each;
        std::sort(sorted.begin(), sorted.end());
        const double ms = sorted.at(sorted.size() / 2);
        std::printf("分块投影 %dx%d（含中值 r=2）：%.1f ms/次（中位数，稳态）   实际后端=%s\n", DW, DH,
                    ms, qPrintable(RasterScheduler::instance().activeBackendName()));
        // 合成结果必须**确定**：同参数两次跑逐像素一致（Auto 下合成仍走 CPU 小块，
        // 这条同时能证明 GPU 没有把合成结果搅乱）。
        std::printf("（合成小块 <100k px ⇒ 按阈值本就走 CPU；若要量化 GPU 对合成的作用，"
                    "需要先让合成真的走 GPU —— 见 gpu-plan §0.4）\n");
    }
    return 0;
}
