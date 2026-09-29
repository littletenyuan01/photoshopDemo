#ifndef DOMAIN_FILTERNODE_H
#define DOMAIN_FILTERNODE_H

#include "engine/op/opname.h"

#include <QString>
#include <QtGlobal>

namespace Ps {

/**
 * 图层滤镜节点（对照 GimpDrawableFilter：可开关、带参数，不写回层像素）。
 * 当前仅支持 BrightnessContrast；其它 OpName 求值时跳过。
 */
class FilterNode
{
public:
    FilterNode() = default;
    explicit FilterNode(OpName op)
        : m_op(op)
    {
    }

    OpName op() const { return m_op; }
    void setOp(OpName op) { m_op = op; }

    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool on) { m_enabled = on; }

    QString title() const { return opNameTitle(m_op); }

    /** 亮度 [-1, 1]，0 为不变。 */
    qreal brightness() const { return m_brightness; }
    void setBrightness(qreal v) { m_brightness = qBound(-1.0, v, 1.0); }

    /** 对比度 [-1, 1]，0 为不变。 */
    qreal contrast() const { return m_contrast; }
    void setContrast(qreal v) { m_contrast = qBound(-1.0, v, 1.0); }

private:
    OpName m_op = OpName::BrightnessContrast;
    bool m_enabled = true;
    qreal m_brightness = 0.0;
    qreal m_contrast = 0.0;
};

} // namespace Ps

#endif // DOMAIN_FILTERNODE_H
