#include "recentdocuments.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QSettings>

namespace Ps {
namespace {

QSettings settings()
{
    // 与应用名一致，便于用户清配置；不依赖 QCoreApplication::organization 是否已设
    return QSettings(QStringLiteral("PhotoshopLite"), QStringLiteral("PSLite"));
}

QImage scaledThumb(const QImage &src)
{
    if (src.isNull())
        return {};
    return src.scaled(RecentDocuments::kThumbEdge, RecentDocuments::kThumbEdge,
                      Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

} // namespace

QString RecentDocuments::normalized(const QString &path)
{
    return QFileInfo(path).absoluteFilePath();
}

QString RecentDocuments::thumbKey(const QString &normalizedPath)
{
    const QByteArray hash = QCryptographicHash::hash(
        normalizedPath.toUtf8(), QCryptographicHash::Sha1).toHex();
    return QStringLiteral("recent/thumbs/%1").arg(QString::fromLatin1(hash));
}

QStringList RecentDocuments::paths()
{
    QSettings s = settings();
    QStringList list = s.value(QStringLiteral("recent/paths")).toStringList();
    QStringList kept;
    kept.reserve(list.size());
    bool pruned = false;
    for (const QString &raw : list) {
        const QString path = normalized(raw);
        if (path.isEmpty() || !QFileInfo::exists(path)) {
            pruned = true;
            if (!path.isEmpty())
                s.remove(thumbKey(path));
            continue;
        }
        if (!kept.contains(path))
            kept.append(path);
    }
    if (pruned || kept.size() != list.size())
        s.setValue(QStringLiteral("recent/paths"), kept);
    return kept;
}

void RecentDocuments::add(const QString &path)
{
    const QString abs = normalized(path);
    if (abs.isEmpty() || !QFileInfo::exists(abs))
        return;

    QStringList list = paths();
    list.removeAll(abs);
    list.prepend(abs);
    while (list.size() > kMaxRecent) {
        const QString dropped = list.takeLast();
        settings().remove(thumbKey(dropped));
    }
    settings().setValue(QStringLiteral("recent/paths"), list);
}

void RecentDocuments::remove(const QString &path)
{
    const QString abs = normalized(path);
    if (abs.isEmpty())
        return;
    QStringList list = paths();
    if (list.removeAll(abs) == 0)
        return;
    QSettings s = settings();
    s.setValue(QStringLiteral("recent/paths"), list);
    s.remove(thumbKey(abs));
}

void RecentDocuments::setThumbnail(const QString &path, const QImage &image)
{
    const QString abs = normalized(path);
    if (abs.isEmpty() || image.isNull())
        return;
    const QImage thumb = scaledThumb(image);
    QByteArray png;
    QBuffer buffer(&png);
    if (!buffer.open(QIODevice::WriteOnly) || !thumb.save(&buffer, "PNG"))
        return;
    settings().setValue(thumbKey(abs), png);
}

QImage RecentDocuments::thumbnail(const QString &path)
{
    const QString abs = normalized(path);
    if (abs.isEmpty())
        return {};
    const QByteArray png = settings().value(thumbKey(abs)).toByteArray();
    if (png.isEmpty())
        return {};
    QImage img;
    if (!img.loadFromData(png, "PNG"))
        return {};
    return img;
}

} // namespace Ps
