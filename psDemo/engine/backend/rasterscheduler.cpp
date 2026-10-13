/**
 * rasterscheduler.cpp — 链级决策 + Failed 降级 + 熔断（gpu-plan.md §4 / §6）。
 */
#include "rasterscheduler.h"

#include "cpurasterbackend.h"
#include "domain/filterstack.h"
#include "gpurasterbackend.h"
#include "rastercost.h"

#include <QDebug>
#include <QSettings>
#include <QThreadPool>
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
    // 【优先级：环境变量 > 持久化的用户选择 > 默认 Auto】
    // 环境变量最高，是为了让排查/门控用例/基准能无视用户设置强行规定行为。
    // 默认 Auto = 自动用 GPU："装了独显却一次都用不上"是说不通的默认值：
    // 实测独显上重核 12～36×、轻核 5～7×（gpu-plan §0.2）。
    // 两条硬护栏：探测失败/无独显 → 静默走 CPU（硬约束 2）；用户可在首选项里改成只用 CPU。
    Accel accel = Accel::Auto;
    const QByteArray v = qgetenv("PSLITE_RASTER").toLower();
    if (v == "cpu")
        accel = Accel::CpuOnly;
    else if (v == "gpu")
        accel = Accel::GpuOnly;
    else if (v.isEmpty())
        accel = accelPreference(); // 环境变量没设 ⇒ 用用户在首选项里的选择
    // 其它未知值按 Auto（宽容，不因为一个拼错的环境变量就变行为）

    switch (accel) {
    case Accel::CpuOnly:
        m_mode = RasterMode::ForceCpu;
        break;
    case Accel::GpuOnly:
        m_mode = RasterMode::ForceGpu;
        break;
    case Accel::Auto:
        m_mode = RasterMode::Auto;
        break;
    }
    m_log = (qgetenv("PSLITE_RASTER_LOG") == "1");
    m_envCached.store(true, std::memory_order_release);
}

namespace {
constexpr char kSettingsGroup[] = "raster";
constexpr char kSettingsKey[] = "accel";
} // namespace

RasterScheduler::Accel RasterScheduler::accelPreference()
{
    QSettings s(QStringLiteral("PhotoshopLite"), QStringLiteral("PSLite"));
    s.beginGroup(QLatin1String(kSettingsGroup));
    const int v = s.value(QLatin1String(kSettingsKey), int(Accel::Auto)).toInt();
    s.endGroup();
    if (v < int(Accel::Auto) || v > int(Accel::GpuOnly))
        return Accel::Auto; // 脏数据不许把行为带偏
    return Accel(v);
}

bool RasterScheduler::setAccelPreference(Accel a)
{
    bool ok = false;
    {
        QSettings s(QStringLiteral("PhotoshopLite"), QStringLiteral("PSLite"));
        s.beginGroup(QLatin1String(kSettingsGroup));
        s.setValue(QLatin1String(kSettingsKey), int(a));
        s.endGroup();
        s.sync(); // setValue 是延迟写；必须 sync 才能知道成败
        ok = (s.status() == QSettings::NoError);
        if (!ok)
            qWarning("RasterScheduler: 加速首选项写盘失败（QSettings status=%d）", int(s.status()));
    }
    // 立即生效：丢掉旧结论与环境缓存，并让下一次重新探测
    //（用户从"只用 CPU"切到"自动"时，必须把设备真的建起来）。
    RasterScheduler::instance().resetCounters();
    return ok;
}

bool RasterScheduler::envOverrideActive()
{
    const QByteArray v = qgetenv("PSLITE_RASTER").toLower();
    return v == "cpu" || v == "gpu";
}

QString RasterScheduler::accelSourceDescription()
{
    const QByteArray v = qgetenv("PSLITE_RASTER").toLower();
    if (v == "cpu" || v == "gpu") {
        return QStringLiteral("环境变量 PSLITE_RASTER=%1 正在覆盖首选项（改下面的设置不会生效）")
            .arg(QString::fromLatin1(v));
    }
    return QStringLiteral("首选项设置生效中（环境变量未设置）");
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
    // `PSLITE_RASTER=cpu` = 永不碰 GPU（连设备都不建）。
    if (m_mode == RasterMode::ForceCpu)
        return false;
    // 熔断中：先挡掉，省得白问一次 supports。
    if (m_gpuDisabled.load(std::memory_order_relaxed))
        return false;

    // —— 以下四条都**只依赖调用参数**，不需要后端在场 ⇒ 先判、早退 ——
    // 【为什么顺序要紧】投影合成会按 64×64 块反复调用（1600×1200 ≈ 1900 次，每块约 4.6k px），
    // 这些块**永远低于像素门槛**。若把 `ensureGpu()` / `supports()` 放在前面，
    // 每个小块都要付一次"原子读 + supports 遍历 + 虚调用"；更糟的是如果 GPU 设备还没建，
    // 一个小块就会**触发建整个 D3D11 设备 + 编译 4 个 shader**（约 300 ms）——
    // 那笔钱只有在真有大链要上卡时才值得付。
    // 实测：这样调整后合成路径与纯 CPU 持平（§0.4），而滤镜链的判定结果一条都没变。
    if (pixelCount < kMinPixelsForGpu)
        return false;
    // T3.0：位置相关不再整体拒绝——由 supports() 逐算子裁决。
    // 但几何位移类（Offset）直接拒：它的输入需求是平移型，超出各 kernel 的邻域能力。
    if (stack.containsOffset())
        return false;
    // Auto：还要求链成本过线（小图/低收益链为几毫秒上一次卡不划算）。
    // ForceGpu：**按设计跳过**成本阈值，便于门控用例与实测对照 ——
    // 所以 `PSLITE_RASTER=gpu` 下"上了卡"不代表"成本判据认为该上卡"（gpu-plan §0.3）。
    if (m_mode == RasterMode::Auto && stackCostNsPerPixel(stack) <= kGpuChainCostThresholdNsPerPixel)
        return false;

    // —— 到这里才值得问后端 ——
    ensureGpu(); // Auto / ForceGpu 都惰性创建真实后端（探测失败即 nullptr ⇒ 走 CPU）
    if (!m_gpu || !m_gpu->isAvailable())
        return false;
    // 整链支持才走 GPU（半链混跑会让同一个算子在不同段用不同实现，语义更难审）
    if (!m_gpu->supports(stack))
        return false;
    return true;
}

QString RasterScheduler::warmUp()
{
    ensureEnvCached();
    if (m_mode == RasterMode::ForceCpu)
        return m_cpu->id(); // 按用户要求连探测都不做

    // 【必须异步】建 D3D11 设备 + 加载编译 4 个 shader 实测约 300 ms（§0.4）。
    // 同步做就是让用户**白等 300 ms 才出窗口** —— 那属于"启动变慢"，不能接受。
    // 异步做则：窗口立刻出来；在探测完成的几百毫秒内，早期的滤镜判定先走 CPU
    //（`ensureGpu` 会被 GCD 挡住直到探测结束），之后自动切到 GPU。
    // 因为不建 `m_gpu`，所以对 `shouldUseGpu` 而言"后端还没就绪"= 走 CPU，语义天然正确。
    static QThreadPool probePool;
    probePool.setMaxThreadCount(1); // 探测只做一次，单线程足够
    probePool.start([this] {
        QMutexLocker lock(&m_gpuInitMutex);
        if (m_gpuInitAttempted || m_blockGpuAutoCreate)
            return;
        m_gpuInitAttempted = true;
        m_gpu = GpuRasterBackend::tryCreate();
    });
    // 立刻返回"当前"生效的后端：注入后端等场景下它已经是最终值；真实探测未完成时就是 CPU。
    // 想知道探测结果可稍后调 activeBackendName()（`probeDone()` 为真时它已确定）。
    return activeBackendName();
}

bool RasterScheduler::probeDone() const
{
    QMutexLocker lock(&m_gpuInitMutex);
    return m_gpuInitAttempted;
}

QString RasterScheduler::activeBackendName() const
{
    ensureEnvCached();
    if (m_mode != RasterMode::ForceCpu && m_gpu && m_gpu->isAvailable())
        return m_gpu->id();
    return m_cpu->id();
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

void RasterScheduler::resetCounters()
{
    QMutexLocker lock(&m_gpuInitMutex);
    // 注意与 resetForTest 的差别：**不动** m_blockGpuAutoCreate，也不预先建后端 —— 只是
    // 把"上次探测/熔断"的结论丢掉，让下一次 shouldUseGpu 重新探测（Auto 下就会真的去用 GPU）。
    m_gpu.reset();
    m_gpuInitAttempted = false;
    m_consecutiveFailures.store(0, std::memory_order_relaxed);
    m_gpuDisabled.store(false, std::memory_order_relaxed);
    m_envCached.store(false, std::memory_order_release);
}

} // namespace Ps