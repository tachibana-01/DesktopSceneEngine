#pragma once

#include <cmath>
#include <optional>

namespace DesktopSceneEngine::Rendering
{

struct PresentationViewport
{
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
};

// Native available area -> draw viewport. An absent authored ratio preserves
// legacy target fill; an invalid explicit ratio has no drawable Canvas.
// Fractional pixels are retained (no integer snapping/aspect distortion).
[[nodiscard]] inline std::optional<PresentationViewport> derivePresentationViewport(
    int nativeWidth, int nativeHeight, std::optional<double> canvasAspect) noexcept
{
    if (nativeWidth <= 0 || nativeHeight <= 0)
        return std::nullopt;

    const double availableWidth = nativeWidth;
    const double availableHeight = nativeHeight;
    double width = availableWidth;
    double height = availableHeight;
    if (canvasAspect)
    {
        if (!std::isfinite(*canvasAspect) || *canvasAspect <= 0.0)
            return std::nullopt;
        // Choose the limiting axis before multiplying: H * aspect cannot
        // overflow in the height-limited branch, even for extreme ratios.
        const double targetAspect = availableWidth / availableHeight;
        if (*canvasAspect > targetAspect)
            height = availableWidth / *canvasAspect;
        else if (*canvasAspect < targetAspect)
            width = availableHeight * *canvasAspect;
        // Equal ratios retain native extents without multiply/divide round-off.
    }

    const auto containedFloat = [](double value)
    {
        float result = static_cast<float>(value);
        if (double(result) > value)
            result = std::nextafter(result, 0.0F);
        return result;
    };
    const float w = containedFloat(width);
    const float h = containedFloat(height);
    if (w <= 0.0F || h <= 0.0F)
        return std::nullopt; // Ratio cannot produce a representable viewport.

    return PresentationViewport{
        canvasAspect ? static_cast<float>((availableWidth - w) * 0.5) : 0.0F,
        canvasAspect ? static_cast<float>((availableHeight - h) * 0.5) : 0.0F,
        w, h};
}

}
