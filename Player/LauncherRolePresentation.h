#pragma once

namespace DesktopSceneEngine::Player
{
class SessionPresentation;

// Presentation shared by the legacy Qt Launcher and its WebView replacement.
// Coordinator remains the owner; this is not a new process/role controller.
class LauncherRolePresentation
{
public:
    virtual ~LauncherRolePresentation() = default;
    virtual SessionPresentation& trayPresentation() = 0;
};
}
