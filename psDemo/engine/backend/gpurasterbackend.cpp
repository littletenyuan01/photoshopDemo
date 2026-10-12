/**
 * gpurasterbackend.cpp — QRhi D3D11 compute（中值族 + 自定卷积族；独立 RHI 线程；池化）。
 *
 * T4.7：Sharpen/Emboss 喂权重给 custom.comp；Maximum/Minimum 改 median.comp 的 kth。
 * FindEdges 是 Sobel 幅值（非线性），不能复用 custom —— 仍走 CPU。
 */
#include "gpurasterbackend.h"

#include "domain/filternode.h"
#include "domain/filterstack.h"
#include "engine/op/opname.h"

#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QMetaObject>
#include <QSurfaceFormat>

#include <rhi/qrhi.h>
#include <rhi/qshader.h>

#include <cstdio>
#include <cstring>
#include <vector>

namespace Ps {

namespace {

enum class KernelKind { None, Median, Custom };

struct MedianParams {
    qint32 width = 0;
    qint32 height = 0;
    qint32 radius = 0;
    qint32 kth = 0; ///< 0=中值公式；(n+1)/2；最大=n；最小=1
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

bool isGpuOp(OpName op)
{
    switch (op) {
    case OpName::MedianBlur:
    case OpName::Maximum:
    case OpName::Minimum:
    case OpName::CustomFilter:
    case OpName::Sharpen:
    case OpName::Emboss:
        return true;
    default:
        return false;
    }
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
    QShader cachedMedianCs;
    QShader cachedCustomCs;
    KernelKind pipeKind = KernelKind::None;
    int bufCapacity = 0;
    bool srcNeedsRecreate = true;

    bool ensureShader(KernelKind kind)
    {
        if (kind == KernelKind::Median) {
            if (cachedMedianCs.isValid())
                return true;
            cachedMedianCs = loadShader(":/shaders/median.comp.qsb");
            return cachedMedianCs.isValid();
        }
        if (kind == KernelKind::Custom) {
            if (cachedCustomCs.isValid())
                return true;
            cachedCustomCs = loadShader(":/shaders/custom.comp.qsb");
            return cachedCustomCs.isValid();
        }
        return false;
    }

    const QShader &shaderFor(KernelKind kind) const
    {
        return kind == KernelKind::Custom ? cachedCustomCs : cachedMedianCs;
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

    void shutdown()
    {
        pipe.reset();
        srb.reset();
        ubuf.reset();
        dstBuf.reset();
        srcBuf.reset();
        rhi.reset();
        cachedMedianCs = {};
        cachedCustomCs = {};
        pipeKind = KernelKind::None;
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
        pipeKind = KernelKind::None;
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

    /** 换 src + 按 kind 建 srb/pipe。 */
    bool recreateSrcBinding(KernelKind kind)
    {
        if (!rhi || bufCapacity <= 0 || !ensureShader(kind))
            return false;
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
        pipe->setShaderStage({QRhiShaderStage::Compute, shaderFor(kind)});
        if (!pipe->create())
            return false;
        pipeKind = kind;
        srcNeedsRecreate = false;
        return true;
    }

    bool ensurePipe(KernelKind kind)
    {
        if (!ensureShader(kind))
            return false;
        if (srcNeedsRecreate || !srcBuf || !srb)
            return recreateSrcBinding(kind);
        if (pipe && pipeKind == kind)
            return true;
        // 同 src，只换 shader/pipe
        pipe.reset(rhi->newComputePipeline());
        pipe->setShaderResourceBindings(srb.get());
        pipe->setShaderStage({QRhiShaderStage::Compute, shaderFor(kind)});
        if (!pipe->create())
            return false;
        pipeKind = kind;
        return true;
    }

    bool dispatchOnce(const QImage &premulArgb32, KernelKind kind, const void *ubo, int uboBytes,
                      QImage *outPremul)
    {
        Q_ASSERT(premulArgb32.format() == QImage::Format_ARGB32_Premultiplied);
        const int w = premulArgb32.width();
        const int h = premulArgb32.height();
        const int bytes = w * h * int(sizeof(quint32));
        if (!ensureCapacity(bytes))
            return false;
        if (!ensurePipe(kind))
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
        u->updateDynamicBuffer(ubuf.get(), 0, uboBytes, ubo);

        cb->beginComputePass(u);
        cb->setComputePipeline(pipe.get());
        cb->setShaderResources();
        cb->dispatch((w + 15) / 16, (h + 15) / 16, 1);

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

        srcNeedsRecreate = (srcBuf->type() == QRhiBuffer::Immutable);

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

    bool runMedianOnce(const QImage &premulArgb32, int radius, int kth, QImage *outPremul)
    {
        MedianParams params{premulArgb32.width(), premulArgb32.height(), qBound(1, radius, 8), kth};
        return dispatchOnce(premulArgb32, KernelKind::Median, &params, sizeof(params), outPremul);
    }

    bool runCustomWeightsOnce(const QImage &premulArgb32, const int w[9], int scale, int offset,
                              QImage *outPremul)
    {
        CustomParams params;
        params.width = premulArgb32.width();
        params.height = premulArgb32.height();
        params.scale = scale;
        params.offset = offset;
        for (int i = 0; i < 4; ++i)
            params.weights0[i] = w[i];
        for (int i = 0; i < 4; ++i)
            params.weights1[i] = w[4 + i];
        params.weight8 = w[8];
        return dispatchOnce(premulArgb32, KernelKind::Custom, &params, sizeof(params), outPremul);
    }

    bool runCustomOnce(const QImage &premulArgb32, const FilterNode &node, QImage *outPremul)
    {
        int w[9];
        for (int i = 0; i < 9; ++i)
            w[i] = node.customWeight(i);
        return runCustomWeightsOnce(premulArgb32, w, node.customScale(), node.customOffset(),
                                    outPremul);
    }

    /** 锐化核：中心 1+4s，四邻 -s（与 SharpenFilterOp 一致；不透明图 alpha 卷积仍为 255）。 */
    bool runSharpenOnce(const QImage &premulArgb32, int strength, QImage *outPremul)
    {
        const int s = qBound(1, strength, 4);
        const int w[9] = {0, -s, 0, -s, 1 + 4 * s, -s, 0, -s, 0};
        return runCustomWeightsOnce(premulArgb32, w, 1, 0, outPremul);
    }

    /** 浮雕核：-s -s 0 / -s 0 s / 0 s s + 偏置 128（与 EmbossFilterOp 一致）。 */
    bool runEmbossOnce(const QImage &premulArgb32, int strength, QImage *outPremul)
    {
        const int s = qBound(1, strength, 4);
        const int w[9] = {-s, -s, 0, -s, 0, s, 0, s, s};
        return runCustomWeightsOnce(premulArgb32, w, 1, 128, outPremul);
    }

    ApplyStackResult runStack(QImage &image, const FilterStack &stack,
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
        QImage work = ensurePremulArgb32(image);
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
            if (n.op() == OpName::MedianBlur) {
                ok = runMedianOnce(work, n.medianRadius(), /*kth=*/0, &next);
            } else if (n.op() == OpName::Maximum || n.op() == OpName::Minimum) {
                const int r = qBound(1, n.extremumRadius(), 8);
                const int win = (2 * r + 1) * (2 * r + 1);
                const int kth = (n.op() == OpName::Maximum) ? win : 1;
                ok = runMedianOnce(work, r, kth, &next);
            } else if (n.op() == OpName::CustomFilter) {
                ok = runCustomOnce(work, n, &next);
            } else if (n.op() == OpName::Sharpen) {
                ok = runSharpenOnce(work, n.sharpenStrength(), &next);
            } else if (n.op() == OpName::Emboss) {
                ok = runEmbossOnce(work, n.embossStrength(), &next);
            }
            if (!ok)
                return ApplyStackResult::Failed;
            work = next;
        }

        image = work;
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
    const bool invoked = QMetaObject::invokeMethod(
        backend->m_hub.get(),
        [&] { ok = backend->m_hub->initDevice(); },
        Qt::BlockingQueuedConnection);
    if (!invoked || !ok) {
        backend->m_thread->quit();
        backend->m_thread->wait();
        return nullptr;
    }
    backend->m_ok = true;
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
    if (stack.hasPositionalOp() || stack.containsOffset())
        return false;
    bool any = false;
    for (int i = 0; i < stack.count(); ++i) {
        const FilterNode &n = stack.at(i);
        if (!n.isEnabled())
            continue;
        if (!isGpuOp(n.op()))
            return false;
        any = true;
    }
    return any;
}

ApplyStackResult GpuRasterBackend::applyStack(QImage &image, const FilterStack &stack,
                                              const QRect &srcRect, const QRect &outRect,
                                              const std::function<bool()> &shouldAbort)
{
    Q_UNUSED(srcRect);
    Q_UNUSED(outRect);

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
        result = m_hub->runStack(image, stack, shouldAbort);
    } else {
        const bool invoked = QMetaObject::invokeMethod(
            m_hub.get(),
            [&] { result = m_hub->runStack(image, stack, shouldAbort); },
            Qt::BlockingQueuedConnection);
        if (!invoked)
            return ApplyStackResult::Failed;
    }
    return result;
}

} // namespace Ps
