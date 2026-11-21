#pragma once

#include "LauncherLanguageProjection.h"
#include "LauncherSessionsProjection.h"
#include <DesktopSceneEngine/Platform/Display.h>
#include <QJsonObject>
#include <QUuid>
#include <vector>

namespace DesktopSceneEngine::Player
{
// Shared value-only catalog projection for bootstrap and explicit Refresh.
// Impossible success descriptors reject the complete projection.
[[nodiscard]] QJsonObject launcherDisplayCatalog(const Platform::DisplayEnumerationResult& displays);

[[nodiscard]] QJsonObject launcherBootstrapSnapshot(const LauncherSessionsSnapshot& sessions,
    const Platform::DisplayEnumerationResult& displays, const LauncherLanguageProjection& language);

// Value projection only. The registry is an observed registered set, never an
// occupancy authority for roles that may still be starting/admitted elsewhere.
[[nodiscard]] QJsonObject launcherBootstrapSnapshot(const QUuid& coordinatorEpoch,
    const Platform::DisplayEnumerationResult& displays,
    const std::vector<LauncherRegisteredSession>& registeredSessions,
    std::optional<UiLanguage> committedLanguage, UiLanguage effectiveLanguage);

[[nodiscard]] QJsonObject launcherBootstrapSnapshot(const QUuid& coordinatorEpoch,
    const Platform::DisplayEnumerationResult& displays,
    const std::vector<LauncherRegisteredSession>& registeredSessions,
    const LauncherLanguageProjection& language);
}
