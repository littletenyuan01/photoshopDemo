#ifndef HISTORYSTACK_H
#define HISTORYSTACK_H

#include "undoitem.h"

#include <QObject>
#include <memory>
#include <vector>

namespace Ps {

class ImageDocument;

/**
 * 文档撤销/重做双栈（对照 GIMP GimpUndoStack + gimp_image_undo / redo）。
 *
 * 【语义】push = 改动**之前**推入旧状态；undo/redo 均对条目调用 pop()（交换）。
 * 新 push 会清空 redo（对照 gimp_image_undo_push 清 redo）。
 *
 * 挂在 ImageDocument 上：一文档一栈。
 */
class HistoryStack : public QObject
{
    Q_OBJECT

public:
    /** 默认约 256MB 上限，超限丢最老 undo。 */
    explicit HistoryStack(QObject *parent = nullptr);

    bool canUndo() const { return !m_undo.empty(); }
    bool canRedo() const { return !m_redo.empty(); }
    bool isApplying() const { return m_applying; }

    QString undoText() const;
    QString redoText() const;

    void push(std::unique_ptr<UndoItem> item);
    void undo(ImageDocument &doc);
    void redo(ImageDocument &doc);
    void clear();

    /** 正在 pop 时禁止再 push（递归守卫）。 */
    void setApplying(bool on) { m_applying = on; }

signals:
    void changed();

private:
    void trimUndo();

    std::vector<std::unique_ptr<UndoItem>> m_undo;
    std::vector<std::unique_ptr<UndoItem>> m_redo;
    quint64 m_undoBytes = 0;
    quint64 m_byteLimit = 256ull * 1024ull * 1024ull;
    bool m_applying = false;
};

} // namespace Ps

#endif // HISTORYSTACK_H
