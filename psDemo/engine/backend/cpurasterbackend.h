/**
 * cpurasterbackend.h — CPU 栅格后端（FilterStack 图路径 ③–⑦）。
 */
#ifndef ENGINE_BACKEND_CPURASTERBACKEND_H
#define ENGINE_BACKEND_CPURASTERBACKEND_H

#include "engine/backend/rasterbackend.h"

namespace Ps {

class CpuRasterBackend final : public RasterBackend
{
public:
    QString id() const override { return QStringLiteral("cpu"); }
    bool isAvailable() const override { return true; }
    bool supports(const FilterStack &stack) const override;
    ApplyStackResult applyStack(QImage &image, const FilterStack &stack, const QRect &srcRect,
                                const QRect &outRect,
                                const std::function<bool()> &shouldAbort) override;
};

} // namespace Ps

#endif
