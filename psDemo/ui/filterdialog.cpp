/**
 * filterdialog.cpp — 滤镜对话框实现（ui 层）。
 */
#include "filterdialog.h"

#include "adjustmentpropshost.h"

#include <QDialogButtonBox>
#include <QKeySequence>
#include <QShortcut>
#include <QVBoxLayout>

FilterDialog::FilterDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("滤镜"));

    auto *lay = new QVBoxLayout(this);
    m_host = new AdjustmentPropsHost(this);
    lay->addWidget(m_host);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    lay->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // 参数面板的实时变化 → 转发给调用方做预览
    connect(m_host, &AdjustmentPropsHost::previewChanged,
            this, &FilterDialog::previewChanged);

    // 松手 / 数值框回车 = 一次参数提交 → 记下「提交前」的节点，供对话框内 Ctrl+Z 逐步回退
    connect(m_host, &AdjustmentPropsHost::commitChanged, this,
            [this](const Ps::FilterNode &n) {
                m_paramUndo.append(m_last);
                m_last = n;
            });

    // 模态对话框会吞掉主窗口的 Ctrl+Z（撤销），故这里自带一条：
    // 只回退本对话框的参数，文档级撤销仍由「确定 / 取消」决定。
    auto *undoShortcut = new QShortcut(QKeySequence(QKeySequence::Undo), this);
    connect(undoShortcut, &QShortcut::activated, this, &FilterDialog::undoLastChange);
}

void FilterDialog::setNode(const Ps::FilterNode &node)
{
    m_last = node;
    m_paramUndo.clear();
    m_host->setNode(node);
}

Ps::FilterNode FilterDialog::node() const
{
    return m_host->node();
}

void FilterDialog::undoLastChange()
{
    if (m_paramUndo.isEmpty())
        return;
    const Ps::FilterNode prev = m_paramUndo.takeLast();
    m_last = prev;
    m_host->setNode(prev); // 面板内有 m_block 保护，不会反过来触发预览/提交
    emit previewChanged(prev); // 让画布跟着回退
}
