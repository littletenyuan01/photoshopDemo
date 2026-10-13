/**
 * performancedialog.cpp —「首选项 → 性能」实现（ui 层）。
 */
#include "performancedialog.h"
#include "ui_performancedialog.h"

#include <QButtonGroup>
#include <QPushButton>
#include <QRadioButton>

using Ps::RasterScheduler;

PerformanceDialog::PerformanceDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::PerformanceDialog)
{
    ui->setupUi(this);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);

    // 单选互斥（.ui 里同一父控件下已天然互斥，这里显式建组便于按 id 取值）
    auto *group = new QButtonGroup(this);
    group->addButton(ui->autoRadio, int(RasterScheduler::Accel::Auto));
    group->addButton(ui->cpuRadio, int(RasterScheduler::Accel::CpuOnly));
    group->addButton(ui->gpuRadio, int(RasterScheduler::Accel::GpuOnly));

    // 回填当前设置，而不是硬写"自动"——否则用户一打开就以为自己的选择丢了
    switch (RasterScheduler::accelPreference()) {
    case RasterScheduler::Accel::CpuOnly:
        ui->cpuRadio->setChecked(true);
        break;
    case RasterScheduler::Accel::GpuOnly:
        ui->gpuRadio->setChecked(true);
        break;
    case RasterScheduler::Accel::Auto:
    default:
        ui->autoRadio->setChecked(true);
        break;
    }

    connect(ui->okButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(ui->cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    for (QRadioButton *b : {ui->autoRadio, ui->cpuRadio, ui->gpuRadio})
        connect(b, &QRadioButton::toggled, this, &PerformanceDialog::onAccelChanged);

    refreshStatus();
}

PerformanceDialog::~PerformanceDialog()
{
    delete ui;
}

RasterScheduler::Accel PerformanceDialog::selectedAccel() const
{
    if (ui->cpuRadio->isChecked())
        return RasterScheduler::Accel::CpuOnly;
    if (ui->gpuRadio->isChecked())
        return RasterScheduler::Accel::GpuOnly;
    return RasterScheduler::Accel::Auto;
}

void PerformanceDialog::onAccelChanged()
{
    refreshStatus();
}

void PerformanceDialog::refreshStatus()
{
    auto &sched = RasterScheduler::instance();
    const QString current = sched.activeBackendName();
    const bool gpu = current.startsWith(QLatin1String("gpu"));

    // 【为什么要显示"当前实际后端"】这个项目吃过"界面看着能改、其实没生效"的亏。
    // 用户改完设置必须能立刻看到"现在到底跑在哪"，否则只能靠猜。
    ui->statusLabel->setText(
        QStringLiteral("当前实际后端：%1%2")
            .arg(current, gpu ? QStringLiteral("（GPU 加速已启用）")
                              : QStringLiteral("（CPU；未检测到可用 GPU 或当前设置为仅 CPU）")));

    // 环境变量覆盖：如实说明"改这里不会生效"，而不是让用户白改一通
    ui->hintLabel->setText(RasterScheduler::accelSourceDescription()
                           + QStringLiteral("\n\n")
                           + QStringLiteral("说明：GPU 路径用于中值 / 极值 / 卷积 / 杂色 / 高斯 / 盒模糊等算子，"
                                            "整条链都支持才走 GPU；否则整链回 CPU。"
                                            "独立显卡上实测重算子约 12～36×、轻算子约 5～7×（端到端含传输）。"));

    // 环境变量在覆盖时把单选项置灰：不可生效的控件不该看起来可点
    const bool overridden = RasterScheduler::envOverrideActive();
    ui->autoRadio->setEnabled(!overridden);
    ui->cpuRadio->setEnabled(!overridden);
    ui->gpuRadio->setEnabled(!overridden);
    ui->accelGroup->setTitle(overridden ? QStringLiteral("滤镜加速后端（被环境变量覆盖，暂不可改）")
                                        : QStringLiteral("滤镜加速后端"));
}
