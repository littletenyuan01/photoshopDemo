/**
 * historystack.cpp — HistoryStack 的 push/undo/redo/trim 实现（app 层）。
 */
#include "historystack.h"

#include "domain/imagedocument.h"

namespace Ps {

HistoryStack::HistoryStack(QObject *parent)
    : QObject(parent)
{
}

QString HistoryStack::undoText() const
{
    return m_undo.empty() ? QString() : m_undo.back()->name();
}

QString HistoryStack::redoText() const
{
    return m_redo.empty() ? QString() : m_redo.back()->name();
}

void HistoryStack::push(std::unique_ptr<UndoItem> item)
{
    if (!item || m_applying)
        return;
    m_redo.clear();
    m_undoBytes += item->byteSize();
    m_undo.push_back(std::move(item));
    trimUndo();
    emit changed();
}

void HistoryStack::undo(ImageDocument &doc)
{
    if (m_undo.empty() || m_applying)
        return;
    m_applying = true;
    auto item = std::move(m_undo.back());
    m_undo.pop_back();
    const quint64 sz = item->byteSize();
    if (m_undoBytes >= sz)
        m_undoBytes -= sz;
    else
        m_undoBytes = 0;
    item->pop(doc);
    m_redo.push_back(std::move(item));
    m_applying = false;
    emit changed();
}

void HistoryStack::redo(ImageDocument &doc)
{
    if (m_redo.empty() || m_applying)
        return;
    m_applying = true;
    auto item = std::move(m_redo.back());
    m_redo.pop_back();
    item->pop(doc);
    m_undoBytes += item->byteSize();
    m_undo.push_back(std::move(item));
    trimUndo();
    m_applying = false;
    emit changed();
}

void HistoryStack::clear()
{
    m_undo.clear();
    m_redo.clear();
    m_undoBytes = 0;
    emit changed();
}

void HistoryStack::trimUndo()
{
    while (m_undoBytes > m_byteLimit && m_undo.size() > 1) {
        const quint64 sz = m_undo.front()->byteSize();
        m_undo.erase(m_undo.begin());
        if (m_undoBytes >= sz)
            m_undoBytes -= sz;
        else
            m_undoBytes = 0;
    }
}

} // namespace Ps
