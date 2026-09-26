#include "mainwindow.h"

#include <QApplication>
#include <QFile>
#include <QIcon>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // 应用级图标：标题栏兜底；Windows 任务栏还需 RC_ICONS 把 .ico 嵌进 exe
    a.setWindowIcon(QIcon(QStringLiteral(":/icons/ui/app-logo.png")));

    // 全局暗色主题：与 PS/GIMP 深色工作区一致，保证左侧浅色描边图标可见
    QFile qss(QStringLiteral(":/styles/dark.qss"));
    if (qss.open(QIODevice::ReadOnly | QIODevice::Text)) {
        a.setStyleSheet(QString::fromUtf8(qss.readAll()));
        qss.close();
    }

    MainWindow w;
    // Photoshop（Windows）常见启动态：主窗口最大化，占满工作区；
    // 并无固定「官方」客户区像素。非最大化时 .ui 默认几何约 1440×900。
    // （早先还按 availableGeometry 先 resize 一次，但紧接着的 showMaximized() 会覆盖它，
    //   那段是死逻辑，已删。）
    w.showMaximized();
    return QCoreApplication::exec();
}
