#ifndef RECENTDOCUMENTS_H
#define RECENTDOCUMENTS_H

#include <QImage>
#include <QString>
#include <QStringList>

namespace Ps {

/**
 * 最近打开/保存的文档列表（app 层），用 QSettings 持久化。
 *
 * - paths：最近在前，最多 kMaxRecent 条
 * - 缩略图：按路径 hash 存小 PNG，避免进主页时反复全量加载 .pslite
 */
class RecentDocuments
{
public:
    static constexpr int kMaxRecent = 12;
    static constexpr int kThumbEdge = 128;

    /** 现存文件路径（已剔除丢失项）；最近在前。 */
    static QStringList paths();

    /** 记入最近（提到最前；超过上限丢最旧）。path 为空则忽略。 */
    static void add(const QString &path);

    /** 从列表移除（文件删除或不存在时）。 */
    static void remove(const QString &path);

    /** 写入/更新缩略图缓存（会缩放到 kThumbEdge 内保比例）。 */
    static void setThumbnail(const QString &path, const QImage &image);

    /** 读缓存缩略图；无缓存返回空图。 */
    static QImage thumbnail(const QString &path);

private:
    static QString normalized(const QString &path);
    static QString thumbKey(const QString &normalizedPath);
};

} // namespace Ps

#endif // RECENTDOCUMENTS_H
