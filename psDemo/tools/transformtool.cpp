/**
 * transformtool.cpp — 自由变换工具实现（tools 层）。
 */
#include "transformtool.h"

#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "domain/selection.h"
#include "engine/op/tilepatch.h"
#include "engine/paintengine.h"
#include "engine/paintselectionclip.h"

#include <QPainter>
#include <QPen>
#include <QTransform>
#include <QtMath>

#include <utility>

namespace Ps {

namespace {

const QColor kBlue(26, 159, 255);
constexpr qreal kHandle = 8.0;
constexpr qreal kHitPad = 10.0;

qreal angleOf(const QPointF &from, const QPointF &to)
{
    return qAtan2(to.y() - from.y(), to.x() - from.x());
}

qreal normalizeDeg(qreal deg)
{
    while (deg > 180.0)
        deg -= 360.0;
    while (deg < -180.0)
        deg += 360.0;
    return deg;
}

} // namespace

TransformTool::TransformTool(QObject *parent)
    : Tool(Ps::ToolId::FreeTransform, parent)
{
}

Qt::CursorShape TransformTool::cursorShape() const
{
    if (!m_session)
        return Qt::ArrowCursor;
    switch ((m_drag != Handle::None) ? m_drag : m_hover) {
    case Handle::Move:
        return Qt::SizeAllCursor;
    case Handle::Rotate:
        return Qt::CrossCursor;
    case Handle::Top:
    case Handle::Bottom:
        return Qt::SizeVerCursor;
    case Handle::Left:
    case Handle::Right:
        return Qt::SizeHorCursor;
    case Handle::TL:
    case Handle::BR:
        return Qt::SizeFDiagCursor;
    case Handle::TR:
    case Handle::BL:
        return Qt::SizeBDiagCursor;
    default:
        return Qt::SizeAllCursor; // 会话中默认调节样式
    }
}

void TransformTool::activate(const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(view)
    beginSession(ctx);
}

void TransformTool::deactivate(const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(view)
    // 切走工具时静默取消，不请求回到 Move（目标工具已由 ToolManager 切好）
    if (m_session)
        cancelSession(ctx, /*userExit=*/false);
}

void TransformTool::setMode(TransformMode mode)
{
    if (m_mode == mode)
        return;
    m_mode = mode;
    emitParams();
}

void TransformTool::setInterpolation(TransformInterpolation interp)
{
    if (m_interpolation == interp)
        return;
    m_interpolation = interp;
    emit repaintRequested();
}

void TransformTool::setLinkAspect(bool on)
{
    m_linkAspect = on;
}

void TransformTool::emitParams()
{
    emit paramsChanged();
}

TransformTool::SessionSnap TransformTool::captureSnap() const
{
    SessionSnap s;
    for (int i = 0; i < 4; ++i)
        s.corners[i] = m_corners[i];
    s.pivot = m_pivotDoc;
    s.paramX = m_paramX;
    s.paramY = m_paramY;
    s.paramW = m_paramW;
    s.paramH = m_paramH;
    s.paramAngle = m_paramAngle;
    s.paramSkewH = m_paramSkewH;
    s.paramSkewV = m_paramSkewV;
    s.srcPixels = m_srcPixels;
    return s;
}

void TransformTool::restoreSnap(const SessionSnap &snap)
{
    for (int i = 0; i < 4; ++i)
        m_corners[i] = snap.corners[i];
    m_pivotDoc = snap.pivot;
    m_paramX = snap.paramX;
    m_paramY = snap.paramY;
    m_paramW = snap.paramW;
    m_paramH = snap.paramH;
    m_paramAngle = snap.paramAngle;
    m_paramSkewH = snap.paramSkewH;
    m_paramSkewV = snap.paramSkewV;
    m_srcPixels = snap.srcPixels;
}

void TransformTool::clearSessionHistory()
{
    m_undoStack.clear();
    m_redoStack.clear();
    emit sessionHistoryChanged();
}

void TransformTool::pushSessionUndo()
{
    m_undoStack.push_back(captureSnap());
    m_redoStack.clear();
    // 限制栈深，避免大图多次翻转占内存过多
    constexpr int kMax = 32;
    while (m_undoStack.size() > kMax)
        m_undoStack.removeFirst();
    emit sessionHistoryChanged();
}

bool TransformTool::cornersDiffer(const QPointF a[4], const QPointF b[4]) const
{
    for (int i = 0; i < 4; ++i) {
        if (qAbs(a[i].x() - b[i].x()) > 1e-6 || qAbs(a[i].y() - b[i].y()) > 1e-6)
            return true;
    }
    return false;
}

bool TransformTool::undoSessionStep()
{
    if (!canSessionUndo())
        return false;
    m_redoStack.push_back(captureSnap());
    restoreSnap(m_undoStack.takeLast());
    updateLayerPreview(m_doc);
    emitParams();
    emit sessionHistoryChanged();
    emit repaintRequested();
    return true;
}

bool TransformTool::redoSessionStep()
{
    if (!canSessionRedo())
        return false;
    m_undoStack.push_back(captureSnap());
    restoreSnap(m_redoStack.takeLast());
    updateLayerPreview(m_doc);
    emitParams();
    emit sessionHistoryChanged();
    emit repaintRequested();
    return true;
}

void TransformTool::updatePivotFromCorners()
{
    m_pivotDoc = QPointF(0.25 * (m_corners[0].x() + m_corners[1].x()
                                 + m_corners[2].x() + m_corners[3].x()),
                         0.25 * (m_corners[0].y() + m_corners[1].y()
                                 + m_corners[2].y() + m_corners[3].y()));
}

void TransformTool::syncParamsFromCorners()
{
    updatePivotFromCorners();
    m_paramX = m_pivotDoc.x();
    m_paramY = m_pivotDoc.y();

    const qreal w = QLineF(m_corners[0], m_corners[1]).length();
    const qreal h = QLineF(m_corners[0], m_corners[3]).length();
    m_paramW = (m_baseW > 1e-6) ? (w / m_baseW * 100.0) : 100.0;
    m_paramH = (m_baseH > 1e-6) ? (h / m_baseH * 100.0) : 100.0;

    m_paramAngle = normalizeDeg(qRadiansToDegrees(
        angleOf(m_corners[0], m_corners[1])));

    // 斜切：左边相对「顶边法向」的偏离（度）
    const qreal topAng = angleOf(m_corners[0], m_corners[1]);
    const qreal leftAng = angleOf(m_corners[0], m_corners[3]);
    m_paramSkewH = normalizeDeg(qRadiansToDegrees(leftAng - (topAng + M_PI_2)));
    // 竖直斜切用底边相对左边的偏离近似
    const qreal botAng = angleOf(m_corners[3], m_corners[2]);
    m_paramSkewV = normalizeDeg(qRadiansToDegrees(botAng - topAng));
}

void TransformTool::rebuildCornersFromParams()
{
    const qreal w = m_baseW * m_paramW / 100.0;
    const qreal h = m_baseH * m_paramH / 100.0;
    const QPointF local[4] = {
        QPointF(-w * 0.5, -h * 0.5),
        QPointF(w * 0.5, -h * 0.5),
        QPointF(w * 0.5, h * 0.5),
        QPointF(-w * 0.5, h * 0.5),
    };

    QTransform t;
    t.translate(m_paramX, m_paramY);
    t.rotate(m_paramAngle);
    const qreal sh = qTan(qDegreesToRadians(m_paramSkewH));
    const qreal sv = qTan(qDegreesToRadians(m_paramSkewV));
    t.shear(sh, sv);

    for (int i = 0; i < 4; ++i)
        m_corners[i] = t.map(local[i]);
    m_pivotDoc = QPointF(m_paramX, m_paramY);
}

void TransformTool::applyNumericParams(qreal x, qreal y, qreal wPercent, qreal hPercent,
                                       qreal angleDeg, qreal skewHDeg, qreal skewVDeg)
{
    if (!m_session)
        return;
    const bool same =
        qAbs(x - m_paramX) < 1e-6 && qAbs(y - m_paramY) < 1e-6
        && qAbs(wPercent - m_paramW) < 1e-6 && qAbs(hPercent - m_paramH) < 1e-6
        && qAbs(angleDeg - m_paramAngle) < 1e-6
        && qAbs(skewHDeg - m_paramSkewH) < 1e-6 && qAbs(skewVDeg - m_paramSkewV) < 1e-6;
    if (same)
        return;

    pushSessionUndo();
    m_paramX = x;
    m_paramY = y;
    if (m_linkAspect) {
        // 以变化更大的一侧为准，保持比例
        const qreal dw = qAbs(wPercent - m_paramW);
        const qreal dh = qAbs(hPercent - m_paramH);
        if (dw >= dh) {
            m_paramW = wPercent;
            m_paramH = wPercent;
        } else {
            m_paramH = hPercent;
            m_paramW = hPercent;
        }
    } else {
        m_paramW = wPercent;
        m_paramH = hPercent;
    }
    m_paramAngle = angleDeg;
    m_paramSkewH = skewHDeg;
    m_paramSkewV = skewVDeg;
    rebuildCornersFromParams();
    updateLayerPreview(m_doc);
    emitParams();
}

bool TransformTool::commitFromUi(const ToolContext &ctx)
{
    return commitSession(ctx);
}

void TransformTool::cancelFromUi(const ToolContext &ctx)
{
    cancelSession(ctx, /*userExit=*/true);
}

void TransformTool::rotateByDegrees(qreal degrees)
{
    if (!m_session)
        return;
    pushSessionUndo();
    m_paramAngle = normalizeDeg(m_paramAngle + degrees);
    rebuildCornersFromParams();
    updateLayerPreview(m_doc);
    emitParams();
}

void TransformTool::flipHorizontal()
{
    if (!m_session || m_srcPixels.isNull())
        return;
    // 对照 PS：框不动，翻转内容。对轴对齐框「镜像四角再交换」会抵消成恒等。
    pushSessionUndo();
    m_srcPixels = m_srcPixels.mirrored(true, false);
    updateLayerPreview(m_doc);
    emitParams();
}

void TransformTool::flipVertical()
{
    if (!m_session || m_srcPixels.isNull())
        return;
    pushSessionUndo();
    m_srcPixels = m_srcPixels.mirrored(false, true);
    updateLayerPreview(m_doc);
    emitParams();
}

bool TransformTool::beginSession(const ToolContext &ctx)
{
    if (m_session)
        cancelSession(ctx, /*userExit=*/false);

    if (!ctx.document)
        return false;
    Layer *layer = ctx.document->activeLayer();
    if (!layer || !layer->isVisible())
        return false;
    // 链接层不可自由变换像素（先栅格化）；对照 GIMP Rasterizable
    if (layer->isLinkedLayer())
        return false;

    const QRect contentDoc = layer->contentBoundsInDocument();
    if (contentDoc.isEmpty())
        return false;

    m_layerIndex = ctx.document->activeLayerIndex();
    // 扩层前整层快照：取消/提交都先回到此状态，避免 expand 泄漏
    m_preSessionPixels = layer->materialize();
    m_preSessionOx = layer->offsetX();
    m_preSessionOy = layer->offsetY();

    m_srcLocal = contentDoc.translated(-layer->offsetX(), -layer->offsetY());
    m_preSrcLocal = m_srcLocal;
    m_srcPixels = TilePatch::extract(layer->tiles(), m_srcLocal);
    if (m_srcPixels.isNull() || m_srcPixels.size().isEmpty()) {
        m_preSessionPixels = QImage();
        m_preSrcLocal = QRect();
        return false;
    }
    m_srcPixelsOriginal = m_srcPixels;

    m_baseW = qMax(1.0, qreal(contentDoc.width()));
    m_baseH = qMax(1.0, qreal(contentDoc.height()));

    m_corners[0] = QPointF(contentDoc.left(), contentDoc.top());
    m_corners[1] = QPointF(contentDoc.right() + 1, contentDoc.top());
    m_corners[2] = QPointF(contentDoc.right() + 1, contentDoc.bottom() + 1);
    m_corners[3] = QPointF(contentDoc.left(), contentDoc.bottom() + 1);

    m_paramX = 0.0;
    m_paramY = 0.0;
    m_paramW = 100.0;
    m_paramH = 100.0;
    m_paramAngle = 0.0;
    m_paramSkewH = 0.0;
    m_paramSkewV = 0.0;
    syncParamsFromCorners();

    // 提起源像素后立刻写回「实时预览」到同一图层，走正常投影 → z 序不变。
    // （若把预览画在画布最上层，会像图层被提到顶。）
    liftSourceFromLayer(layer);
    m_previewDirtyLocal = QRect();
    m_doc = ctx.document;
    clearSessionHistory();
    m_session = true;
    m_drag = Handle::None;
    m_hover = Handle::None;
    m_mode = TransformMode::Free;
    updateLayerPreview(m_doc);
    emit sessionChanged(true);
    emitParams();
    emit cursorChangeRequested(cursor());
    return true;
}

void TransformTool::liftSourceFromLayer(Layer *layer)
{
    if (!layer || m_srcLocal.isEmpty() || m_srcPixels.isNull())
        return;
    clearLayerRect(layer, m_srcLocal);
    m_lifted = true;
}

void TransformTool::putSourceBackToLayer(Layer *layer)
{
    // 取消时必须写回提起时的原始像素（翻转只改 m_srcPixels，不改 Original）
    if (!layer || !m_lifted || m_srcLocal.isEmpty() || m_srcPixelsOriginal.isNull())
        return;
    TilePatch::blit(layer->tiles(), m_srcLocal, m_srcPixelsOriginal);
    layer->invalidateContentBounds();
    m_lifted = false;
}

void TransformTool::clearLayerRect(Layer *layer, const QRect &localRect)
{
    if (!layer || localRect.isEmpty())
        return;
    const QRect r = localRect.intersected(QRect(0, 0, layer->width(), layer->height()));
    if (r.isEmpty())
        return;
    QImage empty(r.size(), QImage::Format_ARGB32_Premultiplied);
    empty.fill(0);
    TilePatch::blit(layer->tiles(), r, empty);
    layer->invalidateContentBounds();
}

QRect TransformTool::previewDestLocal(const Layer *layer) const
{
    if (!layer)
        return {};
    QPolygonF poly;
    for (int i = 0; i < 4; ++i) {
        poly << QPointF(m_corners[i].x() - layer->offsetX(),
                        m_corners[i].y() - layer->offsetY());
    }
    // 不与层 rect 求交：扩层前先要知道真实需要的包围盒
    return poly.boundingRect().toAlignedRect().adjusted(-2, -2, 2, 2);
}

void TransformTool::ensureLayerFitsCorners(Layer *layer)
{
    if (!layer)
        return;
    const QRect need = previewDestLocal(layer);
    const QPoint pad = layer->expandToIncludeLocal(need);
    if (pad.isNull())
        return;
    m_srcLocal.translate(pad);
    m_previewDirtyLocal.translate(pad);
}

void TransformTool::updateLayerPreview(ImageDocument *doc)
{
    if (!m_session || !doc || m_layerIndex < 0)
        return;
    Layer *layer = doc->layers().layerAt(m_layerIndex);
    if (!layer || m_srcPixels.isNull())
        return;

    ensureLayerFitsCorners(layer);

    const QRect newDest = previewDestLocal(layer)
                              .intersected(QRect(0, 0, layer->width(), layer->height()));
    const QRect clearR = m_previewDirtyLocal.united(m_srcLocal).united(newDest)
                             .intersected(QRect(0, 0, layer->width(), layer->height()));
    clearLayerRect(layer, clearR);

    QPointF destLocal[4];
    for (int i = 0; i < 4; ++i) {
        destLocal[i] = QPointF(m_corners[i].x() - layer->offsetX(),
                               m_corners[i].y() - layer->offsetY());
    }

    PaintSelectionClip clip;
    const TransformInterpolation liveInterp =
        (m_interpolation == TransformInterpolation::Nearest)
            ? TransformInterpolation::Nearest
            : TransformInterpolation::Bilinear;
    const QRect written = PaintEngine::freeTransform(
        layer->tiles(), m_srcLocal, m_srcPixels, destLocal,
        /*clearSource=*/false, clip, liveInterp);
    m_previewDirtyLocal = written.isEmpty() ? newDest : written.united(newDest);
    layer->invalidateContentBounds();

    const QRect dirtyDoc = clearR.united(m_previewDirtyLocal)
                               .translated(layer->offsetX(), layer->offsetY());
    if (!dirtyDoc.isEmpty())
        doc->markDirty(dirtyDoc);
    else
        doc->markDirty();
}

void TransformTool::cancelSession(const ToolContext &ctx, bool userExit)
{
    if (!m_session)
        return;
    m_session = false;
    m_drag = Handle::None;
    if (ctx.document) {
        if (Layer *layer = ctx.document->layers().layerAt(m_layerIndex)) {
            // 整层还原到进会话前（含扩层导致的尺寸/偏移）
            if (!m_preSessionPixels.isNull()) {
                layer->replaceFromImage(m_preSessionPixels);
                layer->setOffsetSilent(m_preSessionOx, m_preSessionOy);
                layer->invalidateContentBounds();
            } else {
                const QRect clearR = m_previewDirtyLocal.united(m_srcLocal);
                clearLayerRect(layer, clearR);
                if (m_lifted)
                    putSourceBackToLayer(layer);
            }
            ctx.document->markDirty();
            emit ctx.document->layerPropertiesChanged(m_layerIndex);
        }
    }
    m_lifted = false;
    m_previewDirtyLocal = QRect();
    m_srcPixels = QImage();
    m_srcPixelsOriginal = QImage();
    m_preSessionPixels = QImage();
    m_preSrcLocal = QRect();
    m_doc = nullptr;
    clearSessionHistory();
    emit sessionChanged(false);
    emit repaintRequested();
    if (userExit)
        emit returnToMoveRequested();
}

bool TransformTool::commitSession(const ToolContext &ctx)
{
    if (!m_session || !ctx.document || m_layerIndex < 0)
        return false;
    Layer *layer = ctx.document->layers().layerAt(m_layerIndex);
    if (!layer)
        return false;

    // 还原进会话前几何 → push → 再扩层 + 正式栅格化
    if (!m_preSessionPixels.isNull()) {
        layer->replaceFromImage(m_preSessionPixels);
        layer->setOffsetSilent(m_preSessionOx, m_preSessionOy);
        layer->invalidateContentBounds();
        m_srcLocal = m_preSrcLocal;
    } else {
        const QRect clearR = m_previewDirtyLocal.united(m_srcLocal);
        clearLayerRect(layer, clearR);
        if (m_lifted)
            putSourceBackToLayer(layer);
    }
    m_lifted = false;
    m_previewDirtyLocal = QRect();

    ctx.document->pushLayerPixelsUndo(m_layerIndex, QObject::tr("自由变换"));
    ensureLayerFitsCorners(layer);

    QPointF destLocal[4];
    for (int i = 0; i < 4; ++i) {
        destLocal[i] = QPointF(m_corners[i].x() - layer->offsetX(),
                               m_corners[i].y() - layer->offsetY());
    }

    PaintSelectionClip clip;
    if (!ctx.document->selection().isEmpty()) {
        clip.selection = &ctx.document->selection();
        clip.layerOffsetX = layer->offsetX();
        clip.layerOffsetY = layer->offsetY();
    }

    const QRect dirtyLocal = PaintEngine::freeTransform(
        layer->tiles(), m_srcLocal, m_srcPixels, destLocal,
        /*clearSource=*/true, clip, m_interpolation);
    layer->invalidateContentBounds();
    if (!dirtyLocal.isEmpty()) {
        markDocumentDirty(ctx, dirtyLocal.translated(layer->offsetX(), layer->offsetY()));
    } else {
        ctx.document->markDirty();
        emit repaintRequested();
    }

    m_session = false;
    m_srcPixels = QImage();
    m_srcPixelsOriginal = QImage();
    m_preSessionPixels = QImage();
    m_preSrcLocal = QRect();
    m_doc = nullptr;
    m_drag = Handle::None;
    clearSessionHistory();
    emit sessionChanged(false);
    emit returnToMoveRequested();
    return true;
}

QPointF TransformTool::cornerWidget(int index, const ToolContext &ctx) const
{
    return ctx.imageToWidget(m_corners[index]);
}

TransformTool::Handle TransformTool::hitTest(const QPointF &widgetPos, const ToolContext &ctx) const
{
    const qreal pad = kHitPad;
    const QPointF c[4] = {
        cornerWidget(0, ctx), cornerWidget(1, ctx),
        cornerWidget(2, ctx), cornerWidget(3, ctx)
    };
    const Handle cornerHandles[4] = {Handle::TL, Handle::TR, Handle::BR, Handle::BL};
    for (int i = 0; i < 4; ++i) {
        if (QRectF(c[i].x() - pad, c[i].y() - pad, pad * 2, pad * 2).contains(widgetPos))
            return cornerHandles[i];
    }

    const QPointF mid[4] = {
        (c[0] + c[1]) * 0.5, (c[1] + c[2]) * 0.5,
        (c[2] + c[3]) * 0.5, (c[3] + c[0]) * 0.5
    };
    const Handle sideHandles[4] = {Handle::Top, Handle::Right, Handle::Bottom, Handle::Left};
    for (int i = 0; i < 4; ++i) {
        if (QRectF(mid[i].x() - pad, mid[i].y() - pad, pad * 2, pad * 2).contains(widgetPos))
            return sideHandles[i];
    }

    QPolygonF poly;
    poly << c[0] << c[1] << c[2] << c[3];
    if (poly.containsPoint(widgetPos, Qt::OddEvenFill))
        return Handle::Move;

    // 框外近处 → 旋转
    if (m_mode == TransformMode::Free || m_mode == TransformMode::Rotate) {
        const QRectF box = poly.boundingRect().adjusted(-28, -28, 28, 28);
        if (box.contains(widgetPos))
            return Handle::Rotate;
    }
    return Handle::None;
}

void TransformTool::applyDrag(const ToolEvent &event, const ToolContext &ctx)
{
    Q_UNUSED(ctx)
    const QPointF delta = event.imagePos - m_pressImage;

    auto setCorners = [&](const QPointF c[4]) {
        for (int i = 0; i < 4; ++i)
            m_corners[i] = c[i];
        syncParamsFromCorners();
    };

    QPointF c[4];
    for (int i = 0; i < 4; ++i)
        c[i] = m_pressCorners[i];

    const bool scaleLike = (m_mode == TransformMode::Free || m_mode == TransformMode::Scale);
    const bool distortLike = (m_mode == TransformMode::Distort || m_mode == TransformMode::Perspective
                              || m_mode == TransformMode::Free);
    const bool lockAspect = m_linkAspect || event.modifiers.testFlag(Qt::ShiftModifier)
                            || m_mode == TransformMode::Scale;

    // 仅旋转模式：任何拖拽都当旋转
    if (m_mode == TransformMode::Rotate || m_drag == Handle::Rotate) {
        const qreal a0 = m_pressAngle;
        const qreal a1 = angleOf(m_pivotDoc, event.imagePos);
        QTransform t;
        t.translate(m_pivotDoc.x(), m_pivotDoc.y());
        t.rotateRadians(a1 - a0);
        t.translate(-m_pivotDoc.x(), -m_pivotDoc.y());
        for (int i = 0; i < 4; ++i)
            c[i] = t.map(m_pressCorners[i]);
        setCorners(c);
        return;
    }

    // 斜切模式：边手柄剪切，角点忽略
    if (m_mode == TransformMode::Skew) {
        switch (m_drag) {
        case Handle::Top:
        case Handle::Bottom: {
            // 水平斜切：顶/底边沿 x 平移
            const qreal dx = delta.x();
            if (m_drag == Handle::Top) {
                c[0] = m_pressCorners[0] + QPointF(dx, 0);
                c[1] = m_pressCorners[1] + QPointF(dx, 0);
            } else {
                c[2] = m_pressCorners[2] + QPointF(dx, 0);
                c[3] = m_pressCorners[3] + QPointF(dx, 0);
            }
            break;
        }
        case Handle::Left:
        case Handle::Right: {
            const qreal dy = delta.y();
            if (m_drag == Handle::Left) {
                c[0] = m_pressCorners[0] + QPointF(0, dy);
                c[3] = m_pressCorners[3] + QPointF(0, dy);
            } else {
                c[1] = m_pressCorners[1] + QPointF(0, dy);
                c[2] = m_pressCorners[2] + QPointF(0, dy);
            }
            break;
        }
        case Handle::Move:
            for (int i = 0; i < 4; ++i)
                c[i] = m_pressCorners[i] + delta;
            break;
        default:
            return;
        }
        setCorners(c);
        return;
    }

    switch (m_drag) {
    case Handle::Move:
        for (int i = 0; i < 4; ++i)
            c[i] = m_pressCorners[i] + delta;
        break;
    case Handle::TL:
    case Handle::TR:
    case Handle::BR:
    case Handle::BL: {
        const int idx = (m_drag == Handle::TL) ? 0
                      : (m_drag == Handle::TR) ? 1
                      : (m_drag == Handle::BR) ? 2 : 3;
        const int opp = (idx + 2) % 4;
        if (distortLike && m_mode != TransformMode::Scale && m_mode != TransformMode::Free) {
            // Distort / Perspective：角点独立移动
            c[idx] = m_pressCorners[idx] + delta;
        } else if (scaleLike) {
            c[idx] = m_pressCorners[idx] + delta;
            if (lockAspect) {
                const QPointF anchor = m_pressCorners[opp];
                const QPointF o = m_pressCorners[idx] - anchor;
                const QPointF n = c[idx] - anchor;
                if (qAbs(o.x()) > 1e-3 && qAbs(o.y()) > 1e-3) {
                    const qreal sx = n.x() / o.x();
                    const qreal sy = n.y() / o.y();
                    const qreal s = (qAbs(sx) > qAbs(sy)) ? sx : sy;
                    c[idx] = anchor + QPointF(o.x() * s, o.y() * s);
                }
                // 比例锁定时其余两角按对角缩放
                const int a = (idx + 1) % 4;
                const int b = (idx + 3) % 4;
                const QPointF oa = m_pressCorners[a] - anchor;
                const QPointF ob = m_pressCorners[b] - anchor;
                const qreal s = (qAbs(o.x()) > 1e-3)
                                    ? ((c[idx].x() - anchor.x()) / o.x())
                                    : ((c[idx].y() - anchor.y()) / o.y());
                c[a] = anchor + oa * s;
                c[b] = anchor + ob * s;
            } else if (m_mode == TransformMode::Free) {
                // 自由：只动当前角（与 PS Free 拖角类似；Shift 才锁比例）
            }
        }
        break;
    }
    case Handle::Top:
        if (m_mode == TransformMode::Distort || m_mode == TransformMode::Perspective) {
            c[0] = m_pressCorners[0] + delta;
            c[1] = m_pressCorners[1] + delta;
        } else {
            c[0].ry() += delta.y();
            c[1].ry() += delta.y();
            if (lockAspect && m_baseH > 1e-6) {
                const qreal newH = QLineF(c[0], c[3]).length();
                const qreal ratio = newH / m_baseH;
                const qreal newW = m_baseW * ratio;
                const QPointF mid = (c[0] + c[1]) * 0.5;
                const QPointF dir = c[1] - c[0];
                const qreal len = QLineF(c[0], c[1]).length();
                if (len > 1e-3) {
                    const QPointF u = dir / len;
                    c[0] = mid - u * (newW * 0.5);
                    c[1] = mid + u * (newW * 0.5);
                }
            }
        }
        break;
    case Handle::Bottom:
        if (m_mode == TransformMode::Distort || m_mode == TransformMode::Perspective) {
            c[2] = m_pressCorners[2] + delta;
            c[3] = m_pressCorners[3] + delta;
        } else {
            c[2].ry() += delta.y();
            c[3].ry() += delta.y();
        }
        break;
    case Handle::Left:
        if (m_mode == TransformMode::Distort || m_mode == TransformMode::Perspective) {
            c[0] = m_pressCorners[0] + delta;
            c[3] = m_pressCorners[3] + delta;
        } else {
            c[0].rx() += delta.x();
            c[3].rx() += delta.x();
        }
        break;
    case Handle::Right:
        if (m_mode == TransformMode::Distort || m_mode == TransformMode::Perspective) {
            c[1] = m_pressCorners[1] + delta;
            c[2] = m_pressCorners[2] + delta;
        } else {
            c[1].rx() += delta.x();
            c[2].rx() += delta.x();
        }
        break;
    default:
        return;
    }
    setCorners(c);
}

bool TransformTool::mousePress(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(view)
    if (event.isRight()) {
        if (m_session) {
            emit contextMenuRequested(event.widgetPos.toPoint()); // CanvasView 转全局坐标
            return true;
        }
        return false;
    }
    if (!event.isLeft())
        return false;
    if (!m_session && !beginSession(ctx))
        return false;

    m_drag = hitTest(event.widgetPos, ctx);
    if (m_drag == Handle::None) {
        if (m_mode == TransformMode::Rotate)
            m_drag = Handle::Rotate;
        else
            m_drag = Handle::Move;
    }
    // 缩放模式禁止旋转手柄
    if (m_mode == TransformMode::Scale && m_drag == Handle::Rotate)
        m_drag = Handle::Move;

    m_pressImage = event.imagePos;
    for (int i = 0; i < 4; ++i)
        m_pressCorners[i] = m_corners[i];
    m_dragStartSnap = captureSnap();
    m_pressAngle = angleOf(m_pivotDoc, event.imagePos);
    m_pressParamW = m_paramW;
    m_pressParamH = m_paramH;
    emit cursorChangeRequested(cursor());
    return true;
}

bool TransformTool::mouseMove(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(view)
    if (!m_session)
        return false;
    if (m_drag == Handle::None) {
        const Handle h = hitTest(event.widgetPos, ctx);
        if (h != m_hover) {
            m_hover = h;
            emit cursorChangeRequested(cursor());
        }
        return true;
    }
    if (!(event.buttons & Qt::LeftButton))
        return false;
    applyDrag(event, ctx);
    updateLayerPreview(m_doc ? m_doc : ctx.document);
    emitParams();
    emit repaintRequested();
    return true;
}

bool TransformTool::mouseRelease(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(ctx)
    Q_UNUSED(view)
    if (!m_session)
        return false;
    if (m_drag == Handle::None)
        return event.isRight();
    if (cornersDiffer(m_corners, m_pressCorners)) {
        m_undoStack.push_back(m_dragStartSnap);
        m_redoStack.clear();
        constexpr int kMax = 32;
        while (m_undoStack.size() > kMax)
            m_undoStack.removeFirst();
        emit sessionHistoryChanged();
    }
    m_drag = Handle::None;
    m_hover = hitTest(event.widgetPos, ctx);
    emit cursorChangeRequested(cursor());
    return event.isLeft() || event.isRight();
}

bool TransformTool::wantsShortcutOverride(int key, Qt::KeyboardModifiers modifiers) const
{
    if (!m_session)
        return false;
    if (key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Escape)
        return true;
    // 会话内 Ctrl+Z / Ctrl+Shift+Z = 变换步骤撤销/重做；Esc 才取消整次变换
    if (key == Qt::Key_Z && modifiers.testFlag(Qt::ControlModifier))
        return true;
    if (key == Qt::Key_Y && modifiers.testFlag(Qt::ControlModifier))
        return true;
    return false;
}

bool TransformTool::keyPress(int key, Qt::KeyboardModifiers modifiers,
                             const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(view)
    if (!m_session)
        return false;
    if (key == Qt::Key_Escape) {
        cancelSession(ctx);
        return true;
    }
    if (key == Qt::Key_Z && modifiers.testFlag(Qt::ControlModifier)) {
        if (modifiers.testFlag(Qt::ShiftModifier))
            redoSessionStep();
        else
            undoSessionStep();
        return true;
    }
    if (key == Qt::Key_Y && modifiers.testFlag(Qt::ControlModifier)) {
        redoSessionStep();
        return true;
    }
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        commitSession(ctx);
        return true;
    }
    return false;
}

void TransformTool::drawOverlay(QPainter &painter, const ToolContext &ctx) const
{
    if (!m_session)
        return;
    // 像素预览已写回图层瓦片（updateLayerPreview），投影按正常 z 序合成；此处只画控件框
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);
    QPen pen(kBlue);
    pen.setCosmetic(true);
    pen.setWidth(1);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    QPolygonF box;
    for (int i = 0; i < 4; ++i)
        box << cornerWidget(i, ctx);
    painter.drawPolygon(box);

    painter.setBrush(Qt::white);
    const qreal h = kHandle;
    const qreal half = h * 0.5;
    for (int i = 0; i < 4; ++i) {
        const QPointF c = cornerWidget(i, ctx);
        painter.drawRect(QRectF(c.x() - half, c.y() - half, h, h));
    }
    const QPointF mids[4] = {
        (cornerWidget(0, ctx) + cornerWidget(1, ctx)) * 0.5,
        (cornerWidget(1, ctx) + cornerWidget(2, ctx)) * 0.5,
        (cornerWidget(2, ctx) + cornerWidget(3, ctx)) * 0.5,
        (cornerWidget(3, ctx) + cornerWidget(0, ctx)) * 0.5,
    };
    for (const QPointF &c : mids)
        painter.drawRect(QRectF(c.x() - half, c.y() - half, h, h));

    const QPointF piv = ctx.imageToWidget(m_pivotDoc);
    painter.drawEllipse(piv, 3.0, 3.0);
    painter.restore();
}

} // namespace Ps
