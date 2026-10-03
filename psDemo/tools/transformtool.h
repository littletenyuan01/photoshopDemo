/**
 * transformtool.h — 自由变换工具（tools 层）。
 *
 * 对照 GIMP Transform Grid：交互态只维护四角/矩阵（trans_info），不逐步写文档历史；
 * 实时预览写回同一图层瓦片（正常投影合成，z 序不变）；确认时才 push 一条撤销。
 * 会话内 Ctrl+Z / Ctrl+Shift+Z 逐步还原/重做变换步骤（对照 PS Free Transform）。
 */
#ifndef TRANSFORMTOOL_H
#define TRANSFORMTOOL_H

#include "tool.h"
#include "engine/painttypes.h"

#include <QImage>
#include <QPointF>
#include <QRect>
#include <QVector>

namespace Ps {

class Layer;
class ImageDocument;

/**
 * 自由变换交互模式（对照 PS 右键菜单：缩放/旋转/斜切/扭曲/透视；
 * GIMP 则是分工具或 Unified 上按手柄区分功能）。
 */
enum class TransformMode {
    Free,         ///< 自由变换（默认：缩放+旋转+移动）
    Scale,        ///< 仅缩放
    Rotate,       ///< 仅旋转
    Skew,         ///< 斜切
    Distort,      ///< 扭曲（角点独立）
    Perspective   ///< 透视（角点独立，提交仍走 quadToQuad）
};

class TransformTool : public Tool
{
    Q_OBJECT

public:
    explicit TransformTool(QObject *parent = nullptr);

    Qt::CursorShape cursorShape() const override;
    bool hasOverlay() const override { return m_session; }
    void drawOverlay(QPainter &painter, const ToolContext &ctx) const override;

    void activate(const ToolContext &ctx, ViewPort &view) override;
    void deactivate(const ToolContext &ctx, ViewPort &view) override;

    bool mousePress(const ToolEvent &event, const ToolContext &ctx, ViewPort &view) override;
    bool mouseMove(const ToolEvent &event, const ToolContext &ctx, ViewPort &view) override;
    bool mouseRelease(const ToolEvent &event, const ToolContext &ctx, ViewPort &view) override;
    bool keyPress(int key, Qt::KeyboardModifiers modifiers,
                  const ToolContext &ctx, ViewPort &view) override;
    bool wantsShortcutOverride(int key, Qt::KeyboardModifiers modifiers) const override;

    bool isSessionActive() const { return m_session; }

    TransformMode mode() const { return m_mode; }
    void setMode(TransformMode mode);

    TransformInterpolation interpolation() const { return m_interpolation; }
    void setInterpolation(TransformInterpolation interp);

    bool linkAspect() const { return m_linkAspect; }
    void setLinkAspect(bool on);

    /** 选项栏只读参数（文档像素 / 百分比 / 度）。 */
    qreal paramX() const { return m_paramX; }
    qreal paramY() const { return m_paramY; }
    qreal paramWPercent() const { return m_paramW; }
    qreal paramHPercent() const { return m_paramH; }
    qreal paramAngleDeg() const { return m_paramAngle; }
    qreal paramSkewHDeg() const { return m_paramSkewH; }
    qreal paramSkewVDeg() const { return m_paramSkewV; }

    /** 由选项栏写入；会重建四角并刷新预览。 */
    void applyNumericParams(qreal x, qreal y, qreal wPercent, qreal hPercent,
                            qreal angleDeg, qreal skewHDeg, qreal skewVDeg);

    bool commitFromUi(const ToolContext &ctx);
    void cancelFromUi(const ToolContext &ctx);

    void rotateByDegrees(qreal degrees);
    void flipHorizontal();
    void flipVertical();

    /** 会话内逐步撤销/重做（不退出自由变换，不碰文档 History）。 */
    bool canSessionUndo() const { return m_session && !m_undoStack.isEmpty(); }
    bool canSessionRedo() const { return m_session && !m_redoStack.isEmpty(); }
    bool undoSessionStep();
    bool redoSessionStep();

signals:
    /** 会话开始/结束（选项栏启用、状态栏提示）。 */
    void sessionChanged(bool active);
    /** 四角或数值参数变化（刷新选项栏显示）。 */
    void paramsChanged();
    /** 会话内撤销栈变化（刷新菜单「还原」可用性）。 */
    void sessionHistoryChanged();
    /** 右键菜单请求（控件坐标；MainWindow 用 canvas->mapToGlobal 弹出）。 */
    void contextMenuRequested(const QPoint &widgetPos);
    /**
     * 用户确认或取消变换后请求回到移动工具（对照 PS：Ctrl+T 是临时模式，不是常驻工具）。
     * 切工具导致的 deactivate 取消不发此信号。
     */
    void returnToMoveRequested();

private:
    enum class Handle {
        None,
        Move,
        Rotate,
        TL, TR, BR, BL,
        Top, Right, Bottom, Left
    };

    /** 会话内一步快照（四角 + 源像素；翻转会改源图）。 */
    struct SessionSnap {
        QPointF corners[4];
        QPointF pivot;
        qreal paramX = 0;
        qreal paramY = 0;
        qreal paramW = 100;
        qreal paramH = 100;
        qreal paramAngle = 0;
        qreal paramSkewH = 0;
        qreal paramSkewV = 0;
        QImage srcPixels;
    };

    bool beginSession(const ToolContext &ctx);
    /** @param userExit true = Esc/✗/✓ 后请求回到 Move；deactivate 传 false。 */
    void cancelSession(const ToolContext &ctx, bool userExit = true);
    bool commitSession(const ToolContext &ctx);
    /** 会话内预览：从层上暂时挖空源矩形（不进文档历史；取消时写回）。 */
    void liftSourceFromLayer(Layer *layer);
    void putSourceBackToLayer(Layer *layer);
    /**
     * 把当前四角预览写回该图层瓦片（走正常投影合成，z 序不变）。
     * 对照 GIMP composited preview：预览在原图层位置参与叠层，不盖住上层。
     */
    void updateLayerPreview(ImageDocument *doc);
    QRect previewDestLocal(const Layer *layer) const;
    void clearLayerRect(Layer *layer, const QRect &localRect);
    /** 目标四角超出层 extent 时扩层（旋转常见），并平移 m_srcLocal。 */
    void ensureLayerFitsCorners(Layer *layer);

    Handle hitTest(const QPointF &widgetPos, const ToolContext &ctx) const;
    QPointF cornerWidget(int index, const ToolContext &ctx) const;
    void applyDrag(const ToolEvent &event, const ToolContext &ctx);

    void syncParamsFromCorners();
    void rebuildCornersFromParams();
    void updatePivotFromCorners();
    void emitParams();

    SessionSnap captureSnap() const;
    void restoreSnap(const SessionSnap &snap);
    void pushSessionUndo();
    void clearSessionHistory();
    bool cornersDiffer(const QPointF a[4], const QPointF b[4]) const;

    bool m_session = false;
    bool m_lifted = false;  ///< 源像素已从层上提起（未进 HistoryStack）
    int m_layerIndex = -1;
    ImageDocument *m_doc = nullptr; ///< 会话期文档（选项栏改参时刷新预览用）
    QRect m_srcLocal;       ///< 源矩形（层内）
    QImage m_srcPixels;     ///< 会话快照（预览与提交共用；拖拽只改四角/矩阵）
    QImage m_srcPixelsOriginal; ///< 提起时的原始源（取消时写回层；翻转不改它）
    /** 进入会话前整层像素+偏移+源矩形（取消/提交前还原扩层几何）。 */
    QImage m_preSessionPixels;
    int m_preSessionOx = 0;
    int m_preSessionOy = 0;
    QRect m_preSrcLocal;
    QRect m_previewDirtyLocal; ///< 层内：上次实时预览脏区（取消/重绘前要清）
    qreal m_baseW = 1.0;    ///< 会话开始时宽（文档像素）
    qreal m_baseH = 1.0;

    QPointF m_corners[4];   ///< 文档坐标 TL,TR,BR,BL
    QPointF m_pivotDoc;

    TransformMode m_mode = TransformMode::Free;
    TransformInterpolation m_interpolation = TransformInterpolation::Bicubic;
    bool m_linkAspect = true;

    qreal m_paramX = 0.0;
    qreal m_paramY = 0.0;
    qreal m_paramW = 100.0;
    qreal m_paramH = 100.0;
    qreal m_paramAngle = 0.0;
    qreal m_paramSkewH = 0.0;
    qreal m_paramSkewV = 0.0;

    Handle m_drag = Handle::None;
    Handle m_hover = Handle::None; ///< 悬停命中，供光标
    QPointF m_pressImage;
    QPointF m_pressCorners[4];
    SessionSnap m_dragStartSnap; ///< 本次拖拽开始前的会话状态（松开时入撤销栈）
    qreal m_pressAngle = 0.0;
    qreal m_pressParamW = 100.0;
    qreal m_pressParamH = 100.0;

    QVector<SessionSnap> m_undoStack;
    QVector<SessionSnap> m_redoStack;
};

} // namespace Ps

#endif // TRANSFORMTOOL_H
