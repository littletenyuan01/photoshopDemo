#include "tooloptionsbar.h"
#include "ui_tooloptionsbar.h"

ToolOptionsBar::ToolOptionsBar(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::ToolOptionsBar)
{
    ui->setupUi(this);
    connect(ui->brushSizeSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, &ToolOptionsBar::brushDiameterChanged);
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
    updateForTool(id);
}

void ToolOptionsBar::updateForTool(Ps::ToolId id)
{
    const bool paintLike = (id == Ps::ToolId::Brush || id == Ps::ToolId::Eraser);
    ui->sizeLabel->setVisible(paintLike);
    ui->brushSizeSpin->setVisible(paintLike);

    switch (id) {
    case Ps::ToolId::Brush:
        ui->hintLabel->setText(tr("左键在活动层绘制；Alt+左键平移"));
        break;
    case Ps::ToolId::Eraser:
        ui->hintLabel->setText(tr("左键擦除活动层；Alt+左键平移"));
        break;
    case Ps::ToolId::Hand:
        ui->hintLabel->setText(tr("拖拽平移画布"));
        break;
    case Ps::ToolId::Zoom:
        ui->hintLabel->setText(tr("左键放大，右键缩小"));
        break;
    default:
        ui->hintLabel->setText(tr("该工具逻辑尚未接入"));
        break;
    }
}

QString ToolOptionsBar::toolDisplayName(Ps::ToolId id)
{
    switch (id) {
    case Ps::ToolId::Move: return QObject::tr("移动工具");
    case Ps::ToolId::RectSelect: return QObject::tr("矩形选框工具");
    case Ps::ToolId::EllipseSelect: return QObject::tr("椭圆选框工具");
    case Ps::ToolId::Lasso: return QObject::tr("套索工具");
    case Ps::ToolId::MagicWand: return QObject::tr("魔棒工具");
    case Ps::ToolId::Crop: return QObject::tr("裁剪工具");
    case Ps::ToolId::Eyedropper: return QObject::tr("吸管工具");
    case Ps::ToolId::Brush: return QObject::tr("画笔工具");
    case Ps::ToolId::Eraser: return QObject::tr("橡皮擦工具");
    case Ps::ToolId::PaintBucket: return QObject::tr("油漆桶工具");
    case Ps::ToolId::Gradient: return QObject::tr("渐变工具");
    case Ps::ToolId::Type: return QObject::tr("文字工具");
    case Ps::ToolId::ShapeRect: return QObject::tr("矩形工具");
    case Ps::ToolId::ShapeEllipse: return QObject::tr("椭圆工具");
    case Ps::ToolId::ShapeTriangle: return QObject::tr("三角形工具");
    case Ps::ToolId::ShapeLine: return QObject::tr("直线工具");
    case Ps::ToolId::Hand: return QObject::tr("抓手工具");
    case Ps::ToolId::Zoom: return QObject::tr("缩放工具");
    }
    return QObject::tr("工具");
}
