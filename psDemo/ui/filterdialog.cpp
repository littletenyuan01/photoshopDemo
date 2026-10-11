/**
 * filterdialog.cpp — 滤镜对话框实现（ui 层）。
 */
#include "filterdialog.h"

#include "adjustmentpropshost.h"

#include <QDialogButtonBox>
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
}

void FilterDialog::setNode(const Ps::FilterNode &node)
{
    m_host->setNode(node);
}

Ps::FilterNode FilterDialog::node() const
{
    return m_host->node();
}
