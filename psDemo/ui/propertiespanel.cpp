/**
 * propertiespanel.cpp — 属性/调整/库停靠面板实现（ui 层）。
 */
#include "propertiespanel.h"
#include "ui_propertiespanel.h"

#include "app/appsession.h"
#include "domain/imagedocument.h"
#include "domain/layer.h"

namespace {

/** 折叠分区的箭头；展开/收起各一个（对应 GIMP 展开器 GtkExpander 的三角）。 */
constexpr char kArrowExpanded[] = "▾ ";
constexpr char kArrowCollapsed[] = "▸ ";

} // namespace

PropertiesPanel::PropertiesPanel(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PropertiesPanel)
{
    ui->setupUi(this);
    // 对齐图标 / ≡ 在 propertiespanel.ui
    ui->panelTabs->setCornerWidget(ui->btnPanelMenu, Qt::TopRightCorner);

    // X/Y/旋转：失焦钳制；控件类型来自 ui_propertiespanel.h
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
        ui->editW->setText(QStringLiteral("0"));
        ui->editH->setText(QStringLiteral("0"));
        return;
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
}
