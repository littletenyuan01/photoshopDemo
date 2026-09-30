/**
 * eyedroppertool.cpp — EyedropperTool 实现（tools 层）。
 */
#include "eyedroppertool.h"

#include "domain/imagedocument.h"
#include "engine/compositor.h"
#include "engine/premul.h"
#include "toolcursor.h"

#include <QtMath>

namespace Ps {

EyedropperTool::EyedropperTool(QObject *parent)
    : Tool(Ps::ToolId::Eyedropper, parent)
{
}

QCursor EyedropperTool::cursor() const
{
    return ToolCursor::fromToolIcon(QStringLiteral(":/icons/tools/eyedropper.png"),
                                    0.15, 0.85, 28);
}

bool EyedropperTool::mousePress(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(view)
    if (!event.isLeft() || !ctx.document)
        return false;

    const int x = qFloor(event.imagePos.x());
    const int y = qFloor(event.imagePos.y());
    if (x < 0 || y < 0 || x >= ctx.document->width() || y >= ctx.document->height())
        return false;

    // 对照 sample-merged：取合成像素（本 Demo 固定对合成取样）
    const QImage composite = Compositor::composite(*ctx.document);
    if (composite.isNull() || x >= composite.width() || y >= composite.height())
        return false;

    const QRgb px = reinterpret_cast<const QRgb *>(composite.constScanLine(y))[x];
    int r, g, b, a;
    Premul::unpremultiplyRgb(px, &r, &g, &b, &a);
    const QColor color(r, g, b, a);

    // Alt+单击 → 背景；否则前景（对齐 PS）
    if (event.modifiers.testFlag(Qt::AltModifier))
        emit backgroundPicked(color);
    else
        emit foregroundPicked(color);
    return true;
}

} // namespace Ps
