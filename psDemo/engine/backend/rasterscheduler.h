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

    /**
     * 清掉熔断/失败计数与已探测到的后端，让下一次调用**重新探测**。
     *
     * 与 `resetForTest()` 的区别：**不禁止惰性创建** GPU。基准程序每跑一条链都想从
     * "干净且会真的去用 GPU"的状态开始；用 `resetForTest()` 会顺手把
     * `m_blockGpuAutoCreate` 置真 ⇒ 之后一律 CPU，实测时会得出"GPU 完全没用"的假结论
     *（本轮就踩过：`gpu_bench` 在默认 Auto 下每行都报 cpu）。
     */
    void resetCounters();

    /**
     * 启动时**在后台线程**探测一次 GPU（可用则把设备建好）。
     *
     * 【为什么异步】建 QRhi/D3D11 设备 + 加载编译 4 个 shader 实测约 **300 ms**（gpu-plan §0.4）；
     * 同步做就是让用户白等 300 ms 才出窗口。异步做的代价：探测完成的几百毫秒内，
     * 早期滤镜判定会先走 CPU，之后自动切到 GPU（判定结果始终正确，只是暂时慢一点）。
     * `PSLITE_RASTER=cpu` 时**连探测都不做**。
     *
     * @return 探测**发起时**实际生效的后端 id（通常是 CPU；用 `probeDone()` /
     *         `activeBackendName()` 才能知道探测结果）。探测结果确定后**没有回调** ——
     *         本工程的日志/UI 都在调用点自己再取一次，不引入回调生命周期。
     */
    QString warmUp();

    /** 探测是否已结束（结束前 `activeBackendName()` 可能仍报 CPU）。 */
    bool probeDone() const;

    /**
     * 用户显式选择的加速策略（持久化到 `QSettings`）。三态，见 `RasterScheduler::Accel`。
     *
     * 【优先级】环境变量 `PSLITE_RASTER` > 这里的用户选择 > 默认 `Auto`。
     * 环境变量最高是为了让「排查 / 门控用例 / 基准」可以无视用户设置强行规定行为；
     * 于是**设置了环境变量时，首选项里改什么都不生效**（这正是排查时要的）。
     */
    enum class Accel { Auto = 0, CpuOnly = 1, GpuOnly = 2 };

    /** 读持久化的用户选择（没设过 = Auto）。 */
    static Accel accelPreference();
    /**
     * 写持久化的用户选择。**立即生效**：清掉环境缓存并让下一次调用重新探测。
     * @return 是否**确实写进去了**。`QSettings` 的 `setValue` 失败是**静默**的
     *         （不检查 `status()` 就永远以为成功）；沙箱/受限注册表环境会走到这里，
     *         所以把结果如实返回，免得界面显示"已保存"而实际没保存。
     */
    static bool setAccelPreference(Accel a);
    /** 供首选项界面显示：当前是否有环境变量在覆盖用户选择。 */
    static bool envOverrideActive();
    /** 加速设置实际生效于哪一层（用于界面如实说明"为什么我改了没用"）。 */
    static QString accelSourceDescription();

    /** 当前实际生效的后端名（"cpu" / "gpu:qrhi" / "gpu:<注入>"），供日志与诊断用。 */
    QString activeBackendName() const;

private:
    RasterScheduler();

    /**
     * 由 `PSLITE_RASTER` 决定的模式。**默认 `Auto` = 自动**：
     *   · `Auto`（默认 / 未设）—— 探测到可用 GPU 就用它，探测不到（或无独显/无驱动）就用 CPU；
     *     仍守 `supports()`（整链支持才走 GPU）、像素门槛与**成本阈值**；
     *   · `ForceCpu`（`PSLITE_RASTER=cpu`）—— 永不碰 GPU，**连设备都不创建**；
     *   · `ForceGpu`（`PSLITE_RASTER=gpu`）—— 忽略成本阈值（便于门控与实测），仍守
     *     `supports()` 与像素门槛。
     */
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
