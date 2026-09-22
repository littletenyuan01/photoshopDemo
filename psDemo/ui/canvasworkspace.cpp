#include "canvasworkspace.h"
#include "ui_canvasworkspace.h"

#include "canvasview.h"
#include "rulerwidget.h"

#include <limits>

CanvasWorkspace::CanvasWorkspace(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::CanvasWorkspace)
{
    ui->setupUi(this);

    // Designer 对自定义属性可能未写入，构造后再确保方向
    ui->hRuler->setOrientation(Qt::Horizontal);
    ui->vRuler->setOrientation(Qt::Vertical);

    connect(ui->canvasView, &CanvasView::viewChanged,
            this, &CanvasWorkspace::syncRulers);
    connect(ui->canvasView, &CanvasView::cursorImagePosChanged,
            this, &CanvasWorkspace::onCanvasMouseMoved);

    syncRulers();
}


CanvasWorkspace::~CanvasWorkspace()
{
    delete ui;
}

CanvasView *CanvasWorkspace::canvasView() const
{
    return ui->canvasView;
}

void CanvasWorkspace::syncRulers()
{
    // 视口左/上缘在图像坐标中的值 = -offset / zoom（与 GIMP lower 同构，像素单位）
    const QPointF offset = ui->canvasView->imageOffset();
    const qreal zoom = ui->canvasView->zoom();
    if (zoom <= 0.0)
        return;

    const qreal hLower = -offset.x() / zoom;
    const qreal hUpper = (ui->canvasView->width() - offset.x()) / zoom;
    const qreal vLower = -offset.y() / zoom;
    const qreal vUpper = (ui->canvasView->height() - offset.y()) / zoom;

    ui->hRuler->setRange(hLower, hUpper);
    ui->vRuler->setRange(vLower, vUpper);
}

void CanvasWorkspace::onCanvasMouseMoved(const QPointF &imagePos, bool inside)
{
    if (!inside) {
        ui->hRuler->setCursorValue(std::numeric_limits<qreal>::quiet_NaN());
        ui->vRuler->setCursorValue(std::numeric_limits<qreal>::quiet_NaN());
        return;
    }
    ui->hRuler->setCursorValue(imagePos.x());
    ui->vRuler->setCursorValue(imagePos.y());
}
