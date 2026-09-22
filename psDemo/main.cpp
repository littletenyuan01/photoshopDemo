#include "mainwindow.h"

#include <QApplication>
#include <QFile>
#include <QScreen>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // 全局暗色主题：与 PS/GIMP 深色工作区一致，保证左侧浅色描边图标可见
    QFile qss(QStringLiteral(":/styles/dark.qss"));
    if (qss.open(QIODevice::ReadOnly | QIODevice::Text)) {
        a.setStyleSheet(QString::fromUtf8(qss.readAll()));
        qss.close();
    }

    MainWindow w;
    // Photoshop（Windows）常见启动态：主窗口最大化，占满工作区；
    // 并无固定「官方」客户区像素。非最大化时 .ui 默认几何约 1440×900。
    if (QScreen *screen = w.screen()) {
        const QRect avail = screen->availableGeometry();
        // 设计尺寸作回退；小屏则贴齐可用区域
        if (avail.width() < 1440 || avail.height() < 900)
            w.resize(avail.size());
    }
    w.showMaximized();
    return QCoreApplication::exec();
}
