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
    // 先释放旧文档再广播：保证订阅者收到信号时全局只有一份有效文档指针，
    // 不会出现「新旧同时可访问」导致误用旧文档的窗口期。
    m_document = std::move(document);

    emit documentChanged(m_document.get());
}

} // namespace Ps
