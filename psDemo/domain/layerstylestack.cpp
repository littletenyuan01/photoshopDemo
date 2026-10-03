/**
 * layerstylestack.cpp — LayerStyleStack 管理。
 */
#include "layerstylestack.h"

namespace Ps {

int LayerStyleStack::append(const LayerStyleEffect &effect)
{
    m_effects.append(effect);
    return m_effects.size() - 1;
}

bool LayerStyleStack::removeAt(int index)
{
    if (index < 0 || index >= m_effects.size())
        return false;
    m_effects.removeAt(index);
    return true;
}

bool LayerStyleStack::setEnabled(int index, bool on)
{
    if (index < 0 || index >= m_effects.size())
        return false;
    m_effects[index].setEnabled(on);
    return true;
}

int LayerStyleStack::indexOfKind(LayerStyleKind kind) const
{
    for (int i = 0; i < m_effects.size(); ++i) {
        if (m_effects.at(i).kind() == kind)
            return i;
    }
    return -1;
}

int LayerStyleStack::ensure(LayerStyleKind kind)
{
    const int existing = indexOfKind(kind);
    if (existing >= 0) {
        m_effects[existing].setEnabled(true);
        return existing;
    }
    return append(LayerStyleEffect::makeDefault(kind));
}

bool LayerStyleStack::hasEnabled() const
{
    for (const LayerStyleEffect &e : m_effects) {
        if (e.isEnabled())
            return true;
    }
    return false;
}

int LayerStyleStack::maxPadding() const
{
    int pad = 0;
    for (const LayerStyleEffect &e : m_effects) {
        if (e.isEnabled())
            pad = qMax(pad, e.paddingNeeded());
    }
    return pad;
}

QVector<LayerStyleEffect> LayerStyleClipboard::s_effects;

} // namespace Ps
