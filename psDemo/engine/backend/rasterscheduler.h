/**
 * rasterscheduler.h — 滤镜链后端调度（探测 / 决策 / 降级 / 熔断）。
 */
#ifndef ENGINE_BACKEND_RASTERSCHEDULER_H
#define ENGINE_BACKEND_RASTERSCHEDULER_H

#include "engine/backend/rasterbackend.h"

#include <QMutex>
#include <QString>

#include <atomic>
#include <functional>
#include <memory>

namespace Ps {

class FilterStack;

class RasterScheduler
{
public:
    static RasterScheduler &instance();

    /**
     * 按决策挑后端；GPU Failed → CPU 重算；Cancelled → 立即返回。
     * @return 实际生效的后端 id。
     */
    QString runStack(QImage &image, const FilterStack &stack, const QRect &srcRect,
                     const QRect &outRect, const std::function<bool()> &shouldAbort,
                     ApplyStackResult *resultOut = nullptr);

    /** @p pixelCount = 将上传缓冲的像素数（image 宽×高）。 */
    bool shouldUseGpu(const FilterStack &stack, int pixelCount) const;

    bool gpuDisabled() const { return m_gpuDisabled.load(std::memory_order_relaxed); }
    int consecutiveFailures() const
    {
        return m_consecutiveFailures.load(std::memory_order_relaxed);
    }

    /** 测试注入；传空指针 = 清空并禁止惰性重建（直到 resetForTest）。 */
    void setGpuBackendForTest(std::unique_ptr<RasterBackend> backend);
    void resetForTest();

private:
    RasterScheduler();

    enum class RasterMode { Auto, ForceCpu, ForceGpu };

    void ensureEnvCached() const;
    void ensureGpu() const;
    RasterMode mode() const;

    std::unique_ptr<RasterBackend> m_cpu;
    mutable std::unique_ptr<RasterBackend> m_gpu; ///< 可空；测试可注入 / 惰性 QRhi
    mutable QMutex m_gpuInitMutex;
    mutable bool m_gpuInitAttempted = false;
    mutable bool m_blockGpuAutoCreate = false; ///< 测试注入空后端后为 true

    mutable std::atomic<bool> m_envCached{false};
    mutable RasterMode m_mode = RasterMode::Auto;
    mutable bool m_log = false;

    std::atomic<int> m_consecutiveFailures{0};
    std::atomic<bool> m_gpuDisabled{false};
};

} // namespace Ps

#endif
