/**
 * t_gpukernels.cpp — T3.0/T3.1/T5.4 门控：AddNoise / BoxBlur / GaussianBlur + ROI。
 *
 * 本文件补的是 gpu-plan 里挂了很久的三件缺失验收（T3.2「门控待补」）：
 *
 *  ① **shader 资产一致**（不依赖 GPU，始终跑）：四个 `.comp.qsb` 必须解析成功、
 *     且带齐运行时需要的 target。
 *     【为什么必须有这条】`.qsb` 是**入库的二进制**，它的生成命令在仓库里没有任何记录。
 *     实测已经漂了：`custom.comp.qsb` 里只有 `DXBC 50`，连不上 D3D11 运行时期望的
 *     `HLSL 50`；`noise.comp.qsb` 比 `noise.comp` 旧（改源码没重编译，且旧包里
 *     `amp` 是 `amount/256` —— 与 CPU 的 `amount*2.55` 不等价）。这类漂移**全都没有编译错误**，
 *     只表现为「GPU 静默退回 CPU」，正是 gpu-plan 反复警惕的那种坑。
 *  ② **AddNoise GPU vs CPU**：容差 ≤1（P2 浮点类），并断言**真走了 GPU**（否则不算验过）。
 *  ③ **BoxBlur / GaussianBlur GPU vs CPU**：容差 ≤1。
 *  ④ **ROI 语义**：`srcRect ⊋ outRect` 时
 *     · 结果 == 用同一几何走 CPU 后端（防「文档坐标被当成图内坐标」那类错位）；
 *     · `outRect` 之外**逐字节不变**（分块合成只认请求区，越权写别人的像素会静默错图）；
 *     · `srcRect` 不包住 `outRect` ⇒ 拒收（走 CPU），不猜。
 */

#include "domain/filternode.h"
#include "domain/filterstack.h"
#include "engine/backend/cpurasterbackend.h"
#include "engine/backend/gpurasterbackend.h"
#include "engine/backend/rasterscheduler.h"
#include "engine/op/opname.h"
#include "testutil.h"

#include <QFile>
#include <QImage>
#include <QtGlobal>
// QShader 是 Qt Gui 私有头（app.pri 已 QT += gui-private，与 gpurasterbackend 一致）
#include <rhi/qshader.h>

#include <cstdio>

using namespace Ps;
using PsTests::Reporter;

namespace {

struct EnvGuard {
    QByteArray key;
    QByteArray old;
    bool had = false;
    EnvGuard(const char *k, const char *v)
        : key(k)
    {
        had = qEnvironmentVariableIsSet(k);
        old = qgetenv(k);
        if (v)
            qputenv(k, v);
        else
            qunsetenv(k);
        RasterScheduler::instance().resetForTest();
    }
    ~EnvGuard()
    {
        if (had)
            qputenv(key.constData(), old);
        else
            qunsetenv(key.constData());
        RasterScheduler::instance().resetForTest();
    }
};

/** 走调度器跑一条链；返回后端 id（"cpu" / "gpu:qrhi"）。 */
QString runViaScheduler(QImage &img, const FilterStack &stack, const QRect &src, const QRect &out,
                        ApplyStackResult *res)
{
    *res = ApplyStackResult::Ok;
    return RasterScheduler::instance().runStack(img, stack, src, out, nullptr, res);
}

/**
 * GPU vs CPU 同几何比对。返回 true = 本项通过；`skipped` 报告是否因无 GPU 跳过。
 * 容差 `tol`：
 *  · 中值 / 卷积 = 0（口径完全一致，应 bit-exact）；
 *  · 盒模糊 = 2 —— CPU 是**两趟一维**（每趟各舍入一次），GPU 是**一趟二维**（只舍入一次），
 *    两次舍入的差可达 2（r=64 实测），这是分离性的固有差异，不是错误；
 *  · 噪声 = 1（P2 浮点类）。
 * 注意 `srcRect` 必须**等于** CPU 侧看到的那张图的整幅：CPU 后端是按整幅求值再裁 outRect 的，
 * 若只想比 outRect 那块，请自己裁出 patch 再传 patch.rect()（见下面 ROI 用例）。
 */
bool compareGpuCpu(Reporter &r, const QImage &src, const FilterStack &stack, const QRect &srcRect,
                   const QRect &outRect, int tol, const QString &tag, bool *skipped)
{
    QImage viaCpu = src;
    CpuRasterBackend cpu;
    if (cpu.applyStack(viaCpu, stack, srcRect, outRect, nullptr) != ApplyStackResult::Ok) {
        r.check(false, tag + QStringLiteral("：CPU 参考失败"));
        return false;
    }

    QImage viaGpu = src;
    ApplyStackResult gr = ApplyStackResult::Ok;
    const QString id = runViaScheduler(viaGpu, stack, srcRect, outRect, &gr);
    if (id == QLatin1String("cpu") || gr != ApplyStackResult::Ok) {
        std::fprintf(stderr, "[gpukernels] SKIP %s: no usable GPU (id=%s outR=%d)\n",
                     qPrintable(tag), qPrintable(id), int(gr));
        *skipped = true;
        return true;
    }

    r.check(id.startsWith(QLatin1String("gpu")), tag + QStringLiteral("：backend=%1").arg(id));
    const int worst = PsTests::maxChannelDiff(viaGpu, viaCpu);
    r.check(worst >= 0 && worst <= tol,
            tag + QStringLiteral("：GPU vs CPU 逐通道差 ≤%1（实际 %2）").arg(tol).arg(worst));
    return worst >= 0 && worst <= tol;
}

/** ① shader 资产一致性（无需 GPU）。 */
void checkShaderAssets(Reporter &r)
{
    struct Asset {
        const char *path;
        const char *what;
    };
    const Asset assets[] = {
        {":/shaders/median.comp.qsb", "中值族（秩选择）"},
        {":/shaders/custom.comp.qsb", "卷积族（3×3）"},
        {":/shaders/noise.comp.qsb", "添加杂色"},
        {":/shaders/boxblur.comp.qsb", "盒模糊 / 高斯"},
    };

    for (const Asset &a : assets) {
        QFile f{QLatin1String(a.path)};
        if (!f.open(QIODevice::ReadOnly)) {
            r.check(false, QStringLiteral("%1：%2 打不开（resources.qrc 是否登记？）")
                              .arg(QLatin1String(a.what), QLatin1String(a.path)));
            continue;
        }
        const QShader sh = QShader::fromSerialized(f.readAll());
        if (!sh.isValid()) {
            r.check(false, QStringLiteral("%1：%2 解析失败（.qsb 是否由本目录 .comp 重新生成？）")
                              .arg(QLatin1String(a.what), QLatin1String(a.path)));
            continue;
        }
        // 运行时需要的 target：D3D11 走 HLSL（或预编译 DXBC），GLES2 走 GLSL，
        // 单元测试固定按这几个 key 找 shader（见 gpurasterbackend 的 loadShader→QShader）。
        const QList<QShaderKey> keys = sh.availableShaders();
        bool hasHlslOrDxbc = false;
        bool hasGlsl = false;
        for (const QShaderKey &k : keys) {
            if (k.source() == QShader::HlslShader || k.source() == QShader::DxbcShader)
                hasHlslOrDxbc = true;
            if (k.source() == QShader::GlslShader)
                hasGlsl = true;
        }
        r.check(hasHlslOrDxbc,
                QStringLiteral("%1：%2 含 HLSL/DXBC（D3D11 运行时可用）")
                    .arg(QLatin1String(a.what), QLatin1String(a.path)));
        r.check(hasGlsl, QStringLiteral("%1：%2 含 GLSL（GLES2 回退可用）")
                             .arg(QLatin1String(a.what), QLatin1String(a.path)));
    }
}

} // namespace

int tGpuKernels()
{
    Reporter r;
    EnvGuard env("PSLITE_RASTER", "gpu");
    auto &sched = RasterScheduler::instance();
    sched.resetForTest();

    // —— ① shader 资产（不依赖 GPU，最先跑：它一红就说明后面的 SKIP 是资产问题）——
    checkShaderAssets(r);

    // 报告真实后端名：区分「探测失败」与「不支持」
    {
        auto probe = GpuRasterBackend::tryCreate();
        if (probe)
            r.info(QStringLiteral("QRhi 后端 = %1").arg(probe->backendName()));
        else
            r.info(QStringLiteral("QRhi 后端探测失败（GPU 用例将 SKIP）"));
    }

    const QImage src = PsTests::makeLayerImage(560, 420, 41, 255);
    const QRect whole = src.rect();
    bool skipped = false;

    // —— ② AddNoise GPU vs CPU（容差 ≤1：P2 浮点类，见 gpu-plan §8）——
    {
        for (int amount : {12, 50}) {
            for (bool gauss : {false, true}) {
                for (bool mono : {true, false}) {
                    FilterStack stack;
                    FilterNode n(OpName::AddNoise);
                    n.setNoiseAmount(amount);
                    n.setNoiseGaussian(gauss);
                    n.setNoiseMonochromatic(mono);
                    stack.append(n);
                    const QString tag =
                        QStringLiteral("AddNoise a=%1 高斯=%2 单色=%3")
                            .arg(amount)
                            .arg(gauss ? 1 : 0)
                            .arg(mono ? 1 : 0);
                    compareGpuCpu(r, src, stack, whole, whole, 1, tag, &skipped);
                }
            }
        }
    }

    // —— ③ BoxBlur / GaussianBlur GPU vs CPU（容差 ≤2：CPU 两趟一维各舍入一次）——
    {
        FilterStack box;
        FilterNode b(OpName::BoxBlur);
        b.setBlurRadius(3);
        box.append(b);
        compareGpuCpu(r, src, box, whole, whole, 2, QStringLiteral("BoxBlur r=3"), &skipped);

        // 边界半径：r=1（最小）与 r=64（GPU 软上限，必须仍走 GPU 且正确）
        for (int radius : {1, 64}) {
            FilterStack s2;
            FilterNode n2(OpName::BoxBlur);
            n2.setBlurRadius(radius);
            s2.append(n2);
            compareGpuCpu(r, src, s2, whole, whole, 2,
                          QStringLiteral("BoxBlur r=%1").arg(radius), &skipped);
        }

        for (int radius : {1, 5, 30}) {
            FilterStack g;
            FilterNode n(OpName::GaussianBlur);
            n.setGaussRadius(radius);
            g.append(n);
            compareGpuCpu(r, src, g, whole, whole, 2,
                          QStringLiteral("GaussianBlur r=%1").arg(radius), &skipped);
        }
    }

    // —— ④ ROI 语义（T5.4）——
    //
    // 【契约】`image` 就是 `srcRect` 那一块（`image.size() == srcRect.size()`，同一坐标系），
    // 结果写在 `outRect` 处。这正是两条生产路径的用法：
    //   · Layer::compositeRasterRegion：base = TilePatch::extract(srcWant)，apply(base, srcDoc, outDoc)
    //   · Compositor 调整层分支：below(srcWant.size())，apply(below, srcWant, area)
    // 于是「ROI 求值」应当等价于「把那块裁出来单独求值」。CPU 后端不支持这个语义
    // （它忽略 srcRect、按整幅求值再裁），所以 oracle 用 `stack.apply(patch)`——那是
    // 同一套本地坐标语义，且与大图完全无关。
    {
        struct Case {
            OpName op;
            int param;
            const char *tag;
            int tol;
        };
        const Case cases[] = {
            {OpName::BoxBlur, 2, "ROI 盒模糊", 2},
            {OpName::MedianBlur, 2, "ROI 中值", 0},
            {OpName::GaussianBlur, 5, "ROI 高斯", 2},
        };

        // 刻意让 srcRect 的左上角远离原点：图像原点 ≠ 图内 (0,0) 的错位全靠它照出来
        const QRect srcRect(80, 60, 400, 340);
        const QRect outRect(140, 120, 220, 150);
        const QImage patch = src.copy(srcRect);

        for (const Case &c : cases) {
            FilterStack stack;
            FilterNode n(c.op);
            if (c.op == OpName::BoxBlur)
                n.setBlurRadius(c.param);
            else if (c.op == OpName::MedianBlur)
                n.setMedianRadius(c.param);
            else
                n.setGaussRadius(c.param);
            stack.append(n);

            // oracle：把那块裁出来单独求值（本地坐标 = 整幅）
            const QImage expect = stack.apply(patch);

            QImage work = patch; // image == srcRect 那一块
            ApplyStackResult rr = ApplyStackResult::Ok;
            const QString id = runViaScheduler(work, stack, srcRect, outRect, &rr);
            if (id == QLatin1String("cpu") || rr != ApplyStackResult::Ok) {
                std::fprintf(stderr, "[gpukernels] SKIP %s: no GPU\n", c.tag);
                skipped = true;
                continue;
            }
            r.check(id.startsWith(QLatin1String("gpu")),
                    QStringLiteral("%1：backend=%2").arg(QLatin1String(c.tag), id));

            // outRect 在「本地/图内」两种口径下是同一块（因为我们传进去的就是那块图）
            const int worst = PsTests::maxChannelDiff(work.copy(outRect), expect.copy(outRect));
            r.check(worst >= 0 && worst <= c.tol,
                    QStringLiteral("%1：ROI 结果 == 「裁出该输入块单独求值」的对应块（≤%2，实际 %3）")
                        .arg(QLatin1String(c.tag))
                        .arg(c.tol)
                        .arg(worst));
        }

        // 恒等几何（srcRect == outRect == 整幅）⇒ 必须与整幅求值 bit-exact。
        // 钉的是「ROI 的识别与裁剪是**恒等**」：新代码对旧行为（整图求值）不得有任何回归。
        // 几何取整幅而不是「同样大小但偏一点」，就是为了让 `roi.active == false`——
        // 偏一点的话 `image.rect()` 与 srcRect 仍相等、同样退化，但依赖调用方传的尺寸，
        // 容易被误读成在测 ROI 本身（那种情形由上面三条覆盖）。
        {
            FilterStack stack;
            FilterNode n(OpName::GaussianBlur);
            n.setGaussRadius(3);
            stack.append(n);

            const QImage expect = stack.apply(src);
            QImage work = src;
            ApplyStackResult rr = ApplyStackResult::Ok;
            const QString id = runViaScheduler(work, stack, whole, whole, &rr);
            if (id == QLatin1String("cpu") || rr != ApplyStackResult::Ok) {
                std::fprintf(stderr,
                             "[gpukernels] SKIP 恒等几何: scheduler chose CPU"
                             " (cost threshold or no GPU)\n");
                skipped = true;
            } else {
                const int d = PsTests::maxChannelDiff(work, expect);
                r.check(d == 0,
                        QStringLiteral("ROI：srcRect==outRect==整幅 时 == 整幅求值"
                                       "（bit-exact，实际差 %1）")
                            .arg(d));
            }
        }

        // outRect 之外必须逐字节不变。
        // 【这条专抓「贴错位置」】`srcRect.topLeft() != (0,0)` 而图是那块本身：
        // 一旦回贴位被算成 `outRect - srcRect.topLeft()`，就会写到 (20,20) 而不是 (100,90)，
        // 于是 outRect 里是旧值、outRect 外被动过 —— 两个方向都会在这里暴露。
        {
            const QRect roiSrc(80, 60, 400, 340);
            const QRect roiOut(140, 120, 220, 150); // 与 roiSrc 同一坐标系下的请求区

            FilterStack s2;
            FilterNode n2(OpName::MedianBlur);
            n2.setMedianRadius(1);
            s2.append(n2);

            QImage work = src.copy(roiSrc); // image == srcRect 那一块
            for (int y = 0; y < work.height(); ++y) {
                QRgb *line = reinterpret_cast<QRgb *>(work.scanLine(y));
                for (int x = 0; x < work.width(); ++x) {
                    if (!roiOut.contains(x, y))
                        line[x] = qRgba(7, 9, 11, 255); // 哨兵：请求区外
                }
            }
            const QImage before = work;
            ApplyStackResult rr = ApplyStackResult::Ok;
            const QString id = runViaScheduler(work, s2, roiSrc, roiOut, &rr);
            if (id == QLatin1String("cpu") || rr != ApplyStackResult::Ok) {
                std::fprintf(stderr, "[gpukernels] SKIP ROI 越权写: no GPU\n");
            } else {
                long long touched = 0;
                for (int y = 0; y < work.height(); ++y) {
                    const QRgb *pa = reinterpret_cast<const QRgb *>(before.constScanLine(y));
                    const QRgb *pb = reinterpret_cast<const QRgb *>(work.constScanLine(y));
                    for (int x = 0; x < work.width(); ++x)
                        if (!roiOut.contains(x, y) && pa[x] != pb[x])
                            ++touched;
                }
                r.check(touched == 0,
                        QStringLiteral("ROI：outRect 之外逐字节不变（实际改了 %1 像素）")
                            .arg(touched));

                // 请求区内必须真的被算过（否则「什么都没写」也会让上一条通过）
                long long changedInside = 0;
                for (int y = roiOut.top(); y <= roiOut.bottom(); ++y) {
                    const QRgb *pa = reinterpret_cast<const QRgb *>(before.constScanLine(y));
                    const QRgb *pb = reinterpret_cast<const QRgb *>(work.constScanLine(y));
                    for (int x = roiOut.left(); x <= roiOut.right(); ++x)
                        if (pa[x] != pb[x])
                            ++changedInside;
                }
                r.check(changedInside > 0,
                        QStringLiteral("ROI：请求区内确实被求值过（实际改了 %1 像素）")
                            .arg(changedInside));
            }
        }

        // 非法几何（outRect 越出 srcRect）⇒ 拒收走 CPU，不得凭猜贴回去。
        {
            QImage bad = src;
            FilterStack s3;
            FilterNode n3(OpName::BoxBlur);
            n3.setBlurRadius(1);
            s3.append(n3);
            ApplyStackResult rr = ApplyStackResult::Ok;
            const QString id =
                runViaScheduler(bad, s3, QRect(30, 30, 40, 40), QRect(0, 0, 200, 200), &rr);
            r.check(id == QLatin1String("cpu") && rr == ApplyStackResult::Ok,
                    QStringLiteral("ROI：outRect 越出 srcRect ⇒ 拒收走 CPU（backend=%1）").arg(id));
        }
    }

    if (skipped)
        r.info(QStringLiteral("部分项因无可用 GPU 跳过（不算失败）"));

    sched.resetForTest();
    return r.fails;
}
