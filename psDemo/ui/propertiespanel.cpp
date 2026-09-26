#include "propertiespanel.h"
#include "ui_propertiespanel.h"

#include "app/appsession.h"
#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "itemtreepanel.h"
#include "panelchrome.h"

#include <QIcon>
#include <QListWidgetItem>
#include <QSpinBox>
#include <QToolButton>

namespace {

/** 面板内小按钮图标边长（逻辑像素，与颜色面板一致）。 */
constexpr int kPanelIconSize = 18;

/** 折叠分区的箭头；展开/收起各一个（对应 GIMP 展开器 GtkExpander 的三角）。 */
constexpr char kArrowExpanded[] = "▾ ";
constexpr char kArrowCollapsed[] = "▸ ";

} // namespace

PropertiesPanel::PropertiesPanel(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PropertiesPanel)
{
    ui->setupUi(this);

    PanelChrome::addMenuButton(ui->panelTabs);

    bindCollapsible(ui->toggleTransform, ui->transformBody);
    bindCollapsible(ui->toggleAlign, ui->alignBody);

    // 对齐按钮图标：改用已有的 align-*.svg（同 ItemTreePanel 的 SVG 光栅化）
    ItemTreePanel::applyToolbarIcon(ui->btnAlignLeft,
                                    QStringLiteral(":/icons/ui/align-left.svg"), kPanelIconSize);
    ItemTreePanel::applyToolbarIcon(ui->btnAlignHCenter,
                                    QStringLiteral(":/icons/ui/align-hcenter.svg"), kPanelIconSize);
    ItemTreePanel::applyToolbarIcon(ui->btnAlignRight,
                                    QStringLiteral(":/icons/ui/align-right.svg"), kPanelIconSize);
    ItemTreePanel::applyToolbarIcon(ui->btnAlignTop,
                                    QStringLiteral(":/icons/ui/align-top.svg"), kPanelIconSize);
    ItemTreePanel::applyToolbarIcon(ui->btnAlignVCenter,
                                    QStringLiteral(":/icons/ui/align-vcenter.svg"), kPanelIconSize);
    ItemTreePanel::applyToolbarIcon(ui->btnAlignBottom,
                                    QStringLiteral(":/icons/ui/align-bottom.svg"), kPanelIconSize);

    // 调整类型列表：统一用「调整图层」图标（GIMP 侧对应各颜色 operation）
    const QIcon adjustIcon = ItemTreePanel::svgIcon(QStringLiteral(":/icons/layers/adjustment.svg"),
                                                    kPanelIconSize);
    for (int i = 0; i < ui->adjustList->count(); ++i) {
        QListWidgetItem *item = ui->adjustList->item(i);
        item->setIcon(adjustIcon);
        item->setToolTip(QStringLiteral("UI 占位：尚未接入任何调整算法"));
    }

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
    if (m_document)
        disconnect(m_document, nullptr, this, nullptr);

    m_document = document;

    if (m_document) {
        // 像素/结构变化与换活动图层都只需整块重读，故两个信号共用同一个槽
        // （早先写成两个只差 Q_UNUSED 的包装槽，纯冗余）
        connect(m_document, &Ps::ImageDocument::contentChanged,
                this, &PropertiesPanel::refreshFromDocument);
        connect(m_document, &Ps::ImageDocument::activeLayerChanged,
                this, &PropertiesPanel::refreshFromDocument);
    }
    refreshFromDocument();
}

void PropertiesPanel::bindCollapsible(QToolButton *toggle, QWidget *body)
{
    Q_ASSERT(toggle && body);
    // 分区显示名存在 text 里（如「变换」），箭头由这里统一拼，
    // 避免 .ui 与代码各写一半箭头、出现「箭头说收起、内容还在」的错位
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

void PropertiesPanel::refreshFromDocument()
{
    if (!m_document) {
        ui->labelTarget->setText(QStringLiteral("未打开文档"));
        ui->spinW->setValue(0);
        ui->spinH->setValue(0);
        return;
    }

    const Ps::Layer *layer = m_document->activeLayer();
    if (layer) {
        ui->labelTarget->setText(QStringLiteral("文档 %1 × %2 px｜图层 %3")
                                     .arg(m_document->width())
                                     .arg(m_document->height())
                                     .arg(layer->name()));
        ui->spinW->setValue(layer->width());
        ui->spinH->setValue(layer->height());
    } else {
        ui->labelTarget->setText(QStringLiteral("文档 %1 × %2 px｜无活动图层")
                                     .arg(m_document->width())
                                     .arg(m_document->height()));
        ui->spinW->setValue(0);
        ui->spinH->setValue(0);
    }
}
