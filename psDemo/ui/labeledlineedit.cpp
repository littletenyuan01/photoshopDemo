/**
 * labeledlineedit.cpp — 选项栏「标签 + 文本框」合体控件实现。
 */
#include "labeledlineedit.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSizePolicy>

LabeledLineEdit::LabeledLineEdit(QWidget *parent)
    : QWidget(parent)
{
    init(QString());
}

LabeledLineEdit::LabeledLineEdit(const QString &label, QWidget *parent)
    : QWidget(parent)
{
    init(label);
}

void LabeledLineEdit::init(const QString &label)
{
    // 只占内容宽，避免被选项页 HBox 横向撑开后把 label/框拉开
    setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);

    m_label = new QLabel(label, this);
    m_label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    m_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_edit = new QLineEdit(this);
    m_edit->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_edit->setMaximumSize(m_editMaxWidth, 22);
    m_edit->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_edit->setFrame(true);

    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(2); // 标签与框紧贴，仅留 2px 呼吸
    lay->addWidget(m_label, 0, Qt::AlignVCenter);
    lay->addWidget(m_edit, 0, Qt::AlignVCenter);

    connect(m_edit, &QLineEdit::editingFinished, this, &LabeledLineEdit::editingFinished);
    connect(m_edit, &QLineEdit::textChanged, this, &LabeledLineEdit::textChanged);
}

QString LabeledLineEdit::labelText() const
{
    return m_label->text();
}

void LabeledLineEdit::setLabelText(const QString &text)
{
    m_label->setText(text);
}

QString LabeledLineEdit::text() const
{
    return m_edit->text();
}

void LabeledLineEdit::setText(const QString &text)
{
    m_edit->setText(text);
}

int LabeledLineEdit::editMaxWidth() const
{
    return m_editMaxWidth;
}

void LabeledLineEdit::setEditMaxWidth(int width)
{
    m_editMaxWidth = qMax(24, width);
    m_edit->setMaximumWidth(m_editMaxWidth);
    m_edit->setMinimumWidth(qMin(28, m_editMaxWidth));
}

QSize LabeledLineEdit::sizeHint() const
{
    return layout() ? layout()->sizeHint() : QWidget::sizeHint();
}

QSize LabeledLineEdit::minimumSizeHint() const
{
    return sizeHint();
}
