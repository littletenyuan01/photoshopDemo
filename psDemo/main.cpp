/**
 * main.cpp — QApplication 入口（app 层）。
 *
 * 初始化算子注册表、全局 QSS 与主窗口；对照 GIMP gimp_init / app startup。
 */
#include "mainwindow.h"

#include "engine/op/opsinit.h"

#include <QApplication>
#include <QFile>
#include <QIcon>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // 对照 gimp_operations_init：先注册算子类型与 pad 依赖，再进 UI
    Ps::opsInit();

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
    w.showMaximized();
    return QCoreApplication::exec();
}
