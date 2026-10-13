/**
 * performancedialog.h —「首选项 → 性能」对话框声明（ui 层）。
 *
 * 目前只有一项：**滤镜加速后端**（自动 / 只用 CPU / 强制 GPU）。
 * 这是 rasterbackend 三态策略的 UI 入口，对应 `RasterScheduler::Accel`。
 */
#ifndef PERFORMANCEDIALOG_H
#define PERFORMANCEDIALOG_H

#include "engine/backend/rasterscheduler.h"

#include <QDialog>

QT_BEGIN_NAMESPACE
namespace Ui {
class PerformanceDialog;
}
QT_END_NAMESPACE

/**
 * Photoshop 风格「首选项 → 性能」对话框（.ui）。
 *
 * 诚实口径：界面上**如实说明**当前探测到的后端、以及"环境变量正在覆盖"这类情况 ——
 * 本项目吃过「界面看着能改、其实没生效」的亏（台账 #13 那类"缓存写给谁看"）。
 */
class PerformanceDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PerformanceDialog(QWidget *parent = nullptr);
    ~PerformanceDialog() override;

    /** 用户选的策略（确定后读它；也可取消时不读）。 */
    Ps::RasterScheduler::Accel selectedAccel() const;

private:
    /** 把当前实际状况（探测结果 / 生效层级 / 代价说明）刷新到界面文字。 */
    void refreshStatus();
    void onAccelChanged();

    Ui::PerformanceDialog *ui;
};

#endif // PERFORMANCEDIALOG_H
