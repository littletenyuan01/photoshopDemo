/**
 * magneticlassotool.cpp — 磁性套索：首击落锚 + 圆域贴边 + Livewire 段路径 + 频率自动落锚。
 *
 * 1) 单击 → 立刻在最近像素角点落锚（不依赖合成图）
 * 2) 移动 → 圆形 Width 内吸最强角点；锚点→tip 用 Dijkstra 最短代价贴边（对照 GIMP find_optimal_path）
 * 3) 贴合步数达到 Frequency → 自动再落锚；也可单击强制落锚
 */
#include "magneticlassotool.h"

#include "domain/imagedocument.h"
#include "engine/compositor.h"
#include "engine/magneticedgesnap.h"

#include <QPainter>
#include <QPen>
#include <QPolygonF>
#include <QtMath>

#include <cmath>

namespace Ps {

namespace {

constexpr qreal kCloseWidgetRadius = 10.0;

/**
 * Frequency 1..100 → 锚点最小间距（文档像素），越高越密。
 *
 * 【为什么按距离、不按「更新次数」】旧实现累计的是 tip 格点变化次数，于是同一段距离，
 * 鼠标划得快→事件少→落锚稀，划得慢→落锚密：锚点密度被鼠标速度绑架。
 * 对照 PS，Frequency 是「以什么频度设置紧固点」，应与速度无关。
 *
 * 标定：f=100 → 4px（最密）；f=57（默认）→ 约 19px；f=1 → 约 40px（最疏）。
 * 旧实现在默认值下的等效间距约在 13px（慢速）~130px（快速）之间漂移，这里取偏密的 19px。
 */
qreal spacingFromFrequency(int frequency)
{
    const int f = qBound(1, frequency, 100);
    return qBound(4.0, 40.0 - 0.36 * f, 40.0);
}

qreal minEdgeFromContrast(int contrast)
{
    return 10.0 + qBound(1, contrast, 100) * 1.0;
}

qreal dist2(const QPointF &a, const QPointF &b)
{
    const qreal dx = a.x() - b.x();
    const qreal dy = a.y() - b.y();
    return dx * dx + dy * dy;
}

bool sameGrid(const QPointF &a, const QPointF &b)
{
    return dist2(a, b) < 0.25;
}

} // namespace

MagneticLassoTool::MagneticLassoTool(QObject *parent)
    : Tool(Ps::ToolId::MagneticLasso, parent)
{
}

Qt::CursorShape MagneticLassoTool::cursorShape() const
{
    return Qt::CrossCursor;
}

bool MagneticLassoTool::hasOverlay() const
{
    // 活动中始终画（含首击单锚）；Caps Lock 未落锚时也可看搜索圆
    return m_active || (Tool::capsLockOn() && m_hasCursor);
}

ChannelOp MagneticLassoTool::opFromModifiers(Qt::KeyboardModifiers modifiers)
{
    const bool shift = modifiers.testFlag(Qt::ShiftModifier);
    const bool ctrl = modifiers.testFlag(Qt::ControlModifier);
    if (shift && ctrl)
        return ChannelOp::Intersect;
    if (shift)
        return ChannelOp::Add;
    if (ctrl)
        return ChannelOp::Subtract;
    return ChannelOp::Replace;
}

void MagneticLassoTool::syncParamsFromContext(const ToolContext &ctx)
{
    m_searchRadius = qBound(1, ctx.magneticWidth, 256);
    m_minEdge = minEdgeFromContrast(ctx.magneticContrast);
    m_anchorSpacing = spacingFromFrequency(ctx.magneticFrequency);
}

bool MagneticLassoTool::ensureSource(const ToolContext &ctx)
{
    if (!ctx.document)
        return false;
    if (m_sourceReady
        && !m_source.isNull()
        && m_source.width() == ctx.document->width()
        && m_source.height() == ctx.document->height())
        return true;

    m_source = Compositor::composite(*ctx.document);
    m_sourceReady = false;
    if (m_source.isNull())
        return false;
    if (m_source.format() != QImage::Format_ARGB32_Premultiplied)
        m_source = m_source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    m_sourceReady = true;
    return true;
}

void MagneticLassoTool::clearStroke()
{
    m_active = false;
    m_source = QImage();
    m_sourceReady = false;
    m_fasteners.clear();
    m_edgePath.clear();
    m_hasTip = false;
    m_hasPrevCursor = false;
    m_travelSinceFastener = 0.0;
}

void MagneticLassoTool::addFastener(const QPointF &gridPos)
{
    const QPointF p(std::round(gridPos.x()), std::round(gridPos.y()));
    if (!m_fasteners.isEmpty() && sameGrid(m_fasteners.last(), p))
        return;

    m_fasteners.append(p);
    if (m_edgePath.isEmpty() || !sameGrid(m_edgePath.last(), p))
        m_edgePath.append(p);
    m_tip = p;
    m_hasTip = true;
    m_travelSinceFastener = 0.0;
}

void MagneticLassoTool::rebuildLiveSegment(const QPointF &tip)
{
    if (m_fasteners.isEmpty())
        return;

    const QPointF anchor = m_fasteners.last();
    int cut = 0;
    for (int i = m_edgePath.size() - 1; i >= 0; --i) {
        if (sameGrid(m_edgePath[i], anchor)) {
            cut = i + 1;
            break;
        }
    }
    m_edgePath.resize(cut);
    if (m_edgePath.isEmpty())
        m_edgePath.append(anchor);

    if (sameGrid(anchor, tip))
        return;

    if (m_sourceReady) {
        // Livewire：Contrast 作弱边门槛（对照 GIMP 段间最优路径）
        // ★ 直接用 m_minEdge，不再乘 0.35：该系数没有依据，且会让路径的弱边判定
        //   比吸附松 3 倍（同一个 Contrast 出现两套口径）。统一后语义是
        //   「吸附时不接受的边，路径上也额外罚」。
        const QVector<QPointF> live =
            MagneticEdgeSnap::walkGridEdge(m_source, anchor, tip, m_minEdge);
        for (const QPointF &p : live)
            m_edgePath.append(p);
    } else {
        m_edgePath.append(tip);
    }
}

void MagneticLassoTool::undoLastFastener()
{
    if (m_fasteners.isEmpty())
        return;

    m_fasteners.removeLast();
    if (m_fasteners.isEmpty()) {
        clearStroke();
        return;
    }

    const QPointF keep = m_fasteners.last();
    int cut = m_edgePath.size();
    for (int i = m_edgePath.size() - 1; i >= 0; --i) {
        if (sameGrid(m_edgePath[i], keep)) {
            cut = i + 1;
            break;
        }
    }
    m_edgePath.resize(cut);
    m_tip = keep;
    m_hasTip = true;
    m_travelSinceFastener = 0.0;
}

void MagneticLassoTool::trackAlongEdge(const QPointF &imagePos)
{
    m_cursorImage = imagePos;
    m_hasCursor = true;

    const QPointF prevTip = m_hasTip ? m_tip : QPointF();
    const bool hadTip = m_hasTip;

    // 引导线终点必须跟着光标：落在光标最近格点。
    // （Width 圆只表示“附近会吸边”；不把 Tip 甩到圆内远端强边，否则线会跑到光标前面。）
    m_tip = MagneticEdgeSnap::nearestGridCorner(imagePos);
    // 仅在光标紧邻 1～2px 内微调到强边，避免终点偏离光标
    if (m_sourceReady && m_searchRadius >= 1) {
        const int localR = qMin(2, m_searchRadius);
        const QPointF nudged = MagneticEdgeSnap::snapToGridCorner(
            m_source, imagePos, localR, m_minEdge, /*preferNear=*/40.0);
        // 偏离光标超过 localR 则仍用光标格点
        if (dist2(nudged, imagePos) <= qreal(localR * localR) + 0.5)
            m_tip = nudged;
    }
    m_hasTip = true;

    if (m_fasteners.isEmpty())
        return;

    // ★ 只有 tip 格点真的变了才重算 livewire：路径只取决于 (末锚点, tip)，
    //   同一格点重复跑一次 Dijkstra 纯属浪费（鼠标在同一像素内抖动也会触发）。
    const bool tipMoved = !hadTip || !sameGrid(prevTip, m_tip);
    if (tipMoved)
        rebuildLiveSegment(m_tip);

    // ★ 按「自上一锚点起光标累计行进距离」落锚（对照 PS：频度与鼠标速度无关）。
    //   旧实现按 tip 变化次数计数 → 划得快落锚稀、划得慢落锚密，密度被速度绑架。
    if (m_hasPrevCursor) {
        m_travelSinceFastener += std::hypot(imagePos.x() - m_prevCursor.x(),
                                           imagePos.y() - m_prevCursor.y());
    }
    m_prevCursor = imagePos;
    m_hasPrevCursor = true;

    if (tipMoved
        && m_travelSinceFastener >= m_anchorSpacing
        && !sameGrid(m_fasteners.last(), m_tip)) {
        addFastener(m_tip);
    }
}

bool MagneticLassoTool::nearFirstWidget(const QPointF &widgetPos, const ToolContext &ctx) const
{
    if (m_edgePath.size() < 3 || m_fasteners.isEmpty() || ctx.viewZoom <= 0.0)
        return false;
    const QPointF first = ctx.imageToWidget(m_fasteners.first());
    return dist2(widgetPos, first) <= kCloseWidgetRadius * kCloseWidgetRadius;
}

bool MagneticLassoTool::commit(const ToolContext &ctx)
{
    QVector<QPointF> pts = m_edgePath;
    if (m_hasTip && (pts.isEmpty() || !sameGrid(pts.last(), m_tip)))
        pts.append(m_tip);

    if (!ctx.document || pts.size() < 3)
        return false;

    const QPolygonF poly(pts);
    const QRectF br = poly.boundingRect();
    if (br.width() < 0.5 && br.height() < 0.5)
        return false;

    const ChannelOp op = m_op;
    clearStroke();
    ctx.document->selectPolygon(poly, op);
    emit repaintRequested();
    return true;
}

void MagneticLassoTool::drawOverlay(QPainter &painter, const ToolContext &ctx) const
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    // 搜索圆：活动中或 Caps Lock 时显示（圆心=光标，半径=Width）
    if (m_hasCursor && ctx.viewZoom > 0.0
        && (m_active || Tool::capsLockOn())) {
        const QPointF c = ctx.imageToWidget(m_cursorImage);
        const qreal r = qreal(qMax(1, m_active ? m_searchRadius : ctx.magneticWidth))
                        * ctx.viewZoom;
        QPen radiusPen(QColor(0, 200, 255, 200));
        radiusPen.setCosmetic(true);
        radiusPen.setWidth(1);
        painter.setPen(radiusPen);
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(c, r, r);
    }

    if (!m_active) {
        painter.restore();
        return;
    }

    // 路径折线
    QVector<QPointF> widgetPts;
    widgetPts.reserve(m_edgePath.size() + 1);
    for (const QPointF &p : m_edgePath)
        widgetPts.append(ctx.imageToWidget(p));
    if (m_hasTip && (m_edgePath.isEmpty() || !sameGrid(m_edgePath.last(), m_tip)))
        widgetPts.append(ctx.imageToWidget(m_tip));

    if (widgetPts.size() >= 2) {
        auto stroke = [&](const QColor &color, qreal dashOffset) {
            QPen pen(color);
            pen.setCosmetic(true);
            pen.setWidth(1);
            pen.setStyle(Qt::DashLine);
            pen.setDashPattern({4, 4});
            pen.setDashOffset(dashOffset);
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            painter.drawPolyline(widgetPts.constData(), widgetPts.size());
        };
        stroke(Qt::white, 0);
        stroke(QColor(0, 200, 255), 4);
    }

    // 锚点：固定控件像素大小，首击一个点也必须清晰可见
    for (int i = 0; i < m_fasteners.size(); ++i) {
        const QPointF w = ctx.imageToWidget(m_fasteners[i]);
        QPen cross(QColor(255, 80, 0));
        cross.setCosmetic(true);
        cross.setWidth(2);
        painter.setPen(cross);
        painter.drawLine(QPointF(w.x() - 8, w.y()), QPointF(w.x() + 8, w.y()));
        painter.drawLine(QPointF(w.x(), w.y() - 8), QPointF(w.x(), w.y() + 8));
        painter.setBrush(QColor(255, 200, 0));
        painter.setPen(QPen(Qt::black, 1));
        painter.drawRect(QRectF(w.x() - 5, w.y() - 5, 10, 10));
        if (i == 0 && m_fasteners.size() >= 3) {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(Qt::white, 1));
            painter.drawEllipse(w, 8, 8);
        }
    }

    if (m_hasTip) {
        const QPointF w = ctx.imageToWidget(m_tip);
        painter.setBrush(QColor(0, 220, 255));
        painter.setPen(QPen(Qt::white, 1));
        painter.drawEllipse(w, 4, 4);
    }

    painter.restore();
}

bool MagneticLassoTool::mousePress(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    if (!event.isLeft() || !ctx.document)
        return false;

    syncParamsFromContext(ctx);
    m_cursorImage = event.imagePos;
    m_hasCursor = true;

    if (event.doubleClick) {
        if (m_active && m_edgePath.size() >= 3) {
            if (ensureSource(ctx)) {
                m_tip = MagneticEdgeSnap::snapToGridCorner(
                    m_source, event.imagePos, m_searchRadius, m_minEdge, 0.0);
                rebuildLiveSegment(m_tip);
                addFastener(m_tip);
            }
            commit(ctx);
            view.requestRepaint();
            return true;
        }
        return true;
    }

    if (m_active && nearFirstWidget(event.widgetPos, ctx)) {
        commit(ctx);
        view.requestRepaint();
        return true;
    }

    if (!m_active) {
        // ★ 首击：立刻落锚，绝不先合成（避免卡住/无反馈）
        m_active = true;
        m_op = opFromModifiers(event.modifiers);
        m_fasteners.clear();
        m_edgePath.clear();
        m_travelSinceFastener = 0.0;
        m_hasPrevCursor = false;
        m_sourceReady = false;
        m_source = QImage();

        const QPointF start = MagneticEdgeSnap::nearestGridCorner(event.imagePos);
        addFastener(start);

        emit repaintRequested();
        view.requestRepaint();
        return true;
    }

    // 已活动：强制落锚
    ensureSource(ctx);
    if (m_sourceReady) {
        m_tip = MagneticEdgeSnap::snapToGridCorner(
            m_source, event.imagePos, m_searchRadius, m_minEdge, 0.0);
    } else {
        m_tip = MagneticEdgeSnap::nearestGridCorner(event.imagePos);
    }
    m_hasTip = true;
    rebuildLiveSegment(m_tip);
    addFastener(m_tip);
    emit repaintRequested();
    view.requestRepaint();
    return true;
}

bool MagneticLassoTool::mouseMove(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    m_cursorImage = event.imagePos;
    m_hasCursor = true;

    if (m_active) {
        syncParamsFromContext(ctx);
        // 首击后第一次移动再合成边缘图
        ensureSource(ctx);
        trackAlongEdge(event.imagePos);
        emit repaintRequested();
        view.requestRepaint();
        return true;
    }

    if (Tool::capsLockOn()) {
        emit repaintRequested();
        view.requestRepaint();
        return true;
    }
    return false;
}

bool MagneticLassoTool::mouseRelease(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(event)
    Q_UNUSED(ctx)
    Q_UNUSED(view)
    // 单击落锚后松手不得清状态
    return m_active;
}

bool MagneticLassoTool::keyPress(int key, Qt::KeyboardModifiers modifiers,
                                 const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(modifiers)

    if (key == Qt::Key_CapsLock) {
        emit repaintRequested();
        view.requestRepaint();
        return true;
    }
    if (!m_active)
        return false;

    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        if (m_edgePath.size() >= 3)
            commit(ctx);
        view.requestRepaint();
        return true;
    }
    if (key == Qt::Key_Escape) {
        clearStroke();
        emit repaintRequested();
        view.requestRepaint();
        return true;
    }
    if (key == Qt::Key_Backspace || key == Qt::Key_Delete) {
        undoLastFastener();
        emit repaintRequested();
        view.requestRepaint();
        return true;
    }
    return false;
}

bool MagneticLassoTool::wantsShortcutOverride(int key, Qt::KeyboardModifiers modifiers) const
{
    Q_UNUSED(modifiers)
    if (!m_active)
        return false;
    return key == Qt::Key_Backspace || key == Qt::Key_Delete
           || key == Qt::Key_Escape || key == Qt::Key_Return || key == Qt::Key_Enter;
}

void MagneticLassoTool::deactivate(const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(ctx)
    clearStroke();
    m_hasCursor = false;
    emit repaintRequested();
    view.requestRepaint();
}

} // namespace Ps
