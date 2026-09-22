#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "ui/toolbox.h"
#include "ui/tooloptionsbar.h"

#include <QFileDialog>
#include <QImageReader>
#include <QMessageBox>

#include <memory>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this); // 菜单与布局均来自 mainwindow.ui
    setupMenus();
    setupToolbox();

    // 启动即有可演示文档，避免空白壳
    setDocument(Ps::ImageDocument::createBlank(800, 600, Qt::white));
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

    // —— 窗口：显隐右侧图层面板 ——
    connect(ui->actionWindowLayers, &QAction::toggled, this, &MainWindow::onToggleLayerPanel);

    // —— 帮助 ——
    connect(ui->actionHelpAbout, &QAction::triggered, this, &MainWindow::onAbout);
}

void MainWindow::setupToolbox()
{
    // 工具箱 → 选项栏 + 画布（绘制/抓手/缩放）
    connect(ui->toolBox, &ToolBox::toolChanged, this, &MainWindow::onToolChanged);
    connect(ui->toolBox, &ToolBox::foregroundColorChanged,
            this, &MainWindow::onForegroundColorChanged);
    connect(ui->toolBox, &ToolBox::backgroundColorChanged,
            this, &MainWindow::onBackgroundColorChanged);
    connect(ui->toolOptionsBar, &ToolOptionsBar::brushDiameterChanged,
            this, &MainWindow::onBrushDiameterChanged);

    const Ps::ToolId tool = ui->toolBox->currentTool();
    ui->toolOptionsBar->setCurrentTool(tool);
    ui->canvasView->setCurrentTool(tool);
    ui->canvasView->setForegroundColor(ui->toolBox->foregroundColor());
    ui->canvasView->setBackgroundColor(ui->toolBox->backgroundColor());
    ui->canvasView->setBrushDiameter(ui->toolOptionsBar->brushDiameter());
}

void MainWindow::onToolChanged(Ps::ToolId id)
{
    ui->toolOptionsBar->setCurrentTool(id);
    ui->canvasView->setCurrentTool(id);
    statusBar()->showMessage(tr("当前工具已切换"), 1500);
}

void MainWindow::onBrushDiameterChanged(int diameter)
{
    ui->canvasView->setBrushDiameter(diameter);
}

void MainWindow::onForegroundColorChanged(const QColor &color)
{
    ui->canvasView->setForegroundColor(color);
}

void MainWindow::onBackgroundColorChanged(const QColor &color)
{
    ui->canvasView->setBackgroundColor(color);
}

void MainWindow::onNewDocument()
{
    setDocument(Ps::ImageDocument::createBlank(800, 600, Qt::white));
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
    const int index = doc->layers().addLayer(std::move(layer));
    doc->setActiveLayerIndex(index);
    doc->setFilePath(path);
    doc->clearDirty();

    setDocument(std::move(doc));
    statusBar()->showMessage(tr("已打开：%1").arg(path), 4000);
}

void MainWindow::onZoomFit()
{
    ui->canvasView->zoomFit();
}

void MainWindow::onZoomActual()
{
    ui->canvasView->zoomActual();
}

void MainWindow::onZoomIn()
{
    ui->canvasView->setZoom(ui->canvasView->zoom() * 1.25);
}

void MainWindow::onZoomOut()
{
    ui->canvasView->setZoom(ui->canvasView->zoom() / 1.25);
}

void MainWindow::onToggleLayerPanel(bool visible)
{
    ui->layerPanel->setVisible(visible);
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

void MainWindow::setDocument(std::unique_ptr<Ps::ImageDocument> document)
{
    m_document = std::move(document);
    // 画布与图层面板都不拥有文档，只借用指针
    ui->canvasView->setDocument(m_document.get());
    ui->layerPanel->setDocument(m_document.get());
    updateWindowTitle();
}

void MainWindow::updateWindowTitle()
{
    QString name = tr("未命名");
    if (m_document && !m_document->filePath().isEmpty())
        name = m_document->filePath();
    else if (m_document)
        name = tr("未命名 (%1×%2)").arg(m_document->width()).arg(m_document->height());

    setWindowTitle(tr("%1 — photoshopDemo").arg(name));
}
