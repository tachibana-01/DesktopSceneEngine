#pragma once

#include <vector>

#include <DesktopSceneEngine/Domain/Library.h>
#include <DesktopSceneEngine/Domain/Playlist.h>
#include <DesktopSceneEngine/Domain/Scene.h>

namespace DesktopSceneEngine
{
namespace Domain
{

// Defines the aggregate ownership boundary for project domain data.
struct Project
{
    Library library{};
    std::vector<Scene> scenes{};
    std::vector<Playlist> playlists{};
};

}
}
