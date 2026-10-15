/**
 * historystack.cpp — HistoryStack 的 push/undo/redo/trim 实现（app 层）。
 */
#include "historystack.h"

#include "domain/imagedocument.h"
#include "engine/cache/tilecache.h"

#include <cstdio>

namespace Ps {

HistoryStack::HistoryStack(QObject *parent)
    : QObject(parent)
{
}

void HistoryStack::setByteLimit(quint64 bytes)
{
    m_byteLimit = bytes;
    trimUndo();
}

QString HistoryStack::undoText() const
{
    return m_undo.empty() ? QString() : m_undo.back()->name();
}

QString HistoryStack::redoText() const
{
    return m_redo.empty() ? QString() : m_redo.back()->name();
}

void HistoryStack::syncTopAccounted()
{
    m_topAccounted = m_undo.empty() ? 0 : m_undo.back()->byteSize();
}

void HistoryStack::refreshTopBytes()
{
    if (m_undo.empty()) {
        m_topAccounted = 0;
        return;
    }
    const quint64 now = m_undo.back()->byteSize();
    if (now >= m_topAccounted) {
        m_undoBytes += (now - m_topAccounted);
    } else {
        const quint64 d = m_topAccounted - now;
        if (m_undoBytes >= d)
            m_undoBytes -= d;
        else
            m_undoBytes = 0;
    }
    m_topAccounted = now;
}

void HistoryStack::push(std::unique_ptr<UndoItem> item)
{
    if (!item || m_applying)
        return;
    m_redo.clear();
    m_redoBytes = 0; // A26：新 push 清空 redo 时同步清记账
    m_undoBytes += item->byteSize();
    m_undo.push_back(std::move(item));
    syncTopAccounted();
    trimUndo();
    // 静默点：undo push 之后可淘汰瓦片（文档指针由调用方持有；此处无 Projection）
    TileCache::instance().trim();
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
    m_redoBytes += sz; // A26：迁入 redo 后计入
    m_redo.push_back(std::move(item));
    syncTopAccounted();
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
    const quint64 sz = item->byteSize();
    if (m_redoBytes >= sz)
        m_redoBytes -= sz;
    else
        m_redoBytes = 0;
    item->pop(doc);
    m_undoBytes += sz;
    m_undo.push_back(std::move(item));
    syncTopAccounted();
    trimUndo();
    m_applying = false;
    emit changed();
}

void HistoryStack::clear()
{
    m_undo.clear();
    m_redo.clear();
    m_undoBytes = 0;
    m_redoBytes = 0;
    m_topAccounted = 0;
    emit changed();
}

void HistoryStack::trimUndo()
{
    int dropped = 0;
    while (m_undoBytes > m_byteLimit && m_undo.size() > 1) {
        const quint64 sz = m_undo.front()->byteSize();
        m_undo.erase(m_undo.begin());
        if (m_undoBytes >= sz)
            m_undoBytes -= sz;
        else
            m_undoBytes = 0;
        ++dropped;
    }
    if (dropped > 0) {
        std::printf("[history] trimmed %d oldest undo entr%s (limit=%.0fMB)\n", dropped,
                    dropped == 1 ? "y" : "ies", m_byteLimit / (1024.0 * 1024.0));
        std::fflush(stdout);
        emit historyTrimmed(dropped);
    }
}

} // namespace Ps
