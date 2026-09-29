/**
 * dockpanel.cpp — 右侧三 Tab 停靠壳实现（ui 层）。
 */
#include "dockpanel.h"
#include "ui_layerpanel.h"

#include "app/appsession.h"
#include "domain/imagedocument.h"
#include "ui/layertreepanel.h"

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
    if (m_document)
        disconnect(m_document, nullptr, this, nullptr);

    m_document = document;

    // 与 GIMP 各 tree view 分别 set_image 等价；壳作为统一入口方便子面板同步
    ui->layerTree->setDocument(document);
    ui->channelTree->setDocument(document);
    ui->pathTree->setDocument(document);

    // 壳层再订一份 structureChanged：子面板若漏订，列表仍能强制重建
    // （曾复现：属性栏已显示「图层 N」，图层面板仍只显示「背景」）
    if (m_document) {
        connect(m_document, &Ps::ImageDocument::structureChanged,
                this, &DockPanel::onDocumentStructureChanged);
    }
}

void DockPanel::onDocumentStructureChanged()
{
    ui->layerTree->reload();
    ui->channelTree->reload();
    ui->pathTree->reload();
}
