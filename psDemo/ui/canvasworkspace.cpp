#include "canvasworkspace.h"
#include "ui_canvasworkspace.h"

#include "canvasdocstatusbar.h"
#include "canvasview.h"
#include "rulerwidget.h"

#include <limits>

CanvasWorkspace::CanvasWorkspace(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::CanvasWorkspace)
{
    ui->setupUi(this);

    ui->hRuler->setOrientation(Qt::Horizontal);
    ui->vRuler->setOrientation(Qt::Vertical);

    // 水平滚动条在布局中应吃掉剩余宽度
    if (auto *layout = ui->bottomBarLayout)
        layout->setStretch(1, 1);

    connect(ui->canvasView, &CanvasView::viewChanged,
            this, &CanvasWorkspace::syncRulersAndScrollBars);
    connect(ui->canvasView, &CanvasView::viewChanged,
            this, &CanvasWorkspace::syncDocStatus);
    connect(ui->canvasView, &CanvasView::cursorImagePosChanged,
            this, &CanvasWorkspace::onCanvasMouseMoved);
    connect(ui->hScrollBar, &QScrollBar::valueChanged,
            this, &CanvasWorkspace::onHScroll);
    connect(ui->vScrollBar, &QScrollBar::valueChanged,
            this, &CanvasWorkspace::onVScroll);
    connect(ui->docStatusBar, &CanvasDocStatusBar::zoomCommitted,
            this, &CanvasWorkspace::onStatusZoomCommitted);

    syncRulersAndScrollBars();
    syncDocStatus();
}

CanvasWorkspace::~CanvasWorkspace()
{
    delete ui;
}

CanvasView *CanvasWorkspace::canvasView() const
{
    return ui->canvasView;
}

void CanvasWorkspace::notifyDocumentChanged()
{
    ui->docStatusBar->setDocument(ui->canvasView->document());
    syncDocStatus();
}

void CanvasWorkspace::syncDocStatus()
{
    ui->docStatusBar->setZoomFactor(ui->canvasView->zoom());
    ui->docStatusBar->setDocument(ui->canvasView->document());
}

void CanvasWorkspace::onStatusZoomCommitted(qreal zoom)
{
    ui->canvasView->setZoom(zoom);
    syncDocStatus();
}

void CanvasWorkspace::syncRulersAndScrollBars()
{
    CanvasView *canvas = ui->canvasView;
    const qreal zoom = canvas->zoom();
    if (zoom <= 0.0)
        return;

    const QPointF offset = canvas->imageOffset();
    const qreal hLower = -offset.x() / zoom;
    const qreal hUpper = (canvas->width() - offset.x()) / zoom;
    const qreal vLower = -offset.y() / zoom;
    const qreal vUpper = (canvas->height() - offset.y()) / zoom;
    ui->hRuler->setRange(hLower, hUpper);
    ui->vRuler->setRange(vLower, vUpper);

    m_updatingScrollBars = true;

    const int maxX = canvas->scrollMaxX();
    const int maxY = canvas->scrollMaxY();
    const int pageX = qMax(1, canvas->width());
    const int pageY = qMax(1, canvas->height());

    // Qt：min==max 会禁用滚动条；完整可见时用假行程保持 AlwaysOn
    if (maxX <= 0) {
        ui->hScrollBar->setRange(0, 1);
        ui->hScrollBar->setPageStep(1000);
        ui->hScrollBar->setSingleStep(1);
        ui->hScrollBar->setValue(0);
    } else {
        ui->hScrollBar->setRange(0, maxX);
        ui->hScrollBar->setPageStep(pageX);
        ui->hScrollBar->setSingleStep(qMax(1, pageX / 20));
        ui->hScrollBar->setValue(canvas->scrollX());
    }
    ui->hScrollBar->setEnabled(true);
    ui->hScrollBar->show();

    if (maxY <= 0) {
        ui->vScrollBar->setRange(0, 1);
        ui->vScrollBar->setPageStep(1000);
        ui->vScrollBar->setSingleStep(1);
        ui->vScrollBar->setValue(0);
    } else {
        ui->vScrollBar->setRange(0, maxY);
        ui->vScrollBar->setPageStep(pageY);
        ui->vScrollBar->setSingleStep(qMax(1, pageY / 20));
        ui->vScrollBar->setValue(canvas->scrollY());
    }
    ui->vScrollBar->setEnabled(true);
    ui->vScrollBar->show();

    m_updatingScrollBars = false;
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

void CanvasWorkspace::onHScroll(int value)
{
    if (m_updatingScrollBars)
        return;
    if (ui->canvasView->scrollMaxX() <= 0) {
        m_updatingScrollBars = true;
        ui->hScrollBar->setValue(0);
        m_updatingScrollBars = false;
        return;
    }
    ui->canvasView->setScrollOffset(value, ui->canvasView->scrollY());
}

void CanvasWorkspace::onVScroll(int value)
{
    if (m_updatingScrollBars)
        return;
    if (ui->canvasView->scrollMaxY() <= 0) {
        m_updatingScrollBars = true;
        ui->vScrollBar->setValue(0);
        m_updatingScrollBars = false;
        return;
    }
    ui->canvasView->setScrollOffset(ui->canvasView->scrollX(), value);
}
