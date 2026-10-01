/**
 * labeledlineedit.h — 选项栏「标签 + 文本框」合体控件（ui 层）。
 *
 * 解决 QHBoxLayout 把组撑开后，label 与编辑框被中间空白拉开的问题：
 * 本控件自带紧凑布局，并把横向尺寸策略设为 Maximum，只占内容宽度。
 */
#ifndef LABELEDLINEEDIT_H
#define LABELEDLINEEDIT_H

#include <QWidget>

class QLabel;
class QLineEdit;

/**
 * 标签紧贴文本框的数值编辑单元（对齐 PS 选项条里的「宽度: [10]」）。
 *
 * Designer 里提升为本类，或代码里 `new LabeledLineEdit(tr("宽度"), this)`。
 * 对外仍可通过 lineEdit() 接线；也可用 text() / editingFinished。
 */
class LabeledLineEdit : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(QString labelText READ labelText WRITE setLabelText)
    Q_PROPERTY(QString text READ text WRITE setText NOTIFY textChanged)
    Q_PROPERTY(int editMaxWidth READ editMaxWidth WRITE setEditMaxWidth)

public:
    explicit LabeledLineEdit(QWidget *parent = nullptr);
    explicit LabeledLineEdit(const QString &label, QWidget *parent = nullptr);

    QString labelText() const;
    void setLabelText(const QString &text);

    QString text() const;
    void setText(const QString &text);

    int editMaxWidth() const;
    void setEditMaxWidth(int width);

    QLabel *label() const { return m_label; }
    QLineEdit *lineEdit() const { return m_edit; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void editingFinished();
    void textChanged(const QString &text);

private:
    void init(const QString &label);

    QLabel *m_label = nullptr;
    QLineEdit *m_edit = nullptr;
    int m_editMaxWidth = 36;
};

#endif // LABELEDLINEEDIT_H
