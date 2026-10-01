/**
 * infopanel.h — PS「信息」停靠面板（ui 层）。
 *
 * 四宫格：光标下 RGB / CMYK、X·Y、选区 W·H；文档占用；当前工具说明。
 * 默认隐藏；窗口菜单「信息」或 F8 切换。
 */
#ifndef INFOPANEL_H
#define INFOPANEL_H

#include <QColor>
#include <QPointF>
#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class InfoPanel;
}
QT_END_NAMESPACE

namespace Ps {
class AppSession;
class ImageDocument;
}

/**
 * 信息面板（对照 Photoshop Info / GIMP 无完全对应 dock；本 Demo 按 PS 截图布局）。
 *
 * 数据：
 * - 光标 XY + 合成像素 RGB/CMYK：由 CanvasView 鼠标移动推送；
 * - 选区 W/H：订阅文档 selectionChanged；
 * - 文档占用：按图层瓦片估算「文档/划痕」双值（对齐 PS「文档:aM/bM」文案）；
 * - 工具说明：MainWindow 在切工具时写入。
 */
class InfoPanel : public QWidget
{
    Q_OBJECT

public:
    explicit InfoPanel(QWidget *parent = nullptr);
    ~InfoPanel() override;

    /** 订阅会话文档广播；不取得所有权。 */
    void setSession(Ps::AppSession *session);

    /**
     * 光标在文档上的位置与是否在画布内。
     * @param sample 合成取样色（无效则清空 RGB/CMYK）。
     */
    void setCursorInfo(const QPointF &imagePos, bool inside, const QColor &sample);

    /** 当前工具用法说明（对照 PS 信息面板底部提示）。 */
    void setToolHint(const QString &text);

private slots:
    void onSessionDocumentChanged(Ps::ImageDocument *document);
    void refreshSelectionSize();
    void refreshDocumentMemory();

private:
    void clearColorReadout();
    void setColorReadout(const QColor &color);
    static void rgbToCmykPercent(int r, int g, int b, int *c, int *m, int *y, int *k);
    static QString formatBytesShort(qint64 bytes);

    Ui::InfoPanel *ui = nullptr;
    Ps::AppSession *m_session = nullptr;
    Ps::ImageDocument *m_document = nullptr;
};

#endif // INFOPANEL_H
