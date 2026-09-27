#include "dockpanel.h"
#include "ui_layerpanel.h"

#include "app/appsession.h"
#include "domain/imagedocument.h"

#include <QTabWidget>
#include <QToolButton>

DockPanel::DockPanel(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::LayerPanel)
{
    ui->setupUi(this);

    // ≡ 按钮在 layerpanel.ui；挂到 Tab 右上角（Designer 无法直接设 corner）
    ui->panelTabs->setCornerWidget(ui->btnPanelMenu, Qt::TopRightCorner);
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
