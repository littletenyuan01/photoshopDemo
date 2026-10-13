/**
 * gpurasterbackend.cpp — QRhi D3D11 compute（中值族 + 自定卷积族；独立 RHI 线程；池化）。
 *
 * T4.7：Sharpen/Emboss 喂权重给 custom.comp；Maximum/Minimum 改 median.comp 的 kth。
 * T5.2：点算子 CPU inline 穿透（FilterEval::applyNode），「亮度+中值」不再整条退回 CPU。
 * T5.3：kernel 描述符注册表 —— 加新 GPU 算子 = bindingForOp 表加 1 行（+ .comp 如需新 kernel）。
 *       统一派发：isGpuOp→runNodeOnce(查表+fillUbo+dispatchOnce)；不再 if-else 分支。
 * T5.4：ROI（srcRect/outRect）子矩形裁剪上传 + 回贴，避免全图搬移。
 * T3.0：AddNoise GPU（noise.comp）—— noiseHash 整数 hash 对齐 filtereval.cpp；
 *       放宽位置相关 supports（绑定表逐算子裁决），rasterscheduler 同步放宽。
 *       精度定位：创造性算子，浮点链在 qRound 边界可 ±1（合法容差；区别于中值/卷积的 bit-exact）。
 * FindEdges 是 Sobel 幅值（非线性），不能复用 custom —— 仍走 CPU。
 */
#include "gpurasterbackend.h"

#include "domain/filternode.h"
#include "domain/filterstack.h"
#include "engine/backend/rastercost.h" // isPointOp()：点算子白名单（唯一一处）
#include "engine/filtereval.h"
#include "engine/op/opname.h"
#include "engine/op/gaussianblurfilterop.h"

#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QMetaObject>
#include <QPainter>
#include <QSurfaceFormat>

#include <rhi/qrhi.h>
#include <rhi/qshader.h>

#include <cstdio>
#include <cstring>
#include <vector>

namespace Ps {

namespace {

enum class KernelKind { None, RankSelect, Conv3x3, NoiseHash, BoxBlur };

struct MedianParams {
    qint32 width = 0;
    qint32 height = 0;
    qint32 radius = 0;
    qint32 kth = 0; ///< 0=中值公式；(n+1)/2；最大=n；最小=1
};

/** 与 boxblur.comp std140 Params 一致（16B，含 1 个占位 int 把块补到 16B）。radius = 盒半径。 */
struct BoxParams {
    qint32 width = 0;
    qint32 height = 0;
    qint32 radius = 0;
    qint32 pad = 0;
};

/** 与 noise.comp std140 Params 一致（32B）。amount = noiseAmount()（0..100 百分比）。 */
struct NoiseParams {
    qint32 width = 0;
    qint32 height = 0;
    qint32 offsetX = 0;
    qint32 offsetY = 0;
    quint32 seed = 0x9E3779B9u;
    qint32 amount = 0; ///< noiseAmount() 0..100（GPU: amp = amount*2.55）
    qint32 gaussian = 0; ///< bool: noiseGaussian()
    qint32 mono = 0; ///< bool: noiseMonochromatic()
};

/** 与 custom.comp std140 Params 一致（64B）。 */
struct CustomParams {
    qint32 width = 0;
    qint32 height = 0;
    qint32 scale = 1;
    qint32 offset = 0;
    qint32 weights0[4] = {};
    qint32 weights1[4] = {};
    qint32 weight8 = 0;
    qint32 pad[3] = {};
};
static_assert(sizeof(CustomParams) == 64, "CustomParams must match std140");

constexpr int kUbufBytes = 64; // max(MedianParams, CustomParams)

/**
 * T5.3：Kernel 级描述符 —— 每种 shader 共 1 个。
 * 加新 kernel 类型：这里加一行 + 对应 .comp。
 */
struct KernelDesc {
    KernelKind kind;
    const char *qsbPath;
    int uboBytes;
    int wgSizeX;
    int wgSizeY;
};

const KernelDesc g_kernelDescs[] = {
    {KernelKind::RankSelect, ":/shaders/median.comp.qsb", int(sizeof(MedianParams)), 16, 16},
    {KernelKind::Conv3x3, ":/shaders/custom.comp.qsb", int(sizeof(CustomParams)), 16, 16},
    {KernelKind::NoiseHash, ":/shaders/noise.comp.qsb", int(sizeof(NoiseParams)), 16, 16},
    {KernelKind::BoxBlur, ":/shaders/boxblur.comp.qsb", int(sizeof(BoxParams)), 16, 16},
};

const KernelDesc *descForKind(KernelKind kind)
{
    for (const auto &d : g_kernelDescs)
        if (d.kind == kind)
            return &d;
    return nullptr;
}

/**
 * T5.3：算子绑定表 —— 每个 GPU 算子占 1 行。
 * 加新算子：这里加 1 行 + 调整 isPointOp() 如属点算子。
 */
struct OpKernelBinding {
    OpName op;
    KernelKind kind;
    bool (*paramsOk)(const FilterNode &n);
    /**
     * @p w / @p h 是**本次上传那张图**的尺寸；@p offsetX / @p offsetY 是这张图的
     * (0,0) 在**调用方给的 srcRect 坐标系**里的位置（ROI 裁剪时才非 0，见 runStack）。
     * 位置相关算子必须用 `offset + 本地坐标` 还原绝对坐标，否则噪声图案会随 ROI 平移。
     */
    bool (*fillUbo)(const FilterNode &n, int w, int h, int offsetX, int offsetY, void *ubo);
};

/**
 * median.comp 的 `uint chR[81]`（注释 // 9..81）⇒ n=(2r+1)² ≤ 81 ⇒ **r ≤ 4**。
 * 节点/UI 允许 1..8；超出时 supports() 必须 false，否则静默错像素（台账 #22 / T5.1）。
 */
constexpr int kGpuMedianMaxRadius = 4;

/**
 * boxblur.comp 窗口 = (2r+1)² 全采样（无并行滑窗/共享显存），r 过大时每像素循环膨胀。
 * 设软上限 64：BoxBlur(UI clamp 128) 与 GaussianBlur(gaussRadius≤64 → 盒半径 ≈ σ·1.2 ≤ ~26)
 * 的合法参数内基本都放行；这是「size 上限」护栏，超限走 CPU（行为由 CPU 保证一致）。
 */
constexpr int kGpuBoxMaxRadius = 64;

/**
 * T5.4：一次 applyStack 的 ROI 计划（图内坐标，原点 = image 的 (0,0)）。
 *
 * `active == false` ⇒ 整图求值（`localSrc == image.rect()`），调用方拿到的就是全图结果。
 * `active == true` ⇒ 只上传 `localSrc` 那一块，算完只把 `localOut` 贴回原位。
 * `roiOrigin` 供**位置相关算子**把本地坐标还原成 srcRect 坐标系下的绝对坐标。
 */
struct RoiPlan {
    bool active = false;
    QRect localSrc; ///< 要上传的输入块（图内坐标）
    QPoint pasteAt; ///< 请求区在图内的左上角（= outRect.topLeft()）
    QSize outSize;  ///< 请求区尺寸（= outRect.size()）
};

const OpKernelBinding *bindingForOp(OpName op)
{
    static const OpKernelBinding table[] = {
        {OpName::MedianBlur, KernelKind::RankSelect,
         [](const FilterNode &n)
         { return n.medianRadius() >= 1 && n.medianRadius() <= kGpuMedianMaxRadius; },
         [](const FilterNode &n, int w, int h, int, int, void *ubo) {
             auto *p = static_cast<MedianParams *>(ubo);
             p->width = w;
             p->height = h;
             p->radius = n.medianRadius();
             p->kth = 0; // 中值公式 (n+1)/2
             return true;
         }},
        {OpName::Maximum, KernelKind::RankSelect,
         [](const FilterNode &n)
         { return n.extremumRadius() >= 1 && n.extremumRadius() <= kGpuMedianMaxRadius; },
         [](const FilterNode &n, int w, int h, int, int, void *ubo) {
             auto *p = static_cast<MedianParams *>(ubo);
             p->width = w;
             p->height = h;
             const int r = n.extremumRadius();
             p->radius = r;
             p->kth = (2 * r + 1) * (2 * r + 1); // 最大 = 全部取最后
             return true;
         }},
        {OpName::Minimum, KernelKind::RankSelect,
         [](const FilterNode &n)
         { return n.extremumRadius() >= 1 && n.extremumRadius() <= kGpuMedianMaxRadius; },
         [](const FilterNode &n, int w, int h, int, int, void *ubo) {
             auto *p = static_cast<MedianParams *>(ubo);
             p->width = w;
             p->height = h;
             p->radius = n.extremumRadius();
             p->kth = 1; // 最小 = 全部取第一
             return true;
         }},
        {OpName::CustomFilter, KernelKind::Conv3x3,
         [](const FilterNode &) { return true; },
         [](const FilterNode &n, int w, int h, int, int, void *ubo) {
             auto *p = static_cast<CustomParams *>(ubo);
             p->width = w;
             p->height = h;
             p->scale = n.customScale();
             p->offset = n.customOffset();
             for (int i = 0; i < 4; ++i)
                 p->weights0[i] = n.customWeight(i);
             for (int i = 0; i < 4; ++i)
                 p->weights1[i] = n.customWeight(4 + i);
             p->weight8 = n.customWeight(8);
             return true;
         }},
        {OpName::Sharpen, KernelKind::Conv3x3,
         [](const FilterNode &) { return true; },
         [](const FilterNode &n, int w, int h, int, int, void *ubo) {
             auto *p = static_cast<CustomParams *>(ubo);
             p->width = w;
             p->height = h;
             p->scale = 1;
             p->offset = 0;
             const int s = qBound(1, n.sharpenStrength(), 4);
             const int wgt[9] = {0, -s, 0, -s, 1 + 4 * s, -s, 0, -s, 0};
             for (int i = 0; i < 4; ++i)
                 p->weights0[i] = wgt[i];
             for (int i = 0; i < 4; ++i)
                 p->weights1[i] = wgt[4 + i];
             p->weight8 = wgt[8];
             return true;
         }},
        {OpName::Emboss, KernelKind::Conv3x3,
         [](const FilterNode &) { return true; },
         [](const FilterNode &n, int w, int h, int, int, void *ubo) {
             auto *p = static_cast<CustomParams *>(ubo);
             p->width = w;
             p->height = h;
             p->scale = 1;
             p->offset = 128;
             const int s = qBound(1, n.embossStrength(), 4);
             const int wgt[9] = {-s, -s, 0, -s, 0, s, 0, s, s};
             for (int i = 0; i < 4; ++i)
                 p->weights0[i] = wgt[i];
             for (int i = 0; i < 4; ++i)
                 p->weights1[i] = wgt[4 + i];
             p->weight8 = wgt[8];
             return true;
         }},
        {OpName::AddNoise, KernelKind::NoiseHash,
         [](const FilterNode &n)
         { return n.noiseAmount() > 0; },
         [](const FilterNode &n, int w, int h, int offsetX, int offsetY, void *ubo) {
             auto *p = static_cast<NoiseParams *>(ubo);
             p->width = w;
             p->height = h;
             p->offsetX = offsetX; // ROI 裁剪时非 0：噪声按 srcRect 坐标系的绝对位置取
             p->offsetY = offsetY;
             p->seed = n.noiseSeed();
             p->amount = n.noiseAmount(); // 0..100，GPU amp = amount*2.55
             p->gaussian = n.noiseGaussian() ? 1 : 0;
             p->mono = n.noiseMonochromatic() ? 1 : 0;
             return true;
         }},
        {OpName::BoxBlur, KernelKind::BoxBlur,
         [](const FilterNode &n)
         { return n.blurRadius() >= 1 && n.blurRadius() <= kGpuBoxMaxRadius; },
         [](const FilterNode &n, int w, int h, int, int, void *ubo) {
             auto *p = static_cast<BoxParams *>(ubo);
             p->width = w;
             p->height = h;
             p->radius = n.blurRadius();
             return true;
         }},
        {OpName::GaussianBlur, KernelKind::BoxBlur,
         [](const FilterNode &n)
         { return n.gaussRadius() >= 1 && n.gaussRadius() <= 64; },
         // 三次盒模糊：三个盒半径各自不同，逐趟换 UBO 由 runGaussianOnce 负责（见下）。
         // 这里的 fillUbo 不会被单次 runNodeOnce 调到（GaussianBlur 特判分流），占位即可。
         [](const FilterNode &, int w, int h, int, int, void *ubo) {
             auto *p = static_cast<BoxParams *>(ubo);
             p->width = w;
             p->height = h;
             p->radius = 1;
             return true;
         }},
    };
    for (const auto &b : table)
        if (b.op == op)
            return &b;
    return nullptr;
}

QShader loadShader(const char *qrcPath)
{
    QFile f{QLatin1String(qrcPath)};
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QShader::fromSerialized(f.readAll());
}

/** T4.3：shader 内解/预乘；CPU 只做格式对齐（无逐像素解预乘）。 */
QImage ensurePremulArgb32(const QImage &img)
{
    if (img.format() == QImage::Format_ARGB32_Premultiplied)
        return img;
    return img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

/**
 * T5.3：isGpuOp 改用绑定表查找，不再手写 switch。
 * 加新 GPU 算子 = bindingForOp 表加一行。
 */
bool isGpuOp(OpName op)
{
    return bindingForOp(op) != nullptr;
}

/**
 * T5.2：点算子（`Ps::isPointOp`，白名单本体在 `rastercost.cpp`）—— 逐像素无邻域依赖，
 * 可 CPU inline 穿透。由此「亮度+中值」等常见组合不再因含调整算子而被整条退回 CPU。
 *
 * 【为什么白名单不写在这里】同一个判据还要被 `stackCostNsPerPixel` 用来让点算子
 * **不计入链成本**；写成两份名单必然漂，而本项目已经因为「同一个口径写两处」吃过好几次亏。
 */

/** GPU 后端可处理的算子：内核白名单 + 点算子（CPU inline 穿透）。 */
bool isGpuPathOp(OpName op)
{
    return isGpuOp(op) || isPointOp(op);
}

/**
 * T5.3：gpuParamsSupported 改用绑定表校验参数范围。
 * 加新 GPU 算子 = 绑定表 paramsOk 回调负责校验。
 */
bool gpuParamsSupported(const FilterNode &n)
{
    // T5.2：点算子无需参数校验，直接放行
    if (isPointOp(n.op()))
        return true;
    const OpKernelBinding *b = bindingForOp(n.op());
    return b && b->paramsOk && b->paramsOk(n);
}

/** T4.2/T4.3：PSLITE_GPU_STAGE=1。format=格式对齐；device=upload+kernel+readback。 */
struct StageAccum {
    bool enabled = false;
    double format = 0; ///< ensurePremulArgb32
    double pack = 0;
    double device = 0; ///< beginOffscreenFrame … finish
    double unpack = 0;
    int calls = 0;

    void reset()
    {
        format = pack = device = unpack = 0;
        calls = 0;
    }

    void print(const char *tag, qint64 px) const
    {
        if (!enabled || calls <= 0)
            return;
        const double cpu = format + pack + unpack;
        const double total = cpu + device;
        const double inv = 1.0 / calls;
        std::printf("[gpu-stage] %s n=%d px=%lld | "
                    "format=%.2f pack=%.2f device=%.2f unpack=%.2f | "
                    "cpu=%.2f (%.0f%%) device=%.2f (%.0f%%) total=%.2f ms/call\n",
                    tag, calls, static_cast<long long>(px), format * inv, pack * inv,
                    device * inv, unpack * inv, cpu * inv,
                    total > 0 ? 100.0 * cpu / total : 0.0, device * inv,
                    total > 0 ? 100.0 * device / total : 0.0, total * inv);
    }
};

bool stageProbeEnabled()
{
    return qEnvironmentVariableIsSet("PSLITE_GPU_STAGE");
}

} // namespace

struct GpuRasterBackend::Hub : QObject {
    std::unique_ptr<QRhi> rhi;
    std::unique_ptr<QRhiBuffer> srcBuf;
    std::unique_ptr<QRhiBuffer> dstBuf;
    std::unique_ptr<QRhiBuffer> ubuf;
    std::unique_ptr<QRhiShaderResourceBindings> srb;
    std::unique_ptr<QRhiComputePipeline> pipe;
    StageAccum stage;
    QHash<QByteArray, QShader> cachedShaders;
    QByteArray pipeQrc; // 当前 pipe 对应的 shader qrc 路径
    int bufCapacity = 0;
    bool srcNeedsRecreate = true;

    /**
     * T5.3：按 qsb 路径缓存 shader（hash 查找，不依赖 KernelKind 枚举）。
     * 加新 kernel = g_kernelDescs 加一行即可，这里无需改动。
     */
    const QShader *ensureShader(const char *qsbPath)
    {
        const QByteArray key(qsbPath);
        auto it = cachedShaders.find(key);
        if (it != cachedShaders.end() && it->isValid())
            return &*it;
        QShader s = loadShader(qsbPath);
        if (!s.isValid())
            return nullptr;
        cachedShaders[key] = s;
        return &cachedShaders[key]; // 迭代器在 insert 后失效，重新查
    }

    bool initDevice()
    {
        QRhiD3D11InitParams d3d;
        rhi.reset(QRhi::create(QRhi::D3D11, &d3d));
        if (!rhi) {
            QRhiGles2InitParams gl;
            gl.format = QSurfaceFormat::defaultFormat();
            rhi.reset(QRhi::create(QRhi::OpenGLES2, &gl));
        }
        return rhi && rhi->isFeatureSupported(QRhi::Compute);
    }

    /** 探测成功后回填后端名（供 GpuRasterBackend::backendName() 诊断用）。 */
    QString backendName() const
    {
        if (!rhi)
            return QStringLiteral("none");
        switch (rhi->backend()) {
        case QRhi::D3D11:
            return QStringLiteral("D3D11");
        case QRhi::OpenGLES2:
            return QStringLiteral("OpenGLES2");
        case QRhi::Vulkan:
            return QStringLiteral("Vulkan");
        case QRhi::Metal:
            return QStringLiteral("Metal");
        case QRhi::D3D12:
            return QStringLiteral("D3D12");
        case QRhi::Null:
            return QStringLiteral("Null");
        }
        return QStringLiteral("unknown");
    }

    void shutdown()
    {
        pipe.reset();
        srb.reset();
        ubuf.reset();
        dstBuf.reset();
        srcBuf.reset();
        rhi.reset();
        cachedShaders.clear();
        pipeQrc.clear();
        bufCapacity = 0;
        srcNeedsRecreate = true;
    }

    bool ensureCapacity(int bytes)
    {
        if (dstBuf && ubuf && bufCapacity >= bytes)
            return true;

        srcBuf.reset();
        dstBuf.reset();
        ubuf.reset();
        srb.reset();
        pipe.reset();
        pipeQrc.clear();
        bufCapacity = 0;
        srcNeedsRecreate = true;

        if (!rhi)
            return false;

        dstBuf.reset(rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::StorageBuffer, bytes));
        ubuf.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kUbufBytes));
        if (!dstBuf->create() || !ubuf->create())
            return false;

        bufCapacity = bytes;
        srcNeedsRecreate = true;
        return true;
    }

    /** T5.3：按 KernelDesc 建 srb/pipe；shader 从 hash 取。 */
    bool recreateSrcBinding(const KernelDesc &desc)
    {
        if (!rhi || bufCapacity <= 0)
            return false;
        const QShader *cs = ensureShader(desc.qsbPath);
        if (!cs)
            return false;
        // 【必须是 Immutable】QRhi 明确拒绝 `StorageBuffer + Dynamic`（D3D11 / GLES2 后端
        // 实测 `qWarning("StorageBuffer cannot be combined with Dynamic")` + newBuffer 返回
        // nullptr）→ create() 失败 → tryCreate() 失败 → 整个后端静默退回 CPU（表现为
        // 所有 GPU 门控用例都 SKIP 且 supports() 测试仍在跑，极难察觉）。
        // 复用方式本来就是「每次重建 srcBuf」（每次上传都换一块），Immutable 才是指定类型。
        srcBuf.reset(rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::StorageBuffer, bufCapacity));
        if (!srcBuf->create())
            return false;
        srb.reset(rhi->newShaderResourceBindings());
        srb->setBindings({
            QRhiShaderResourceBinding::bufferLoad(0, QRhiShaderResourceBinding::ComputeStage,
                                                  srcBuf.get()),
            QRhiShaderResourceBinding::bufferLoadStore(1, QRhiShaderResourceBinding::ComputeStage,
                                                       dstBuf.get()),
            QRhiShaderResourceBinding::uniformBuffer(2, QRhiShaderResourceBinding::ComputeStage,
                                                     ubuf.get()),
        });
        if (!srb->create())
            return false;
        pipe.reset(rhi->newComputePipeline());
        pipe->setShaderResourceBindings(srb.get());
        pipe->setShaderStage({QRhiShaderStage::Compute, *cs});
        if (!pipe->create())
            return false;
        pipeQrc = QByteArray(desc.qsbPath);
        srcNeedsRecreate = false;
        return true;
    }

    bool ensurePipe(const KernelDesc &desc)
    {
        const QShader *cs = ensureShader(desc.qsbPath);
        if (!cs)
            return false;
        // 【srcBuf 必须**每次上传都换一块**】`StorageBuffer + Dynamic` 被 QRhi 拒绝
        // （见 recreateSrcBinding），所以 srcBuf 是 Immutable 的 —— 而 Immutable 缓冲
        // 在 QRhi 里**不能二次 uploadStaticBuffer**（D3D11 后端会静默跳过，只告警
        // "Cannot upload to an already initialized Immutable buffer"）。
        // 于是「重建 srcBuf」就是「允许下一次上传」的唯一手段。
        // 【曾经的错】只在需要时重建 ⇒ 链里第二个算子拿到的 srcBuf 里还是**第一个算子算出
        // 的结果**（同一块缓冲没换、上传被跳过）⇒ 整条链静默算错。单算子用例完全照不出来。
        // 判据：换了 shader 必须换缓冲；同 shader（高斯三次盒）则复用同一块 —— 每次
        // dispatchOnce 会重建一次并把 srcNeedsRecreate 留在 false 供本趟使用。
        if (srcNeedsRecreate || !srcBuf || !srb)
            return recreateSrcBinding(desc);
        if (pipe && pipeQrc == QByteArrayView(desc.qsbPath))
            return true;
        // 换了 kernel：旧 srcBuf 里的数据已经用不上了，连缓冲一起重建，保证能再上传
        srcNeedsRecreate = true;
        return recreateSrcBinding(desc);
    }

    /** T5.3：dispatchOnce 改用 KernelDesc；wgSize 从描述符取而非硬编码。 */
    bool dispatchOnce(const QImage &premulArgb32, const KernelDesc &desc, const void *ubo,
                      QImage *outPremul)
    {
        Q_ASSERT(premulArgb32.format() == QImage::Format_ARGB32_Premultiplied);
        const int w = premulArgb32.width();
        const int h = premulArgb32.height();
        const int bytes = w * h * int(sizeof(quint32));
        if (!ensureCapacity(bytes))
            return false;
        if (!ensurePipe(desc))
            return false;

        QElapsedTimer t;
        const void *uploadPtr = nullptr;
        std::vector<quint32> packed;
        if (stage.enabled)
            t.start();
        if (premulArgb32.bytesPerLine() == w * int(sizeof(quint32))) {
            uploadPtr = premulArgb32.constBits();
        } else {
            packed.resize(size_t(w * h));
            for (int y = 0; y < h; ++y) {
                const auto *row = reinterpret_cast<const quint32 *>(premulArgb32.constScanLine(y));
                std::memcpy(packed.data() + size_t(y * w), row, size_t(w) * sizeof(quint32));
            }
            uploadPtr = packed.data();
        }
        if (stage.enabled)
            stage.pack += double(t.nsecsElapsed()) / 1e6;

        QRhiCommandBuffer *cb = nullptr;
        if (stage.enabled)
            t.start();
        if (rhi->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess)
            return false;

        QRhiResourceUpdateBatch *u = rhi->nextResourceUpdateBatch();
        u->uploadStaticBuffer(srcBuf.get(), 0, bytes, uploadPtr);
        u->updateDynamicBuffer(ubuf.get(), 0, desc.uboBytes, ubo);

        cb->beginComputePass(u);
        cb->setComputePipeline(pipe.get());
        cb->setShaderResources();
        cb->dispatch((w + desc.wgSizeX - 1) / desc.wgSizeX,
                     (h + desc.wgSizeY - 1) / desc.wgSizeY, 1);

        QRhiReadbackResult rb;
        bool done = false;
        rb.completed = [&] { done = true; };
        QRhiResourceUpdateBatch *ru = rhi->nextResourceUpdateBatch();
        ru->readBackBuffer(dstBuf.get(), 0, quint32(bytes), &rb);
        cb->endComputePass(ru);
        rhi->endOffscreenFrame();
        rhi->finish();
        if (stage.enabled)
            stage.device += double(t.nsecsElapsed()) / 1e6;

        // 【必须置回 true】srcBuf 是 Immutable，QRhi 不允许同一块 Immutable 缓冲二次上传
        // （D3D11 后端只安静地跳过 ⇒ 下一趟 dispatch 读到的还是上一趟的输入）。
        // 所以每次 dispatch 之后都作废 srcBuf，下一次由 recreateSrcBinding 换一块新的。
        srcNeedsRecreate = true;

        if (!done || rb.data.size() < bytes)
            return false;

        if (stage.enabled)
            t.start();
        *outPremul = QImage(w, h, QImage::Format_ARGB32_Premultiplied);
        if (outPremul->bytesPerLine() == w * int(sizeof(quint32))) {
            std::memcpy(outPremul->bits(), rb.data.constData(), size_t(bytes));
        } else {
            for (int y = 0; y < h; ++y) {
                std::memcpy(outPremul->scanLine(y),
                            rb.data.constData() + size_t(y * w) * sizeof(quint32),
                            size_t(w) * sizeof(quint32));
            }
        }
        if (stage.enabled)
            stage.unpack += double(t.nsecsElapsed()) / 1e6;
        return true;
    }

    /**
     * T5.3：统一节点派发 —— 查绑定表取 KernelDesc + fillUbo，不再需要按算子分 if-else。
     * 加新 GPU 算子 = bindingForOp 表加一行 + 配对的 .comp（如需要）；此方法无需改动。
     */
    bool runNodeOnce(const QImage &premulArgb32, const FilterNode &n, const QPoint &roiOrigin,
                     QImage *outPremul)
    {
        // T3.1：GaussianBlur = 三次盒模糊（半径各异），逐趟换 UBO 连续作用于同一张图
        if (n.op() == OpName::GaussianBlur)
            return runGaussianOnce(premulArgb32, n, outPremul);
        const OpKernelBinding *b = bindingForOp(n.op());
        if (!b || !b->fillUbo)
            return false;
        const KernelDesc *desc = descForKind(b->kind);
        if (!desc)
            return false;
        alignas(16) char ubo[64] = {};
        // roiOrigin：本地 (0,0) 在 srcRect 坐标系里的位置（整图时为 (0,0)）。
        // 位置相关算子（AddNoise）靠它把本地坐标还原成绝对坐标。
        if (!b->fillUbo(n, premulArgb32.width(), premulArgb32.height(), roiOrigin.x(),
                        roiOrigin.y(), ubo))
            return false;
        return dispatchOnce(premulArgb32, *desc, ubo, outPremul);
    }

    /** T3.1：高斯 = 三次盒模糊（与 CPU GaussianBlurFilterOp::process 相同口径）。 */
    bool runGaussianOnce(const QImage &premulArgb32, const FilterNode &n, QImage *outPremul)
    {
        const KernelDesc *desc = descForKind(KernelKind::BoxBlur);
        if (!desc || desc->uboBytes != int(sizeof(BoxParams)))
            return false;
        int radii[3] = {0, 0, 0};
        GaussianBlurFilterOp::boxRadiiFor(n.gaussRadius(), radii);
        QImage work = premulArgb32;
        for (int i = 0; i < 3; ++i) {
            if (radii[i] <= 0)
                continue; // 盒宽 1 = 恒等，跳过（对齐 CPU）
            alignas(16) BoxParams ubo;
            ubo.width = premulArgb32.width();
            ubo.height = premulArgb32.height();
            ubo.radius = radii[i];
            QImage next;
            if (!dispatchOnce(work, *desc, &ubo, &next))
                return false;
            work = next;
        }
        *outPremul = work;
        return true;
    }

    ApplyStackResult runStack(QImage &image, const FilterStack &stack, const QRect &srcRect,
                              const QRect &outRect,
                              const std::function<bool()> &shouldAbort)
    {
        if (shouldAbort && shouldAbort()) {
            image = QImage();
            return ApplyStackResult::Cancelled;
        }
        if (image.isNull())
            return ApplyStackResult::Ok;

        stage.enabled = stageProbeEnabled();
        if (stage.enabled)
            stage.reset();

        QElapsedTimer t;
        if (stage.enabled)
            t.start();
        // —— T5.4 ROI：算出「要上传哪一块 / 结果贴回哪一行列」，再决定是否真的裁剪 ——
        //
        // 【坐标系：srcRect / outRect 与 image 是**同一个坐标系**】
        // 接口注释说这两者是「文档坐标」，但两条生产路径传进来的「文档原点」其实不同：
        //   · Layer::compositeRasterRegion：srcDoc = srcWant + layerOffset（层的文档位置）；
        //   · Compositor 调整层分支：srcWant/area 是文档坐标，而 below 是从 (0,0) 取出的快照。
        // 唯一能同时解释这两条、且与调用方读回结果的方式一致的口径是：
        //   **手里这张图就是 srcRect 那一块**（`image.size() == srcRect.size()`，
        //   `image` 的 (0,0) 就是 srcRect 的左上角在调用方坐标系里的位置），
        //   求值结果写在 `outRect` 处。
        // 于是：裁剪 = `srcRect ∩ image.rect()`（整幅时为恒等、不搬像素），
        //       回贴 = `drawImage(outRect.topLeft(), …)`（**绝对**，不减 srcRect 原点）。
        // 【曾经的错】中途一版把回贴位算成 `outRect - srcRect.topLeft()` ⇒ 图像原点与 srcRect
        // 不重合时**少贴一圈**（实测 160×120 的 ROI 被贴到偏 (-60,-50) 处，越权改了 11573 个
        // 像素，而 outRect 内还是旧值）。这条不报错、不崩溃，只是**默默贴错地方**，
        // 只能靠门控用例发现（`gpukernels` 的「outRect 外逐字节不变」+「请求区内确实被求值」）。
        RoiPlan roi;
        if (!srcRect.isNull() || !outRect.isNull()) {
            const QRect src = srcRect.isNull() ? image.rect() : srcRect;
            const QRect out = outRect.isNull() ? image.rect() : outRect;
            // 图外的输入取不到：夹到图内（层边缘 = 透明 abyss，与 CPU 侧 intersect 同义）
            const QRect crop = src.intersected(image.rect());
            if (crop.isEmpty() || !crop.contains(out))
                return ApplyStackResult::Failed; // 契约不成立：宁走 CPU 补算，也不凭猜贴
            roi.active = crop != image.rect();
            roi.localSrc = crop;
            roi.pasteAt = out.topLeft();
            roi.outSize = out.size();
        }
        QImage work = roi.active ? ensurePremulArgb32(image).copy(roi.localSrc)
                                 : ensurePremulArgb32(image);
        if (stage.enabled)
            stage.format += double(t.nsecsElapsed()) / 1e6;

        for (int i = 0; i < stack.count(); ++i) {
            const FilterNode &n = stack.at(i);
            if (!n.isEnabled())
                continue;
            if (shouldAbort && shouldAbort()) {
                image = QImage();
                return ApplyStackResult::Cancelled;
            }
            QImage next;
            bool ok = false;
            // T5.3：GPU 内核算子统一走 runNodeOnce（查绑定表 + fillUbo + dispatchOnce）
            if (isGpuOp(n.op())) {
                ok = runNodeOnce(work, n, roi.localSrc.topLeft(), &next);
            } else if (isPointOp(n.op())) {
                // T5.2：点算子 CPU inline 穿透（FilterEval::applyNode 就地修改）
                next = work;
                FilterEval::applyNode(next, n, shouldAbort);
                ok = true;
            }
            if (!ok)
                return ApplyStackResult::Failed;
            work = next;
        }

        // T5.4：只把**请求区**回贴回去 —— 其余像素保持调用前的值。
        // （分块合成时调用方只认 outRect；把 ROI 外也写掉等于越权改别人的像素。）
        if (roi.active) {
            if (image.format() != QImage::Format_ARGB32_Premultiplied)
                image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            // 回贴：**只写请求区**（其余像素保持调用前的值 —— 分块合成时调用方只认 outRect，
            // 越权写掉别人的像素会静默错图）。
            // work 里 outRect 所在的局部矩形 = (outRect - srcRect.topLeft())，取它的左上角。
            const QPoint inWork = roi.pasteAt - roi.localSrc.topLeft();
            const QRect outSize(roi.pasteAt, roi.outSize);
            QPainter painter(&image);
            painter.setCompositionMode(QPainter::CompositionMode_Source);
            painter.drawImage(outSize.topLeft(), work, QRect(inWork, roi.outSize));
            painter.end();
        } else {
            image = work;
        }
        if (stage.enabled) {
            stage.calls = 1;
            stage.print("applyStack", qint64(image.width()) * image.height());
        }
        return ApplyStackResult::Ok;
    }
};

std::unique_ptr<GpuRasterBackend> GpuRasterBackend::tryCreate()
{
    if (!QGuiApplication::instance())
        return nullptr;

    auto backend = std::unique_ptr<GpuRasterBackend>(new GpuRasterBackend);
    backend->m_thread = std::make_unique<QThread>();
    backend->m_hub = std::make_unique<Hub>();
    backend->m_hub->moveToThread(backend->m_thread.get());
    backend->m_thread->start();

    bool ok = false;
    // 与 applyStack 对齐：已经在 Hub 线程上时不能再用 BlockingQueuedConnection（会自锁死）。
    // 但**不能顺手把 ok 写死成 true** —— 那样 initDevice() 失败会被吞掉，
    // m_ok 说「可用」而 rhi 是空，后面每个 dispatch 都 Failed（比探测失败更难查）。
    if (QThread::currentThread() == backend->m_thread.get()) {
        ok = backend->m_hub->initDevice();
    } else {
        const bool invoked = QMetaObject::invokeMethod(
            backend->m_hub.get(), [&] { ok = backend->m_hub->initDevice(); },
            Qt::BlockingQueuedConnection);
        if (!invoked)
            ok = false;
    }
    if (!ok) {
        backend->m_thread->quit();
        backend->m_thread->wait();
        return nullptr;
    }
    backend->m_ok = true;
    backend->m_backendName = backend->m_hub->backendName();
    return backend;
}

GpuRasterBackend::~GpuRasterBackend()
{
    if (m_thread && m_thread->isRunning() && m_hub) {
        QMetaObject::invokeMethod(
            m_hub.get(), [this] { m_hub->shutdown(); }, Qt::BlockingQueuedConnection);
        m_thread->quit();
        m_thread->wait();
    }
}

bool GpuRasterBackend::supports(const FilterStack &stack) const
{
    if (!m_ok || !stack.hasEnabled())
        return false;
    // T3.0：位置相关不再整体拒绝——AddNoise 是位置锚定的逐像素散列（等价点算子），
    // 已在绑定表内；几何位移（Offset/Ripple）无绑定项，下面逐算子即拒绝。
    if (stack.containsOffset())
        return false;
    bool any = false;
    for (int i = 0; i < stack.count(); ++i) {
        const FilterNode &n = stack.at(i);
        if (!n.isEnabled())
            continue;
        // 能力声明 = op 名 + 参数范围（与 median.comp 真实上限对齐，T5.1）
        // T5.2：点算子通过 CPU inline 穿透，同样视为 GPU 路径可处理
        if (!isGpuPathOp(n.op()) || !gpuParamsSupported(n))
            return false;
        any = true;
    }
    return any;
}

ApplyStackResult GpuRasterBackend::applyStack(QImage &image, const FilterStack &stack,
                                              const QRect &srcRect, const QRect &outRect,
                                              const std::function<bool()> &shouldAbort)
{
    if (!supports(stack))
        return ApplyStackResult::Unsupported;
    if (shouldAbort && shouldAbort()) {
        image = QImage();
        return ApplyStackResult::Cancelled;
    }
    if (image.isNull())
        return ApplyStackResult::Ok;
    if (!m_hub || !m_thread)
        return ApplyStackResult::Failed;

    QMutexLocker lock(&m_mutex);
    ApplyStackResult result = ApplyStackResult::Failed;
    if (QThread::currentThread() == m_thread.get()) {
        result = m_hub->runStack(image, stack, srcRect, outRect, shouldAbort);
    } else {
        const bool invoked = QMetaObject::invokeMethod(
            m_hub.get(),
            [&] { result = m_hub->runStack(image, stack, srcRect, outRect, shouldAbort); },
            Qt::BlockingQueuedConnection);
        if (!invoked)
            return ApplyStackResult::Failed;
    }
    return result;
}

} // namespace Ps