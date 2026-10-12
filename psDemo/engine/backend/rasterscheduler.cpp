/**
 * rasterscheduler.cpp — 链级决策 + Failed 降级 + 熔断（gpu-plan.md §4 / §6）。
 */
#include "rasterscheduler.h"

#include "cpurasterbackend.h"
#include "domain/filterstack.h"
#include "gpurasterbackend.h"
#include "rastercost.h"

#include <QDebug>
#include <QtGlobal>

namespace Ps {

namespace {

constexpr int kCircuitBreakerLimit = 3;

} // namespace

RasterScheduler &RasterScheduler::instance()
{
    static RasterScheduler s;
    return s;
}

RasterScheduler::RasterScheduler()
    : m_cpu(std::make_unique<CpuRasterBackend>())
{
}

void RasterScheduler::ensureEnvCached() const
{
    if (m_envCached.load(std::memory_order_acquire))
        return;
    const QByteArray v = qgetenv("PSLITE_RASTER").toLower();
    if (v == "cpu")
        m_mode = RasterMode::ForceCpu;
    else if (v == "gpu")
        m_mode = RasterMode::ForceGpu;
    else
        m_mode = RasterMode::Auto;
    m_log = (qgetenv("PSLITE_RASTER_LOG") == "1");
    m_envCached.store(true, std::memory_order_release);
}

void RasterScheduler::ensureGpu() const
{
    if (m_gpuInitAttempted || m_blockGpuAutoCreate)
        return;
    QMutexLocker lock(&m_gpuInitMutex);
    if (m_gpuInitAttempted || m_blockGpuAutoCreate)
        return;
    m_gpuInitAttempted = true;
    if (!m_gpu)
        m_gpu = GpuRasterBackend::tryCreate();
}

RasterScheduler::RasterMode RasterScheduler::mode() const
{
    ensureEnvCached();
    return m_mode;
}

bool RasterScheduler::shouldUseGpu(const FilterStack &stack, int pixelCount) const
{
    ensureEnvCached();
    // T1.9 / §7.2.2 方案 A：Auto = CPU；GPU 仅 PSLITE_RASTER=gpu（opt-in）
    // 钉：t_rasterbackend「Auto opt-in ⇒ 不上 GPU」
    if (m_mode == RasterMode::ForceCpu || m_mode == RasterMode::Auto)
        return false;
    ensureGpu(); // 仅 ForceGpu 才惰性创建真实后端
    if (!m_gpu || !m_gpu->isAvailable())
        return false;
    if (m_gpuDisabled.load(std::memory_order_relaxed))
        return false;
    if (!m_gpu->supports(stack))
        return false;
    if (stack.hasPositionalOp() || stack.containsOffset())
        return false;
    if (pixelCount < kMinPixelsForGpu)
        return false;
    // ForceGpu：仍守像素门槛与 supports，但跳过成本阈值（便于门控用例）
    return true;
}

QString RasterScheduler::runStack(QImage &image, const FilterStack &stack, const QRect &srcRect,
                                 const QRect &outRect,
                                 const std::function<bool()> &shouldAbort,
                                 ApplyStackResult *resultOut)
{
    ensureEnvCached();
    const int px = image.width() * image.height();
    const int cost = stackCostNsPerPixel(stack);
    const int ops = [&] {
        int n = 0;
        for (int i = 0; i < stack.count(); ++i)
            if (stack.at(i).isEnabled())
                ++n;
        return n;
    }();

    const bool tryGpu = shouldUseGpu(stack, px);
    QString backendId = m_cpu->id();

    if (tryGpu) {
        const ApplyStackResult gr =
            m_gpu->applyStack(image, stack, srcRect, outRect, shouldAbort);
        if (gr == ApplyStackResult::Ok) {
            // 只有 GPU 自己 Ok 才清零熔断（§3.4 纪律 3）
            // 钉：t_rasterbackend「随后 GPU Ok → 计数清零」
            m_consecutiveFailures.store(0, std::memory_order_relaxed);
            if (m_log) {
                qDebug().nospace() << "[raster] ops=" << ops << " cost=" << cost
                                   << " ns/px px=" << px << " backend=" << m_gpu->id();
            }
            if (resultOut)
                *resultOut = ApplyStackResult::Ok;
            return m_gpu->id();
        }
        if (gr == ApplyStackResult::Cancelled) {
            // Cancelled：不重试、不计熔断（§3.4）
            // 钉：t_rasterbackend「Cancelled 不计熔断」
            if (resultOut)
                *resultOut = ApplyStackResult::Cancelled;
            return m_gpu->id();
        }
        if (gr == ApplyStackResult::Unsupported) {
            Q_ASSERT_X(false, "RasterScheduler::runStack",
                       "GPU applyStack returned Unsupported — call supports() first");
        } else if (gr == ApplyStackResult::Failed) {
            // Failed：先计数再同图 CPU 重算；连续 3 次熔断（§6.4）
            // 钉：t_rasterbackend「Failed×3 → 熔断」
            const int n =
                m_consecutiveFailures.fetch_add(1, std::memory_order_relaxed) + 1;
            if (n >= kCircuitBreakerLimit)
                m_gpuDisabled.store(true, std::memory_order_relaxed);
        }
    }

    // CPU 降级补算成功 → 不清零熔断（纪律 3）
    // 钉：t_rasterbackend「CPU 补算成功不得清零 → 仍熔断」
    const ApplyStackResult cr =
        m_cpu->applyStack(image, stack, srcRect, outRect, shouldAbort);
    if (cr == ApplyStackResult::Failed) {
        // CPU 是最后防线：不回退（会死循环），空图 + warning（§3.4 纪律 2）
        qWarning("CpuRasterBackend::applyStack returned Failed — last-resort path");
        image = QImage();
        if (resultOut)
            *resultOut = ApplyStackResult::Failed;
        return backendId;
    }
    if (m_log) {
        qDebug().nospace() << "[raster] ops=" << ops << " cost=" << cost << " ns/px px=" << px
                           << " backend=cpu";
    }
    if (resultOut)
        *resultOut = cr;
    return backendId;
}

void RasterScheduler::setGpuBackendForTest(std::unique_ptr<RasterBackend> backend)
{
    QMutexLocker lock(&m_gpuInitMutex);
    m_gpu = std::move(backend);
    m_gpuInitAttempted = true;
    m_blockGpuAutoCreate = !m_gpu; // 注入空 = 禁止再惰性创建
}

void RasterScheduler::resetForTest()
{
    QMutexLocker lock(&m_gpuInitMutex);
    m_gpu.reset();
    m_gpuInitAttempted = false;
    m_blockGpuAutoCreate = false;
    m_consecutiveFailures.store(0, std::memory_order_relaxed);
    m_gpuDisabled.store(false, std::memory_order_relaxed);
    m_envCached.store(false, std::memory_order_release);
}

} // namespace Ps
