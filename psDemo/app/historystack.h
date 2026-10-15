/**
 * historystack.h — 文档撤销/重做双栈（app 层）。
 *
 * 挂在 ImageDocument 上；对照 GIMP GimpUndoStack。
 */
#ifndef HISTORYSTACK_H
#define HISTORYSTACK_H

#include "undoitem.h"

#include <QObject>
#include <QPair>
#include <QString>
#include <QVector>
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
    /** undo 栈深度（测例 / 调试用）。 */
    int undoDepth() const { return int(m_undo.size()); }
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

    /** undo 栈累计字节（供 PSLITE_MEM / 页缓存口径）。 */
    quint64 undoBytes() const { return m_undoBytes; }
    /** redo 栈累计字节（A26：与 undo 一并计入内存上限口径）。 */
    quint64 redoBytes() const { return m_redoBytes; }
    /** undo+redo 合计（PSLITE_MEM / trim 决策用）。 */
    quint64 historyBytes() const { return m_undoBytes + m_redoBytes; }
    /** 超限丢最老 undo 的字节上限（默认 256MB；可调）。 */
    quint64 byteLimit() const { return m_byteLimit; }
    void setByteLimit(quint64 bytes);

    /** 栈顶 undo 条目（无可返回 nullptr）；供 markDirty 累加触碰区。 */
    UndoItem *topUndoItem() { return m_undo.empty() ? nullptr : m_undo.back().get(); }
    /** 栈顶条目字节数变化后同步账面（T0；由 ImageDocument::markDirty 调用）。 */
    void refreshTopBytes();

    /** 历史总步数（已发生 + 可重做）。 */
    int stepCount() const { return int(m_undo.size() + m_redo.size()); }
    /** 游标：已发生的步数。0 = 全撤到底，stepCount() = 全重做到顶。 */
    int currentStep() const { return int(m_undo.size()); }
    /** 第 step 步（0-based，从最老开始）的条目名；越界返回空串。 */
    QString stepName(int step) const;
    /** 跳转到第 step 步（clamp 到 [0, stepCount()]）。返回是否真的发生了跳转。 */
    bool jumpToStep(int step, ImageDocument &doc);

    /** 第 step 步是否是书签；是则同时输出书签名。 */
    bool bookmarkAt(int step, QString *label = nullptr) const;
    /** 按标签查书签步号；未找到返回 -1。 */
    int bookmarkStep(const QString &label) const;
    /** 书签（= 位置标记）。step 越界或不落在 [0, stepCount()] 返回 false。上限 64。 */
    bool addBookmark(int step, const QString &label);
    bool removeBookmark(int step);
    QVector<QPair<int, QString>> bookmarks() const { return m_bookmarks; }

signals:
    /** undo/redo 栈或 canUndo/canRedo 状态变化时发出。 */
    void changed();
    /** 因超限丢弃了最老 undo（至少一条）时发出。 */
    void historyTrimmed(int droppedCount);
    /** 书签增删或重基后发出。 */
    void bookmarksChanged();

private:
    /** 当 m_undoBytes 超过 m_byteLimit 时丢弃最老条目。 */
    void trimUndo();
    /** 换栈后把 m_topAccounted 对齐到当前 undo 栈顶。 */
    void syncTopAccounted();
    /** 单步撤销：无重入守卫、不发 changed()、不 trim。 */
    void undoOne(ImageDocument &doc);
    /** 单步重做：无重入守卫、不发 changed()、不 trim。 */
    void redoOne(ImageDocument &doc);
    /** push 清 redo 前：删除步号 > currentStep() 的书签。 */
    void dropBookmarksBeyondCurrent();

    std::vector<std::unique_ptr<UndoItem>> m_undo;  ///< undo 栈（底→顶 = 旧→新）
    std::vector<std::unique_ptr<UndoItem>> m_redo;  ///< redo 栈
    QVector<QPair<int, QString>> m_bookmarks;       ///< 按 step 升序；label 非空
    quint64 m_undoBytes = 0;                        ///< undo 栈累计字节（用于 trim）
    quint64 m_redoBytes = 0;                        ///< redo 栈累计字节（A26）
    quint64 m_byteLimit = 256ull * 1024ull * 1024ull; ///< 超限丢最老 undo
    quint64 m_topAccounted = 0;                     ///< T0：栈顶条目的已记账字节
    bool m_applying = false;                        ///< pop 进行中，禁止嵌套 push
    static constexpr int kMaxBookmarks = 64;
};

} // namespace Ps

#endif // HISTORYSTACK_H
