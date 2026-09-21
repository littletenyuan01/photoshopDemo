#include "tooloptionsbar.h"
#include "ui_tooloptionsbar.h"

ToolOptionsBar::ToolOptionsBar(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::ToolOptionsBar)
{
    ui->setupUi(this);
    setCurrentTool(Ps::ToolId::Move);
}

ToolOptionsBar::~ToolOptionsBar()
{
    delete ui;
}

void ToolOptionsBar::setCurrentTool(Ps::ToolId id)
{
    ui->toolNameLabel->setText(toolDisplayName(id));
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
