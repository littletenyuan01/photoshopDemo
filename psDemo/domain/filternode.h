/**
 * filternode.h — 图层滤镜节点：OpName + 开关 + 参数（domain 层）。
 *
 * 非破坏；求值在 FilterStack::apply，不写回 Layer 瓦片。
 */
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
    /** 更换滤镜类型（当前求值仅 BrightnessContrast 生效）。 */
    void setOp(OpName op) { m_op = op; }

    bool isEnabled() const { return m_enabled; }
    /** 开关本节点；关闭时 apply 跳过。 */
    void setEnabled(bool on) { m_enabled = on; }

    /** 面板显示名（来自 OpName 名字表）。 */
    QString title() const { return opNameTitle(m_op); }

    /** 亮度 [-1, 1]，0 为不变。 */
    qreal brightness() const { return m_brightness; }
    void setBrightness(qreal v) { m_brightness = qBound(-1.0, v, 1.0); }

    /** 对比度 [-1, 1]，0 为不变。 */
    qreal contrast() const { return m_contrast; }
    void setContrast(qreal v) { m_contrast = qBound(-1.0, v, 1.0); }

private:
    OpName m_op = OpName::BrightnessContrast; ///< 滤镜算子类型
    bool m_enabled = true;                    ///< 是否参与 apply 求值
    qreal m_brightness = 0.0;                 ///< 亮度 [-1,1]
    qreal m_contrast = 0.0;                   ///< 对比度 [-1,1]
};

} // namespace Ps

#endif // DOMAIN_FILTERNODE_H
