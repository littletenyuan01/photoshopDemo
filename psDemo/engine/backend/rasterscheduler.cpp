/**
 * rasterscheduler.cpp — 链级决策 + Failed 降级 + 熔断（gpu-plan.md §4 / §6）。
 */
#include "rasterscheduler.h"

#include "cpurasterbackend.h"
#include "domain/filterstack.h"
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

RasterScheduler::RasterMode RasterScheduler::mode() const
{
    ensureEnvCached();
    return m_mode;
}

bool RasterScheduler::shouldUseGpu(const FilterStack &stack, int pixelCount) const
{
    ensureEnvCached();
    if (m_mode == RasterMode::ForceCpu)
        return false;
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
    // ForceGpu：仍要 supports / 像素门槛，但跳过成本阈值（优先试 GPU）。
    if (m_mode != RasterMode::ForceGpu) {
        const int cost = stackCostNsPerPixel(stack);
        if (cost < 0 || cost <= kGpuChainCostThresholdNsPerPixel)
            return false;
    }
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
            if (resultOut)
                *resultOut = ApplyStackResult::Cancelled;
            return m_gpu->id();
        }
        if (gr == ApplyStackResult::Unsupported) {
            Q_ASSERT_X(false, "RasterScheduler::runStack",
                       "GPU applyStack returned Unsupported — call supports() first");
            // Release：不计熔断，落入 CPU
        } else if (gr == ApplyStackResult::Failed) {
            const int n =
                m_consecutiveFailures.fetch_add(1, std::memory_order_relaxed) + 1;
            if (n >= kCircuitBreakerLimit)
                m_gpuDisabled.store(true, std::memory_order_relaxed);
            // image 必须未改；继续 CPU
        }
    }

    const ApplyStackResult cr =
        m_cpu->applyStack(image, stack, srcRect, outRect, shouldAbort);
    if (cr == ApplyStackResult::Failed) {
        qWarning("CpuRasterBackend::applyStack returned Failed — last-resort path");
        image = QImage();
        if (resultOut)
            *resultOut = ApplyStackResult::Failed;
        return backendId;
    }
    // CPU Ok 不清零熔断（纪律 3）
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
    m_gpu = std::move(backend);
}

void RasterScheduler::resetForTest()
{
    m_gpu.reset();
    m_consecutiveFailures.store(0, std::memory_order_relaxed);
    m_gpuDisabled.store(false, std::memory_order_relaxed);
    m_envCached.store(false, std::memory_order_release);
}

} // namespace Ps
