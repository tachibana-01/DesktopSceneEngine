#pragma once

#include <DesktopSceneEngine/PresentationTarget.h>

#include <optional>

namespace DesktopSceneEngine::Rendering
{

struct NormalizedRectangle
{
    float x = 0.0F;
    float y = 0.0F;
    float width = 1.0F;
    float height = 1.0F;
};

// Immutable, non-owning presentation state for one render call.
// sourceUv addresses the media texture; destination is normalized authored
// Canvas geometry, independent of the native presentation target.
struct RenderPresentation
{
    NormalizedRectangle sourceUv{};
    NormalizedRectangle destination{};
};

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
