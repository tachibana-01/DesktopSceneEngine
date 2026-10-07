#pragma once

#include <DesktopSceneEngine/DisplayIdentity.h>
#include <DesktopSceneEngine/Domain/DisplayOwner.h>

namespace DesktopSceneEngine::Player::TestSupport
{

[[nodiscard]] inline DisplayId mainWindowTestDisplayId()
{
    return DisplayId::fromTargetDevicePath(
        "\\\\?\\DISPLAY#DESKTOPSCENEENGINE_TEST#DISPLAY_1");
}

[[nodiscard]] inline Domain::DisplayOwner mainWindowTestDisplayOwner()
{
    return *Domain::DisplayOwner::fromDisplayId(mainWindowTestDisplayId());
}

}
