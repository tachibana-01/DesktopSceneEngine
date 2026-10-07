#pragma once

#include <nlohmann/json.hpp>

#include <DesktopSceneEngine/Domain/Project.h>

namespace DesktopSceneEngine
{
namespace Infrastructure
{
namespace Json
{

nlohmann::json toJson(const Domain::Project& project);

Domain::Project fromJson(const nlohmann::json& document);

}
}
}
