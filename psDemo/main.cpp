/**
 * main.cpp — QApplication 入口（app 层）。
 *
 * 初始化算子注册表、全局 QSS 与主窗口；对照 GIMP gimp_init / app startup。
 */
#include "mainwindow.h"

#include "engine/backend/rasterscheduler.h"
#include "engine/op/opsinit.h"
#include "engine/graph/graphselfcheck.h"

#include <QApplication>
#include <QDebug>
#include <QFile>
#include <QIcon>
#include <QTimer>

#include <memory>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // 对照 gimp_operations_init：先注册算子类型与 pad 依赖，再进 UI
    Ps::opsInit();

    // 图引擎自检（结构 / 缓存失效 / 图化合成一致性）；失败仅告警，不阻断启动
    if (!Ps::graphSelfCheck())
        qWarning("PsGraph self-check failed");

    // 【栅格后端探测：后台线程发起，不阻塞启动】默认 Auto：探测到独显就用 GPU，
    // 探测不到就静默走 CPU。建 D3D11 设备 + 编译 shader 约 300 ms（gpu-plan §0.4），
    // 同步做会让用户白等 ⇒ 这里只**发起**探测，窗口立刻出来。
    // 日志用 50 ms 轮询等结论落地（**不许 sleep**：那等于把 300 ms 又搬回主线程），
    // 最多轮 100 次（5 s）后报当前值，绝不无限等。
    {
        auto &sched = Ps::RasterScheduler::instance();
        const QString atStart = sched.warmUp();
        qInfo().noquote() << "[raster] 栅格后端探测已发起（探测前 =" << atStart << "）";
        auto tries = std::make_shared<int>(0);
        auto *timer = new QTimer(&a);
        timer->setInterval(50);
        QObject::connect(timer, &QTimer::timeout, &a, [&sched, atStart, tries, timer] {
            if (!sched.probeDone() && ++(*tries) < 100)
                return;
            timer->stop();
            const QString final = sched.activeBackendName();
            qInfo().noquote()
                << "[raster] 栅格后端 =" << final
                << (final == QLatin1String("cpu") ? "（CPU；未检测到可用 GPU 或已强制 CPU）"
                                                  : "（GPU 加速已启用）");
            timer->deleteLater();
        });
        timer->start();
    }

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
