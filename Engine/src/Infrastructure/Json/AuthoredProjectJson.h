#pragma once

#include <nlohmann/json.hpp>

#include <DesktopSceneEngine/Domain/AuthoredProject.h>

#include <string_view>

namespace DesktopSceneEngine::Infrastructure::Json
{

inline constexpr std::string_view AuthoredProjectDocumentType =
    "DesktopSceneEngineProject";
inline constexpr std::string_view AuthoredProjectDocumentVersion = "3.0";

nlohmann::json toAuthoredProjectJson(const Domain::AuthoredProject& project);

Domain::AuthoredProject fromAuthoredProjectJson(
    const nlohmann::json& document);

}
