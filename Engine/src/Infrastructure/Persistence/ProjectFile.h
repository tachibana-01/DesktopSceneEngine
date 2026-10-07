#pragma once

#include <filesystem>

#include <DesktopSceneEngine/Domain/AuthoredProject.h>
#include <DesktopSceneEngine/Domain/Project.h>

namespace DesktopSceneEngine
{
namespace Infrastructure
{
namespace Persistence
{

void saveProjectFile(
    const Domain::Project& project,
    const std::filesystem::path& path
);

Domain::Project loadProjectFile(
    const std::filesystem::path& path
);

void saveAuthoredProjectFile(
    const Domain::AuthoredProject& project,
    const std::filesystem::path& path
);

void validateAuthoredProjectSaveAsDestination(
    const Domain::DisplayOwner& sourceOwner,
    const std::filesystem::path& destination
);

[[nodiscard]] Domain::AuthoredProject loadAuthoredProjectFile(
    const std::filesystem::path& path
);

}
}
}
