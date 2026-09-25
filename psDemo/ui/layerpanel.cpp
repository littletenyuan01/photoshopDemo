#include "layerpanel.h"
#include "ui_layerpanel.h"

#include <QToolButton>

LayerPanel::LayerPanel(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::LayerPanel)
{
    ui->setupUi(this);

    // PS 面板右上角 ≡；GIMP 各 dock 自有菜单，此处合一面板共用一个入口
    auto *menuBtn = new QToolButton(this);
    menuBtn->setObjectName(QStringLiteral("btnPanelMenu"));
    menuBtn->setText(QStringLiteral("≡"));
    menuBtn->setToolTip(QStringLiteral("面板选项"));
    menuBtn->setAutoRaise(true);
    ui->panelTabs->setCornerWidget(menuBtn, Qt::TopRightCorner);
}

LayerPanel::~LayerPanel()
{
    delete ui;
}

void LayerPanel::setDocument(Ps::ImageDocument *document)
{
    // 与 GIMP 各 tree view 分别 set_image 等价；壳统一入口方便 MainWindow
    ui->layerTree->setDocument(document);
    ui->channelTree->setDocument(document);
    ui->pathTree->setDocument(document);
}
