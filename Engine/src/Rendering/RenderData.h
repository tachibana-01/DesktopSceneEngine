#pragma once

#include <DesktopSceneEngine/PresentationTarget.h>
#include <DesktopSceneEngine/Presentation/PresentationGeometry.h>

#include <optional>

namespace DesktopSceneEngine::Rendering
{

using RenderPresentation = Presentation::ResolvedPresentationGeometry;

// Internal render input boundary. It intentionally carries no domain or GPU state.
struct RenderData
{
    float clearColor[4] = {0.1F, 0.1F, 0.1F, 1.0F};
    RenderPresentation presentation{};
    // Cause only: each target derives its viewport from its own native size.
    // nullopt retains legacy target fill; invalid explicit values draw no Canvas.
    std::optional<double> canvasAspect{};
    // Owned snapshot: no policy is retained by a target or rendering resource.
    std::optional<CanvasOutsideInstruction> canvasOutside{};
};

}
