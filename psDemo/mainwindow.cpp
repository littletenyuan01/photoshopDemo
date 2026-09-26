#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "app/appsession.h"
#include "domain/imagedocument.h"
#include "domain/layer.h"
// 必须早于 ui_mainwindow.h：其中 DockPanel 头文件对 CanvasView 仅有前向声明
#include "ui/canvasview.h"
#include "ui/canvasworkspace.h"
#include "ui/dockpanel.h"
#include "ui/toolbox.h"
#include "ui/tooloptionsbar.h"

#include <QFileDialog>
#include <QImageReader>
#include <QMessageBox>

#include <memory>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_session(new Ps::AppSession(this))
{
    ui->setupUi(this); // 菜单与布局均来自 mainwindow.ui

    setupMenus();
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
    connect(ui->actionExit, &QAction::triggered, this, &QWidget::close);

    // —— 视图（缩放已实现）——
    connect(ui->actionZoomFit, &QAction::triggered, this, &MainWindow::onZoomFit);
    connect(ui->actionZoomActual, &QAction::triggered, this, &MainWindow::onZoomActual);
    connect(ui->actionZoomIn, &QAction::triggered, this, &MainWindow::onZoomIn);
    connect(ui->actionZoomOut, &QAction::triggered, this, &MainWindow::onZoomOut);

    // —— 窗口：显隐右侧面板 ——
    connect(ui->actionWindowLayers, &QAction::toggled, this, &MainWindow::onToggleDockPanel);

    // —— 帮助 ——
    connect(ui->actionHelpAbout, &QAction::triggered, this, &MainWindow::onAbout);
}

void MainWindow::setupSession()
{
    // 一次性交付：此后文档变化全部由 AppSession 广播，无需在此逐个转发
    ui->canvasWorkspace->setSession(m_session);
    ui->dockPanel->setSession(m_session);

    // 标题跟着 session 走
    connect(m_session, &Ps::AppSession::documentChanged,
            this, &MainWindow::updateWindowTitle);
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
    connect(ui->toolOptionsBar, &ToolOptionsBar::brushDiameterChanged,
            this, &MainWindow::onBrushDiameterChanged);

    // 用工具箱当前值对齐其余组件（这里同步不经过槽，避免半初始化状态）
    const Ps::ToolId tool = ui->toolBox->currentTool();
    ui->toolOptionsBar->setCurrentTool(tool);
    canvas->setCurrentTool(tool);
    canvas->setForegroundColor(ui->toolBox->foregroundColor());
    canvas->setBackgroundColor(ui->toolBox->backgroundColor());
    canvas->setBrushDiameter(ui->toolOptionsBar->brushDiameter());
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
    m_session->setDocument(Ps::ImageDocument::createBlank(800, 600, Qt::white));
    statusBar()->showMessage(tr("已新建 800×600 文档"), 3000);
}

void MainWindow::onOpenDocument()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        tr("打开图像"),
        QString(),
        tr("图像文件 (*.png *.jpg *.jpeg *.bmp *.webp);;所有文件 (*.*)"));
    if (path.isEmpty())
        return;

    QImageReader reader(path);
    reader.setAutoTransform(true); // 尊重 EXIF 方向
    QImage image = reader.read();
    if (image.isNull()) {
        QMessageBox::warning(this, tr("打开失败"),
                             tr("无法读取：%1\n%2").arg(path, reader.errorString()));
        return;
    }

    // 目前：打开 = 单「背景」层；多层工程格式以后再做
    auto doc = std::make_unique<Ps::ImageDocument>(image.width(), image.height());
    auto layer = std::make_unique<Ps::Layer>(tr("背景"), image);
    // 走 ImageDocument::addLayer —— 它是挂 Layer::owner 的唯一入口。
    // （早先这里直接调 layers().addLayer，漏挂 owner 导致改背景层显隐/透明度时
    //   属性信号不发、画布与面板静默不同步。现在 LayerStack 改栈方法是 private，
    //   绕过会编译不过。）
    const int index = doc->addLayer(std::move(layer));
    doc->setActiveLayerIndex(index);
    doc->setFilePath(path);
    doc->clearDirty();

    m_session->setDocument(std::move(doc));
    statusBar()->showMessage(tr("已打开：%1").arg(path), 4000);
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

void MainWindow::onAbout()
{
    QMessageBox::about(
        this,
        tr("关于 photoshopDemo"),
        tr("photoshopDemo\n"
           "Qt 仿 Photoshop 简历向 Demo。\n"
           "菜单栏顶层结构对齐 Photoshop 中文版；功能按路线图逐步实现。"));
}

void MainWindow::updateWindowTitle(Ps::ImageDocument *document)
{
    QString name = tr("未命名");
    if (document && !document->filePath().isEmpty())
        name = document->filePath();
    else if (document)
        name = tr("未命名 (%1×%2)").arg(document->width()).arg(document->height());

    setWindowTitle(tr("%1 — photoshopDemo").arg(name));
}
