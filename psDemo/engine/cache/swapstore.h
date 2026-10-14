/**
 * swapstore.h — 瓦片换页存储抽象（engine/cache）。
 *
 * 单一追加写接口；可注入 Memory / File / 失败假实现（对照 gpu-plan 的 setGpuBackendForTest）。
 * 见 docs/page-cache-plan.md §4 B 档。
 */
#ifndef ENGINE_CACHE_SWAPSTORE_H
#define ENGINE_CACHE_SWAPSTORE_H

#include <QByteArray>
#include <QString>
#include <QtGlobal>

#include <memory>

namespace Ps {

/**
 * 瓦片换页后端：put 返回单调偏移；get 按偏移读回。
 * 失败时 put 返回 -1 / get 返回 false —— 调用方须降级（留内存、关换页），不许因此崩。
 */
class SwapStore
{
public:
    virtual ~SwapStore() = default;

    /** 追加写入；成功返回起始偏移，失败返回 -1。 */
    virtual qint64 put(const QByteArray &bytes) = 0;
    /** 从 @p offset 读取 @p n 字节到 @p out；失败返回 false。 */
    virtual bool get(qint64 offset, int n, QByteArray *out) const = 0;
    /** 已写入的累计字节（含碎片；本实现不做回收）。 */
    virtual qint64 writtenBytes() const = 0;
    virtual bool isOpen() const = 0;
};

/** 堆上假实现：单测用，可记录 put 顺序。 */
class MemorySwapStore final : public SwapStore
{
public:
    qint64 put(const QByteArray &bytes) override;
    bool get(qint64 offset, int n, QByteArray *out) const override;
    qint64 writtenBytes() const override { return m_bytes.size(); }
    bool isOpen() const override { return m_open; }

    void setOpen(bool on) { m_open = on; }
    /** 已 put 的次数（LRU 顺序断言用）。 */
    int putCount() const { return m_putCount; }

private:
    QByteArray m_bytes;
    int m_putCount = 0;
    bool m_open = true;
};

/**
 * 表面上可打开，但 put/get 总失败 —— 验证「I/O 失败后降级」路径。
 * （若 isOpen()==false，TileCache 会在注入时直接关换页，测不到 put 失败分支。）
 */
class FailingSwapStore final : public SwapStore
{
public:
    qint64 put(const QByteArray &) override { return -1; }
    bool get(qint64, int, QByteArray *) const override { return false; }
    qint64 writtenBytes() const override { return 0; }
    bool isOpen() const override { return true; }
};

/**
 * 单一临时文件追加写（默认 QDir::tempPath，可用 PSLITE_TILE_CACHE_DIR）。
 * 进程退出随 QTemporaryFile 删除；不做空间回收。
 */
class FileSwapStore final : public SwapStore
{
public:
    explicit FileSwapStore(const QString &dirPath = QString());
    ~FileSwapStore() override;

    qint64 put(const QByteArray &bytes) override;
    bool get(qint64 offset, int n, QByteArray *out) const override;
    qint64 writtenBytes() const override { return m_written; }
    bool isOpen() const override { return m_ok; }

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    qint64 m_written = 0;
    bool m_ok = false;
};

} // namespace Ps

#endif // ENGINE_CACHE_SWAPSTORE_H
