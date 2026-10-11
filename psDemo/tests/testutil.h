#ifndef PS_TESTS_TESTUTIL_H
#define PS_TESTS_TESTUTIL_H

/**
 * testutil.h — 常驻测试公共工具（tests/ 专用，不进主程序）。
 *
 * 约定：每个用例实现为 `int tXxx()`，返回**失败项数**（0 = 通过）；
 * 断言用 `Reporter::check`，只打印不下断言的信息用 `Reporter::info`。
 */

#include <QDir>
#include <QImage>
#include <QString>

#include <cstdio>

namespace PsTests {

/** 用例入口：返回失败项数。 */
using TestFn = int (*)();

struct Case {
    const char *name; ///< 命令行用的用例名
    const char *desc; ///< 一行说明
    TestFn fn;
};

/** 简易断言计数器：逐项打印 PASS/FAIL，`fails` 即用例返回值。 */
struct Reporter {
    int fails = 0;

    void check(bool ok, const QString &what)
    {
        std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.toUtf8().constData());
        if (!ok)
            ++fails;
    }

    /** 信息行：不作断言（例如耗时、帧率这类机器相关量）。 */
    void info(const QString &what) const
    {
        std::printf("  ---- %s\n", what.toUtf8().constData());
    }
};

/** 生成有变化内容的 ARGB32_Premultiplied 图（测试 fixture）。 */
inline QImage makeLayerImage(int w, int h, int seed = 1, int alpha = 255)
{
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < h; ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const int r = (x * 255 / w + seed * 40) % 256;
            const int g = (y * 255 / h + seed * 70) % 256;
            const int b = ((x ^ y) + seed * 90) % 256;
            line[x] = qRgba(r * alpha / 255, g * alpha / 255, b * alpha / 255, alpha);
        }
    }
    return img;
}

/** 逐像素差异数；尺寸或格式不同返回 -1。 */
inline long long diffCount(const QImage &a, const QImage &b)
{
    if (a.size() != b.size() || a.format() != b.format())
        return -1;
    long long d = 0;
    for (int y = 0; y < a.height(); ++y) {
        const QRgb *pa = reinterpret_cast<const QRgb *>(a.constScanLine(y));
        const QRgb *pb = reinterpret_cast<const QRgb *>(b.constScanLine(y));
        for (int x = 0; x < a.width(); ++x)
            if (pa[x] != pb[x])
                ++d;
    }
    return d;
}

/**
 * 最大单通道绝对差（RGB 三通道；尺寸/格式不同返回 -1）。
 * 用于量化「逼近算法 vs 精确算法」的误差 —— 比「看起来差不多」可验证。
 */
inline int maxChannelDiff(const QImage &a, const QImage &b)
{
    if (a.size() != b.size() || a.format() != b.format())
        return -1;
    int worst = 0;
    for (int y = 0; y < a.height(); ++y) {
        const QRgb *pa = reinterpret_cast<const QRgb *>(a.constScanLine(y));
        const QRgb *pb = reinterpret_cast<const QRgb *>(b.constScanLine(y));
        for (int x = 0; x < a.width(); ++x) {
            worst = qMax(worst, qAbs(qRed(pa[x]) - qRed(pb[x])));
            worst = qMax(worst, qAbs(qGreen(pa[x]) - qGreen(pb[x])));
            worst = qMax(worst, qAbs(qBlue(pa[x]) - qBlue(pb[x])));
        }
    }
    return worst;
}

/** 临时文件路径（放系统临时目录，避免污染工作区）。 */inline QString tempFilePath(const QString &name)
{
    return QDir(QDir::tempPath()).filePath(QStringLiteral("psdemo_test_") + name);
}

} // namespace PsTests

#endif // PS_TESTS_TESTUTIL_H
