#ifndef ENGINE_OP_SOLIDFILLOP_H
#define ENGINE_OP_SOLIDFILLOP_H

#include "bufferop.h"
#include "opname.h"

#include <QColor>
#include <QRect>

namespace Ps {

/**
 * 实色 / 透明填充算子（编辑→填充 / 清除；尊重选区）。
 *
 * 直接在「工作窗口」覆盖的瓦片上写色，**不**整层 materialize / setFromImage；
 * 无选区且窗口为整层时走 TileBuffer::fill（透明填充可释放瓦片）。
 */
class SolidFillOp : public BufferOp
{
public:
    SolidFillOp() = default;

    QString id() const override { return opNameId(OpName::SolidFill); }
    QString name() const override { return opNameTitle(OpName::SolidFill); }

    void setColor(const QColor &c) { m_color = c; }
    QColor color() const { return m_color; }

protected:
    bool prepare(OpContext &ctx) override;
    QRect process(OpContext &ctx) override;

private:
    QColor m_color = Qt::transparent;
    QRect m_window; ///< 工作窗口（层内坐标，= roi ∩ 选区外接框 ∩ 层）
};

} // namespace Ps

#endif // ENGINE_OP_SOLIDFILLOP_H
