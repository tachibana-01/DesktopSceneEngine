#pragma once

#include <DesktopSceneEngine/DisplayIdentity.h>

#include <QString>
#include <QStringList>

#include <optional>
#include <variant>

namespace DesktopSceneEngine::Player
{

struct LauncherProcessMode
{
};

struct DisplaySessionProcessMode
{
    DisplayId displayId{};
};

enum class ProcessModeParseError
{
    InvalidArguments,
    NonPersistentDisplayIdentity
};

using ApplicationProcessMode = std::variant<
    LauncherProcessMode,
    DisplaySessionProcessMode,
    ProcessModeParseError
>;

struct DisplaySessionLaunchRequest
{
    QString program{};
    QStringList arguments{};

    friend bool operator==(
        const DisplaySessionLaunchRequest&,
        const DisplaySessionLaunchRequest&
    ) = default;
};

[[nodiscard]] ApplicationProcessMode parseApplicationProcessMode(
    const QStringList& arguments
);

[[nodiscard]] std::optional<DisplaySessionLaunchRequest> makeDisplaySessionLaunchRequest(
    const QString& applicationFilePath,
    const DisplayId& displayId
);

[[nodiscard]] bool startDisplaySessionDetached(const DisplayId& displayId);

}
