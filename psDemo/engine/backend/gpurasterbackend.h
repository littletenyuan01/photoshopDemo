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
    bool supports(const FilterStack &stack) const override;
    ApplyStackResult applyStack(QImage &image, const FilterStack &stack, const QRect &srcRect,
                                const QRect &outRect,
                                const std::function<bool()> &shouldAbort) override;

private:
    GpuRasterBackend() = default;

    class Hub;

    bool m_ok = false;
    mutable QMutex m_mutex; ///< 单飞（§1.5）
    std::unique_ptr<QThread> m_thread;
    std::unique_ptr<Hub> m_hub;
};

} // namespace Ps

#endif
