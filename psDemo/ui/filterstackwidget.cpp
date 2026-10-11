/**
 * filterstackwidget.cpp — 图层滤镜栈面板实现（ui 层）。
 */
#include "filterstackwidget.h"

#include "filterdialog.h"

#include "domain/filternode.h"
#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "engine/op/opname.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace {

/** 一行显示什么：算子标题 + 关键参数（否则列表里全是「锐化」看不出区别）。 */
QString filterLabel(const Ps::FilterNode &n)
{
    QString s = Ps::opNameTitle(n.op());
    switch (n.op()) {
    case Ps::OpName::BoxBlur:
        s += QStringLiteral("（半径 %1）").arg(n.blurRadius());
        break;
    case Ps::OpName::GaussianBlur:
        s += QStringLiteral("（半径 %1）").arg(n.gaussRadius());
        break;
    case Ps::OpName::MedianBlur:
        s += QStringLiteral("（半径 %1）").arg(n.medianRadius());
        break;
    case Ps::OpName::UnsharpMask:
        s += QStringLiteral("（半径 %1 / 强度 %2%）")
                 .arg(n.usmRadius())
                 .arg(int(n.usmAmount() * 100.0 + 0.5));
        break;
    case Ps::OpName::Sharpen:
        s += QStringLiteral("（强度 %1）").arg(n.sharpenStrength());
        break;
    case Ps::OpName::Emboss:
        s += QStringLiteral("（强度 %1）").arg(n.embossStrength());
        break;
    default:
        break; // 无参数算子（如查找边缘）
    }
    return s;
}

} // namespace

FilterStackWidget::FilterStackWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(4);

    auto *title = new QLabel(tr("滤镜（非破坏，勾选＝启用）"), this);
    col->addWidget(title);

    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("filterList"));
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setMaximumHeight(110);
    col->addWidget(m_list);

    auto *row = new QHBoxLayout;
    m_editBtn = new QPushButton(tr("编辑…"), this);
    m_editBtn->setObjectName(QStringLiteral("btnFilterEdit"));
    m_delBtn = new QPushButton(tr("删除"), this);
    m_delBtn->setObjectName(QStringLiteral("btnFilterDelete"));
    m_upBtn = new QPushButton(tr("上移"), this);
    m_upBtn->setObjectName(QStringLiteral("btnFilterUp"));
    m_downBtn = new QPushButton(tr("下移"), this);
    m_downBtn->setObjectName(QStringLiteral("btnFilterDown"));
    for (QPushButton *b : {m_editBtn, m_delBtn, m_upBtn, m_downBtn})
        row->addWidget(b);
    row->addStretch(1);
    col->addLayout(row);

    connect(m_list, &QListWidget::itemChanged, this, &FilterStackWidget::onItemChanged);
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this]() { onEdit(); });
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int) { updateButtons(); });
    connect(m_editBtn, &QPushButton::clicked, this, &FilterStackWidget::onEdit);
    connect(m_delBtn, &QPushButton::clicked, this, &FilterStackWidget::onDelete);
    connect(m_upBtn, &QPushButton::clicked, this, [this]() { onMove(-1); });
    connect(m_downBtn, &QPushButton::clicked, this, [this]() { onMove(+1); });

    hide(); // 没有滤镜时不占地方
}

void FilterStackWidget::setContext(Ps::ImageDocument *doc, int layerIndex)
{
    m_doc = doc;
    m_layer = layerIndex;
    refresh();
}

int FilterStackWidget::currentIndex() const
{
    QListWidgetItem *item = m_list->currentItem();
    return item ? item->data(Qt::UserRole).toInt() : -1;
}

void FilterStackWidget::refresh()
{
    Ps::Layer *layer = (m_doc && m_layer >= 0) ? m_doc->layers().layerAt(m_layer) : nullptr;
    const bool usable = layer && !layer->isAdjustmentLayer();

    m_block = true;
    m_list->clear();
    if (usable) {
        for (int i = 0; i < layer->filters().count(); ++i) {
            const Ps::FilterNode &n = layer->filters().at(i);
            auto *item = new QListWidgetItem(filterLabel(n), m_list);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(n.isEnabled() ? Qt::Checked : Qt::Unchecked);
            item->setData(Qt::UserRole, i);
        }
        if (m_list->count() == 0) {
            // 没有滤镜时**不隐藏**，给一行占位说明：否则用户根本不知道有这个功能
            auto *hint = new QListWidgetItem(
                tr("（暂无滤镜：用「滤镜」菜单应用后会出现在这里）"), m_list);
            hint->setFlags(Qt::NoItemFlags);
        }
        if (m_list->count() > 0 && layer->filters().count() > 0)
            m_list->setCurrentRow(0);
    }
    m_block = false;

    // 只有在没有可用像素层时才隐藏（调整层走上面的参数页）
    setVisible(usable);
    updateButtons();
}

void FilterStackWidget::updateButtons()
{
    Ps::Layer *layer = (m_doc && m_layer >= 0) ? m_doc->layers().layerAt(m_layer) : nullptr;
    const int rows = layer ? layer->filters().count() : 0;
    const int cur = currentIndex();
    m_editBtn->setEnabled(cur >= 0);
    m_delBtn->setEnabled(cur >= 0);
    m_upBtn->setEnabled(cur > 0);
    m_downBtn->setEnabled(cur >= 0 && cur + 1 < rows);
}

void FilterStackWidget::onItemChanged(QListWidgetItem *item)
{
    if (m_block || !item || !m_doc)
        return;
    const int index = item->data(Qt::UserRole).toInt();
    const bool on = (item->checkState() == Qt::Checked);
    Ps::Layer *layer = m_doc->layers().layerAt(m_layer);
    if (!layer || index < 0 || index >= layer->filters().count())
        return;
    if (layer->filters().at(index).isEnabled() == on)
        return; // 同值不提交（避免空操作 undo）
    m_doc->setLayerFilterEnabled(m_layer, index, on);
    emit stackChanged();
}

void FilterStackWidget::onDelete()
{
    const int index = currentIndex();
    if (!m_doc || index < 0)
        return;
    if (m_doc->removeLayerFilter(m_layer, index)) {
        refresh();
        emit stackChanged();
    }
}

void FilterStackWidget::onEdit()
{
    const int index = currentIndex();
    if (!m_doc || index < 0)
        return;
    Ps::Layer *layer = m_doc->layers().layerAt(m_layer);
    if (!layer || index >= layer->filters().count())
        return;

    const Ps::FilterNode original = layer->filters().at(index);
    FilterDialog dlg(this);
    dlg.setNode(original);

    // 实时预览：沿用「新加滤镜」那条通道 —— region 传空 = 用文档里的**当前可见区提示**
    // （面板不持有画布），再按 80ms 节流（拖动再快也不排队）。
    QTimer throttle;
    throttle.setSingleShot(true);
    throttle.setInterval(80);
    Ps::FilterNode latest = original;
    connect(&dlg, &FilterDialog::previewChanged, this,
            [&latest, &throttle](const Ps::FilterNode &n) {
                latest = n;
                if (!throttle.isActive())
                    throttle.start();
            });
    connect(&throttle, &QTimer::timeout, this, [this, index, &latest]() {
        m_doc->setLayerFilterNodePreview(m_layer, index, latest, QRect());
    });

    if (dlg.exec() != QDialog::Accepted) {
        // 取消：预览只刷过可见区，屏幕外仍是预览值 ⇒ 必须用整层标脏把原参数写回去
        if (m_doc->setLayerFilterNode(m_layer, index, original, /*pushUndo=*/false)) {
            refresh();
            emit stackChanged();
        }
        return;
    }
    // 确定：整层标脏 + 恰好一条撤销
    if (m_doc->setLayerFilterNode(m_layer, index, dlg.node(), /*pushUndo=*/true)) {
        refresh();
        emit stackChanged();
    }
}

void FilterStackWidget::onMove(int delta)
{
    const int index = currentIndex();
    if (!m_doc || index < 0 || delta == 0)
        return;
    const int to = index + delta;
    if (m_doc->moveLayerFilter(m_layer, index, to)) {
        refresh();
        m_list->setCurrentRow(to);
        emit stackChanged();
    }
}
