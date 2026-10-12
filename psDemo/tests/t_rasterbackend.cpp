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
    void setFailThenOkCount(int n) { m_failRemaining = n; }

private:
    Behavior m_behavior;
    bool m_available = true;
    bool m_supportsAllCostly = true;
    int m_failRemaining = 1;
    std::atomic<int> m_calls{0};
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

    // 1) 默认 auto + 无 GPU
    {
        EnvGuard env("PSLITE_RASTER", "auto");
        sched.resetForTest();
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
                QStringLiteral("默认无 GPU → backend=cpu"));
        r.check(PsTests::diffCount(got, ref) == 0, QStringLiteral("默认路径 == 直接 CPU"));
    }

    // 2) PSLITE_RASTER=cpu ⇒ GPU 调用次数 0
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

    // 3) 总是 Failed → 结果仍正确；3 次后不再调用
    {
        EnvGuard env("PSLITE_RASTER", "auto");
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

    // 4) 计数方向
    {
        EnvGuard env("PSLITE_RASTER", "auto");
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
        EnvGuard env("PSLITE_RASTER", "auto");
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
        EnvGuard env("PSLITE_RASTER", "auto");
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

    // 5) shouldUseGpu 边界
    {
        EnvGuard env("PSLITE_RASTER", "auto");
        auto fake = std::make_unique<FakeGpuBackend>(FakeGpuBackend::Behavior::AlwaysOk);
        fake->setSupportsAllCostly(true);
        sched.setGpuBackendForTest(std::move(fake));

        FilterStack pts = stackOf(OpName::BrightnessContrast, OpName::Invert);
        r.check(!sched.shouldUseGpu(pts, 200000), QStringLiteral("纯点算子链 ⇒ false"));

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
            if (opCostNsPerPixel(static_cast<OpName>(i)) < 0) {
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
        EnvGuard env("PSLITE_RASTER", "auto");
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
