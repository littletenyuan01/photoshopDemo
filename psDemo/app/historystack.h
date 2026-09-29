/**
 * historystack.h — 文档撤销/重做双栈（app 层）。
 *
 * 挂在 ImageDocument 上；对照 GIMP GimpUndoStack。
 */
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
    /** 正在 pop 条目时为 true；此时禁止再 push（递归守卫）。 */
    bool isApplying() const { return m_applying; }

    /** 栈顶 undo 条目的菜单文案；空栈返回空串。 */
    QString undoText() const;
    /** 栈顶 redo 条目的菜单文案；空栈返回空串。 */
    QString redoText() const;

    /** 推入一条 undo；新 push 会清空 redo 栈。 */
    void push(std::unique_ptr<UndoItem> item);
    /** 执行 undo：弹出 undo 顶并 pop 交换文档状态。 */
    void undo(ImageDocument &doc);
    /** 执行 redo：弹出 redo 顶并 pop 交换文档状态。 */
    void redo(ImageDocument &doc);
    /** 清空 undo/redo 双栈与字节计数。 */
    void clear();

    /** 正在 pop 时禁止再 push（递归守卫）。 */
    void setApplying(bool on) { m_applying = on; }

signals:
    /** undo/redo 栈或 canUndo/canRedo 状态变化时发出。 */
    void changed();

private:
    /** 当 m_undoBytes 超过 m_byteLimit 时丢弃最老条目。 */
    void trimUndo();

    std::vector<std::unique_ptr<UndoItem>> m_undo;  ///< undo 栈（底→顶 = 旧→新）
    std::vector<std::unique_ptr<UndoItem>> m_redo;  ///< redo 栈
    quint64 m_undoBytes = 0;                        ///< undo 栈累计字节（用于 trim）
    quint64 m_byteLimit = 256ull * 1024ull * 1024ull; ///< 超限丢最老 undo
    bool m_applying = false;                        ///< pop 进行中，禁止嵌套 push
};

} // namespace Ps

#endif // HISTORYSTACK_H
