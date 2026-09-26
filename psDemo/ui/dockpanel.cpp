#include "dockpanel.h"
#include "ui_layerpanel.h"

#include "app/appsession.h"
#include "domain/imagedocument.h"

#include <QToolButton>

DockPanel::DockPanel(QWidget *parent)
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

DockPanel::~DockPanel()
{
    delete ui;
}

void DockPanel::setSession(Ps::AppSession *session)
{
    if (m_session == session)
        return;
    if (m_session)
        disconnect(m_session, nullptr, this, nullptr);

    m_session = session;
    if (m_session) {
        connect(m_session, &Ps::AppSession::documentChanged,
                this, &DockPanel::onSessionDocumentChanged);
    }
    // 立即对齐一次当前状态（对应 GIMP set_image 的即时同步语义）
    onSessionDocumentChanged(m_session ? m_session->document() : nullptr);
}

void DockPanel::onSessionDocumentChanged(Ps::ImageDocument *document)
{
    // 与 GIMP 各 tree view 分别 set_image 等价；壳作为统一入口方便子面板同步
    ui->layerTree->setDocument(document);
    ui->channelTree->setDocument(document);
    ui->pathTree->setDocument(document);
}
