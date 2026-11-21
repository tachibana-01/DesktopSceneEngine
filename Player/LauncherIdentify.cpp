#include "LauncherIdentify.h"
#include <stdexcept>

namespace DesktopSceneEngine::Player
{
QJsonObject identifyLauncherDisplay(const DisplayId& id, const LauncherIdentifyOperation& identify)
{
    const auto canonical = id.canonicalRepresentation();
    if (!canonical) throw std::invalid_argument("Invalid Identify adapter target");
    const auto status = identify(id, LauncherIdentifyDurationMs);
    const char* value = nullptr;
    switch (status)
    {
    case Platform::DisplayIdentifyStatus::Shown: value = "Shown"; break;
    case Platform::DisplayIdentifyStatus::Unresolved: value = "Unresolved"; break;
    case Platform::DisplayIdentifyStatus::InvalidRequest: value = "InvalidRequest"; break;
    case Platform::DisplayIdentifyStatus::ApiFailure: value = "ApiFailure"; break;
    }
    if (!value) throw std::invalid_argument("Unknown Identify result");
    return {{"displayId", QString::fromStdString(*canonical)}, {"status", value}};
}
}
