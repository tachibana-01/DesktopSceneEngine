#include "DisplaySessionBootstrap.h"

#include <DesktopSceneEngine/DisplayIdentity.h>

#include <QString>
#include <QStringList>

#include <iostream>
#include <optional>
#include <string>
#include <variant>

namespace
{

using DesktopSceneEngine::DisplayId;
using DesktopSceneEngine::Player::ApplicationProcessMode;
using DesktopSceneEngine::Player::DisplaySessionProcessMode;
using DesktopSceneEngine::Player::LauncherProcessMode;
using DesktopSceneEngine::Player::ProcessModeParseError;

bool expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
    }

    return condition;
}

QString canonicalArgument(const DisplayId& displayId)
{
    const std::optional<std::string> canonical = displayId.canonicalRepresentation();
    if (!canonical.has_value())
    {
        return {};
    }

    return QString::fromUtf8(canonical->data(), static_cast<qsizetype>(canonical->size()));
}

bool isParseError(
    const ApplicationProcessMode& mode,
    ProcessModeParseError expected
)
{
    const auto* error = std::get_if<ProcessModeParseError>(&mode);
    return error != nullptr && *error == expected;
}

bool testLauncherMode()
{
    return expect(
        std::holds_alternative<LauncherProcessMode>(
            DesktopSceneEngine::Player::parseApplicationProcessMode({})
        ),
        "No application arguments must select Launcher mode."
    );
}

bool testDisplaySessionModeAndExactIdentity()
{
    const std::string identityValue =
        "\\\\?\\DISPLAY#"
        "\xE6\x98\xBE\xE7\xA4\xBA \xE5\x99\xA8:"
        "route\\segment";
    const DisplayId expected = DisplayId::fromTargetDevicePath(identityValue);
    const QString canonical = canonicalArgument(expected);

    const ApplicationProcessMode mode =
        DesktopSceneEngine::Player::parseApplicationProcessMode(
            {QStringLiteral("--display-session"), canonical}
        );
    const auto* session = std::get_if<DisplaySessionProcessMode>(&mode);

    return expect(session != nullptr, "A canonical owner-capable identity must select Session mode.")
        && expect(
            session->displayId == expected,
            "Session mode must preserve the exact canonical DisplayId identity."
        );
}

bool testMalformedArgumentsFailClosed()
{
    const DisplayId strong = DisplayId::fromTargetDevicePath("display-a");
    const QString canonical = canonicalArgument(strong);
    bool result = true;

    result = expect(
        isParseError(
            DesktopSceneEngine::Player::parseApplicationProcessMode(
                {QStringLiteral("--unknown")}
            ),
            ProcessModeParseError::InvalidArguments
        ),
        "Unknown arguments must fail closed."
    ) && result;
    result = expect(
        isParseError(
            DesktopSceneEngine::Player::parseApplicationProcessMode(
                {QStringLiteral("--display-session")}
            ),
            ProcessModeParseError::InvalidArguments
        ),
        "A missing display identity must fail closed."
    ) && result;
    result = expect(
        isParseError(
            DesktopSceneEngine::Player::parseApplicationProcessMode(
                {QStringLiteral("--display-session"), canonical, QStringLiteral("extra")}
            ),
            ProcessModeParseError::InvalidArguments
        ),
        "Extra arguments must fail closed."
    ) && result;
    result = expect(
        isParseError(
            DesktopSceneEngine::Player::parseApplicationProcessMode(
                {
                    QStringLiteral("--display-session"),
                    canonical,
                    QStringLiteral("--display-session"),
                    canonical
                }
            ),
            ProcessModeParseError::InvalidArguments
        ),
        "A duplicate display-session option must fail closed."
    ) && result;
    result = expect(
        isParseError(
            DesktopSceneEngine::Player::parseApplicationProcessMode(
                {QStringLiteral("--display-session=anything")}
            ),
            ProcessModeParseError::InvalidArguments
        ),
        "Combined option syntax must fail closed."
    ) && result;
    result = expect(
        isParseError(
            DesktopSceneEngine::Player::parseApplicationProcessMode(
                {QStringLiteral("--display-session"), QStringLiteral("not-canonical")}
            ),
            ProcessModeParseError::InvalidArguments
        ),
        "A malformed canonical identity must fail closed."
    ) && result;

    return result;
}

bool testWeakIdentityFailsClosed()
{
    const DisplayId weak = DisplayId::fromSourceDeviceName("\\\\.\\DISPLAY1");
    return expect(
        isParseError(
            DesktopSceneEngine::Player::parseApplicationProcessMode(
                {QStringLiteral("--display-session"), canonicalArgument(weak)}
            ),
            ProcessModeParseError::NonPersistentDisplayIdentity
        ),
        "A weak DisplayId must not start a display-owning Session."
    );
}

bool testDetachedLaunchRequest()
{
    const QString program = QStringLiteral(
        "C:/Program Files/DesktopSceneEngine/DesktopSceneEngine_QtPlayer.exe"
    );
    const DisplayId strong = DisplayId::fromTargetDevicePath("display: route\\segment");
    const QString canonical = canonicalArgument(strong);
    const auto request = DesktopSceneEngine::Player::makeDisplaySessionLaunchRequest(
        program,
        strong
    );

    bool result = expect(request.has_value(), "A strong DisplayId must produce a launch request.");
    if (request.has_value())
    {
        result = expect(request->program == program, "The launch request must preserve the executable path.")
            && result;
        result = expect(
            request->arguments
                == QStringList{QStringLiteral("--display-session"), canonical},
            "The launch request must contain exactly the mode switch and canonical identity."
        ) && result;
    }

    result = expect(
        !DesktopSceneEngine::Player::makeDisplaySessionLaunchRequest({}, strong).has_value(),
        "An empty executable path must not produce a launch request."
    ) && result;
    result = expect(
        !DesktopSceneEngine::Player::makeDisplaySessionLaunchRequest(
            program,
            DisplayId{}
        ).has_value(),
        "An invalid DisplayId must not produce a launch request."
    ) && result;
    result = expect(
        !DesktopSceneEngine::Player::makeDisplaySessionLaunchRequest(
            program,
            DisplayId::fromSourceDeviceName("\\\\.\\DISPLAY1")
        ).has_value(),
        "A weak DisplayId must not produce a display-owning launch request."
    ) && result;

    return result;
}

}

int main()
{
    const bool passed = testLauncherMode()
        && testDisplaySessionModeAndExactIdentity()
        && testMalformedArgumentsFailClosed()
        && testWeakIdentityFailsClosed()
        && testDetachedLaunchRequest();

    return passed ? 0 : 1;
}
