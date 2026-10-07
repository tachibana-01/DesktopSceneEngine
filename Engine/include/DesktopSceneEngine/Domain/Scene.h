#pragma once

#include <DesktopSceneEngine/Domain/Crop.h>
#include <DesktopSceneEngine/Domain/EntityId.h>
#include <DesktopSceneEngine/Domain/Layout.h>
#include <DesktopSceneEngine/Domain/MediaPresentation.h>
#include <DesktopSceneEngine/Domain/Playback.h>

namespace DesktopSceneEngine
{
namespace Domain
{

// Represents one scene configuration and its value-owned settings.
struct Scene
{
    EntityId id{};
    EntityId mediaId{};
    Crop crop{};
    Layout layout{};
    Playback playback{};
    float duration = 0.0F;
    MediaPresentation presentation{};
};

}
}
