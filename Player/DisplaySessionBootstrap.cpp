#include "DisplaySessionBootstrap.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QProcess>

#include <optional>
#include <string_view>

namespace DesktopSceneEngine::Player
{

ApplicationProcessMode parseApplicationProcessMode(const QStringList& arguments)
{
    if (arguments.isEmpty())
    {
        return LauncherProcessMode{};
    }

    if (arguments.size() != 2 || arguments.front() != QStringLiteral("--display-session"))
    {
        return ProcessModeParseError::InvalidArguments;
    }

    const QByteArray canonicalUtf8 = arguments.back().toUtf8();
    const std::optional<DisplayId> displayId = DisplayId::parseCanonicalRepresentation(
        std::string_view{
            canonicalUtf8.constData(),
            static_cast<std::size_t>(canonicalUtf8.size())
        }
    );
    if (!displayId.has_value())
    {
        return ProcessModeParseError::InvalidArguments;
    }

    if (!displayId->isPersistentOwnerCapable())
    {
        return ProcessModeParseError::NonPersistentDisplayIdentity;
    }

    return DisplaySessionProcessMode{*displayId};
}

std::optional<DisplaySessionLaunchRequest> makeDisplaySessionLaunchRequest(
    const QString& applicationFilePath,
    const DisplayId& displayId
)
{
    if (
        applicationFilePath.isEmpty()
        || !displayId.isValid()
        || !displayId.isPersistentOwnerCapable()
    )
    {
        return std::nullopt;
    }

    const std::optional<std::string> canonical = displayId.canonicalRepresentation();
    if (!canonical.has_value())
    {
        return std::nullopt;
    }

    return DisplaySessionLaunchRequest{
        applicationFilePath,
        {
            QStringLiteral("--display-session"),
            QString::fromUtf8(canonical->data(), static_cast<qsizetype>(canonical->size()))
        }
    };
}

bool startDisplaySessionDetached(const DisplayId& displayId)
{
    const std::optional<DisplaySessionLaunchRequest> request =
        makeDisplaySessionLaunchRequest(QCoreApplication::applicationFilePath(), displayId);
    if (!request.has_value())
    {
        return false;
    }

    return QProcess::startDetached(request->program, request->arguments);
}

}
