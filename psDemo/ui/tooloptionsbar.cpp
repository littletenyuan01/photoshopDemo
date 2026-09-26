#include "tooloptionsbar.h"
#include "ui_tooloptionsbar.h"

#include <QButtonGroup>
#include <QFontComboBox>
#include <QSize>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStackedWidget>
#include <QToolButton>

ToolOptionsBar::ToolOptionsBar(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::ToolOptionsBar)
{
    ui->setupUi(this);

    // 唯一真正接线的选项：画笔/橡皮直径（其余全是 UI 占位，见头文件说明）
    connect(ui->brushSizeSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, &ToolOptionsBar::brushDiameterChanged);

    // 画笔预设 / 渐变预设 / 文字颜色三个按钮：只做个色块样式，不接功能
    ui->paintPresetButton->setIconSize(QSize(28, 18));
    ui->gradientPreviewButton->setIconSize(QSize(36, 16));
    ui->textColorButton->setStyleSheet(
        QStringLiteral("background-color: #1a1a1a; border: 1px solid #222;"));

    // 选项页套在 optionsScroll（QScrollArea）里：各页最小宽度不同（绘画页实测 1038px），
    // 让它保持自然宽度、窗口不够宽时横向滚动。**不要压缩 stack**——早先设
    // QSizePolicy::Ignored，末尾 Expanding spacer 会吃掉全部空间，控件被压扁到只剩一位数字。
    // QScrollArea 的 sizeHint 按字号推算（font-based，约 70px），故用 min/maxSize 压回内容高度。

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

void ToolOptionsBar::setCurrentTool(Ps::ToolId id)
{
    m_tool = id;

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
    case Ps::ToolId::Brush:
        return QObject::tr("左键绘制（仅「大小」生效）");
    case Ps::ToolId::Eraser:
        return QObject::tr("左键擦除（仅「大小」生效）");
    case Ps::ToolId::Hand:
        return QObject::tr("拖拽平移画布");
    case Ps::ToolId::Zoom:
        return QObject::tr("左键放大，右键缩小");
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
