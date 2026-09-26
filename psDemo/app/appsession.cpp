#include "appsession.h"

#include "domain/imagedocument.h"

namespace Ps {

AppSession::AppSession(QObject *parent)
    : QObject(parent)
{
}

AppSession::~AppSession() = default;

void AppSession::setDocument(std::unique_ptr<ImageDocument> document)
{
    // 【顺序很要紧】旧文档要**活到广播结束**再析构：
    // 订阅者收到 documentChanged 时会对自己保存的旧指针调
    // `disconnect(oldDoc, nullptr, this, nullptr)`；若旧对象此时已被 delete，
    // QObject::disconnect 内部会调用 `sender->metaObject()`（虚调用）→ 读已释放内存。
    // 内存恰好被新文档复用时就会读到空 vtable → **第二次换文档必崩**
    // （实测：连续两次 setDocument 100% 触发 0xC0000005，读地址 0）。
    // 现在用局部 unique_ptr 兜住旧文档：m_document 立刻指向新文档（订阅者拿不到旧文档），
    // 但旧对象在 emit 返回、各订阅者断开连接之后才析构。
    std::unique_ptr<ImageDocument> previous = std::move(m_document);
    m_document = std::move(document);

    emit documentChanged(m_document.get());
    // previous 在这里析构（本调用返回前）
}

} // namespace Ps
