/**
 * layerfilterjob.h — 图层滤镜的**异步定稿**任务（engine 层）。
 *
 * 【它解决什么】滤镜「定稿」（点确定 / 取消 / 面板改参数）目前是**同步**的：整层标脏 →
 * `Projection::sync` 逐块合成 → 每块在合成路径里现场跑滤镜链。点滤镜尚可，**面积算子**
 * 就顶不住：实测 512² r=3 的中值单次 49 ms，而投影按 64×64 块推进 —— 演示文档
 * （2480×3508 ≈ 8.7 Mpx）里一个 64×64 块带 pad 后要几十毫秒，整层就是**秒级**，
 * 期间主线程虽分帧但每一帧都在算滤镜，UI 表现为「一块一块慢慢糊」且无法取消。
 *
 * 【做法】把「跑滤镜」这段挪到工作线程，算完把结果**按矩形**交给层缓存
 * （`Layer::setRegionFilterResult`）。随后的 `Projection::sync` 逐块合成时命中缓存，
 * 只做取子图 + 混合 —— 于是重滤镜的整层重算不再发生在合成路径上。
 *
 * 【线程安全的关键：先快照，再算】
 * 工作线程**绝不**碰 `ImageDocument` / `Layer` / `TileBuffer`：主线程随时可能写瓦片，
 * 跨线程读稀疏瓦片是数据竞争。所以：
 *   · `start()` 在**主线程**把「请求区 + pad」用 `TilePatch::extract` 快照成一张独立 `QImage`；
 *   · 工作线程只在这张快照上跑 `FilterStack::apply`；
 *   · 结果回主线程后才写进 Layer 缓存（回调里再复查层是否还在）。
 * 快照成本是一次区域拷贝（占整层比例很小），换来的是**不需要加锁**。
 *
 * 【取消】两级：
 *   1. 谓词协作式中止 —— `FilterStack::apply(source, shouldAbort)` 让面积算子在自己的
 *      行循环里周期性检查（见 engine/op/filterop.h 的 `FilterContext::shouldAbort`）；
 *   2. **世代号丢弃** —— 每次 `start()` 自增 `runId`，回调只认自己那一代：
 *      被新任务接替、被 `cancel()`、或层内容变了（`Layer::contentGeneration`）都写不进缓存。
 * 只靠 1 不够（取消有延迟），只靠 2 不够（会白算一份没人要的结果）。
 *
 * 【降采样预览】`previewDivisor > 1` 时：快照先缩小该倍数、**滤镜的空间参数同步缩小**
 * （`FilterNode::scaledForDownsampledPreview`）、求值后再放大回 `want` 尺寸。换来的是
 * 求值量按倍数平方下降（PS 那种「更糊但更快」）。**只用于拖参数预览**，定稿一律传 1。
 *
 * 【为什么允许「接替」在跑的任务】同一份滤镜改动会**连续触发两次**
 * `contentChanged`（临时节点 + 正式加入、`markDirty` 与属性广播），若第二次因
 * 「上一个还在跑」而拒启，就会出现「第一个被取消、第二个没起来」→ **一次都不写缓存**
 * （本轮实测端到端卡在这里）。所以 `start()` 总是接替：旧任务靠 runId 失配自行作废。
 */
#ifndef ENGINE_LAYERFILTERJOB_H
#define ENGINE_LAYERFILTERJOB_H

#include "domain/filterstack.h"

#include <QFuture>
#include <QImage>
#include <QObject>
#include <QPoint>
#include <QRect>

#include <atomic>
#include <memory>

namespace Ps {

class ImageDocument;
class Layer;

/**
 * 单个图层的「异步定稿」任务（一次一个；重复发起前由调用方先 `cancel()`）。
 *
 * 生命周期：`start()` → 后台求值 → `finished()`（主线程）。任务对象归调用方（画布）所有。
 * 同一对象可反复 `start()`：新一代替换旧一代（旧的写不进去）。
 */
class LayerFilterJob : public QObject
{
    Q_OBJECT

public:
    explicit LayerFilterJob(QObject *parent = nullptr);
    ~LayerFilterJob() override;

    /**
     * 在**主线程**快照 + 启动后台求值。
     *
     * @param doc         目标文档（借用；回调前会复查它是否还活着）
     * @param layerIndex  目标图层下标（回调时复查看层是否还在，避免裸指针悬垂）
     * @param want        要产出的**文档矩形**（= 层的 bounds ∩ 画布；结果缓存的覆盖范围）
     * @param previewDivisor 预览降采样倍数（1 = 原分辨率；>1 = 「更糊但更快」的预览）。
     *        见 `FilterNode::scaledForDownsampledPreview`：空间参数会同步缩小，
     *        结果再放大回 `want` 尺寸写进缓存 —— 故**只用于预览**，定稿必须传 1。
     * @return false = 参数不成立 / 已有任务在跑（调用方应退回同步路径）
     */
    bool start(ImageDocument *doc, int layerIndex, const QRect &want, int previewDivisor = 1);

    /** 是否正在后台求值。 */
    bool isRunning() const { return m_running.load(); }

    /** 后台是否还有**任何**在途工作（含已被新任务接替、正在等回调的旧任务）。 */
    bool hasRunningWork() const { return m_runId.load() > 0; }

    /** 请求取消：协作式谓词立即生效，且结果一定不会被写入缓存。 */
    void cancel();

    /** 当前进度 [0,1]（信息展示用；由后台按滤镜节点数推进）。 */
    qreal progress() const { return m_progress.load(); }

signals:
    /** 后台求值结束（写缓存成功 / 取消 / 失败都会发，便于调用方收尾与刷新）。 */
    void finished(bool wroteCache);

private:
    /**
     * 在途 future。**析构必须等它**（见 `~LayerFilterJob`）：后台 lambda 捕获了 `this`
     * 并会写 `m_progress`，所以对象不能先走。
     */
    QFuture<void> m_future;
    std::shared_ptr<std::atomic<bool>> m_cancel; ///< 与后台共享的取消旗标
    /**
     * 在途任务令牌：0 = 无；>0 = 该号任务在跑。
     * `start()` 自增并写这里；回调只处理 `捕获号 == m_runId` 的那一代（过期的一律丢弃）。
     */
    std::atomic<quint32> m_runId {0};
    std::atomic<bool> m_running {false};
    std::atomic<qreal> m_progress {0.0};
};

} // namespace Ps

#endif // ENGINE_LAYERFILTERJOB_H
