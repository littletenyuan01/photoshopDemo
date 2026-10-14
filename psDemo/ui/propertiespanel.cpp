/**
 * propertiespanel.cpp — 属性/调整/库停靠面板实现（ui 层）。
 */
#include "propertiespanel.h"
#include "ui_propertiespanel.h"

#include "adjustmentpropshost.h"
#include "filterstackwidget.h"
#include "app/appsession.h"
#include "domain/filternode.h"
#include "domain/imagedocument.h"
#include "domain/layer.h"

namespace {

constexpr char kArrowExpanded[] = "▾ ";
constexpr char kArrowCollapsed[] = "▸ ";

} // namespace

PropertiesPanel::PropertiesPanel(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PropertiesPanel)
{
    ui->setupUi(this);
    ui->panelTabs->setCornerWidget(ui->btnPanelMenu, Qt::TopRightCorner);

    m_adjHost = new AdjustmentPropsHost(ui->propertiesScrollBody);
    ui->propertiesBodyLayout->insertWidget(1, m_adjHost);
    m_adjHost->hide();

    // 图层滤镜栈（像素层）：与调整层参数并列在同一个属性面板里（见 filterstackwidget.h 的说明）。
    // 插到**最前面**：放底部需要滚动才能看到 —— 实测「用户找不到」= 功能等于没有。
    m_filterStack = new FilterStackWidget(ui->propertiesScrollBody);
    ui->propertiesBodyLayout->insertWidget(0, m_filterStack);

    connect(ui->editAngle, &QLineEdit::editingFinished, this, [this]() {
        bool ok = false;
        const double v = ui->editAngle->text().trimmed().toDouble(&ok);
        ui->editAngle->setText(ok ? QString::number(qBound(-360.0, v, 360.0), 'f', 1)
                                  : QStringLiteral("0.0"));
    });
    connect(ui->editX, &QLineEdit::editingFinished, this, [this]() {
        bool ok = false;
        const int v = ui->editX->text().trimmed().toInt(&ok);
        ui->editX->setText(QString::number(ok ? qBound(-100000, v, 100000) : 0));
    });
    connect(ui->editY, &QLineEdit::editingFinished, this, [this]() {
        bool ok = false;
        const int v = ui->editY->text().trimmed().toInt(&ok);
        ui->editY->setText(QString::number(ok ? qBound(-100000, v, 100000) : 0));
    });

    bindCollapsible(ui->toggleTransform, ui->transformBody);
    bindCollapsible(ui->toggleAlign, ui->alignBody);

    connect(m_adjHost, &AdjustmentPropsHost::previewChanged,
            this, &PropertiesPanel::onAdjPreview);
    connect(m_adjHost, &AdjustmentPropsHost::commitChanged,
            this, &PropertiesPanel::onAdjCommit);

    refreshFromDocument();
}

PropertiesPanel::~PropertiesPanel()
{
    delete ui;
}

void PropertiesPanel::setSession(Ps::AppSession *session)
{
    if (m_session == session)
        return;
    if (m_session)
        disconnect(m_session, nullptr, this, nullptr);

    m_session = session;
    if (m_session) {
        connect(m_session, &Ps::AppSession::documentChanged,
                this, &PropertiesPanel::onSessionDocumentChanged);
    }
    onSessionDocumentChanged(m_session ? m_session->document() : nullptr);
}

void PropertiesPanel::onSessionDocumentChanged(Ps::ImageDocument *document)
{
    // A22：换文档前丢弃未决预览并清绑定，禁止把旧层参数 commit 进新文档同下标
    m_adjHost->clear();
    m_boundAdjLayer = -1;

    if (m_document)
        disconnect(m_document, nullptr, this, nullptr);

    m_document = document;

    if (m_document) {
        // contentChanged 在调参预览时很频繁：只刷新摘要，不重载滑条
        connect(m_document, &Ps::ImageDocument::contentChanged,
                this, [this]() {
                    if (!m_document)
                        return;
                    const Ps::Layer *layer = m_document->activeLayer();
                    if (layer) {
                        ui->labelTarget->setText(
                            QStringLiteral("文档 %1 × %2 px｜图层 %3")
                                .arg(m_document->width())
                                .arg(m_document->height())
                                .arg(layer->name()));
                    }
                });
        connect(m_document, &Ps::ImageDocument::activeLayerChanged,
                this, &PropertiesPanel::refreshFromDocument);
        connect(m_document, &Ps::ImageDocument::layerPropertiesChanged,
                this, &PropertiesPanel::refreshFromDocument);
        connect(m_document, &Ps::ImageDocument::structureChanged,
                this, &PropertiesPanel::refreshFromDocument);
    }
    refreshFromDocument();
}

void PropertiesPanel::bindCollapsible(QToolButton *toggle, QWidget *body)
{
    Q_ASSERT(toggle && body);
    const QString label = toggle->text();
    const auto apply = [toggle, body, label](bool expanded) {
        toggle->setText((expanded ? QString::fromUtf8(kArrowExpanded)
                                  : QString::fromUtf8(kArrowCollapsed))
                        + label);
        body->setVisible(expanded);
    };
    connect(toggle, &QToolButton::toggled, this, apply);
    apply(toggle->isChecked());
}

void PropertiesPanel::setAdjustmentMode(bool on)
{
    m_adjHost->setVisible(on);
    ui->toggleTransform->setVisible(!on);
    ui->transformBody->setVisible(!on && ui->toggleTransform->isChecked());
    ui->toggleAlign->setVisible(!on);
    ui->alignBody->setVisible(!on && ui->toggleAlign->isChecked());
}

void PropertiesPanel::onAdjPreview(const Ps::FilterNode &node)
{
    if (!m_document)
        return;
    const int li = m_document->activeLayerIndex();
    m_boundAdjLayer = li;
    m_document->setLayerFilterNode(li, 0, node, /*pushUndo=*/false);
}

void PropertiesPanel::onAdjCommit(const Ps::FilterNode &node)
{
    if (!m_document)
        return;
    // 换层后 active 可能已变：提交到绑定过的调整层，而不是误写到新活动层
    const int li = (m_boundAdjLayer >= 0) ? m_boundAdjLayer : m_document->activeLayerIndex();
    m_document->setLayerFilterNode(li, 0, node, /*pushUndo=*/true);
}

void PropertiesPanel::refreshFromDocument()
{
    if (!m_document) {
        ui->labelTarget->setText(QStringLiteral("未打开文档"));
        ui->editW->setText(QStringLiteral("0"));
        ui->editH->setText(QStringLiteral("0"));
        setAdjustmentMode(false);
        m_adjHost->clear();
        m_boundAdjLayer = -1;
        return;
    }

    // 仅在「切走活动层」时提交未决预览（A22：结构变化时下标可能已不是同一层，勿盲写）
    if (m_adjHost->hasPendingPreview() && m_boundAdjLayer >= 0
        && m_document->activeLayerIndex() != m_boundAdjLayer) {
        const Ps::Layer *prev = m_document->layers().layerAt(m_boundAdjLayer);
        if (prev && prev->isAdjustmentLayer())
            m_adjHost->commitPendingIfAny();
        else
            m_adjHost->clear();
    }

    const Ps::Layer *layer = m_document->activeLayer();
    if (layer) {
        ui->labelTarget->setText(QStringLiteral("文档 %1 × %2 px｜图层 %3")
                                     .arg(m_document->width())
                                     .arg(m_document->height())
                                     .arg(layer->name()));
        ui->editW->setText(QString::number(layer->width()));
        ui->editH->setText(QString::number(layer->height()));
    } else {
        ui->labelTarget->setText(QStringLiteral("文档 %1 × %2 px｜无活动图层")
                                     .arg(m_document->width())
                                     .arg(m_document->height()));
        ui->editW->setText(QStringLiteral("0"));
        ui->editH->setText(QStringLiteral("0"));
    }

    if (layer && layer->isAdjustmentLayer() && layer->filters().count() > 0) {
        setAdjustmentMode(true);
        m_boundAdjLayer = m_document->activeLayerIndex();
        m_adjHost->setNode(layer->filters().at(0));
    } else {
        setAdjustmentMode(false);
        m_adjHost->clear();
        m_boundAdjLayer = -1;
    }

    // 像素层的滤镜栈：让「非破坏」真的可见、可管（调整层走上面的参数页，不进这个列表）
    m_filterStack->setContext(m_document, m_document ? m_document->activeLayerIndex() : -1);
}
