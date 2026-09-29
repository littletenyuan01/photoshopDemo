#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "app/appsession.h"
#include "app/recentdocuments.h"
#include "domain/imagedocument.h"
#include "domain/layer.h"
// 必须早于 ui_mainwindow.h：其中 DockPanel 头文件对 CanvasView 仅有前向声明
#include "ui/canvasview.h"
#include "ui/canvasworkspace.h"
#include "ui/dockpanel.h"
#include "engine/compositor.h"
#include "io/projectio.h"
#include "io/psdio.h"
#include "ui/canvassizedialog.h"
#include "ui/colorspanel.h"
#include "ui/homescreen.h"
#include "ui/imagesizedialog.h"
#include "ui/newdocumentdialog.h"
#include "ui/propertiespanel.h"
#include "ui/toolbox.h"
#include "ui/tooloptionsbar.h"

#include <QAbstractButton>
#include <QAction>
#include <QCloseEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>

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
    ui->actionSave->setEnabled(true);
    ui->actionSave->setToolTip(tr("存储为 PhotoshopLite 工程（.pslite）"));
    ui->actionSaveAs->setEnabled(true);
    ui->actionSaveAs->setToolTip(tr("另存为 PhotoshopLite 工程（.pslite）"));
    connect(ui->actionSave, &QAction::triggered, this, &MainWindow::onSaveDocument);
    connect(ui->actionSaveAs, &QAction::triggered, this, &MainWindow::onSaveDocumentAs);
    connect(ui->actionExit, &QAction::triggered, this, &QWidget::close);

    connect(ui->actionImageSize, &QAction::triggered, this, &MainWindow::onImageSize);
    connect(ui->actionCanvasSize, &QAction::triggered, this, &MainWindow::onCanvasSize);

    connect(ui->actionLayerNew, &QAction::triggered, this, &MainWindow::onNewLayer);
    connect(ui->actionLayerDuplicate, &QAction::triggered, this, &MainWindow::onDuplicateLayer);

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

    // —— 视图（缩放已实现）——
    connect(ui->actionZoomFit, &QAction::triggered, this, &MainWindow::onZoomFit);
    connect(ui->actionZoomActual, &QAction::triggered, this, &MainWindow::onZoomActual);
    connect(ui->actionZoomIn, &QAction::triggered, this, &MainWindow::onZoomIn);
    connect(ui->actionZoomOut, &QAction::triggered, this, &MainWindow::onZoomOut);

    // —— 窗口：显隐右侧面板 ——
    connect(ui->actionWindowLayers, &QAction::toggled, this, &MainWindow::onToggleDockPanel);
    // 颜色/属性各自是一块独立面板，直接切显隐，不需要额外的槽
    connect(ui->actionWindowColor, &QAction::toggled, ui->colorsPanel, &QWidget::setVisible);
    connect(ui->actionWindowProperties, &QAction::toggled, ui->propertiesPanel, &QWidget::setVisible);

    // —— 帮助 ——
    connect(ui->actionHelpAbout, &QAction::triggered, this, &MainWindow::onAbout);
}

void MainWindow::setupSession()
{
    // 一次性交付：此后文档变化全部由 AppSession 广播，无需在此逐个转发
    ui->canvasWorkspace->setSession(m_session);
    ui->dockPanel->setSession(m_session);
    ui->propertiesPanel->setSession(m_session); // 「属性」页展示真实文档/图层数据
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
    connect(ui->toolOptionsBar, &ToolOptionsBar::gradientOptionsChanged, this, [this]() {
        ui->canvasWorkspace->canvasView()->setGradientOptions(
            ui->toolOptionsBar->gradientType(),
            ui->toolOptionsBar->gradientOpacityPercent() / 100.0,
            ui->toolOptionsBar->gradientOffsetPercent(),
            ui->toolOptionsBar->gradientReverse(),
            ui->toolOptionsBar->gradientDither());
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
    canvas->setGradientOptions(ui->toolOptionsBar->gradientType(),
                               ui->toolOptionsBar->gradientOpacityPercent() / 100.0,
                               ui->toolOptionsBar->gradientOffsetPercent(),
                               ui->toolOptionsBar->gradientReverse(),
                               ui->toolOptionsBar->gradientDither());
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
}

void MainWindow::createInitialDocument()
{
    m_session->setDocument(Ps::ImageDocument::createBlank(800, 600, Qt::white));
}

void MainWindow::onToolChanged(Ps::ToolId id)
{
    ui->toolOptionsBar->setCurrentTool(id);
    ui->canvasWorkspace->canvasView()->setCurrentTool(id);
    // 工具提示语显示在状态栏（选项条里只放参数，对齐 PS）
    statusBar()->showMessage(ui->toolOptionsBar->currentHint(), 4000);
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
    statusBar()->showMessage(
        tr("已新建 %1×%2 文档").arg(size.width()).arg(size.height()), 3000);
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
        statusBar()->showMessage(tr("已打开工程：%1").arg(path), 4000);
        return true;
    }

    QImageReader reader(path);
    reader.setAutoTransform(true); // 尊重 EXIF 方向
    QImage image = reader.read();
    if (image.isNull()) {
        QMessageBox::warning(this, tr("打开失败"),
                             tr("无法读取：%1\n%2").arg(path, reader.errorString()));
        Ps::RecentDocuments::remove(path);
        rebuildRecentMenu();
        ui->homeScreen->refreshRecent();
        return false;
    }

    // 栅格打开 = 单「背景」层（不可再编辑图层结构于原文件；请另存 .pslite）
    auto doc = std::make_unique<Ps::ImageDocument>(image.width(), image.height());
    auto layer = std::make_unique<Ps::Layer>(tr("背景"), image);
    const int index = doc->addLayer(std::move(layer));
    doc->setActiveLayerIndex(index);
    doc->setFilePath(path);
    doc->clearDirty();

    m_session->setDocument(std::move(doc));
    Ps::RecentDocuments::add(path);
    Ps::RecentDocuments::setThumbnail(path, image);
    rebuildRecentMenu();
    onShowWorkspace();
    statusBar()->showMessage(tr("已打开：%1").arg(path), 4000);
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
    statusBar()->showMessage(
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
    statusBar()->showMessage(
        tr("已新建：%1").arg(layer ? layer->name() : tr("图层")), 3000);
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
    statusBar()->showMessage(
        tr("已复制：%1").arg(layer ? layer->name() : tr("图层")), 3000);
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
        statusBar()->showMessage(tr("未重新采样：像素尺寸保持 %1×%2")
                                     .arg(doc->width())
                                     .arg(doc->height()),
                                 3000);
        return;
    }
    if (size.width() == doc->width() && size.height() == doc->height())
        return;

    doc->scaleImage(size.width(), size.height());
    ui->canvasWorkspace->canvasView()->zoomFit();
    statusBar()->showMessage(
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
    statusBar()->showMessage(
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
    // 图层区最长（PS 也是），上面两块够用即可
    splitter->setSizes({usable * 26 / 100, usable * 24 / 100, usable * 50 / 100});
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
