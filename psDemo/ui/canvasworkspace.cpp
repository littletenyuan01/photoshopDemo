#include "canvasworkspace.h"
#include "ui_canvasworkspace.h"

#include "app/appsession.h"
#include "canvasdocstatusbar.h"
#include "canvasview.h"
#include "domain/imagedocument.h"
#include "rulerwidget.h"

#include <QScrollBar>
#include <QSignalBlocker>

#include <limits>

namespace {

constexpr qreal kNaN = std::numeric_limits<qreal>::quiet_NaN();

} // namespace

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

void CanvasWorkspace::setSession(Ps::AppSession *session)
{
    if (!session)
        return;

    // 文档换了 → 画布换文档 + 状态条换文档
    connect(session, &Ps::AppSession::documentChanged, this,
            [this](Ps::ImageDocument *doc) {
                ui->canvasView->setDocument(doc);
                ui->docStatusBar->setDocument(doc);
                syncDocStatus();
                syncRulersAndScrollBars();
            });

    // 文档内容变了 → 底栏尺寸/分辨率文案刷新（早先靠 MainWindow 手工调 notifyDocumentChanged）
    connect(session, &Ps::AppSession::documentChanged, this,
            [this](Ps::ImageDocument *doc) {
                if (!doc)
                    return;
                connect(doc, &Ps::ImageDocument::contentChanged, this,
                        &CanvasWorkspace::syncDocStatus, Qt::UniqueConnection);
            });

    ui->canvasView->setDocument(session->document());
    ui->docStatusBar->setDocument(session->document());
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

void CanvasWorkspace::syncScrollBar(QScrollBar *bar, int maxScroll, int pageSize, int value)
{
    // RAII 抑制回环：本函数内写 bar 不会反过来触发 onHScroll/onVScroll
    const QSignalBlocker blocker(bar);

    if (maxScroll <= 0) {
        // Qt：min==max 会禁用滚动条；完整可见时用假行程保持 AlwaysOn 外观
        bar->setRange(0, 1);
        bar->setPageStep(1000);
        bar->setSingleStep(1);
        bar->setValue(0);
    } else {
        bar->setRange(0, maxScroll);
        bar->setPageStep(pageSize);
        bar->setSingleStep(qMax(1, pageSize / 20));
        bar->setValue(qBound(0, value, maxScroll));
    }
    bar->setEnabled(true);
    bar->show();
}

void CanvasWorkspace::syncRulersAndScrollBars()
{
    CanvasView *canvas = ui->canvasView;
    const qreal zoom = canvas->zoom();
    if (zoom <= 0.0)
        return;

    const QPointF offset = canvas->imageOffset();
    // 标尺范围 = 视口两边在图像坐标中的值（对照 gimp_display_shell_rulers_update）
    ui->hRuler->setRange(-offset.x() / zoom, (canvas->width() - offset.x()) / zoom);
    ui->vRuler->setRange(-offset.y() / zoom, (canvas->height() - offset.y()) / zoom);

    syncScrollBar(ui->hScrollBar, canvas->scrollMaxX(),
                  qMax(1, canvas->width()), canvas->scrollX());
    syncScrollBar(ui->vScrollBar, canvas->scrollMaxY(),
                  qMax(1, canvas->height()), canvas->scrollY());
}

void CanvasWorkspace::onCanvasMouseMoved(const QPointF &imagePos, bool inside)
{
    if (!inside) {
        ui->hRuler->setCursorValue(kNaN);
        ui->vRuler->setCursorValue(kNaN);
        return;
    }
    ui->hRuler->setCursorValue(imagePos.x());
    ui->vRuler->setCursorValue(imagePos.y());
}

void CanvasWorkspace::handleScroll(QScrollBar *bar, int maxScroll, int value, bool horizontal)
{
    if (maxScroll <= 0) {
        // 无行程：把滚动条归零（同样用 RAII 抑制回环）
        const QSignalBlocker blocker(bar);
        bar->setValue(0);
        return;
    }

    CanvasView *canvas = ui->canvasView;
    if (horizontal)
        canvas->setScrollOffset(value, canvas->scrollY());
    else
        canvas->setScrollOffset(canvas->scrollX(), value);
}

void CanvasWorkspace::onHScroll(int value)
{
    handleScroll(ui->hScrollBar, ui->canvasView->scrollMaxX(), value, true);
}

void CanvasWorkspace::onVScroll(int value)
{
    handleScroll(ui->vScrollBar, ui->canvasView->scrollMaxY(), value, false);
}
