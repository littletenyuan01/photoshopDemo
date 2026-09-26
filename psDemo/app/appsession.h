#ifndef APPSESSION_H
#define APPSESSION_H

#include <QObject>
#include <memory>

namespace Ps {

class ImageDocument;

/**
 * 应用会话（app 层）：**当前文档的唯一持有者与广播中心**。
 *
 * 【对照 GIMP】对应 GimpContext 的「当前图像」职责（app/core/gimpcontext.c 的
 * image-changed 信号）：任何关心「当前是哪个文档」的组件订阅本类信号即可，
 * 新增面板 **不需要** 修改 MainWindow 或任何已有组件。
 *
 * 为什么要有它：
 * 早先 MainWindow 换文档时要手工逐个 setDocument（画布、工作区、三个面板…），
 * 加一个面板就得加一行，漏一行就是静默不刷新。现在换成广播：
 *
 *   MainWindow ──setDocument()──> AppSession ──documentChanged(doc)──> 所有订阅者
 *
 * 【所有权】本类独占文档（unique_ptr）。画布/面板只拿裸指针观察，不拥有、不缓存生命周期。
 */
class AppSession : public QObject
{
    Q_OBJECT

public:
    explicit AppSession(QObject *parent = nullptr);
    ~AppSession() override;

    /** 当前文档；无文档返回 nullptr。 */
    ImageDocument *document() const { return m_document.get(); }

    /**
     * 换文档（传入 nullptr 表示关闭）。旧文档在本调用返回后即被析构，
     * 订阅者收到新指针时必须已丢弃旧文档的一切引用。
     */
    void setDocument(std::unique_ptr<ImageDocument> document);

    /** 是否有文档。 */
    bool hasDocument() const { return m_document != nullptr; }

signals:
    /** 当前文档已变更；doc 可能为 nullptr。 */
    void documentChanged(ImageDocument *doc);

private:
    std::unique_ptr<ImageDocument> m_document;
};

} // namespace Ps

#endif // APPSESSION_H
