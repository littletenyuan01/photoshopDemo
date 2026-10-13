/**
 * gpurasterbackend.h — QRhi compute 可选加速（中值 + 自定 3×3；opt-in）。
 *
 * QRhi 只在独立线程上跑：滤镜定稿在工作线程，GUI 又在等 future，
 * 不能 BlockingQueued 回 GUI。默认 Auto=CPU；PSLITE_RASTER=gpu 才启用。
 */
#ifndef ENGINE_BACKEND_GPURASTERBACKEND_H
#define ENGINE_BACKEND_GPURASTERBACKEND_H

#include "engine/backend/rasterbackend.h"

#include <QMutex>
#include <QThread>

#include <memory>

namespace Ps {

class GpuRasterBackend final : public RasterBackend
{
public:
    /** 探测失败返回 nullptr（调用方静默走 CPU）。 */
    static std::unique_ptr<GpuRasterBackend> tryCreate();

    ~GpuRasterBackend() override;

    QString id() const override { return QStringLiteral("gpu:qrhi"); }
    bool isAvailable() const override { return m_ok; }
    /**
     * 真实生效的 QRhi 后端名（"D3D11" / "OpenGLES2" / …），探测失败返回 "none"。
     *
     * 【为什么要有这个】后端探测失败是**静默**的（按设计走 CPU），于是「GPU 没生效」
     * 与「GPU 生效但不支持这条链」在调度日志里长得一模一样（都是 `backend=cpu`）。
     * 本项目已经因此漏过一次真 bug：`StorageBuffer + Dynamic` 被 QRhi 拒绝 ⇒ 整个后端
     * 起不来，而所有 GPU 门控用例都只是 SKIP（不算失败），错误信息里只有 "no usable GPU"。
     * 把后端名暴露出来，测试与日志才能区分「探测失败」与「不支持」。
     */
    QString backendName() const { return m_backendName; }
    bool supports(const FilterStack &stack) const override;
    ApplyStackResult applyStack(QImage &image, const FilterStack &stack, const QRect &srcRect,
                                const QRect &outRect,
                                const std::function<bool()> &shouldAbort) override;

private:
    GpuRasterBackend() = default;

    class Hub;

    bool m_ok = false;
    QString m_backendName = QStringLiteral("none"); ///< 探测成功后的 QRhi 后端名（诊断用）
    mutable QMutex m_mutex; ///< 单飞（§1.5）
    std::unique_ptr<QThread> m_thread;
    std::unique_ptr<Hub> m_hub;
};

} // namespace Ps

#endif
