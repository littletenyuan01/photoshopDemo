/**
 * labeledcombobox.cpp — 选项栏「标签 + 下拉框」合体控件实现。
 */
#include "labeledcombobox.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSizePolicy>

LabeledComboBox::LabeledComboBox(QWidget *parent)
    : QWidget(parent)
{
    init(QString());
}

LabeledComboBox::LabeledComboBox(const QString &label, QWidget *parent)
    : QWidget(parent)
{
    init(label);
}

void LabeledComboBox::init(const QString &label)
{
    setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);

    m_label = new QLabel(label, this);
    m_label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    m_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_combo = new QComboBox(this);
    m_combo->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    m_combo->setMinimumHeight(22);

    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(2);
    lay->addWidget(m_label, 0, Qt::AlignVCenter);
    lay->addWidget(m_combo, 0, Qt::AlignVCenter);

    connect(m_combo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &LabeledComboBox::currentIndexChanged);
}

QString LabeledComboBox::labelText() const
{
    return m_label->text();
}

void LabeledComboBox::setLabelText(const QString &text)
{
    m_label->setText(text);
}

QStringList LabeledComboBox::itemsText() const
{
    QStringList out;
    for (int i = 0; i < m_combo->count(); ++i)
        out << m_combo->itemText(i);
    return out;
}

void LabeledComboBox::setItemsText(const QStringList &items)
{
    const int keep = m_combo->currentIndex();
    m_combo->clear();
    m_combo->addItems(items);
    if (keep >= 0 && keep < m_combo->count())
        m_combo->setCurrentIndex(keep);
}

int LabeledComboBox::currentIndex() const
{
    return m_combo->currentIndex();
}

void LabeledComboBox::setCurrentIndex(int index)
{
    m_combo->setCurrentIndex(index);
}

QString LabeledComboBox::currentText() const
{
    return m_combo->currentText();
}

void LabeledComboBox::addItem(const QString &text)
{
    m_combo->addItem(text);
}

void LabeledComboBox::clear()
{
    m_combo->clear();
}

QSize LabeledComboBox::sizeHint() const
{
    return layout() ? layout()->sizeHint() : QWidget::sizeHint();
}

QSize LabeledComboBox::minimumSizeHint() const
{
    return sizeHint();
}
