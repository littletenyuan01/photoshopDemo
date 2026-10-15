/**
 * historystack.cpp — HistoryStack 的 push/undo/redo/trim 实现（app 层）。
 */
#include "historystack.h"

#include "domain/imagedocument.h"
#include "engine/cache/tilecache.h"

#include <QtGlobal>
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

QString HistoryStack::stepName(int step) const
{
    const int n = stepCount();
    const int c = currentStep();
    if (step < 0 || step >= n)
        return {};
    if (step < c)
        return m_undo[size_t(step)]->name();
    // redo 栈顶 = 最早被撤销的一步 ⇒ 时间序 i 对应 redo[n-1-i]
    return m_redo[size_t(n - 1 - step)]->name();
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

void HistoryStack::undoOne(ImageDocument &doc)
{
    if (m_undo.empty())
        return;
    auto item = std::move(m_undo.back());
    m_undo.pop_back();
    const quint64 sz = item->byteSize();
    if (m_undoBytes >= sz)
        m_undoBytes -= sz;
    else
        m_undoBytes = 0;
    item->pop(doc);
    m_redoBytes += sz;
    m_redo.push_back(std::move(item));
    syncTopAccounted();
}

void HistoryStack::redoOne(ImageDocument &doc)
{
    if (m_redo.empty())
        return;
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
}

bool HistoryStack::jumpToStep(int step, ImageDocument &doc)
{
    if (m_applying)
        return false;
    const int n = stepCount();
    if (n == 0)
        return false;
    const int target = qBound(0, step, n);
    if (target == currentStep())
        return false;

    m_applying = true;
    {
        ImageDocument::ChangeBatch batch(doc);
        while (currentStep() > target)
            undoOne(doc);
        while (currentStep() < target)
            redoOne(doc);
    }
    trimUndo(); // 跳转结束后统一 trim（单步 redoOne 不 trim）
    m_applying = false;
    emit changed();
    return true;
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
    undoOne(doc);
    m_applying = false;
    emit changed();
}

void HistoryStack::redo(ImageDocument &doc)
{
    if (m_redo.empty() || m_applying)
        return;
    m_applying = true;
    redoOne(doc);
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
