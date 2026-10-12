/**
 * rasterbackend.h — 栅格后端接口（可选加速；见 docs/gpu-plan.md §3.1）。
 */
#ifndef ENGINE_BACKEND_RASTERBACKEND_H
#define ENGINE_BACKEND_RASTERBACKEND_H

#include <QImage>
#include <QRect>
#include <QString>

#include <functional>

namespace Ps {

class FilterStack;

/** applyStack 的结局 —— 取消 / 失败 / 不支持 不得混用（gpu-plan.md §3.4）。 */
enum class ApplyStackResult {
    Ok,           ///< image 已是最终结果
    Failed,       ///< 尝试执行但失败；image 必须与调用前逐字节相同
    Cancelled,    ///< shouldAbort；image 须置为空图；调度器不得重试、不得计熔断
    Unsupported   ///< 不能跑这条链；不计熔断；应在调用前用 supports() 拦下
};

/**
 * 栅格后端：把整条滤镜链作用在一张 QImage 上。
 *
 * 【并发】必须并发安全 —— LayerFilterJob 会多线程同时调用（gpu-plan.md §1.5）。
 *
 * 【几何】srcRect / outRect 用文档坐标（与 FilterStack::apply 一致）；空矩形 = 整张图。
 *
 * 【Failed 无副作用】失败路径不得写坏 @p image；先在临时/设备缓冲算完，最后一步才提交。
 */
class RasterBackend
{
public:
    virtual ~RasterBackend() = default;

    virtual QString id() const = 0;
    virtual bool isAvailable() const = 0;
    virtual bool supports(const FilterStack &stack) const = 0;
    virtual ApplyStackResult applyStack(QImage &image, const FilterStack &stack,
                                        const QRect &srcRect, const QRect &outRect,
                                        const std::function<bool()> &shouldAbort) = 0;
};

} // namespace Ps

#endif
