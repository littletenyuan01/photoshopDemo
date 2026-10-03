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

/**
 * 应用内图层样式剪贴板（对照 PS「拷贝/粘贴图层样式」；非系统剪贴板）。
 * 跨文档、跨面板共享。
 */
class LayerStyleClipboard
{
public:
    static bool isEmpty() { return s_effects.isEmpty(); }
    static QVector<LayerStyleEffect> snapshot() { return s_effects; }
    static void set(const QVector<LayerStyleEffect> &effects) { s_effects = effects; }
    static void clear() { s_effects.clear(); }

private:
    static QVector<LayerStyleEffect> s_effects;
};

} // namespace Ps

#endif // DOMAIN_LAYERSTYLESTACK_H
