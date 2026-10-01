/**
 * labeledcombobox.h — 选项栏「标签 + 下拉框」合体控件（ui 层）。
 *
 * 与 LabeledLineEdit 同理：自带紧凑布局 + 横向 Maximum，避免被 HBox 撑开后
 * 把标签与 Combo 中间拉开。
 */
#ifndef LABELEDCOMBOBOX_H
#define LABELEDCOMBOBOX_H

#include <QStringList>
#include <QWidget>

class QComboBox;
class QLabel;

/**
 * 标签紧贴下拉框（对齐 PS 选项条里的「运算 [新选区 ▾]」）。
 *
 * Designer 提升，或 `new LabeledComboBox(tr("运算"), this)`。
 * `.ui` 里用 `itemsText`（stringlist）填选项。
 */
class LabeledComboBox : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(QString labelText READ labelText WRITE setLabelText)
    Q_PROPERTY(QStringList itemsText READ itemsText WRITE setItemsText)
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)

public:
    explicit LabeledComboBox(QWidget *parent = nullptr);
    explicit LabeledComboBox(const QString &label, QWidget *parent = nullptr);

    QString labelText() const;
    void setLabelText(const QString &text);

    QStringList itemsText() const;
    void setItemsText(const QStringList &items);

    int currentIndex() const;
    void setCurrentIndex(int index);

    QString currentText() const;
    void addItem(const QString &text);
    void clear();

    QLabel *label() const { return m_label; }
    QComboBox *comboBox() const { return m_combo; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void currentIndexChanged(int index);

private:
    void init(const QString &label);

    QLabel *m_label = nullptr;
    QComboBox *m_combo = nullptr;
};

#endif // LABELEDCOMBOBOX_H
