/**
 * gpurasterbackend.cpp — QRhi D3D11 compute（中值 + 自定卷积；独立 RHI 线程；池化）。
 */
#include "gpurasterbackend.h"

#include "domain/filternode.h"
#include "domain/filterstack.h"
#include "engine/op/opname.h"

#include <QFile>
#include <QGuiApplication>
#include <QMetaObject>
#include <QSurfaceFormat>

#include <rhi/qrhi.h>
#include <rhi/qshader.h>

#include <cstring>
#include <vector>

namespace Ps {

namespace {

enum class KernelKind { None, Median, Custom };

struct MedianParams {
    qint32 width = 0;
    qint32 height = 0;
    qint32 radius = 0;
    qint32 pad = 0;
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

QImage toStraightArgb32(const QImage &premul)
{
    QImage src = premul;
    if (src.format() != QImage::Format_ARGB32_Premultiplied
        && src.format() != QImage::Format_ARGB32)
        src = src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QImage out(src.size(), QImage::Format_ARGB32);
    for (int y = 0; y < src.height(); ++y) {
        const QRgb *s = reinterpret_cast<const QRgb *>(src.constScanLine(y));
        QRgb *d = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < src.width(); ++x)
            d[x] = (src.format() == QImage::Format_ARGB32) ? s[x] : qUnpremultiply(s[x]);
    }
    return out;
}

QImage toPremul(const QImage &straight, QImage::Format fmt)
{
    Q_UNUSED(fmt);
    QImage out(straight.size(), QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < straight.height(); ++y) {
        const QRgb *s = reinterpret_cast<const QRgb *>(straight.constScanLine(y));
        QRgb *d = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < straight.width(); ++x)
            d[x] = qPremultiply(s[x]);
    }
    return out;
}

bool isGpuOp(OpName op)
{
    return op == OpName::MedianBlur || op == OpName::CustomFilter;
}

} // namespace

struct GpuRasterBackend::Hub : QObject {
    std::unique_ptr<QRhi> rhi;
    std::unique_ptr<QRhiBuffer> srcBuf;
    std::unique_ptr<QRhiBuffer> dstBuf;
    std::unique_ptr<QRhiBuffer> ubuf;
    std::unique_ptr<QRhiShaderResourceBindings> srb;
    std::unique_ptr<QRhiComputePipeline> pipe;
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

    bool dispatchOnce(const QImage &straightArgb32, KernelKind kind, const void *ubo,
                      int uboBytes, QImage *outStraight)
    {
        const int w = straightArgb32.width();
        const int h = straightArgb32.height();
        const int bytes = w * h * int(sizeof(quint32));
        if (!ensureCapacity(bytes))
            return false;
        if (!ensurePipe(kind))
            return false;

        std::vector<quint32> packed(size_t(w * h));
        for (int y = 0; y < h; ++y) {
            const auto *row = reinterpret_cast<const quint32 *>(straightArgb32.constScanLine(y));
            std::memcpy(packed.data() + size_t(y * w), row, size_t(w) * sizeof(quint32));
        }

        QRhiCommandBuffer *cb = nullptr;
        if (rhi->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess)
            return false;

        QRhiResourceUpdateBatch *u = rhi->nextResourceUpdateBatch();
        u->uploadStaticBuffer(srcBuf.get(), 0, bytes, packed.data());
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

        srcNeedsRecreate = (srcBuf->type() == QRhiBuffer::Immutable);

        if (!done || rb.data.size() < bytes)
            return false;

        *outStraight = QImage(w, h, QImage::Format_ARGB32);
        for (int y = 0; y < h; ++y) {
            std::memcpy(outStraight->scanLine(y),
                        rb.data.constData() + size_t(y * w) * sizeof(quint32),
                        size_t(w) * sizeof(quint32));
        }
        return true;
    }

    bool runMedianOnce(const QImage &straightArgb32, int radius, QImage *outStraight)
    {
        MedianParams params{straightArgb32.width(), straightArgb32.height(),
                            qBound(1, radius, 8), 0};
        return dispatchOnce(straightArgb32, KernelKind::Median, &params, sizeof(params),
                            outStraight);
    }

    bool runCustomOnce(const QImage &straightArgb32, const FilterNode &node, QImage *outStraight)
    {
        CustomParams params;
        params.width = straightArgb32.width();
        params.height = straightArgb32.height();
        params.scale = node.customScale();
        params.offset = node.customOffset();
        for (int i = 0; i < 4; ++i)
            params.weights0[i] = node.customWeight(i);
        for (int i = 0; i < 4; ++i)
            params.weights1[i] = node.customWeight(4 + i);
        params.weight8 = node.customWeight(8);
        return dispatchOnce(straightArgb32, KernelKind::Custom, &params, sizeof(params),
                            outStraight);
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

        QImage work = toStraightArgb32(image);
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
            if (n.op() == OpName::MedianBlur)
                ok = runMedianOnce(work, n.medianRadius(), &next);
            else if (n.op() == OpName::CustomFilter)
                ok = runCustomOnce(work, n, &next);
            if (!ok)
                return ApplyStackResult::Failed;
            work = next;
        }
        image = toPremul(work, QImage::Format_ARGB32_Premultiplied);
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
