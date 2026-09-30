/**
 * tooloptionsbar.cpp — 工具选项栏实现（ui 层）。
 */
#include "tooloptionsbar.h"
#include "ui_tooloptionsbar.h"

#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QToolButton>

ToolOptionsBar::ToolOptionsBar(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::ToolOptionsBar)
{
    ui->setupUi(this);
    // 图标 / iconSize / 色块样式见 tooloptionsbar.ui

    // 画笔/橡皮直径
    connect(ui->brushSizeSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, &ToolOptionsBar::brushDiameterChanged);

    // 油漆桶：容差 / 连续 / 填充源 / 不透明度 → 汇总为 fillOptionsChanged
    const auto emitFill = [this]() { emit fillOptionsChanged(); };
    connect(ui->fillToleranceSpin, qOverload<int>(&QSpinBox::valueChanged), this, emitFill);
    connect(ui->fillContiguousCheck, &QCheckBox::toggled, this, emitFill);
    connect(ui->fillTypeCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, emitFill);
    connect(ui->fillOpacitySpin, qOverload<int>(&QSpinBox::valueChanged), this, emitFill);

    // 魔棒 / 快速选择：容差 / 连续 / 对所有图层取样
    const auto emitSelFlood = [this]() { emit selectionFloodOptionsChanged(); };
    connect(ui->selToleranceSpin, qOverload<int>(&QSpinBox::valueChanged), this, emitSelFlood);
    connect(ui->selContiguousCheck, &QCheckBox::toggled, this, emitSelFlood);
    connect(ui->selSampleMergedCheck, &QCheckBox::toggled, this, emitSelFlood);

    // 渐变：类型 / 不透明度 / 偏移 / 仿色 / 反向（混合模式暂未接入引擎）
    const auto emitGrad = [this]() { emit gradientOptionsChanged(); };
    connect(ui->gradTypeCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, emitGrad);
    connect(ui->gradOpacitySpin, qOverload<int>(&QSpinBox::valueChanged), this, emitGrad);
    connect(ui->gradOffsetSpin, qOverload<int>(&QSpinBox::valueChanged), this, emitGrad);
    connect(ui->gradDitherCheck, &QCheckBox::toggled, this, emitGrad);
    connect(ui->gradReverseCheck, &QCheckBox::toggled, this, emitGrad);

    // 仿制图章：对齐 / 对所有图层取样
    const auto emitClone = [this]() { emit cloneStampOptionsChanged(); };
    connect(ui->paintAlignCheck, &QCheckBox::toggled, this, emitClone);
    connect(ui->paintSampleMergedCheck, &QCheckBox::toggled, this, emitClone);

    // 「家」→ 主页（UI 阶段只发信号，由 MainWindow 切到 HomeScreen）
    connect(ui->homeButton, &QToolButton::clicked, this, &ToolOptionsBar::homeClicked);

    setCurrentTool(Ps::ToolId::Move);
}

ToolOptionsBar::~ToolOptionsBar()
{
    delete ui;
}

int ToolOptionsBar::brushDiameter() const
{
    return ui->brushSizeSpin->value();
}

void ToolOptionsBar::setBrushDiameter(int diameter)
{
    ui->brushSizeSpin->blockSignals(true);
    ui->brushSizeSpin->setValue(qBound(1, diameter, 500));
    ui->brushSizeSpin->blockSignals(false);
}

int ToolOptionsBar::fillTolerance() const
{
    return ui->fillToleranceSpin->value();
}

bool ToolOptionsBar::fillContiguous() const
{
    return ui->fillContiguousCheck->isChecked();
}

Ps::FillSource ToolOptionsBar::fillSource() const
{
    const int index = ui->fillTypeCombo->currentIndex();
    if (index == static_cast<int>(Ps::FillSource::Background))
        return Ps::FillSource::Background;
    if (index == static_cast<int>(Ps::FillSource::Pattern))
        return Ps::FillSource::Pattern;
    return Ps::FillSource::Foreground;
}

int ToolOptionsBar::fillOpacityPercent() const
{
    return ui->fillOpacitySpin->value();
}

int ToolOptionsBar::selTolerance() const
{
    return ui->selToleranceSpin->value();
}

bool ToolOptionsBar::selContiguous() const
{
    return ui->selContiguousCheck->isChecked();
}

bool ToolOptionsBar::selSampleMerged() const
{
    return ui->selSampleMergedCheck->isChecked();
}

Ps::GradientType ToolOptionsBar::gradientType() const
{
    const int index = ui->gradTypeCombo->currentIndex();
    const int max = static_cast<int>(Ps::GradientType::Diamond);
    if (index < 0 || index > max)
        return Ps::GradientType::Linear;
    return static_cast<Ps::GradientType>(index);
}

int ToolOptionsBar::gradientOpacityPercent() const
{
    return ui->gradOpacitySpin->value();
}

int ToolOptionsBar::gradientOffsetPercent() const
{
    return ui->gradOffsetSpin->value();
}

bool ToolOptionsBar::gradientReverse() const
{
    return ui->gradReverseCheck->isChecked();
}

bool ToolOptionsBar::gradientDither() const
{
    return ui->gradDitherCheck->isChecked();
}

bool ToolOptionsBar::cloneAlign() const
{
    return ui->paintAlignCheck->isChecked();
}

bool ToolOptionsBar::cloneSampleMerged() const
{
    return ui->paintSampleMergedCheck->isChecked();
}

void ToolOptionsBar::setCurrentTool(Ps::ToolId id)
{
    ui->toolNameLabel->setText(toolDisplayName(id));

    // 按工具族整页切换 —— 对照 GIMP `gimp_tools_get_tool_options_gui()`
    if (QWidget *page = pageForTool(id))
        ui->optionsStack->setCurrentWidget(page);

    updateToolSpecificControls(id);
    // 提示语交给 MainWindow 显示在状态栏（选项条里只放参数，对齐 PS）
    m_hint = hintForTool(id);
}

QWidget *ToolOptionsBar::pageForTool(Ps::ToolId id) const
{
    switch (id) {
    case Ps::ToolId::Move:
        return ui->pageMove;

    // 选区族（选框 / 套索 / 魔棒 / 快速选择）共用一个页面
    case Ps::ToolId::RectSelect:
    case Ps::ToolId::EllipseSelect:
    case Ps::ToolId::Lasso:
    case Ps::ToolId::PolygonalLasso:
    case Ps::ToolId::MagneticLasso:
    case Ps::ToolId::QuickSelect:
    case Ps::ToolId::MagicWand:
        return ui->pageSelection;

    case Ps::ToolId::Crop:
    case Ps::ToolId::PerspectiveCrop:
        return ui->pageCrop;

    case Ps::ToolId::Eyedropper:
        return ui->pageEyedropper;

    // 绘画族（画笔 / 铅笔 / 混合器 / 橡皮 / 图章 / 模糊锐化涂抹 / 减淡海绵）
    case Ps::ToolId::Brush:
    case Ps::ToolId::Pencil:
    case Ps::ToolId::MixerBrush:
    case Ps::ToolId::Eraser:
    case Ps::ToolId::BackgroundEraser:
    case Ps::ToolId::CloneStamp:
    case Ps::ToolId::Blur:
    case Ps::ToolId::Sharpen:
    case Ps::ToolId::Smudge:
    case Ps::ToolId::Dodge:
    case Ps::ToolId::Sponge:
        return ui->pagePaint;

    case Ps::ToolId::PaintBucket:
        return ui->pageFill;

    case Ps::ToolId::Gradient:
        return ui->pageGradient;

    case Ps::ToolId::Pen:
    case Ps::ToolId::FreeformPen:
    case Ps::ToolId::AddAnchorPoint:
        return ui->pagePath;

    case Ps::ToolId::Type:
    case Ps::ToolId::TypeVertical:
        return ui->pageText;

    case Ps::ToolId::ShapeRect:
    case Ps::ToolId::ShapeEllipse:
    case Ps::ToolId::ShapeTriangle:
    case Ps::ToolId::ShapeLine:
        return ui->pageShape;

    case Ps::ToolId::Hand:
    case Ps::ToolId::Zoom:
        return ui->pageView;
    }
    return ui->pageMove;
}

void ToolOptionsBar::updateToolSpecificControls(Ps::ToolId id)
{
    // 选区页：容差/连续/对所有图层取样 只属于魔棒与快速选择
    const bool wandLike = (id == Ps::ToolId::MagicWand || id == Ps::ToolId::QuickSelect);
    ui->selToleranceLabel->setVisible(wandLike);
    ui->selToleranceSpin->setVisible(wandLike);
    ui->selContiguousCheck->setVisible(wandLike);
    ui->selSampleMergedCheck->setVisible(wandLike);

    // 绘画页：对齐 / 对所有图层取样 只属于仿制图章
    // （对照 GIMP：gimp_clone_options_gui 在 paint options 之上追加 clone-type /
    //   sample-merged / align-mode，其余绘画工具没有这几项）
    const bool cloneLike = (id == Ps::ToolId::CloneStamp);
    ui->paintAlignCheck->setVisible(cloneLike);
    ui->paintSampleMergedCheck->setVisible(cloneLike);
}

QString ToolOptionsBar::hintForTool(Ps::ToolId id)
{
    // 提示语要短（标签最大宽 320px）。已接入的写用法，其余统一说明「参数是占位」
    switch (id) {
    case Ps::ToolId::Move:
        return QObject::tr("点击选中图层并拖拽移动；图层面板同步选中");
    case Ps::ToolId::Brush:
        return QObject::tr("左键绘制（仅「大小」生效）");
    case Ps::ToolId::Eraser:
        return QObject::tr("左键擦除（仅「大小」生效）");
    case Ps::ToolId::PaintBucket:
        return QObject::tr("左键填充；透明层单击即可整层填色");
    case Ps::ToolId::Gradient:
        return QObject::tr("拖拽绘制渐变；类型/偏移/反向对照 GIMP Blend");
    case Ps::ToolId::Hand:
        return QObject::tr("拖拽平移画布");
    case Ps::ToolId::Zoom:
        return QObject::tr("左键放大，右键缩小");
    case Ps::ToolId::RectSelect:
        return QObject::tr("拖拽建立矩形选区；拖中 Shift 正方形；按下 Shift 加选 / Ctrl 减选 / 二者相交");
    case Ps::ToolId::EllipseSelect:
        return QObject::tr("拖拽建立椭圆选区；拖中 Shift 正圆；按下 Shift 加选 / Ctrl 减选 / 二者相交");
    case Ps::ToolId::Lasso:
        return QObject::tr("拖拽手绘套索；松手闭合；按下 Shift 加选 / Ctrl 减选 / 二者相交");
    case Ps::ToolId::PolygonalLasso:
        return QObject::tr("单击加点；Shift 吸附水平/垂直/垂线；双击/Enter 闭合；Backspace 撤点；Esc 取消");
    case Ps::ToolId::MagneticLasso:
        return QObject::tr("拖拽沿线吸附边缘；松手闭合；按下 Shift 加选 / Ctrl 减选");
    case Ps::ToolId::MagicWand:
        return QObject::tr("单击按颜色建选区；选项栏调容差/连续/取样；Shift 加选 / Ctrl 减选");
    case Ps::ToolId::QuickSelect:
        return QObject::tr("拖拽扩张选区（连通域）；选项栏调容差/取样；Ctrl 减选");
    case Ps::ToolId::Crop:
        return QObject::tr("拖出裁剪框；Enter/双击确认；Esc 取消；Shift 正方形");
    case Ps::ToolId::Eyedropper:
        return QObject::tr("单击取前景色；Alt+单击取背景色（合成取样）");
    case Ps::ToolId::CloneStamp:
        return QObject::tr("Alt+单击设源；拖拽仿制；选项栏调对齐/取样；大小生效");
    case Ps::ToolId::Blur:
        return QObject::tr("拖拽柔化像素（大小生效）");
    case Ps::ToolId::Sharpen:
        return QObject::tr("拖拽锐化像素（大小生效）");
    case Ps::ToolId::Smudge:
        return QObject::tr("拖拽涂抹像素（大小生效）");
    case Ps::ToolId::Dodge:
        return QObject::tr("拖拽提亮像素（大小生效）");
    case Ps::ToolId::Sponge:
        return QObject::tr("拖拽提高饱和度（大小生效）");
    default:
        return QObject::tr("参数为 UI 占位，逻辑尚未接入");
    }
}

QString ToolOptionsBar::toolDisplayName(Ps::ToolId id)
{
    // 名称须与 toolbox.cpp 的 buildToolSlots() 保持一致。
    // 【已知债】工具元数据目前分散在 toolbox.cpp / 本文件 / toolid.h 三处，
    // 计划收敛成 ToolInfo 注册表（见 docs/code-map.md「计划中」）。
    switch (id) {
    case Ps::ToolId::Move: return QObject::tr("移动工具");

    case Ps::ToolId::RectSelect: return QObject::tr("矩形选框工具");
    case Ps::ToolId::EllipseSelect: return QObject::tr("椭圆选框工具");

    case Ps::ToolId::Lasso: return QObject::tr("套索工具");
    case Ps::ToolId::PolygonalLasso: return QObject::tr("多边形套索工具");
    case Ps::ToolId::MagneticLasso: return QObject::tr("磁性套索工具");

    case Ps::ToolId::QuickSelect: return QObject::tr("快速选择工具");
    case Ps::ToolId::MagicWand: return QObject::tr("魔棒工具");

    case Ps::ToolId::Crop: return QObject::tr("裁剪工具");
    case Ps::ToolId::PerspectiveCrop: return QObject::tr("透视裁剪工具");

    case Ps::ToolId::Eyedropper: return QObject::tr("吸管工具");

    case Ps::ToolId::Brush: return QObject::tr("画笔工具");
    case Ps::ToolId::Pencil: return QObject::tr("铅笔工具");
    case Ps::ToolId::MixerBrush: return QObject::tr("混合器画笔工具");

    case Ps::ToolId::CloneStamp: return QObject::tr("仿制图章工具");

    case Ps::ToolId::Eraser: return QObject::tr("橡皮擦工具");
    case Ps::ToolId::BackgroundEraser: return QObject::tr("背景橡皮擦工具");

    case Ps::ToolId::PaintBucket: return QObject::tr("油漆桶工具");
    case Ps::ToolId::Gradient: return QObject::tr("渐变工具");

    case Ps::ToolId::Blur: return QObject::tr("模糊工具");
    case Ps::ToolId::Sharpen: return QObject::tr("锐化工具");
    case Ps::ToolId::Smudge: return QObject::tr("涂抹工具");

    case Ps::ToolId::Dodge: return QObject::tr("减淡工具");
    case Ps::ToolId::Sponge: return QObject::tr("海绵工具");

    case Ps::ToolId::Pen: return QObject::tr("钢笔工具");
    case Ps::ToolId::FreeformPen: return QObject::tr("自由钢笔工具");
    case Ps::ToolId::AddAnchorPoint: return QObject::tr("添加锚点工具");

    case Ps::ToolId::Type: return QObject::tr("横排文字工具");
    case Ps::ToolId::TypeVertical: return QObject::tr("直排文字工具");

    case Ps::ToolId::ShapeRect: return QObject::tr("矩形工具");
    case Ps::ToolId::ShapeEllipse: return QObject::tr("椭圆工具");
    case Ps::ToolId::ShapeTriangle: return QObject::tr("三角形工具");
    case Ps::ToolId::ShapeLine: return QObject::tr("直线工具");

    case Ps::ToolId::Hand: return QObject::tr("抓手工具");
    case Ps::ToolId::Zoom: return QObject::tr("缩放工具");
    }
    return QObject::tr("工具");
}
