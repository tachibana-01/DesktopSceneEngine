#pragma once
#include <DesktopSceneEngine/Platform/Display.h>
#include <QString>

namespace DesktopSceneEngine::Player
{
// Friendly presentation only. Identity/order remain Platform snapshot authorities.
[[nodiscard]] QString displayLabel(const Platform::DisplayDescriptor& display);
}
