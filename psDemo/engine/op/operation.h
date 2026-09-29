/**
 * operation.h — 算子轻量基类（engine/op 层）。
 *
 * 缓冲算子与点算子的共同 id/name 接口；对照 GEGL GeglOperation 语义壳（不引入 GEGL）。
 */
#ifndef ENGINE_OP_OPERATION_H
#define ENGINE_OP_OPERATION_H

#include <QString>

namespace Ps {

/**
 * 轻量算子基类（对照 GEGL GeglOperation 的语义壳，**不**引入 GEGL）。
 *
 * 缓冲类算子：由 OpRegistry 注册，OpRunner 调度 prepare → process → finish。
 * 约定：不碰 Qt Widgets；缓冲格式 ARGB32_Premultiplied（混合内部可解预乘）。
 */
class Operation
{
public:
    virtual ~Operation() = default;

    /** 稳定 id，如 "ps:layer-mode" / "ps:flood-fill"。 */
    virtual QString id() const = 0;
    /** 可读名（调试 / 日后滤镜列表）。 */
    virtual QString name() const = 0;
};

} // namespace Ps

#endif // ENGINE_OP_OPERATION_H
