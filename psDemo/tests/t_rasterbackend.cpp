/**
 * t_rasterbackend.cpp — 后端调度：降级 / 熔断 / supports / 取消（gpu-plan.md §6.6）。
 */
#include "domain/filternode.h"
#include "domain/filterstack.h"
#include "engine/backend/cpurasterbackend.h"
#include "engine/backend/rasterbackend.h"
#include "engine/backend/rastercost.h"
#include "engine/backend/rasterscheduler.h"
#include "engine/op/opname.h"
#include "testutil.h"

#include <QImage>
#include <QtGlobal>

#include <atomic>
#include <memory>

using namespace Ps;
using PsTests::Reporter;

namespace {

/** 可配置假 GPU：supports / 返回值序列；Failed 时不改 image。 */
class FakeGpuBackend final : public RasterBackend
{
public:
    enum class Behavior { AlwaysOk, AlwaysFail, FailThenOk, AlwaysCancel };

    explicit FakeGpuBackend(Behavior b = Behavior::AlwaysFail)
        : m_behavior(b)
    {
    }

    QString id() const override { return QStringLiteral("gpu:fake"); }
    bool isAvailable() const override { return m_available; }
    void setAvailable(bool on) { m_available = on; }

    bool supports(const FilterStack &stack) const override
    {
        // 【为什么要数它】判定顺序是本项目的性能前提之一：投影合成按 64×64 块反复问
        // （1600×1200 ≈ 1900 次，每块约 4.6k px）。若把像素门槛放到 supports() 之后，
        // 每个小块都要白问后端一次；设备未建时更会被一个小块触发建整个设备（约 300 ms）。
        // 只数 applyStack 是看不出这个问题的 —— 那时根本还没提交。
        ++m_probes;
        if (!m_supportsAllCostly) {
            // 仅「纯中值」—— 贴近 P1，便于测 supports==false 的高成本链
            for (int i = 0; i < stack.count(); ++i) {
                const FilterNode &n = stack.at(i);
                if (!n.isEnabled())
                    continue;
                if (n.op() != OpName::MedianBlur)
                    return false;
            }
            return stack.hasEnabled();
        }
        if (stack.hasPositionalOp() || stack.containsOffset())
            return false;
        return stackCostNsPerPixel(stack) > kGpuChainCostThresholdNsPerPixel;
    }

    /** true = 成本过线且非位置/位移即支持（P0 §6.6）；false = 仅中值。 */
    void setSupportsAllCostly(bool on) { m_supportsAllCostly = on; }

    ApplyStackResult applyStack(QImage &image, const FilterStack &stack, const QRect &srcRect,
                                const QRect &outRect,
                                const std::function<bool()> &shouldAbort) override
    {
        ++m_calls;
        if (shouldAbort && shouldAbort()) {
            image = QImage();
            return ApplyStackResult::Cancelled;
        }
        switch (m_behavior) {
        case Behavior::AlwaysOk: {
            // 用 CPU 算出正确结果再「提交」，模拟最后一步 assign
            CpuRasterBackend cpu;
            return cpu.applyStack(image, stack, srcRect, outRect, shouldAbort);
        }
        case Behavior::AlwaysFail:
            // 无副作用：不改 image
            return ApplyStackResult::Failed;
        case Behavior::FailThenOk:
            if (m_failRemaining > 0) {
                --m_failRemaining;
                return ApplyStackResult::Failed;
            }
            {
                CpuRasterBackend cpu;
                return cpu.applyStack(image, stack, srcRect, outRect, shouldAbort);
            }
        case Behavior::AlwaysCancel:
            image = QImage();
            return ApplyStackResult::Cancelled;
        }
        return ApplyStackResult::Failed;
    }

    int callCount() const { return m_calls.load(); }
    /** supports() 被问了几次（**不含** applyStack）。用于钉「小块必须短路、不许问后端」。 */
    int probeCount() const { return m_probes.load(); }
    void setFailThenOkCount(int n) { m_failRemaining = n; }

private:
    Behavior m_behavior;
    bool m_available = true;
    bool m_supportsAllCostly = true;
    int m_failRemaining = 1;
    std::atomic<int> m_calls{0};
    mutable std::atomic<int> m_probes{0};
};

FilterStack stackOf(OpName a, OpName b = OpName::Count)
{
    FilterStack s;
    s.append(FilterNode(a));
    if (b != OpName::Count)
        s.append(FilterNode(b));
    return s;
}

QImage cpuApply(const QImage &src, const FilterStack &stack)
{
    QImage img = src;
    if (img.format() != QImage::Format_ARGB32_Premultiplied)
        img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    CpuRasterBackend cpu;
    ApplyStackResult r =
        cpu.applyStack(img, stack, src.rect(), src.rect(), nullptr);
    if (r != ApplyStackResult::Ok)
        return QImage();
    return img;
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

int tRasterBackend()
{
    Reporter r;
    auto &sched = RasterScheduler::instance();

    // 1) 默认 auto + **无可用 GPU**（显式注入空后端，禁止惰性 QRhi）⇒ 静默走 CPU
    {
        EnvGuard env("PSLITE_RASTER", "auto");
        sched.setGpuBackendForTest(nullptr);
        const QImage src = PsTests::makeLayerImage(64, 48, 3, 255);
        FilterStack stack = stackOf(OpName::MedianBlur);
        stack.at(0).setMedianRadius(2);
        ApplyStackResult outR = ApplyStackResult::Ok;
        QImage got = src;
        got = got.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        const QString id =
            sched.runStack(got, stack, src.rect(), src.rect(), nullptr, &outR);
        const QImage ref = cpuApply(src, stack);
        r.check(id == QLatin1String("cpu") && outR == ApplyStackResult::Ok,
                QStringLiteral("默认 Auto 但无可用 GPU → backend=cpu"));
        r.check(PsTests::diffCount(got, ref) == 0, QStringLiteral("默认路径 == 直接 CPU"));
    }

    // 2) PSLITE_RASTER=cpu ⇒ GPU 调用次数 0（连设备都不该建）
    {
        EnvGuard env("PSLITE_RASTER", "cpu");
        auto fake = std::make_unique<FakeGpuBackend>(FakeGpuBackend::Behavior::AlwaysOk);
        FakeGpuBackend *raw = fake.get();
        sched.setGpuBackendForTest(std::move(fake));
        const QImage src = PsTests::makeLayerImage(400, 300, 5, 255); // 120k px
        FilterStack stack = stackOf(OpName::MedianBlur);
        stack.at(0).setMedianRadius(1);
        QImage got = src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        sched.runStack(got, stack, src.rect(), src.rect(), nullptr, nullptr);
        r.check(raw->callCount() == 0, QStringLiteral("PSLITE_RASTER=cpu → GPU 调用 0"));
    }

    // 2b) ★ Auto = **自动用 GPU**（默认行为）：注入可用假 GPU + 足够像素 + 成本过线 ⇒ 上卡。
    // 这条钉的是产品语义的反转：旧行为是"Auto 永不调用 GPU"（opt-in），
    // 现在默认就该用上独显（gpu-plan §0.2 实测重核 12～36×）。
    {
        EnvGuard env("PSLITE_RASTER", "auto");
        auto fake = std::make_unique<FakeGpuBackend>(FakeGpuBackend::Behavior::AlwaysOk);
        FakeGpuBackend *raw = fake.get();
        sched.setGpuBackendForTest(std::move(fake));
        FilterStack med = stackOf(OpName::MedianBlur); // 187 ns/px > 阈值 5
        r.check(sched.shouldUseGpu(med, 200000), QStringLiteral("Auto + 有 GPU + 成本过线 ⇒ true"));
        const QImage src = PsTests::makeLayerImage(400, 300, 4, 255);
        QImage got = src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        const QString id = sched.runStack(got, med, src.rect(), src.rect(), nullptr, nullptr);
        r.check(id.startsWith(QLatin1String("gpu")) && raw->callCount() == 1,
                QStringLiteral("Auto ⇒ 自动上 GPU（backend=%1）").arg(id));

        // 但 Auto **仍守** 成本阈值与像素门槛（ForceGpu 才跳过）
        FilterStack pts = stackOf(OpName::BrightnessContrast, OpName::Invert);
        r.check(!sched.shouldUseGpu(pts, 200000),
                QStringLiteral("Auto：纯点算子链（成本 0）⇒ false"));
        r.check(!sched.shouldUseGpu(med, 1000), QStringLiteral("Auto：像素过少 ⇒ false"));
    }

    // 3) 总是 Failed → 结果仍正确；3 次后不再调用（需 opt-in gpu）
    {
        EnvGuard env("PSLITE_RASTER", "gpu");
        auto fake = std::make_unique<FakeGpuBackend>(FakeGpuBackend::Behavior::AlwaysFail);
        FakeGpuBackend *raw = fake.get();
        sched.setGpuBackendForTest(std::move(fake));
        const QImage src = PsTests::makeLayerImage(400, 300, 7, 255);
        FilterStack stack = stackOf(OpName::MedianBlur);
        stack.at(0).setMedianRadius(1);
        const QImage ref = cpuApply(src, stack);

        for (int i = 0; i < 3; ++i) {
            QImage got = src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            sched.runStack(got, stack, src.rect(), src.rect(), nullptr, nullptr);
            r.check(PsTests::diffCount(got, ref) == 0,
                    QStringLiteral("Failed#%1 后 CPU 重算正确").arg(i + 1));
        }
        r.check(raw->callCount() == 3, QStringLiteral("Failed×3 → GPU 恰调用 3 次"));
        r.check(sched.gpuDisabled(), QStringLiteral("Failed×3 → 熔断"));

        QImage got = src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        sched.runStack(got, stack, src.rect(), src.rect(), nullptr, nullptr);
        r.check(raw->callCount() == 3, QStringLiteral("熔断后不再调用 GPU"));
        r.check(PsTests::diffCount(got, ref) == 0, QStringLiteral("熔断后仍走 CPU 正确"));
    }

    // 3c) ★ 启动探测：`warmUp()` 必须按模式返回真实生效的后端
    // 钉的是「软件起来自己检测」这条产品行为：
    //   · Auto 且探测到可用后端 ⇒ 报 GPU（并把设备建好，别等首次绘制才建）
    //   · `cpu` ⇒ 报 CPU，且**不碰**注入的 GPU（连探测都不做）
    {
        EnvGuard env("PSLITE_RASTER", "auto");
        auto fake = std::make_unique<FakeGpuBackend>(FakeGpuBackend::Behavior::AlwaysOk);
        FakeGpuBackend *raw = fake.get();
        sched.setGpuBackendForTest(std::move(fake));
        const QString id = sched.warmUp();
        r.check(id == QLatin1String("gpu:fake"),
                QStringLiteral("warmUp（Auto + 有后端）⇒ 报 GPU（实际 %1）").arg(id));
        r.check(sched.activeBackendName() == QLatin1String("gpu:fake"),
                QStringLiteral("activeBackendName 与 warmUp 一致"));
        r.check(raw->callCount() == 0, QStringLiteral("warmUp 只探测建后端，不跑任何链"));
    }
    {
        EnvGuard env("PSLITE_RASTER", "cpu");
        auto fake = std::make_unique<FakeGpuBackend>(FakeGpuBackend::Behavior::AlwaysOk);
        FakeGpuBackend *raw = fake.get();
        sched.setGpuBackendForTest(std::move(fake));
        const QString id = sched.warmUp();
        r.check(id == QLatin1String("cpu"),
                QStringLiteral("warmUp（PSLITE_RASTER=cpu）⇒ 报 CPU（实际 %1）").arg(id));
        r.check(raw->callCount() == 0, QStringLiteral("cpu 模式不调用 GPU"));
    }

    // 3d) ★ 小图/低收益链必须**短路**，连 supports() 都不该问
    // 钉的是合成路径的性能前提：投影按 64×64 块反复问（1600×1200 ≈ 1900 次，每块约 4.6k px），
    // 这些块永远低于像素门槛。若判定顺序把它放到 ensureGpu()/supports() 之后，
    // 每个小块都要付一次后端查询；设备未建时更会**被一个小块触发建整个设备**（约 300 ms）。
    {
        EnvGuard env("PSLITE_RASTER", "auto");
        auto fake = std::make_unique<FakeGpuBackend>(FakeGpuBackend::Behavior::AlwaysOk);
        FakeGpuBackend *raw = fake.get();
        sched.setGpuBackendForTest(std::move(fake));

        FilterStack med = stackOf(OpName::MedianBlur); // 成本 187 > 5，仅像素不够
        r.check(!sched.shouldUseGpu(med, 4624),
                QStringLiteral("小块（4.6k px，合成块的量级）⇒ false"));
        r.check(raw->probeCount() == 0,
                QStringLiteral("小块判定**没有触碰后端**（supports 调用 0 次，实际 %1）")
                    .arg(raw->probeCount()));

        // 对照：同样这条链、像素够了 ⇒ 必须真的去问后端并上卡
        r.check(sched.shouldUseGpu(med, 200000), QStringLiteral("同链 + 足够像素 ⇒ true"));
        r.check(raw->probeCount() >= 1, QStringLiteral("够像素时才去问后端（supports 被调用）"));
    }

    // 3e) ★ 首选项（QSettings）持久化 + 优先级：环境变量 > 用户选择 > 默认 Auto
    // 这是「首选项 → 性能」的落点。必须能往返、且**不能污染开发者机器上的真实设置**，
    // 所以这里先存旧值、结束时恢复。
    {
        const RasterScheduler::Accel savedPref = RasterScheduler::accelPreference();
        // 先探一次写入能力：受限/沙箱环境里 QSettings 写注册表会静默失败，
        // 那时**跳过**往返断言并如实说明，而不是报一条环境导致的假失败。
        const bool writable = RasterScheduler::setAccelPreference(RasterScheduler::Accel::CpuOnly);
        if (!writable) {
            r.info(QStringLiteral("本机 QSettings 不可写 ⇒ 跳过首选项往返断言"
                                  "（不报假失败；写失败已由 setAccelPreference 返回 false 暴露）"));
        } else {
            r.check(RasterScheduler::accelPreference() == RasterScheduler::Accel::CpuOnly,
                    QStringLiteral("首选项往返：写入「仅 CPU」后读回一致"));
            RasterScheduler::setAccelPreference(RasterScheduler::Accel::GpuOnly);
            r.check(RasterScheduler::accelPreference() == RasterScheduler::Accel::GpuOnly,
                    QStringLiteral("首选项往返：写入「强制 GPU」后读回一致"));
        }

        // 优先级（不依赖写盘能力：直接改内存里的偏好不可行，故仅在可写时验）
        if (writable) {
            // 环境变量未设 ⇒ 用首选项（此处首选项 = 强制 GPU）
            {
                EnvGuard env("PSLITE_RASTER", nullptr);
                sched.resetForTest(); // 清环境缓存，让 ensureEnvCached 重新按新规则解析
                auto fake = std::make_unique<FakeGpuBackend>(FakeGpuBackend::Behavior::AlwaysOk);
                sched.setGpuBackendForTest(std::move(fake));
                FilterStack med = stackOf(OpName::MedianBlur);
                r.check(sched.shouldUseGpu(med, 200000),
                        QStringLiteral("无环境变量 ⇒ 按首选项「强制 GPU」上卡"));
                r.check(!RasterScheduler::envOverrideActive(),
                        QStringLiteral("无环境变量 ⇒ envOverrideActive() == false"));
            }

            // 环境变量 = cpu ⇒ **压过**首选项的「强制 GPU」
            {
                EnvGuard env("PSLITE_RASTER", "cpu");
                sched.resetForTest();
                auto fake = std::make_unique<FakeGpuBackend>(FakeGpuBackend::Behavior::AlwaysOk);
                FakeGpuBackend *raw = fake.get();
                sched.setGpuBackendForTest(std::move(fake));
                FilterStack med = stackOf(OpName::MedianBlur);
                r.check(!sched.shouldUseGpu(med, 200000),
                        QStringLiteral("环境变量 cpu **压过**首选项的强制 GPU"));
                r.check(raw->callCount() == 0,
                        QStringLiteral("被环境变量压过时 GPU 一次都没被调用"));
                r.check(RasterScheduler::envOverrideActive(),
                        QStringLiteral("有环境变量 ⇒ envOverrideActive() == true（界面据此置灰）"));
            }
        }

        // 恢复现场：别把开发者的真实首选项改掉
        RasterScheduler::setAccelPreference(savedPref);
    }

    // 4) 计数方向
    {
        EnvGuard env("PSLITE_RASTER", "gpu");
        auto fake = std::make_unique<FakeGpuBackend>(FakeGpuBackend::Behavior::FailThenOk);
        fake->setFailThenOkCount(1);
        FakeGpuBackend *raw = fake.get();
        sched.setGpuBackendForTest(std::move(fake));
        const QImage src = PsTests::makeLayerImage(400, 300, 9, 255);
        FilterStack stack = stackOf(OpName::MedianBlur);
        stack.at(0).setMedianRadius(1);

        QImage got = src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        sched.runStack(got, stack, src.rect(), src.rect(), nullptr, nullptr);
        r.check(sched.consecutiveFailures() == 1, QStringLiteral("Failed 一次 → 计数 1"));

        got = src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        sched.runStack(got, stack, src.rect(), src.rect(), nullptr, nullptr);
        r.check(sched.consecutiveFailures() == 0 && raw->callCount() == 2,
                QStringLiteral("随后 GPU Ok → 计数清零"));
    }
    {
        // GPU 一直 Failed、CPU 每次补算成功 ⇒ 仍累积到 3 并熔断
        EnvGuard env("PSLITE_RASTER", "gpu");
        auto fake = std::make_unique<FakeGpuBackend>(FakeGpuBackend::Behavior::AlwaysFail);
        sched.setGpuBackendForTest(std::move(fake));
        const QImage src = PsTests::makeLayerImage(400, 300, 11, 255);
        FilterStack stack = stackOf(OpName::MedianBlur);
        stack.at(0).setMedianRadius(1);
        for (int i = 0; i < 3; ++i) {
            QImage got = src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            sched.runStack(got, stack, src.rect(), src.rect(), nullptr, nullptr);
        }
        r.check(sched.gpuDisabled() && sched.consecutiveFailures() >= 3,
                QStringLiteral("CPU 补算成功不得清零 → 仍熔断"));
    }
    {
        EnvGuard env("PSLITE_RASTER", "gpu");
        auto fake = std::make_unique<FakeGpuBackend>(FakeGpuBackend::Behavior::AlwaysCancel);
        sched.setGpuBackendForTest(std::move(fake));
        const QImage src = PsTests::makeLayerImage(400, 300, 13, 255);
        FilterStack stack = stackOf(OpName::MedianBlur);
        ApplyStackResult outR = ApplyStackResult::Ok;
        QImage got = src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        sched.runStack(got, stack, src.rect(), src.rect(), nullptr, &outR);
        r.check(outR == ApplyStackResult::Cancelled && got.isNull(),
                QStringLiteral("Cancelled → 空图"));
        r.check(sched.consecutiveFailures() == 0, QStringLiteral("Cancelled 不计熔断"));
    }

    // 5) shouldUseGpu 边界（ForceGpu；真后端 supports 仅中值，假后端可放开）
    {
        EnvGuard env("PSLITE_RASTER", "gpu");
        auto fake = std::make_unique<FakeGpuBackend>(FakeGpuBackend::Behavior::AlwaysOk);
        fake->setSupportsAllCostly(true);
        sched.setGpuBackendForTest(std::move(fake));

        FilterStack pts = stackOf(OpName::BrightnessContrast, OpName::Invert);
        // ForceGpu 跳过成本阈值，但仍问 supports；假后端 supports 高成本链 → 点算子成本低仍 true
        // 点算子：假后端 supportsAllCostly 看成本，点算子不过线 → false
        r.check(!sched.shouldUseGpu(pts, 200000), QStringLiteral("纯点算子链 ⇒ false"));

        // 点算子**不计入链成本**：这张表的语义是"GPU 能省下多少 CPU 工作量"，
        // 而点算子在两条路径上都是 CPU 就地求值（T5.2），省不掉 ⇒ 不该被累加。
        // 只钉 `stackCostNsPerPixel` 本身：在 ForceGpu 下 `shouldUseGpu` **按设计跳过**
        // 成本阈值（见 rasterscheduler.cpp 末尾），拿它来验成本是测不到东西的。
        {
            FilterStack manyPts;
            for (int i = 0; i < 6; ++i)
                manyPts.append(FilterNode(i % 2 ? OpName::Invert : OpName::BrightnessContrast));
            r.check(stackCostNsPerPixel(manyPts) == 0,
                    QStringLiteral("一串点算子的链成本 == 0（不计入，实际 %1）")
                        .arg(stackCostNsPerPixel(manyPts)));
            // 点算子 + 中值：成本应等于**中值自己的** 187，点算子一分不加
            FilterStack mixed = stackOf(OpName::BrightnessContrast, OpName::MedianBlur);
            r.check(stackCostNsPerPixel(mixed) == stackCostNsPerPixel(stackOf(OpName::MedianBlur)),
                    QStringLiteral("点算子+中值的链成本 == 仅中值（实际 %1）")
                        .arg(stackCostNsPerPixel(mixed)));
        }

        FilterStack med = stackOf(OpName::MedianBlur);
        r.check(sched.shouldUseGpu(med, 200000), QStringLiteral("仅中值 + 足够像素 ⇒ true"));
        r.check(!sched.shouldUseGpu(med, 1000), QStringLiteral("像素过少 ⇒ false"));

        FilterStack medBright = stackOf(OpName::MedianBlur, OpName::BrightnessContrast);
        r.check(sched.shouldUseGpu(medBright, 200000),
                QStringLiteral("中值+亮度（假后端 supports）⇒ true"));

        FilterStack noise = stackOf(OpName::AddNoise);
        r.check(!sched.shouldUseGpu(noise, 200000), QStringLiteral("AddNoise ⇒ false"));
        FilterStack rip = stackOf(OpName::Ripple);
        r.check(!sched.shouldUseGpu(rip, 200000), QStringLiteral("Ripple ⇒ false"));
        FilterStack off = stackOf(OpName::Offset);
        r.check(!sched.shouldUseGpu(off, 200000), QStringLiteral("Offset ⇒ false"));

        // supports==false 的高成本链（仅蒙尘；假后端改为仅中值）
        sched.resetForTest();
        auto fake2 = std::make_unique<FakeGpuBackend>(FakeGpuBackend::Behavior::AlwaysFail);
        fake2->setSupportsAllCostly(false);
        FakeGpuBackend *raw2 = fake2.get();
        sched.setGpuBackendForTest(std::move(fake2));
        FilterStack dust = stackOf(OpName::DustAndScratches);
        r.check(!sched.shouldUseGpu(dust, 200000), QStringLiteral("蒙尘 supports=false ⇒ false"));
        QImage got = PsTests::makeLayerImage(400, 300, 1, 255);
        got = got.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        sched.runStack(got, dust, got.rect(), got.rect(), nullptr, nullptr);
        r.check(raw2->callCount() == 0 && sched.consecutiveFailures() == 0,
                QStringLiteral("supports=false 不调用、不计熔断"));

        // 熔断后
        sched.resetForTest();
        auto fake3 = std::make_unique<FakeGpuBackend>(FakeGpuBackend::Behavior::AlwaysFail);
        sched.setGpuBackendForTest(std::move(fake3));
        for (int i = 0; i < 3; ++i) {
            QImage g = PsTests::makeLayerImage(400, 300, 2, 255)
                           .convertToFormat(QImage::Format_ARGB32_Premultiplied);
            FilterStack m = stackOf(OpName::MedianBlur);
            sched.runStack(g, m, g.rect(), g.rect(), nullptr, nullptr);
        }
        r.check(!sched.shouldUseGpu(med, 200000), QStringLiteral("熔断后 shouldUseGpu ⇒ false"));
    }

    // 6) 取消：FilterStack::apply 返回空图
    {
        EnvGuard env("PSLITE_RASTER", "auto");
        sched.resetForTest();
        FilterStack stack = stackOf(OpName::MedianBlur);
        stack.at(0).setMedianRadius(1);
        const QImage src = PsTests::makeLayerImage(32, 32, 4, 255);
        const QImage got = stack.apply(src, []() { return true; });
        r.check(got.isNull(), QStringLiteral("shouldAbort 立即 true → 空图"));
        r.check(sched.consecutiveFailures() == 0, QStringLiteral("取消不动熔断计数"));
    }

    // 7) 成本表覆盖
    {
        bool all = true;
        for (int i = 0; i < int(OpName::Count); ++i) {
            if (opCostNsPerPixel(static_cast<OpName>(i)) == OpCostNsPx::UNREGISTERED) {
                all = false;
                r.check(false, QStringLiteral("成本表缺 OpName=%1").arg(i));
            }
        }
        if (all)
            r.check(true, QStringLiteral("成本表覆盖全部 OpName < Count"));
    }

    // 8) 带几何重载
    {
        EnvGuard env("PSLITE_RASTER", "auto");
        sched.resetForTest();
        const QImage src = PsTests::makeLayerImage(80, 60, 6, 255);
        FilterStack stack = stackOf(OpName::BoxBlur);
        stack.at(0).setBlurRadius(2);
        const QRect out(10, 10, 40, 30);
        const QRect in = stack.requiredInputRect(out);
        QImage pad = QImage(in.size(), QImage::Format_ARGB32_Premultiplied);
        pad.fill(Qt::transparent);
        for (int y = 0; y < src.height(); ++y)
            for (int x = 0; x < src.width(); ++x)
                pad.setPixel(x, y, src.pixel(x, y));

        const QImage viaStack = stack.apply(pad, in, out, nullptr);
        CpuRasterBackend cpu;
        QImage viaCpu = pad;
        cpu.applyStack(viaCpu, stack, in, out, nullptr);
        r.check(PsTests::diffCount(viaStack, viaCpu) == 0,
                QStringLiteral("带几何 apply 走后端 == CpuRasterBackend"));
    }

    // 9) Failed 无副作用（最终结果正确已在 3 覆盖；再钉「失败瞬间 image 未改」）
    {
        EnvGuard env("PSLITE_RASTER", "gpu");
        class PeekFail final : public RasterBackend
        {
        public:
            QString id() const override { return QStringLiteral("gpu:peek"); }
            bool isAvailable() const override { return true; }
            bool supports(const FilterStack &s) const override
            {
                return s.hasEnabled() && !s.hasPositionalOp() && !s.containsOffset()
                    && stackCostNsPerPixel(s) > kGpuChainCostThresholdNsPerPixel;
            }
            ApplyStackResult applyStack(QImage &image, const FilterStack &, const QRect &,
                                        const QRect &,
                                        const std::function<bool()> &) override
            {
                before = image.copy();
                // 故意不改 image
                return ApplyStackResult::Failed;
            }
            QImage before;
        };
        auto peek = std::make_unique<PeekFail>();
        PeekFail *raw = peek.get();
        sched.setGpuBackendForTest(std::move(peek));
        const QImage src = PsTests::makeLayerImage(400, 300, 8, 255);
        FilterStack stack = stackOf(OpName::MedianBlur);
        QImage got = src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        const QImage snapshot = got.copy();
        sched.runStack(got, stack, src.rect(), src.rect(), nullptr, nullptr);
        r.check(PsTests::diffCount(raw->before, snapshot) == 0,
                QStringLiteral("Failed 瞬间 image 与调用前一致（无副作用）"));
    }

    sched.resetForTest();
    return r.fails;
}
