/**
 * path.h — 路径数据模型（domain 层）。
 *
 * 【对照 GIMP】app/path/gimppath.c 的 GimpPath + GimpStroke。
 * 【刻意不照抄】GIMP 把锚点与控制柄都存成 GimpAnchor、靠 type 区分，
 * 是为了支持多种 stroke 实现（GimpStrokeClass 有 30+ 虚函数）。本仓库没有多态
 * stroke 的需求，照抄等于白背 30 个虚函数 —— 故采用 PS 口径：一个锚点自带
 * in/out 两个控制柄，且**存绝对文档坐标**（存相对偏移在"移动整条路径/多选拖动"
 * 时会累积浮点误差；PS 的 .psd 里也是绝对坐标）。
 */
#ifndef DOMAIN_PATH_H
#define DOMAIN_PATH_H

#include <QDataStream>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QTransform>
#include <QVector>

namespace Ps {

/// 单个锚点（PS 口径：自带前后控制柄，绝对文档坐标）。
struct PathAnchor
{
    QPointF point;    ///< 锚点位置（文档坐标）
    QPointF inHandle; ///< 进入控制柄（绝对坐标）
    QPointF outHandle; ///< 离开控制柄（绝对坐标）
    /**
     * 是否由用户显式指定（拖过控制柄）。
     * 【为什么需要】弯度钢笔会自动推导控制柄；一旦用户手动拖过，自动推导就不许再覆盖它。
     * 这是"自动曲线 + 手动微调可共存"的唯一开关。
     */
    bool inExplicit = false;
    bool outExplicit = false;
    /// 尖角（两侧控制柄各自独立）；false = 平滑（共线）
    bool corner = false;
    bool selected = false; ///< 供多选拖动

    bool hasIn() const { return inExplicit || inHandle != point; }
    bool hasOut() const { return outExplicit || outHandle != point; }
};

/// 一条子路径（对照 GIMP 的一个 GimpStroke）。
struct SubPath
{
    QVector<PathAnchor> anchors; ///< 顺序即描画顺序
    bool closed = false;

    bool isEmpty() const { return anchors.size() < 2; }
    int segmentCount() const;
};

/// 一条命名路径（对照 GimpPath：一条路径可含多个子轮廓）。
class Path
{
public:
    QString name;
    QVector<SubPath> subPaths;
    bool visible = true;

    QRectF bounds() const;
    bool isEmpty() const;
    int totalAnchors() const;

    /// 在 (sub, seg) 段上按参数 t∈(0,1) 插入锚点；de Casteljau 分裂，形状不变。
    bool insertAnchor(int sub, int seg, qreal t);
    /// 删除锚点；保持端点不动。
    bool deleteAnchor(int sub, int anchorIdx);
    /// 移动锚点：连带平移两个控制柄（保持相对形状）。
    bool moveAnchor(int sub, int anchorIdx, const QPointF &newPoint);
    bool setCorner(int sub, int anchorIdx, bool corner);
    /// 把某侧控制柄设为显式；mirror 时另一侧镜像（平滑）。
    bool setHandle(int sub, int anchorIdx, bool isOut, const QPointF &h, bool mirror);
    /// 弯度/自动曲线：按 Catmull-Rom 重算所有非 explicit 的控制柄。
    void recomputeAutoHandles(int sub, qreal tension = 1.0);

    struct Hit {
        int sub = -1;
        int index = -1;
        qreal dist = 0.0;
    };
    Hit hitTestAnchor(const QPointF &docPos, qreal tolDoc) const;

    struct HandleHit {
        int sub = -1;
        int index = -1;
        bool isOut = false;
        qreal dist = 0.0;
    };
    HandleHit hitTestHandle(const QPointF &docPos, qreal tolDoc,
                            bool selectedOnly = true) const;

    struct SegHit {
        int sub = -1;
        int seg = -1;
        qreal t = 0.0;
        qreal dist = 0.0;
    };
    SegHit nearestOnPath(const QPointF &docPos, qreal tolDoc) const;

    void translate(const QPointF &delta);
    void transform(const QTransform &m);

    QPainterPath toPainterPath() const;
    static Path fromPainterPath(const QPainterPath &pp);

    void write(QDataStream &out) const;
    static Path read(QDataStream &in);
};

} // namespace Ps

#endif
