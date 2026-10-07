#pragma once

#include <vector>

#include <DesktopSceneEngine/Domain/EntityId.h>

namespace DesktopSceneEngine
{
namespace Domain
{

// Represents an ordered collection of Scene identifiers without owning Scenes.
struct Playlist
{
    EntityId id{};
    std::vector<EntityId> sceneIds{};
};

}
}
