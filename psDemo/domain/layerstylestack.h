/**
 * layerstylestack.h — 图层样式栈：增删开关（domain 层）。
 *
 * 对照 GIMP drawable filter_stack；语义对齐 PS 图层样式列表（可多效果并存）。
 */
#ifndef DOMAIN_LAYERSTYLESTACK_H
#define DOMAIN_LAYERSTYLESTACK_H

#include "layerstyle.h"

#include <QVector>

namespace Ps {

class LayerStyleStack
{
public:
    bool isEmpty() const { return m_effects.isEmpty(); }
    int count() const { return m_effects.size(); }

    const LayerStyleEffect &at(int index) const { return m_effects.at(index); }
    LayerStyleEffect &at(int index) { return m_effects[index]; }

    /** @return 新效果下标。 */
    int append(const LayerStyleEffect &effect);
    bool removeAt(int index);
    bool setEnabled(int index, bool on);

    /** 若已有同 kind，返回其下标；否则 -1。 */
    int indexOfKind(LayerStyleKind kind) const;

    /**
     * 确保存在指定 kind（无则追加默认）；返回下标。
     * 已存在则只打开 enabled。
     */
    int ensure(LayerStyleKind kind);

    /** 替换整栈（对话框确认 / undo）。 */
    void replaceAll(const QVector<LayerStyleEffect> &effects) { m_effects = effects; }
    QVector<LayerStyleEffect> snapshot() const { return m_effects; }

    void clear() { m_effects.clear(); }

    bool hasEnabled() const;
    /** 所有已启用效果需要的最大外扩（像素）。 */
    int maxPadding() const;

private:
    QVector<LayerStyleEffect> m_effects;
};

} // namespace Ps

#endif // DOMAIN_LAYERSTYLESTACK_H
