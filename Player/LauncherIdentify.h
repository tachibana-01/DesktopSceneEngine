#pragma once

#include <DesktopSceneEngine/Platform/Display.h>
#include <QJsonObject>
#include <functional>

namespace DesktopSceneEngine::Player
{
// Match the existing production Qt Launcher. Duration is never supplied by JS.
inline constexpr std::uint32_t LauncherIdentifyDurationMs = Platform::DisplayIdentifyDurationMs;
using LauncherIdentifyOperation = std::function<Platform::DisplayIdentifyStatus(const DisplayId&, std::uint32_t)>;

// Only invokes the production capability and projects its result. No overlay,
// Display catalog, Session or preference ownership belongs to this adapter.
[[nodiscard]] QJsonObject identifyLauncherDisplay(const DisplayId& id,
    const LauncherIdentifyOperation& identify = Platform::identifyDisplay);
}
