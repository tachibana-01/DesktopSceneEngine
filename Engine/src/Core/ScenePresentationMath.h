#pragma once

#include "../Rendering/RenderData.h"

#include <DesktopSceneEngine/Domain/AspectRatio.h>
#include <DesktopSceneEngine/Presentation/PresentationGeometry.h>

#include <optional>

namespace DesktopSceneEngine::Core
{

// Converts the active Scene's pure Domain presentation values into the
// renderer-private normalized rectangles by adapting the public pure geometry
// contract. The resolver owns all presentation semantics, including the
// editor-up to Canvas-down Y conversion.
[[nodiscard]] inline std::optional<Rendering::RenderPresentation>
makeScenePresentationRenderState(
    int sourceWidth,
    int sourceHeight,
    const Domain::AspectRatio& canvasAspect,
    const Domain::Crop& crop,
    const Domain::Layout& layout,
    const Domain::MediaPresentation& presentation
) noexcept
{
    if (!canvasAspect.isValid())
    {
        return std::nullopt;
    }

    const auto geometry = Presentation::resolvePresentationGeometry({
        sourceWidth,
        sourceHeight,
        canvasAspect.numerator,
        canvasAspect.denominator,
        crop,
        layout,
        presentation});
    if (!geometry)
    {
        return std::nullopt;
    }

    return Rendering::RenderPresentation{
        {geometry->sourceUv.x,
         geometry->sourceUv.y,
         geometry->sourceUv.width,
         geometry->sourceUv.height},
        {geometry->destination.x,
         geometry->destination.y,
         geometry->destination.width,
         geometry->destination.height}};
}

}
