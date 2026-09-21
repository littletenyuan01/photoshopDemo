#include "mainwindow.h"

#include <QApplication>
#include <QFile>

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
    w.show();
    return QCoreApplication::exec();
}
