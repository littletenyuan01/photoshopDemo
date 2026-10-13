/**
 * bezier.h — 三次贝塞尔纯函数（engine 层，无状态）。
 *
 * 【对照】GIMP gimpstroke 的求值/扁平化；本仓库只做 cubic，直线 = 控制柄退化到端点。
 * 【为什么独立成 TU】最容易单测；path 编辑与栅格化都复用同一套数学。
 */
#ifndef ENGINE_BEZIER_H
#define ENGINE_BEZIER_H

#include <QPair>
#include <QPointF>
#include <QRectF>
#include <QVector>

namespace Ps {

struct PathAnchor;

namespace Bezier {

/** 三次贝塞尔：p0/p3 端锚点，p1/p2 控制柄（绝对坐标）。 */
struct Seg {
    QPointF p0, p1, p2, p3;
};

/** 由两个锚点构造段（控制柄取自 a.outHandle / b.inHandle）。 */
Seg fromAnchors(const PathAnchor &a, const PathAnchor &b);

QPointF pointAt(const Seg &s, qreal t);
/** 未归一化的导数 B'(t)；调用方自行 normalize。 */
QPointF tangentAt(const Seg &s, qreal t);

/**
 * de Casteljau 分裂：左段.p3 == 右段.p0 == pointAt(t)。
 * 插入锚点「形状不变」依赖此恒等式（见 t_pathgeom）。
 */
QPair<Seg, Seg> splitAt(const Seg &s, qreal t);

/**
 * 控制点凸包 AABB（故意不用精确极值）。
 * 【为什么】精确极值要解一元二次并处理分母为 0，收益只是脏区略小，不值得。
 */
QRectF bounds(const Seg &s);

/**
 * 折线近似；tol = 最大偏离（文档像素）。
 * 【为什么不用固定步数】长段会糊、短段白算；递归到弦偏离 < tol 即停。
 */
QVector<QPointF> flatten(const Seg &s, qreal tol, int *stepsOut = nullptr);

qreal length(const Seg &s, int steps = 32);
/** 按弧长参数取点（描边等距落 dab 用）。 */
QPointF pointAtLength(const Seg &s, qreal len, int steps = 32);

} // namespace Bezier
} // namespace Ps

#endif
