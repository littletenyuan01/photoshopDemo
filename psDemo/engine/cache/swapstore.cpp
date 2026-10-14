/**
 * swapstore.cpp — Memory / File / Failing 换页存储实现。
 */
#include "swapstore.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QTemporaryFile>
#include <QtGlobal>

namespace Ps {

qint64 MemorySwapStore::put(const QByteArray &bytes)
{
    if (!m_open)
        return -1;
    const qint64 off = m_bytes.size();
    m_bytes.append(bytes);
    ++m_putCount;
    return off;
}

bool MemorySwapStore::get(qint64 offset, int n, QByteArray *out) const
{
    if (!m_open || !out || offset < 0 || n < 0)
        return false;
    if (offset + n > m_bytes.size())
        return false;
    *out = m_bytes.mid(int(offset), n);
    return out->size() == n;
}

struct FileSwapStore::Impl {
    QTemporaryFile file;
};

FileSwapStore::FileSwapStore(const QString &dirPath)
    : m_impl(std::make_unique<Impl>())
{
    QString dir = dirPath;
    if (dir.isEmpty()) {
        const QByteArray env = qgetenv("PSLITE_TILE_CACHE_DIR");
        dir = env.isEmpty() ? QDir::tempPath() : QString::fromLocal8Bit(env);
    }
    QDir().mkpath(dir);
    m_impl->file.setFileTemplate(dir + QStringLiteral("/pslite-tiles-XXXXXX.swap"));
    m_ok = m_impl->file.open();
    if (m_ok)
        m_impl->file.setAutoRemove(true);
}

FileSwapStore::~FileSwapStore() = default;

qint64 FileSwapStore::put(const QByteArray &bytes)
{
    if (!m_ok)
        return -1;
    const qint64 off = m_impl->file.size();
    if (m_impl->file.write(bytes) != bytes.size()) {
        m_ok = false;
        return -1;
    }
    m_impl->file.flush();
    m_written = m_impl->file.size();
    return off;
}

bool FileSwapStore::get(qint64 offset, int n, QByteArray *out) const
{
    if (!m_ok || !out || offset < 0 || n < 0)
        return false;
    if (!m_impl->file.seek(offset))
        return false;
    *out = m_impl->file.read(n);
    return out->size() == n;
}

} // namespace Ps
