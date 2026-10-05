/**
 * mainwindow.cpp — MainWindow 菜单槽、文件 IO 与组件装配实现（app 层）。
 */
#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "app/appsession.h"
#include "app/historystack.h"
#include "app/recentdocuments.h"
#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "domain/layermask.h"
#include "domain/layerstyle.h"
#include "domain/layerstylestack.h"
// 必须早于 ui_mainwindow.h：其中 DockPanel 头文件对 CanvasView 仅有前向声明
#include "ui/canvasview.h"
#include "ui/canvasworkspace.h"
#include "ui/dockpanel.h"
#include "engine/compositor.h"
#include "io/projectio.h"
#include "io/psdio.h"
#include "io/rasterio.h"
#include "ui/canvassizedialog.h"
#include "ui/colorspanel.h"
#include "ui/homescreen.h"
#include "ui/imagesizedialog.h"
#include "ui/infopanel.h"
#include "ui/newdocumentdialog.h"
#include "ui/propertiespanel.h"
#include "ui/toolbox.h"
#include "ui/tooloptionsbar.h"
#include "tools/toolmanager.h"
#include "tools/transformtool.h"

#include <QAbstractButton>
#include <QAction>
#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QKeySequence>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QAbstractSpinBox>
#include <QLineEdit>
#include <QShortcut>
#include <QSplitter>
#include <QTextEdit>
#include <QTimer>

#include <memory>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_session(new Ps::AppSession(this))
{
    ui->setupUi(this); // 菜单、标题、图标与布局均来自 mainwindow.ui

    // 右侧栏三段可拖动调节高度（QSplitter）；拖不到折叠，靠每段的 minimumHeight 兜底
    for (int i = 0; i < ui->rightSplitter->count(); ++i) {
        ui->rightSplitter->setCollapsible(i, false);
        ui->rightSplitter->setStretchFactor(i, i == ui->rightSplitter->count() - 1 ? 1 : 0);
    }
    // 用户一旦自己拖过，就再也不覆盖他的高度
    connect(ui->rightSplitter, &QSplitter::splitterMoved,
            this, [this]() { m_rightColumnUserSized = true; });

    setupHomeStack();
    setupMenus();
    rebuildRecentMenu();
    setupSession();
    createInitialDocument(); // 启动即有可演示文档，避免空白壳
    setupToolbox();          // 放在文档之后：工具箱初始状态要与画布一致
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::setupMenus()
{
    // —— 文件（已实现）——
    connect(ui->actionNew, &QAction::triggered, this, &MainWindow::onNewDocument);
    connect(ui->actionOpen, &QAction::triggered, this, &MainWindow::onOpenDocument);
    // 打开为智能对象 ≈ GIMP「打开为链接图层」：始终新建文档 + 链接层
    ui->actionOpenAsSmartObject->setEnabled(true);
    connect(ui->actionOpenAsSmartObject, &QAction::triggered,
            this, &MainWindow::onOpenAsSmartObject);
    // 置入 ≈ GIMP「打开为图层」：当前文档加层；无文档时 placePath 回退到打开
    ui->actionPlaceEmbedded->setEnabled(true);
    connect(ui->actionPlaceEmbedded, &QAction::triggered, this, &MainWindow::onPlaceEmbedded);
    // 链接层 ≈ GIMP「打开为链接图层」
    ui->actionPlaceLinked->setEnabled(true);
    connect(ui->actionPlaceLinked, &QAction::triggered, this, &MainWindow::onPlaceLinked);
    ui->actionRasterizeSmartObject->setEnabled(true);
    connect(ui->actionRasterizeSmartObject, &QAction::triggered,
            this, &MainWindow::onRasterizeLinkedLayer);
    {
        auto *updateLinked = new QAction(tr("更新链接"), this);
        updateLinked->setToolTip(
            tr("从源文件重新读入活动链接层像素（对照 GIMP 刷新 Link Layer）"));
        ui->menuLayer->insertAction(ui->actionSmartObjects, updateLinked);
        connect(updateLinked, &QAction::triggered, this, &MainWindow::onUpdateLinkedLayer);
    }
    ui->actionSave->setEnabled(true);
    ui->actionSave->setToolTip(tr("存储为 PhotoshopLite 工程（.pslite）"));
    ui->actionSaveAs->setEnabled(true);
    ui->actionSaveAs->setToolTip(tr("另存为 PhotoshopLite 工程（.pslite）"));
    connect(ui->actionSave, &QAction::triggered, this, &MainWindow::onSaveDocument);
    connect(ui->actionSaveAs, &QAction::triggered, this, &MainWindow::onSaveDocumentAs);
    ui->actionExport->setEnabled(true);
    ui->actionExport->setToolTip(tr("把当前合成结果导出为 PNG（不改工程文件）"));
    ui->actionExportAs->setEnabled(true);
    ui->actionExportAs->setToolTip(tr("导出为 PNG 或 JPEG"));
    connect(ui->actionExport, &QAction::triggered, this, &MainWindow::onExportPng);
    connect(ui->actionExportAs, &QAction::triggered, this, &MainWindow::onExportAs);
    connect(ui->actionExit, &QAction::triggered, this, &QWidget::close);

    connect(ui->actionImageSize, &QAction::triggered, this, &MainWindow::onImageSize);
    connect(ui->actionCanvasSize, &QAction::triggered, this, &MainWindow::onCanvasSize);

    connect(ui->actionLayerNew, &QAction::triggered, this, &MainWindow::onNewLayer);
    connect(ui->actionLayerDuplicate, &QAction::triggered, this, &MainWindow::onDuplicateLayer);
    ui->actionLayerDuplicate->setShortcut(QKeySequence(QStringLiteral("Ctrl+J")));
    ui->actionLayerDuplicate->setToolTip(tr("复制当前图层"));
    ui->actionLayerDelete->setEnabled(true);
    ui->actionLayerDelete->setToolTip(tr("删除当前图层（至少保留一层）"));
    connect(ui->actionLayerDelete, &QAction::triggered, this, &MainWindow::onDeleteLayer);

    // 图层→图层蒙版（对照 PS Layer → Layer Mask / GIMP layers-add-mask）
    auto enableMaskAction = [this](QAction *a, Ps::ImageDocument::LayerMaskInit init) {
        a->setEnabled(true);
        a->setToolTip(tr("为活动层添加图层蒙版"));
        connect(a, &QAction::triggered, this, [this, init]() {
            Ps::ImageDocument *doc = m_session ? m_session->document() : nullptr;
            if (!doc || doc->activeLayerIndex() < 0)
                return;
            doc->addLayerMask(doc->activeLayerIndex(), init);
        });
    };
    enableMaskAction(ui->actionMaskRevealAll, Ps::ImageDocument::LayerMaskInit::RevealAll);
    enableMaskAction(ui->actionMaskHideAll, Ps::ImageDocument::LayerMaskInit::HideAll);
    enableMaskAction(ui->actionMaskRevealSelection,
                     Ps::ImageDocument::LayerMaskInit::RevealSelection);
    enableMaskAction(ui->actionMaskHideSelection,
                     Ps::ImageDocument::LayerMaskInit::HideSelection);
    ui->actionMaskDelete->setEnabled(true);
    ui->actionMaskDelete->setToolTip(tr("删除活动层的图层蒙版"));
    connect(ui->actionMaskDelete, &QAction::triggered, this, [this]() {
        Ps::ImageDocument *doc = m_session ? m_session->document() : nullptr;
        if (!doc || doc->activeLayerIndex() < 0)
            return;
        doc->removeLayerMask(doc->activeLayerIndex());
    });
    ui->actionMaskDisable->setEnabled(true);
    ui->actionMaskDisable->setToolTip(tr("启用/停用活动层蒙版（不删除灰度数据）"));
    connect(ui->actionMaskDisable, &QAction::triggered, this, [this]() {
        Ps::ImageDocument *doc = m_session ? m_session->document() : nullptr;
        if (!doc)
            return;
        Ps::Layer *layer = doc->activeLayer();
        if (!layer || !layer->mask() || layer->mask()->isNull())
            return;
        doc->setLayerMaskEnabled(doc->activeLayerIndex(), !layer->mask()->isEnabled());
    });
    ui->actionMaskApply->setEnabled(true);
    ui->actionMaskApply->setToolTip(tr("将蒙版乘进像素后删除蒙版"));
    connect(ui->actionMaskApply, &QAction::triggered, this, [this]() {
        Ps::ImageDocument *doc = m_session ? m_session->document() : nullptr;
        if (!doc || doc->activeLayerIndex() < 0)
            return;
        doc->applyLayerMask(doc->activeLayerIndex());
    });
    ui->actionMaskLinkUnlink->setEnabled(true);
    ui->actionMaskLinkUnlink->setToolTip(tr("链接/取消链接蒙版与图层（取消后可单独移动）"));
    connect(ui->actionMaskLinkUnlink, &QAction::triggered, this, [this]() {
        Ps::ImageDocument *doc = m_session ? m_session->document() : nullptr;
        if (!doc)
            return;
        Ps::Layer *layer = doc->activeLayer();
        if (!layer || !layer->mask() || layer->mask()->isNull())
            return;
        doc->setLayerMaskLinked(doc->activeLayerIndex(), !layer->mask()->isLinked());
    });

    ui->actionExportAs->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+Shift+W")));
    ui->actionStepForward->setShortcuts({
        QKeySequence(QStringLiteral("Ctrl+Shift+Z")),
        QKeySequence(QStringLiteral("Ctrl+Y")),
    });

    // 画笔直径 [ / ]（对齐 PS）；Shift 步进 10
    auto *brushDown = new QShortcut(QKeySequence(Qt::Key_BracketLeft), this);
    auto *brushUp = new QShortcut(QKeySequence(Qt::Key_BracketRight), this);
    auto *brushDownFast = new QShortcut(QKeySequence(Qt::SHIFT | Qt::Key_BracketLeft), this);
    auto *brushUpFast = new QShortcut(QKeySequence(Qt::SHIFT | Qt::Key_BracketRight), this);
    const auto nudgeBrush = [this](int delta) {
        const int next = ui->toolOptionsBar->brushDiameter() + delta;
        ui->toolOptionsBar->setBrushDiameter(next);
        onBrushDiameterChanged(ui->toolOptionsBar->brushDiameter());
    };
    connect(brushDown, &QShortcut::activated, this, [nudgeBrush]() { nudgeBrush(-1); });
    connect(brushUp, &QShortcut::activated, this, [nudgeBrush]() { nudgeBrush(1); });
    connect(brushDownFast, &QShortcut::activated, this, [nudgeBrush]() { nudgeBrush(-10); });
    connect(brushUpFast, &QShortcut::activated, this, [nudgeBrush]() { nudgeBrush(10); });

    const auto typingInField = [this]() -> bool {
        QWidget *w = focusWidget();
        return qobject_cast<QLineEdit *>(w) || qobject_cast<QAbstractSpinBox *>(w)
               || qobject_cast<QTextEdit *>(w);
    };
    auto *swapColors = new QShortcut(QKeySequence(Qt::Key_X), this);
    auto *defaultColors = new QShortcut(QKeySequence(Qt::Key_D), this);
    connect(swapColors, &QShortcut::activated, this, [this, typingInField]() {
        if (!typingInField())
            ui->toolBox->swapColors();
    });
    connect(defaultColors, &QShortcut::activated, this, [this, typingInField]() {
        if (!typingInField())
            ui->toolBox->resetDefaultColors();
    });

    // —— 选择（矩形选区已实现：全选 / 取消 / 反选）——
    ui->actionSelectAll->setEnabled(true);
    ui->actionSelectAll->setToolTip(tr("选择整幅画布"));
    ui->actionSelectDeselect->setEnabled(true);
    ui->actionSelectDeselect->setToolTip(tr("取消当前选区"));
    ui->actionSelectInverse->setEnabled(true);
    ui->actionSelectInverse->setToolTip(tr("反转选区"));
    connect(ui->actionSelectAll, &QAction::triggered, this, &MainWindow::onSelectAll);
    connect(ui->actionSelectDeselect, &QAction::triggered, this, &MainWindow::onSelectDeselect);
    connect(ui->actionSelectInverse, &QAction::triggered, this, &MainWindow::onSelectInverse);

    // —— 编辑：清除 / 填充 / 撤销 / 重做 ——
    ui->actionClear->setEnabled(true);
    ui->actionClear->setToolTip(tr("清除选区或整层像素（透明）"));
    ui->actionFill->setEnabled(true);
    ui->actionFill->setToolTip(tr("用前景色填充选区或整层"));
    connect(ui->actionClear, &QAction::triggered, this, &MainWindow::onClear);
    connect(ui->actionFill, &QAction::triggered, this, &MainWindow::onFill);

    // 编辑→自由变换（Ctrl+T）；对照 PS Free Transform / GIMP Unified Transform
    ui->actionFreeTransform->setEnabled(true);
    ui->actionFreeTransform->setToolTip(tr("自由变换活动层内容：缩放/旋转/扭曲；Enter 确认，Esc 取消"));
    connect(ui->actionFreeTransform, &QAction::triggered, this, &MainWindow::onFreeTransform);

    // —— 图像→调整：亮度/对比度（非破坏滤镜节点，无对话框用默认参数）——
    ui->actionAdjBrightness->setEnabled(true);
    ui->actionAdjBrightness->setToolTip(tr("给活动层追加亮度/对比度滤镜（非破坏，可重复叠加）"));
    connect(ui->actionAdjBrightness, &QAction::triggered, this, &MainWindow::onBrightnessContrast);

    auto enableStyleAction = [this](QAction *a, Ps::LayerStyleKind kind) {
        a->setEnabled(true);
        a->setToolTip(tr("给活动层追加/启用该样式（非破坏）"));
        connect(a, &QAction::triggered, this, [this, kind]() {
            onEnsureLayerStyle(static_cast<int>(kind));
        });
    };
    enableStyleAction(ui->actionLSDropShadow, Ps::LayerStyleKind::DropShadow);
    enableStyleAction(ui->actionLSInnerShadow, Ps::LayerStyleKind::InnerShadow);
    enableStyleAction(ui->actionLSOuterGlow, Ps::LayerStyleKind::OuterGlow);
    enableStyleAction(ui->actionLSInnerGlow, Ps::LayerStyleKind::InnerGlow);
    enableStyleAction(ui->actionLSStroke, Ps::LayerStyleKind::Stroke);
    enableStyleAction(ui->actionLSColorOverlay, Ps::LayerStyleKind::ColorOverlay);
    ui->actionLSClear->setEnabled(true);
    ui->actionLSClear->setToolTip(tr("清除活动层全部图层样式"));
    connect(ui->actionLSClear, &QAction::triggered, this, &MainWindow::onClearLayerStyles);

    ui->actionLSCopy->setEnabled(true);
    ui->actionLSCopy->setToolTip(tr("将活动层样式拷贝到样式剪贴板"));
    connect(ui->actionLSCopy, &QAction::triggered, this, &MainWindow::onCopyLayerStyles);
    ui->actionLSPaste->setEnabled(true);
    ui->actionLSPaste->setToolTip(tr("将样式剪贴板应用到活动层"));
    connect(ui->actionLSPaste, &QAction::triggered, this, &MainWindow::onPasteLayerStyles);

    ui->actionUndo->setEnabled(false);
    ui->actionUndo->setToolTip(tr("还原"));
    ui->actionStepForward->setEnabled(false);
    ui->actionStepForward->setToolTip(tr("重做"));
    connect(ui->actionUndo, &QAction::triggered, this, &MainWindow::onUndo);
    connect(ui->actionStepForward, &QAction::triggered, this, &MainWindow::onRedo);

    // —— 视图（缩放已实现）——
    connect(ui->actionZoomFit, &QAction::triggered, this, &MainWindow::onZoomFit);
    connect(ui->actionZoomActual, &QAction::triggered, this, &MainWindow::onZoomActual);
    connect(ui->actionZoomIn, &QAction::triggered, this, &MainWindow::onZoomIn);
    connect(ui->actionZoomOut, &QAction::triggered, this, &MainWindow::onZoomOut);

    // —— 窗口：显隐右侧面板 ——
    connect(ui->actionWindowLayers, &QAction::toggled, this, &MainWindow::onToggleDockPanel);
    // 颜色/属性/信息各自是一块独立面板，直接切显隐
    connect(ui->actionWindowColor, &QAction::toggled, ui->colorsPanel, &QWidget::setVisible);
    connect(ui->actionWindowProperties, &QAction::toggled, ui->propertiesPanel, &QWidget::setVisible);
    connect(ui->actionWindowInfo, &QAction::toggled, ui->infoPanel, &QWidget::setVisible);

    // —— 帮助 ——
    connect(ui->actionHelpAbout, &QAction::triggered, this, &MainWindow::onAbout);
}

void MainWindow::setupSession()
{
    // 一次性交付：此后文档变化全部由 AppSession 广播，无需在此逐个转发
    ui->canvasWorkspace->setSession(m_session);
    ui->dockPanel->setSession(m_session);
    ui->propertiesPanel->setSession(m_session); // 「属性」页展示真实文档/图层数据
    ui->infoPanel->setSession(m_session);
    connect(m_session, &Ps::AppSession::documentChanged,
            this, &MainWindow::onDocumentChanged);
}

void MainWindow::onDocumentChanged(Ps::ImageDocument *doc)
{
    if (m_historyConn) {
        disconnect(m_historyConn);
        m_historyConn = {};
    }
    if (m_docStatusConn) {
        disconnect(m_docStatusConn);
        m_docStatusConn = {};
    }
    if (doc) {
        m_historyConn = connect(&doc->history(), &Ps::HistoryStack::changed,
                                this, &MainWindow::updateUndoRedoActions);
        // 像素/属性/结构变化都会标脏并走 contentChanged → 刷新 * 与路径
        m_docStatusConn = connect(doc, &Ps::ImageDocument::contentChanged,
                                  this, &MainWindow::refreshDocumentPathStatus);
    }
    updateUndoRedoActions();
    refreshDocumentPathStatus();
}

void MainWindow::refreshDocumentPathStatus()
{
    Ps::ImageDocument *doc = m_session ? m_session->document() : nullptr;
    if (!doc) {
        setWindowTitle(tr("PhotoshopLite"));
        statusBar()->clearMessage();
        return;
    }

    QString pathText = doc->filePath().isEmpty()
                           ? tr("未标题-1")
                           : QDir::toNativeSeparators(doc->filePath());
    if (doc->isDirty())
        pathText.prepend(QLatin1Char('*'));

    setWindowTitle(tr("%1 - PhotoshopLite").arg(pathText));
    // timeout 0：常驻显示路径（有临时 flash 时会被盖住，结束后再刷回来）
    statusBar()->showMessage(pathText);
}

void MainWindow::flashStatusMessage(const QString &message, int ms)
{
    statusBar()->showMessage(message, ms);
    QTimer::singleShot(ms, this, [this]() { refreshDocumentPathStatus(); });
}

void MainWindow::updateUndoRedoActions()
{
    Ps::ImageDocument *doc = m_session ? m_session->document() : nullptr;
    CanvasView *cv = ui->canvasWorkspace->canvasView();
    Ps::TransformTool *ft = nullptr;
    if (Ps::ToolManager *tm = cv->toolManager())
        ft = qobject_cast<Ps::TransformTool *>(tm->tool(Ps::ToolId::FreeTransform));
    const bool ftSession = ft && ft->isSessionActive();

    // 变换会话中：还原/前进只针对本次变换内的步骤，不退出会话、不动文档历史
    const bool canUndo = ftSession ? ft->canSessionUndo()
                                   : (doc && doc->history().canUndo());
    const bool canRedo = ftSession ? ft->canSessionRedo()
                                   : (doc && doc->history().canRedo());
    ui->actionUndo->setEnabled(canUndo);
    ui->actionStepForward->setEnabled(canRedo);
    if (ftSession && canUndo)
        ui->actionUndo->setText(tr("还原(&U) 变换步骤"));
    else if (ftSession)
        ui->actionUndo->setText(tr("还原(&U)"));
    else if (canUndo)
        ui->actionUndo->setText(tr("还原(&U) %1").arg(doc->history().undoText()));
    else
        ui->actionUndo->setText(tr("还原(&U)"));
    if (ftSession && canRedo)
        ui->actionStepForward->setText(tr("向前一步 变换步骤"));
    else if (canRedo && !ftSession)
        ui->actionStepForward->setText(tr("向前一步 %1").arg(doc->history().redoText()));
    else
        ui->actionStepForward->setText(tr("向前一步"));
}

void MainWindow::onUndo()
{
    CanvasView *cv = ui->canvasWorkspace->canvasView();
    if (Ps::ToolManager *tm = cv->toolManager()) {
        if (auto *ft = qobject_cast<Ps::TransformTool *>(tm->tool(Ps::ToolId::FreeTransform))) {
            if (ft->isSessionActive()) {
                if (ft->undoSessionStep())
                    flashStatusMessage(tr("已还原上一步变换"));
                return;
            }
        }
    }
    if (Ps::ImageDocument *doc = m_session ? m_session->document() : nullptr)
        doc->undo();
}

void MainWindow::onRedo()
{
    CanvasView *cv = ui->canvasWorkspace->canvasView();
    if (Ps::ToolManager *tm = cv->toolManager()) {
        if (auto *ft = qobject_cast<Ps::TransformTool *>(tm->tool(Ps::ToolId::FreeTransform))) {
            if (ft->isSessionActive()) {
                if (ft->redoSessionStep())
                    flashStatusMessage(tr("已重做变换步骤"));
                return;
            }
        }
    }
    if (Ps::ImageDocument *doc = m_session ? m_session->document() : nullptr)
        doc->redo();
}

void MainWindow::setupToolbox()
{
    CanvasView *canvas = ui->canvasWorkspace->canvasView();

    // 工具箱 → 选项栏 + 画布（绘制/抓手/缩放）
    connect(ui->toolBox, &ToolBox::toolChanged, this, &MainWindow::onToolChanged);
    connect(ui->toolBox, &ToolBox::foregroundColorChanged,
            this, &MainWindow::onForegroundColorChanged);
    connect(ui->toolBox, &ToolBox::backgroundColorChanged,
            this, &MainWindow::onBackgroundColorChanged);

    // 吸管 → 工具箱颜色（再经既有双向同步到 ColorsPanel / 画布）
    connect(canvas, &CanvasView::foregroundPicked,
            ui->toolBox, &ToolBox::setForegroundColor);
    connect(canvas, &CanvasView::backgroundPicked,
            ui->toolBox, &ToolBox::setBackgroundColor);

    // 拖放文件：对照 GIMP gimpdisplayshell-dnd（有文档→置入图层）
    connect(canvas, &CanvasView::filesDropped,
            this, &MainWindow::onCanvasFilesDropped);

    // 信息面板：光标 XY + 投影取样（RGB/CMYK）；默认隐藏，窗口→信息 / F8
    connect(canvas, &CanvasView::cursorImagePosChanged, this,
            [this, canvas](const QPointF &imagePos, bool inside) {
                const QColor sample = inside ? canvas->sampleProjectionPixel(imagePos)
                                             : QColor();
                ui->infoPanel->setCursorInfo(imagePos, inside, sample);
            });

    // 右侧颜色面板 ↔ 工具箱前/背景色（双向，避免回环靠相等短路）
    connect(ui->colorsPanel, &ColorsPanel::foregroundColorChanged,
            ui->toolBox, &ToolBox::setForegroundColor);
    connect(ui->colorsPanel, &ColorsPanel::backgroundColorChanged,
            ui->toolBox, &ToolBox::setBackgroundColor);
    connect(ui->toolBox, &ToolBox::foregroundColorChanged,
            ui->colorsPanel, &ColorsPanel::setForegroundColor);
    connect(ui->toolBox, &ToolBox::backgroundColorChanged,
            ui->colorsPanel, &ColorsPanel::setBackgroundColor);
    connect(ui->toolOptionsBar, &ToolOptionsBar::brushDiameterChanged,
            this, &MainWindow::onBrushDiameterChanged);
    connect(ui->toolOptionsBar, &ToolOptionsBar::fillOptionsChanged, this, [this]() {
        ui->canvasWorkspace->canvasView()->setFillOptions(
            ui->toolOptionsBar->fillTolerance(),
            ui->toolOptionsBar->fillContiguous(),
            ui->toolOptionsBar->fillSource(),
            ui->toolOptionsBar->fillOpacityPercent() / 100.0);
    });
    connect(ui->toolOptionsBar, &ToolOptionsBar::selectionFloodOptionsChanged, this, [this]() {
        ui->canvasWorkspace->canvasView()->setSelectionFloodOptions(
            ui->toolOptionsBar->selTolerance(),
            ui->toolOptionsBar->selContiguous(),
            ui->toolOptionsBar->selSampleMerged());
    });
    connect(ui->toolOptionsBar, &ToolOptionsBar::magneticLassoOptionsChanged, this, [this]() {
        ui->canvasWorkspace->canvasView()->setMagneticLassoOptions(
            ui->toolOptionsBar->magneticWidth(),
            ui->toolOptionsBar->magneticContrast(),
            ui->toolOptionsBar->magneticFrequency());
    });
    connect(ui->toolOptionsBar, &ToolOptionsBar::gradientOptionsChanged, this, [this]() {
        ui->canvasWorkspace->canvasView()->setGradientOptions(
            ui->toolOptionsBar->gradientType(),
            ui->toolOptionsBar->gradientOpacityPercent() / 100.0,
            ui->toolOptionsBar->gradientOffsetPercent(),
            ui->toolOptionsBar->gradientReverse(),
            ui->toolOptionsBar->gradientDither());
    });
    connect(ui->toolOptionsBar, &ToolOptionsBar::cloneStampOptionsChanged, this, [this]() {
        ui->canvasWorkspace->canvasView()->setCloneStampOptions(
            ui->toolOptionsBar->cloneAlign(),
            ui->toolOptionsBar->cloneSampleMerged());
    });
    connect(ui->toolOptionsBar, &ToolOptionsBar::shapeOptionsChanged, this, [this]() {
        ui->canvasWorkspace->canvasView()->setShapeOptions(
            ui->toolOptionsBar->shapeFill(),
            ui->toolOptionsBar->shapeStroke(),
            ui->toolOptionsBar->shapeStrokeWidth(),
            ui->toolOptionsBar->shapeCornerRadius(),
            ui->toolOptionsBar->shapeAntialias());
    });

    // 自由变换：选项栏 ↔ TransformTool；右键菜单
    if (Ps::ToolManager *tm = canvas->toolManager()) {
        if (auto *ft = qobject_cast<Ps::TransformTool *>(tm->tool(Ps::ToolId::FreeTransform))) {
            connect(ft, &Ps::TransformTool::paramsChanged, this, &MainWindow::syncFreeTransformOptionsBar);
            connect(ft, &Ps::TransformTool::sessionChanged, this, [this](bool) {
                syncFreeTransformOptionsBar();
                updateUndoRedoActions();
            });
            connect(ft, &Ps::TransformTool::sessionHistoryChanged,
                    this, &MainWindow::updateUndoRedoActions);
            connect(ft, &Ps::TransformTool::contextMenuRequested,
                    this, &MainWindow::onFreeTransformContextMenu);
            // 对照 PS：Ctrl+T 是临时模式；确认/取消后回到移动工具，否则仍停在 FreeTransform，
            // 点击画布会重新开会话，无法点选其它图层。
            connect(ft, &Ps::TransformTool::returnToMoveRequested, this, [this]() {
                ui->toolBox->setCurrentTool(Ps::ToolId::Move);
            });
        }
    }
    connect(ui->toolOptionsBar, &ToolOptionsBar::freeTransformParamsEdited, this,
            [this](qreal x, qreal y, qreal w, qreal h, qreal ang, qreal sh, qreal sv) {
                CanvasView *cv = ui->canvasWorkspace->canvasView();
                auto *ft = qobject_cast<Ps::TransformTool *>(
                    cv->toolManager() ? cv->toolManager()->tool(Ps::ToolId::FreeTransform) : nullptr);
                if (ft && ft->isSessionActive())
                    ft->applyNumericParams(x, y, w, h, ang, sh, sv);
            });
    connect(ui->toolOptionsBar, &ToolOptionsBar::freeTransformLinkAspectChanged, this,
            [this](bool linked) {
                CanvasView *cv = ui->canvasWorkspace->canvasView();
                auto *ft = qobject_cast<Ps::TransformTool *>(
                    cv->toolManager() ? cv->toolManager()->tool(Ps::ToolId::FreeTransform) : nullptr);
                if (ft)
                    ft->setLinkAspect(linked);
            });
    connect(ui->toolOptionsBar, &ToolOptionsBar::freeTransformInterpolationChanged, this,
            [this](int idx) {
                CanvasView *cv = ui->canvasWorkspace->canvasView();
                auto *ft = qobject_cast<Ps::TransformTool *>(
                    cv->toolManager() ? cv->toolManager()->tool(Ps::ToolId::FreeTransform) : nullptr);
                if (!ft)
                    return;
                using I = Ps::TransformInterpolation;
                ft->setInterpolation(idx <= 0 ? I::Nearest : (idx == 1 ? I::Bilinear : I::Bicubic));
            });
    connect(ui->toolOptionsBar, &ToolOptionsBar::freeTransformCommitClicked, this, [this]() {
        CanvasView *cv = ui->canvasWorkspace->canvasView();
        auto *ft = qobject_cast<Ps::TransformTool *>(
            cv->toolManager() ? cv->toolManager()->tool(Ps::ToolId::FreeTransform) : nullptr);
        if (ft && ft->isSessionActive()) {
            ft->commitFromUi(cv->toolContext());
            flashStatusMessage(tr("已应用自由变换"));
        }
    });
    connect(ui->toolOptionsBar, &ToolOptionsBar::freeTransformCancelClicked, this, [this]() {
        CanvasView *cv = ui->canvasWorkspace->canvasView();
        auto *ft = qobject_cast<Ps::TransformTool *>(
            cv->toolManager() ? cv->toolManager()->tool(Ps::ToolId::FreeTransform) : nullptr);
        if (ft && ft->isSessionActive()) {
            ft->cancelFromUi(cv->toolContext());
            flashStatusMessage(tr("已取消自由变换"));
        }
    });

    connect(ui->toolOptionsBar, &ToolOptionsBar::homeClicked,
            this, &MainWindow::onShowHomeScreen);

    // 用工具箱当前值对齐其余组件（这里同步不经过槽，避免半初始化状态）
    const Ps::ToolId tool = ui->toolBox->currentTool();
    ui->toolOptionsBar->setCurrentTool(tool);
    canvas->setCurrentTool(tool);
    canvas->setForegroundColor(ui->toolBox->foregroundColor());
    canvas->setBackgroundColor(ui->toolBox->backgroundColor());
    ui->colorsPanel->setForegroundColor(ui->toolBox->foregroundColor());
    ui->colorsPanel->setBackgroundColor(ui->toolBox->backgroundColor());
    canvas->setBrushDiameter(ui->toolOptionsBar->brushDiameter());
    canvas->setFillOptions(ui->toolOptionsBar->fillTolerance(),
                           ui->toolOptionsBar->fillContiguous(),
                           ui->toolOptionsBar->fillSource(),
                           ui->toolOptionsBar->fillOpacityPercent() / 100.0);
    canvas->setSelectionFloodOptions(ui->toolOptionsBar->selTolerance(),
                                     ui->toolOptionsBar->selContiguous(),
                                     ui->toolOptionsBar->selSampleMerged());
    canvas->setMagneticLassoOptions(ui->toolOptionsBar->magneticWidth(),
                                    ui->toolOptionsBar->magneticContrast(),
                                    ui->toolOptionsBar->magneticFrequency());
    canvas->setGradientOptions(ui->toolOptionsBar->gradientType(),
                               ui->toolOptionsBar->gradientOpacityPercent() / 100.0,
                               ui->toolOptionsBar->gradientOffsetPercent(),
                               ui->toolOptionsBar->gradientReverse(),
                               ui->toolOptionsBar->gradientDither());
    canvas->setCloneStampOptions(ui->toolOptionsBar->cloneAlign(),
                                 ui->toolOptionsBar->cloneSampleMerged());
    canvas->setShapeOptions(ui->toolOptionsBar->shapeFill(),
                            ui->toolOptionsBar->shapeStroke(),
                            ui->toolOptionsBar->shapeStrokeWidth(),
                            ui->toolOptionsBar->shapeCornerRadius(),
                            ui->toolOptionsBar->shapeAntialias());
    ui->infoPanel->setToolHint(ui->toolOptionsBar->currentHint());
}

void MainWindow::setupHomeStack()
{
    // 栈页已在 mainwindow.ui：0=workspacePage，1=homeScreen
    connect(ui->homeScreen, &HomeScreen::newFileRequested, this, &MainWindow::onNewDocument);
    connect(ui->homeScreen, &HomeScreen::openFileRequested, this, &MainWindow::onOpenDocument);
    connect(ui->homeScreen, &HomeScreen::backToWorkspaceRequested, this, &MainWindow::onShowWorkspace);
    connect(ui->homeScreen, &HomeScreen::recentFileActivated, this, [this](const QString &path) {
        openPath(path);
    });
}

void MainWindow::onShowHomeScreen()
{
    ui->homeScreen->refreshRecent();
    rebuildRecentMenu();
    ui->mainStack->setCurrentWidget(ui->homeScreen);
}

void MainWindow::onShowWorkspace()
{
    ui->mainStack->setCurrentIndex(0);
    // 进入工作区后键盘应落在画布（否则焦点常在选项条「家」按钮上，空格会回主页）
    ui->canvasWorkspace->canvasView()->setFocus(Qt::OtherFocusReason);
}

void MainWindow::createInitialDocument()
{
    m_session->setDocument(Ps::ImageDocument::createBlank(800, 600, Qt::white));
}

void MainWindow::onToolChanged(Ps::ToolId id)
{
    ui->toolOptionsBar->setCurrentTool(id);
    CanvasView *canvas = ui->canvasWorkspace->canvasView();
    canvas->setCurrentTool(id);
    // 点工具箱（NoFocus）后 Windows/Qt 常把键盘焦点清掉，空格到不了画布；
    // 选工具就是为了在画布上用，焦点应回到画布。
    canvas->setFocus(Qt::OtherFocusReason);
    const QString hint = ui->toolOptionsBar->currentHint();
    ui->infoPanel->setToolHint(hint);
    // 工具提示语显示在状态栏（选项条里只放参数，对齐 PS）
    flashStatusMessage(hint, 4000);
}

void MainWindow::onBrushDiameterChanged(int diameter)
{
    ui->canvasWorkspace->canvasView()->setBrushDiameter(diameter);
}

void MainWindow::onForegroundColorChanged(const QColor &color)
{
    ui->canvasWorkspace->canvasView()->setForegroundColor(color);
}

void MainWindow::onBackgroundColorChanged(const QColor &color)
{
    ui->canvasWorkspace->canvasView()->setBackgroundColor(color);
}

void MainWindow::onNewDocument()
{
    // UI 阶段：弹出 PS 式新建对话框；确认后按宽高建空白文档（对照 GIMP image-new-dialog）
    NewDocumentDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QSize size = dialog.documentSize();
    m_session->setDocument(Ps::ImageDocument::createBlank(size.width(), size.height(), Qt::white));
    onShowWorkspace();
    flashStatusMessage(tr("已新建 %1×%2 文档").arg(size.width()).arg(size.height()), 3000);
}

void MainWindow::onOpenDocument()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        tr("打开"),
        QString(),
        tr("PhotoshopLite 工程 (*.pslite);;"
           "图像文件 (*.png *.jpg *.jpeg *.bmp *.webp);;"
           "所有文件 (*.*)"));
    if (path.isEmpty())
        return;
    openPath(path);
}

void MainWindow::onOpenAsSmartObject()
{
    // 对照 PS「打开为智能对象」/ GIMP「打开为链接图层」：新文档 + 链接层
    const QString path = QFileDialog::getOpenFileName(
        this,
        tr("打开为智能对象"),
        QString(),
        tr("图像文件 (*.png *.jpg *.jpeg *.bmp *.webp);;"
           "所有文件 (*.*)"));
    if (path.isEmpty())
        return;
    openAsSmartObjectPath(path);
}

void MainWindow::onPlaceEmbedded()
{
    // 对照 GIMP「打开为图层」/ PS「置入嵌入的对象」：解码位图 → 当前文档新层
    const QString path = QFileDialog::getOpenFileName(
        this,
        tr("置入嵌入的对象"),
        QString(),
        tr("图像文件 (*.png *.jpg *.jpeg *.bmp *.webp);;"
           "所有文件 (*.*)"));
    if (path.isEmpty())
        return;
    placePath(path);
}

void MainWindow::onPlaceLinked()
{
    // 对照 GIMP「打开为链接图层」/ PS「置入链接的对象」
    const QString path = QFileDialog::getOpenFileName(
        this,
        tr("置入链接的对象"),
        QString(),
        tr("图像文件 (*.png *.jpg *.jpeg *.bmp *.webp);;"
           "所有文件 (*.*)"));
    if (path.isEmpty())
        return;
    placeLinkedPath(path);
}

void MainWindow::onUpdateLinkedLayer()
{
    Ps::ImageDocument *doc = m_session ? m_session->document() : nullptr;
    if (!doc)
        return;
    Ps::Layer *layer = doc->activeLayer();
    if (!layer || !layer->isLinkedLayer()) {
        flashStatusMessage(tr("当前层不是链接图层"), 3000);
        return;
    }
    if (!doc->updateLinkedLayer(doc->activeLayerIndex())) {
        QMessageBox::warning(
            this, tr("更新链接"),
            tr("无法从源文件读取：\n%1").arg(layer->linkPath()));
        return;
    }
    flashStatusMessage(tr("已更新链接：%1").arg(layer->linkPath()), 4000);
}

void MainWindow::onRasterizeLinkedLayer()
{
    Ps::ImageDocument *doc = m_session ? m_session->document() : nullptr;
    if (!doc)
        return;
    Ps::Layer *layer = doc->activeLayer();
    if (!layer || !layer->isLinkedLayer()) {
        flashStatusMessage(tr("当前层不是链接图层"), 3000);
        return;
    }
    if (doc->rasterizeLinkedLayer(doc->activeLayerIndex()))
        flashStatusMessage(tr("已栅格化链接图层"), 3000);
}

QImage MainWindow::readRasterImage(const QString &path, QString *errorOut)
{
    QImageReader reader(path);
    reader.setAutoTransform(true); // 尊重 EXIF 方向
    QImage image = reader.read();
    if (image.isNull() && errorOut)
        *errorOut = reader.errorString();
    return image;
}

bool MainWindow::placePath(const QString &path)
{
    if (path.isEmpty())
        return false;

    // 无文档时与 GIMP 一致：先按「打开」建文档（file_open_dialog 无 image 分支）
    Ps::ImageDocument *doc = m_session ? m_session->document() : nullptr;
    if (!doc)
        return openPath(path);

    if (path.endsWith(QStringLiteral(".pslite"), Qt::CaseInsensitive)) {
        QMessageBox::information(
            this, tr("置入"),
            tr("工程文件请用「打开」；置入仅支持位图（对照 GIMP 打开为图层）。"));
        return false;
    }

    QString err;
    const QImage image = readRasterImage(path, &err);
    if (image.isNull()) {
        QMessageBox::warning(this, tr("置入失败"),
                             tr("无法读取：%1\n%2").arg(path, err));
        return false;
    }

    onShowWorkspace();
    const QString layerName = QFileInfo(path).completeBaseName();
    const int index = doc->placeImageAsLayer(image, layerName);
    if (index < 0) {
        QMessageBox::warning(this, tr("置入失败"), tr("无法将图像加入当前文档。"));
        return false;
    }

    flashStatusMessage(tr("已置入图层「%1」").arg(layerName), 4000);
    return true;
}

bool MainWindow::openAsSmartObjectPath(const QString &path)
{
    if (path.isEmpty())
        return false;

    if (path.endsWith(QStringLiteral(".pslite"), Qt::CaseInsensitive)) {
        QMessageBox::information(
            this, tr("打开为智能对象"),
            tr("工程文件请用「打开」；智能对象仅支持位图。"));
        return false;
    }

    QString err;
    const QImage image = readRasterImage(path, &err);
    if (image.isNull()) {
        QMessageBox::warning(this, tr("打开失败"),
                             tr("无法读取：%1\n%2").arg(path, err));
        return false;
    }

    // 始终新建文档（对照 PS Open as Smart Object；不追加到当前文档）
    const QString abs = QFileInfo(path).absoluteFilePath();
    auto newDoc = std::make_unique<Ps::ImageDocument>(image.width(), image.height());
    {
        Ps::ImageDocument::HistorySuppress suppress(*newDoc);
        auto layer = std::make_unique<Ps::Layer>(
            QFileInfo(path).completeBaseName(), image);
        layer->setLinkPathSilent(abs);
        const int index = newDoc->addLayer(std::move(layer));
        newDoc->setActiveLayerIndex(index);
    }
    newDoc->setFilePath(QString()); // 链接源 ≠ 工程路径
    newDoc->clearDirty();
    m_session->setDocument(std::move(newDoc));
    onShowWorkspace();
    flashStatusMessage(tr("已打开为智能对象：%1").arg(abs), 4000);
    return true;
}

bool MainWindow::placeLinkedPath(const QString &path)
{
    if (path.isEmpty())
        return false;

    if (path.endsWith(QStringLiteral(".pslite"), Qt::CaseInsensitive)) {
        QMessageBox::information(
            this, tr("置入链接"),
            tr("工程文件请用「打开」；链接置入仅支持位图。"));
        return false;
    }

    // 无文档：与「打开为智能对象」相同
    Ps::ImageDocument *doc = m_session ? m_session->document() : nullptr;
    if (!doc)
        return openAsSmartObjectPath(path);

    onShowWorkspace();
    const QString layerName = QFileInfo(path).completeBaseName();
    const int index = doc->placeLinkedImageAsLayer(
        QFileInfo(path).absoluteFilePath(), layerName);
    if (index < 0) {
        QMessageBox::warning(this, tr("置入链接失败"),
                             tr("无法读取：%1").arg(path));
        return false;
    }

    flashStatusMessage(tr("已置入链接图层「%1」").arg(layerName), 4000);
    return true;
}

void MainWindow::onCanvasFilesDropped(const QStringList &paths)
{
    if (paths.isEmpty())
        return;

    // 对照 GIMP：空显示→打开；已有文档→逐个打开为图层
    Ps::ImageDocument *doc = m_session ? m_session->document() : nullptr;
    if (!doc) {
        openPath(paths.first());
        return;
    }

    int ok = 0;
    for (const QString &path : paths) {
        if (placePath(path))
            ++ok;
    }
    if (ok > 1)
        flashStatusMessage(tr("已置入 %1 个文件为图层").arg(ok), 4000);
}

bool MainWindow::openPath(const QString &path)
{
    if (path.isEmpty())
        return false;

    if (path.endsWith(QStringLiteral(".pslite"), Qt::CaseInsensitive)) {
        QString err;
        auto doc = Ps::ProjectIo::load(path, &err);
        if (!doc) {
            QMessageBox::warning(this, tr("打开失败"), err);
            Ps::RecentDocuments::remove(path);
            rebuildRecentMenu();
            ui->homeScreen->refreshRecent();
            return false;
        }
        m_session->setDocument(std::move(doc));
        rememberRecent(path);
        onShowWorkspace();
        flashStatusMessage(tr("已打开工程：%1").arg(path), 4000);
        return true;
    }

    QString err;
    QImage image = readRasterImage(path, &err);
    if (image.isNull()) {
        QMessageBox::warning(this, tr("打开失败"),
                             tr("无法读取：%1\n%2").arg(path, err));
        Ps::RecentDocuments::remove(path);
        rebuildRecentMenu();
        ui->homeScreen->refreshRecent();
        return false;
    }

    // 栅格打开 = 单「背景」层（对照 GIMP file_open_image + 插件建 Background）
    auto doc = std::make_unique<Ps::ImageDocument>(image.width(), image.height());
    {
        Ps::ImageDocument::HistorySuppress suppress(*doc);
        auto layer = std::make_unique<Ps::Layer>(tr("背景"), image);
        const int index = doc->addLayer(std::move(layer));
        doc->setActiveLayerIndex(index);
    }
    doc->setFilePath(path);
    doc->clearDirty();

    m_session->setDocument(std::move(doc));
    Ps::RecentDocuments::add(path);
    Ps::RecentDocuments::setThumbnail(path, image);
    rebuildRecentMenu();
    onShowWorkspace();
    flashStatusMessage(tr("已打开：%1").arg(path), 4000);
    return true;
}

void MainWindow::rememberRecent(const QString &path)
{
    Ps::RecentDocuments::add(path);
    if (Ps::ImageDocument *doc = m_session ? m_session->document() : nullptr)
        Ps::RecentDocuments::setThumbnail(path, Ps::Compositor::composite(*doc));
    rebuildRecentMenu();
}

void MainWindow::rebuildRecentMenu()
{
    ui->menuOpenRecent->clear();
    const QStringList paths = Ps::RecentDocuments::paths();
    if (paths.isEmpty()) {
        ui->actionRecentPlaceholder->setText(tr("（无最近文件）"));
        ui->actionRecentPlaceholder->setEnabled(false);
        ui->menuOpenRecent->addAction(ui->actionRecentPlaceholder);
        return;
    }

    for (const QString &path : paths) {
        QAction *act = ui->menuOpenRecent->addAction(QFileInfo(path).fileName());
        act->setToolTip(path);
        connect(act, &QAction::triggered, this, [this, path]() { openPath(path); });
    }
}

bool MainWindow::saveDocumentTo(const QString &path)
{
    Ps::ImageDocument *doc = m_session ? m_session->document() : nullptr;
    if (!doc) {
        QMessageBox::information(this, tr("存储"), tr("当前没有文档。"));
        return false;
    }
    QString err;
    const bool asPsd = path.endsWith(QStringLiteral(".psd"), Qt::CaseInsensitive);
    const bool ok = asPsd ? Ps::PsdIo::save(*doc, path, &err)
                          : Ps::ProjectIo::save(*doc, path, &err);
    if (!ok) {
        QMessageBox::warning(this, tr("存储失败"), err);
        return false;
    }
    doc->setFilePath(path);
    doc->clearDirty();
    rememberRecent(path);
    refreshDocumentPathStatus();
    flashStatusMessage(
        asPsd ? tr("已存储 PSD（子集）：%1").arg(path)
              : tr("已存储工程：%1").arg(path),
        4000);
    return true;
}

bool MainWindow::saveDocumentAsDialog(bool forcePslite)
{
    Ps::ImageDocument *doc = m_session ? m_session->document() : nullptr;
    if (!doc) {
        QMessageBox::information(this, tr("存储"), tr("当前没有文档。"));
        return false;
    }

    const QString psliteFilter = tr("PhotoshopLite 工程 (*.pslite)");
    const QString psdFilter = tr("Photoshop 文件 (*.psd)");
    const QString filters = psliteFilter + QStringLiteral(";;") + psdFilter;

    QString selected = psliteFilter;
    QString suggested = doc->filePath();
    if (suggested.isEmpty()) {
        suggested = QStringLiteral("untitled.pslite");
    } else if (forcePslite
               && suggested.endsWith(QStringLiteral(".psd"), Qt::CaseInsensitive)) {
        suggested.chop(4);
        suggested += QStringLiteral(".pslite");
    }

    QString path = QFileDialog::getSaveFileName(
        this, tr("存储为"), suggested, filters, &selected);
    if (path.isEmpty())
        return false;

    // 以对话框所选过滤器为准；路径已带扩展名时也尊重扩展名
    const bool wantPsd = selected.contains(QStringLiteral("*.psd"), Qt::CaseInsensitive)
                         || path.endsWith(QStringLiteral(".psd"), Qt::CaseInsensitive);
    if (wantPsd) {
        if (path.endsWith(QStringLiteral(".pslite"), Qt::CaseInsensitive))
            path.chop(7);
        if (!path.endsWith(QStringLiteral(".psd"), Qt::CaseInsensitive))
            path += QStringLiteral(".psd");
    } else {
        if (path.endsWith(QStringLiteral(".psd"), Qt::CaseInsensitive))
            path.chop(4);
        if (!path.endsWith(QStringLiteral(".pslite"), Qt::CaseInsensitive))
            path += QStringLiteral(".pslite");
    }
    return saveDocumentTo(path);
}

void MainWindow::onSaveDocument()
{
    Ps::ImageDocument *doc = m_session ? m_session->document() : nullptr;
    if (!doc) {
        QMessageBox::information(this, tr("存储"), tr("当前没有文档。"));
        return;
    }
    // Ctrl+S：始终写 .pslite；无工程路径或当前是 .psd 时弹出「存储为」
    const QString path = doc->filePath();
    if (path.isEmpty()
        || !path.endsWith(QStringLiteral(".pslite"), Qt::CaseInsensitive)) {
        saveDocumentAsDialog(/*forcePslite=*/true);
        return;
    }
    saveDocumentTo(path);
}

void MainWindow::onSaveDocumentAs()
{
    saveDocumentAsDialog(/*forcePslite=*/false);
}

QString MainWindow::suggestExportPath(const QString &suffix) const
{
    Ps::ImageDocument *doc = m_session ? m_session->document() : nullptr;
    QString base = QStringLiteral("untitled");
    if (doc && !doc->filePath().isEmpty()) {
        const QFileInfo info(doc->filePath());
        base = info.completeBaseName();
        if (!info.absolutePath().isEmpty())
            return info.absolutePath() + QLatin1Char('/') + base + QLatin1Char('.') + suffix;
    }
    return base + QLatin1Char('.') + suffix;
}

bool MainWindow::exportCompositeTo(const QString &path)
{
    Ps::ImageDocument *doc = m_session ? m_session->document() : nullptr;
    if (!doc) {
        QMessageBox::information(this, tr("导出"), tr("当前没有文档。"));
        return false;
    }
    QString err;
    if (!Ps::RasterIo::exportFile(*doc, path, &err)) {
        QMessageBox::warning(this, tr("导出失败"), err);
        return false;
    }
    flashStatusMessage(tr("已导出：%1").arg(QDir::toNativeSeparators(path)), 4000);
    return true;
}

void MainWindow::onExportPng()
{
    Ps::ImageDocument *doc = m_session ? m_session->document() : nullptr;
    if (!doc) {
        QMessageBox::information(this, tr("导出"), tr("当前没有文档。"));
        return;
    }
    if (doc->filePath().isEmpty()) {
        QString path = QFileDialog::getSaveFileName(
            this, tr("快速导出为 PNG"), suggestExportPath(QStringLiteral("png")),
            tr("PNG 图像 (*.png)"));
        if (path.isEmpty())
            return;
        if (!path.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive))
            path += QStringLiteral(".png");
        exportCompositeTo(path);
        return;
    }
    exportCompositeTo(suggestExportPath(QStringLiteral("png")));
}

void MainWindow::onExportAs()
{
    const QString pngFilter = tr("PNG 图像 (*.png)");
    const QString jpegFilter = tr("JPEG 图像 (*.jpg *.jpeg)");
    QString selected = pngFilter;
    QString path = QFileDialog::getSaveFileName(
        this, tr("导出为"), suggestExportPath(QStringLiteral("png")),
        pngFilter + QStringLiteral(";;") + jpegFilter, &selected);
    if (path.isEmpty())
        return;

    const bool wantJpeg = selected.contains(QStringLiteral("*.jpg"), Qt::CaseInsensitive)
                          || path.endsWith(QStringLiteral(".jpg"), Qt::CaseInsensitive)
                          || path.endsWith(QStringLiteral(".jpeg"), Qt::CaseInsensitive);
    if (wantJpeg) {
        if (path.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive))
            path.chop(4);
        if (!path.endsWith(QStringLiteral(".jpg"), Qt::CaseInsensitive)
            && !path.endsWith(QStringLiteral(".jpeg"), Qt::CaseInsensitive))
            path += QStringLiteral(".jpg");
    } else {
        if (path.endsWith(QStringLiteral(".jpg"), Qt::CaseInsensitive))
            path.chop(4);
        else if (path.endsWith(QStringLiteral(".jpeg"), Qt::CaseInsensitive))
            path.chop(5);
        if (!path.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive))
            path += QStringLiteral(".png");
    }
    exportCompositeTo(path);
}

void MainWindow::onNewLayer()
{
    Ps::ImageDocument *doc = m_session ? m_session->document() : nullptr;
    if (!doc) {
        QMessageBox::information(this, tr("新建图层"), tr("当前没有打开的文档。"));
        return;
    }

    const int index = doc->addTransparentLayer();
    if (index < 0)
        return;

    const Ps::Layer *layer = doc->layers().layerAt(index);
    flashStatusMessage(tr("已新建：%1").arg(layer ? layer->name() : tr("图层")), 3000);
}

void MainWindow::onDuplicateLayer()
{
    // 【功能】图层→复制图层；对照 GIMP layers-duplicate / 图层面板右键
    Ps::ImageDocument *doc = m_session ? m_session->document() : nullptr;
    if (!doc) {
        QMessageBox::information(this, tr("复制图层"), tr("当前没有打开的文档。"));
        return;
    }
    const int src = doc->activeLayerIndex();
    if (src < 0) {
        QMessageBox::information(this, tr("复制图层"), tr("请先选中一个图层。"));
        return;
    }
    const int index = doc->duplicateLayer(src);
    if (index < 0)
        return;
    const Ps::Layer *layer = doc->layers().layerAt(index);
    flashStatusMessage(tr("已复制：%1").arg(layer ? layer->name() : tr("图层")), 3000);
}

void MainWindow::onDeleteLayer()
{
    Ps::ImageDocument *doc = m_session ? m_session->document() : nullptr;
    if (!doc) {
        QMessageBox::information(this, tr("删除图层"), tr("当前没有打开的文档。"));
        return;
    }
    if (!doc->removeLayer(doc->activeLayerIndex())) {
        QMessageBox::information(this, tr("删除图层"), tr("至少保留一个图层。"));
        return;
    }
    flashStatusMessage(tr("已删除图层"), 3000);
}

void MainWindow::onImageSize()
{
    Ps::ImageDocument *doc = m_session ? m_session->document() : nullptr;
    if (!doc) {
        QMessageBox::information(this, tr("图像大小"), tr("当前没有打开的文档。"));
        return;
    }

    const QImage preview = Ps::Compositor::composite(*doc);
    ImageSizeDialog dialog(doc, preview, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QSize size = dialog.resultPixelSize();
    if (!dialog.resampleEnabled()) {
        // 未勾选重新采样：像素不变（PPI 尚未写入 domain）
        flashStatusMessage(tr("未重新采样：像素尺寸保持 %1×%2")
                               .arg(doc->width())
                               .arg(doc->height()),
                           3000);
        return;
    }
    if (size.width() == doc->width() && size.height() == doc->height())
        return;

    doc->scaleImage(size.width(), size.height());
    ui->canvasWorkspace->canvasView()->zoomFit();
    flashStatusMessage(
        tr("图像大小已改为 %1×%2").arg(size.width()).arg(size.height()), 3000);
}

void MainWindow::onCanvasSize()
{
    Ps::ImageDocument *doc = m_session ? m_session->document() : nullptr;
    if (!doc) {
        QMessageBox::information(this, tr("画布大小"), tr("当前没有打开的文档。"));
        return;
    }

    CanvasSizeDialog dialog(doc, ui->toolBox->foregroundColor(),
                            ui->toolBox->backgroundColor(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QSize size = dialog.resultPixelSize();
    if (size.width() == doc->width() && size.height() == doc->height())
        return;

    doc->resizeCanvas(size.width(), size.height(), dialog.anchorRow(), dialog.anchorCol(),
                      dialog.extensionColor());
    ui->canvasWorkspace->canvasView()->zoomFit();
    flashStatusMessage(
        tr("画布大小已改为 %1×%2").arg(size.width()).arg(size.height()), 3000);
}

void MainWindow::onZoomFit()
{
    ui->canvasWorkspace->canvasView()->zoomFit();
}

void MainWindow::onZoomActual()
{
    ui->canvasWorkspace->canvasView()->zoomActual();
}

void MainWindow::onZoomIn()
{
    CanvasView *canvas = ui->canvasWorkspace->canvasView();
    canvas->setZoom(canvas->zoom() * 1.25);
}

void MainWindow::onZoomOut()
{
    CanvasView *canvas = ui->canvasWorkspace->canvasView();
    canvas->setZoom(canvas->zoom() / 1.25);
}

void MainWindow::onToggleDockPanel(bool visible)
{
    ui->dockPanel->setVisible(visible);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    // 真实高度要等 resize 之后才知道，所以这里按比例分配，直到用户自己拖过为止
    if (!m_rightColumnUserSized)
        applyDefaultRightColumnSizes();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // 关闭统一收口在这里：右上角 ×、文件→退出、Alt+F4、以及**标题栏 logo 双击**
    // （Windows 把它变成 SC_CLOSE），到最后都是这一个 QCloseEvent，所以只问一次即可。
    // 【为什么不去挡标题栏 logo 的点击】PS 与 Windows 的行为一致（单击弹系统菜单、
    // 双击请求关闭），那是平台约定，不是 bug；要改的是「双击就直接没了」这件事本身。
    if (m_closeConfirming) {
        event->ignore(); // 已经在问用户了，别再叠一层对话框
        return;
    }

    QMessageBox box(this);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(tr("退出 PhotoshopLite"));
    box.setText(tr("确定要退出 PhotoshopLite 吗？"));
    const Ps::ImageDocument *document = m_session ? m_session->document() : nullptr;
    QPushButton *saveQuitButton = nullptr;
    if (document && document->isDirty()) {
        box.setInformativeText(tr("当前文档有未保存的修改。"));
        saveQuitButton = box.addButton(tr("存储并退出"), QMessageBox::AcceptRole);
        box.addButton(tr("不存储退出"), QMessageBox::DestructiveRole);
    } else {
        box.addButton(tr("退出"), QMessageBox::AcceptRole);
    }
    QPushButton *cancelButton = box.addButton(tr("取消"), QMessageBox::RejectRole);
    box.setDefaultButton(cancelButton); // 默认停在「取消」，避免回车/手滑直接退出

    m_closeConfirming = true;
    box.exec();
    m_closeConfirming = false;

    const QAbstractButton *clicked = box.clickedButton();
    if (clicked == cancelButton || !clicked) {
        event->ignore();
        return;
    }
    if (clicked == saveQuitButton) {
        if (document->filePath().isEmpty()) {
            if (!saveDocumentAsDialog()) {
                event->ignore();
                return;
            }
        } else if (!saveDocumentTo(document->filePath())) {
            event->ignore();
            return;
        }
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::applyDefaultRightColumnSizes()
{
    QSplitter *splitter = ui->rightSplitter;
    const int usable = splitter->height() - splitter->handleWidth() * (splitter->count() - 1);
    if (usable <= 0)
        return; // 还没拿到真实高度，下一次 resize 再说

    // 信息面板默认隐藏；可见段按「颜色 / 属性 / 图层」比例分配（图层最长）
    QList<int> sizes;
    sizes.reserve(splitter->count());
    int visibleWeight = 0;
    for (int i = 0; i < splitter->count(); ++i) {
        QWidget *w = splitter->widget(i);
        if (w && w->isVisibleTo(splitter)) {
            if (w == ui->infoPanel)
                visibleWeight += 22;
            else if (w == ui->colorsPanel)
                visibleWeight += 26;
            else if (w == ui->propertiesPanel)
                visibleWeight += 24;
            else
                visibleWeight += 50; // dockPanel
        }
    }
    if (visibleWeight <= 0)
        return;

    for (int i = 0; i < splitter->count(); ++i) {
        QWidget *w = splitter->widget(i);
        if (!w || !w->isVisibleTo(splitter)) {
            sizes.append(0);
            continue;
        }
        int weight = 50;
        if (w == ui->infoPanel)
            weight = 22;
        else if (w == ui->colorsPanel)
            weight = 26;
        else if (w == ui->propertiesPanel)
            weight = 24;
        sizes.append(usable * weight / visibleWeight);
    }
    splitter->setSizes(sizes);
}

void MainWindow::onAbout()
{
    QMessageBox::about(
        this,
        tr("关于 PhotoshopLite"),
        tr("PhotoshopLite\n"
           "Qt 仿 Photoshop 简历向 Demo。\n"
           "菜单栏顶层结构对齐 Photoshop 中文版；功能按路线图逐步实现。"));
}

void MainWindow::onSelectAll()
{
    if (Ps::ImageDocument *doc = m_session->document())
        doc->selectAll();
}

void MainWindow::onSelectDeselect()
{
    if (Ps::ImageDocument *doc = m_session->document())
        doc->clearSelection();
}

void MainWindow::onSelectInverse()
{
    if (Ps::ImageDocument *doc = m_session->document())
        doc->invertSelection();
}

void MainWindow::onClear()
{
    Ps::ImageDocument *doc = m_session->document();
    if (!doc)
        return;
    if (!doc->clearActiveLayerPixels())
        flashStatusMessage(tr("无法清除：无可见活动层"));
}

void MainWindow::onFill()
{
    Ps::ImageDocument *doc = m_session->document();
    if (!doc)
        return;
    if (!doc->fillActiveLayer(ui->toolBox->foregroundColor()))
        flashStatusMessage(tr("无法填充：无可见活动层"));
}

void MainWindow::onFreeTransform()
{
    Ps::ImageDocument *doc = m_session->document();
    if (!doc || !doc->activeLayer() || !doc->activeLayer()->isVisible()) {
        flashStatusMessage(tr("无法变换：无可见活动层"));
        return;
    }
    if (doc->activeLayer()->contentBoundsInDocument().isEmpty()) {
        flashStatusMessage(tr("无法变换：活动层无可见像素"));
        return;
    }
    // 只走一条路径开会话，避免 toolBox→onToolChanged 与此处各 activate 一次：
    // 第二次会先 undo 还原像素再挖空，投影/浮层容易叠成「复制一份」。
    CanvasView *cv = ui->canvasWorkspace->canvasView();
    if (ui->toolBox->currentTool() == Ps::ToolId::FreeTransform) {
        ui->toolOptionsBar->setCurrentTool(Ps::ToolId::FreeTransform);
        cv->setCurrentTool(Ps::ToolId::FreeTransform); // 已在变换：重启会话
    } else {
        ui->toolBox->setCurrentTool(Ps::ToolId::FreeTransform); // → onToolChanged 一次 activate
    }
    flashStatusMessage(tr("自由变换：右键切换模式；Ctrl+Z 还原步骤，Esc/✕ 取消，Enter/✓ 确认"), 4000);
}

void MainWindow::syncFreeTransformOptionsBar()
{
    CanvasView *cv = ui->canvasWorkspace->canvasView();
    auto *ft = qobject_cast<Ps::TransformTool *>(
        cv->toolManager() ? cv->toolManager()->tool(Ps::ToolId::FreeTransform) : nullptr);
    if (!ft || !ft->isSessionActive())
        return;
    const int interp = (ft->interpolation() == Ps::TransformInterpolation::Nearest) ? 0
                     : (ft->interpolation() == Ps::TransformInterpolation::Bilinear) ? 1 : 2;
    ui->toolOptionsBar->setFreeTransformParams(
        ft->paramX(), ft->paramY(), ft->paramWPercent(), ft->paramHPercent(),
        ft->paramAngleDeg(), ft->paramSkewHDeg(), ft->paramSkewVDeg(),
        ft->linkAspect(), interp);
}

void MainWindow::applyFreeTransformMode(int mode)
{
    CanvasView *cv = ui->canvasWorkspace->canvasView();
    auto *ft = qobject_cast<Ps::TransformTool *>(
        cv->toolManager() ? cv->toolManager()->tool(Ps::ToolId::FreeTransform) : nullptr);
    if (!ft)
        return;
    ft->setMode(static_cast<Ps::TransformMode>(mode));
    static const char *kHints[] = {
        QT_TR_NOOP("模式：自由变换"),
        QT_TR_NOOP("模式：缩放"),
        QT_TR_NOOP("模式：旋转"),
        QT_TR_NOOP("模式：斜切"),
        QT_TR_NOOP("模式：扭曲"),
        QT_TR_NOOP("模式：透视"),
    };
    const int idx = qBound(0, mode, 5);
    flashStatusMessage(tr(kHints[idx]), 2500);
}

void MainWindow::onFreeTransformContextMenu(const QPoint &widgetPos)
{
    CanvasView *cv = ui->canvasWorkspace->canvasView();
    auto *ft = qobject_cast<Ps::TransformTool *>(
        cv->toolManager() ? cv->toolManager()->tool(Ps::ToolId::FreeTransform) : nullptr);
    if (!ft || !ft->isSessionActive())
        return;

    QMenu menu(cv);
    auto addMode = [&](const QString &text, Ps::TransformMode mode) {
        QAction *a = menu.addAction(text);
        a->setCheckable(true);
        a->setChecked(ft->mode() == mode);
        connect(a, &QAction::triggered, this, [this, mode]() {
            applyFreeTransformMode(static_cast<int>(mode));
        });
    };
    // 对照 PS：首项「自由变换」即综合模式
    addMode(tr("自由变换"), Ps::TransformMode::Free);
    menu.addSeparator();
    addMode(tr("缩放"), Ps::TransformMode::Scale);
    addMode(tr("旋转"), Ps::TransformMode::Rotate);
    addMode(tr("斜切"), Ps::TransformMode::Skew);
    addMode(tr("扭曲"), Ps::TransformMode::Distort);
    addMode(tr("透视"), Ps::TransformMode::Perspective);
    menu.addSeparator();

    menu.addAction(tr("变形"))->setEnabled(false);
    menu.addAction(tr("水平拆分变形"))->setEnabled(false);
    menu.addAction(tr("垂直拆分变形"))->setEnabled(false);
    menu.addAction(tr("交叉拆分变形"))->setEnabled(false);
    menu.addAction(tr("移去变形拆分"))->setEnabled(false);
    menu.addSeparator();
    menu.addAction(tr("转换变形锚点"))->setEnabled(false);
    menu.addSeparator();
    menu.addAction(tr("切换参考线"))->setEnabled(false);
    menu.addSeparator();
    menu.addAction(tr("内容识别缩放"))->setEnabled(false);
    menu.addAction(tr("操控变形"))->setEnabled(false);
    menu.addSeparator();

    menu.addAction(tr("旋转 180 度"), this, [ft]() { ft->rotateByDegrees(180.0); });
    menu.addAction(tr("顺时针旋转 90 度"), this, [ft]() { ft->rotateByDegrees(90.0); });
    menu.addAction(tr("逆时针旋转 90 度"), this, [ft]() { ft->rotateByDegrees(-90.0); });
    menu.addSeparator();
    menu.addAction(tr("水平翻转"), this, [ft]() { ft->flipHorizontal(); });
    menu.addAction(tr("垂直翻转"), this, [ft]() { ft->flipVertical(); });

    menu.exec(cv->mapToGlobal(widgetPos));
}

void MainWindow::onBrightnessContrast()
{
    Ps::ImageDocument *doc = m_session->document();
    if (!doc)
        return;
    if (doc->addBrightnessContrastFilter() < 0)
        flashStatusMessage(tr("无法调整：无可见活动层"));
    else
        flashStatusMessage(tr("已添加亮度/对比度滤镜（非破坏）"));
}

void MainWindow::onEnsureLayerStyle(int kind)
{
    Ps::ImageDocument *doc = m_session ? m_session->document() : nullptr;
    if (!doc)
        return;
    if (doc->ensureActiveLayerStyle(static_cast<Ps::LayerStyleKind>(kind)) < 0)
        flashStatusMessage(tr("无法添加图层样式：无可见活动层"));
    else
        flashStatusMessage(tr("已应用图层样式"), 2000);
}

void MainWindow::onClearLayerStyles()
{
    Ps::ImageDocument *doc = m_session ? m_session->document() : nullptr;
    if (!doc)
        return;
    if (!doc->clearActiveLayerStyles())
        flashStatusMessage(tr("当前层没有图层样式"));
    else
        flashStatusMessage(tr("已清除图层样式"), 2000);
}

void MainWindow::onCopyLayerStyles()
{
    Ps::ImageDocument *doc = m_session ? m_session->document() : nullptr;
    Ps::Layer *layer = doc ? doc->activeLayer() : nullptr;
    if (!layer || layer->styles().isEmpty()) {
        flashStatusMessage(tr("当前层没有图层样式"));
        return;
    }
    Ps::LayerStyleClipboard::set(layer->styles().snapshot());
    flashStatusMessage(tr("已拷贝图层样式"), 2000);
}

void MainWindow::onPasteLayerStyles()
{
    Ps::ImageDocument *doc = m_session ? m_session->document() : nullptr;
    if (!doc)
        return;
    if (Ps::LayerStyleClipboard::isEmpty()) {
        flashStatusMessage(tr("样式剪贴板为空"));
        return;
    }
    if (!doc->activeLayer()) {
        flashStatusMessage(tr("无活动层"));
        return;
    }
    doc->replaceActiveLayerStyles(Ps::LayerStyleClipboard::snapshot());
    flashStatusMessage(tr("已粘贴图层样式"), 2000);
}
