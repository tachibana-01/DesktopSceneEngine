#include "DisplayPresentation.h"
#include <QCoreApplication>

namespace DesktopSceneEngine::Player
{
QString displayLabel(const Platform::DisplayDescriptor& display)
{
    const auto text = [](const char* source) { return QCoreApplication::translate("LauncherWindow", source); };
    QString name = QString::fromUtf8(display.friendlyName.data(), static_cast<qsizetype>(display.friendlyName.size())).trimmed();
    if (name.isEmpty()) name = text("Display %1").arg(display.displayNumber);
    QString label = text("%1 — %2 × %3").arg(name).arg(display.pixelWidth).arg(display.pixelHeight);
    if (display.primary) label += text(" — Primary");
    if (!display.id.isPersistentOwnerCapable()) label += text(" — Session unavailable");
    return label;
}
}
