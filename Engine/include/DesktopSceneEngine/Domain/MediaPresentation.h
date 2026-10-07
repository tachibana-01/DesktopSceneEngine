#pragma once

#include <DesktopSceneEngine/Domain/AspectRatio.h>

namespace DesktopSceneEngine::Domain
{

enum class MediaPresentationMode
{
    Native,
    AutoFill,
    Custom
};

// Scene-level presentation semantics. This contains no renderer, display, or
// GPU state; the Engine translates it to its private render input per frame.
struct MediaPresentation
{
    MediaPresentationMode mode = MediaPresentationMode::AutoFill;
    AspectRatio customAspect{};
};

}
