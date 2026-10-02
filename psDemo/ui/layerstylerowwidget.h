/**
 * layerstylerowwidget.h — 图层样式子行（ui）。
 *
 * 对照 PS 图层面板层下缩进的「投影 / 内发光…」行：眼睛 + 名称。
 */
#ifndef LAYERSTYLEROWWIDGET_H
#define LAYERSTYLEROWWIDGET_H

#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class LayerStyleRowWidget;
}
QT_END_NAMESPACE

class LayerStyleRowWidget : public QWidget
{
    Q_OBJECT

public:
    explicit LayerStyleRowWidget(QWidget *parent = nullptr);
    ~LayerStyleRowWidget() override;

    void setTitle(const QString &title);
    void setEffectVisible(bool visible);
    void setStyleIndex(int index);
    int styleIndex() const { return m_styleIndex; }

signals:
    void visibilityToggled(int styleIndex, bool visible);

private:
    void updateEyeIcon(bool visible);

    Ui::LayerStyleRowWidget *ui;
    int m_styleIndex = -1;
};

#endif // LAYERSTYLEROWWIDGET_H
