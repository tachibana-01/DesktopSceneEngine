#pragma once

#include <DesktopSceneEngine/Domain/Crop.h>
#include <DesktopSceneEngine/Domain/Layout.h>
#include <DesktopSceneEngine/Domain/MediaPresentation.h>

#include <cmath>
#include <optional>

namespace DesktopSceneEngine::Presentation
{

// A rectangle in normalized Canvas or source-UV coordinates. Resolved
// destination coordinates use a top-left origin with positive Y down.
struct NormalizedRectangle
{
    float x = 0.0F;
    float y = 0.0F;
    float width = 1.0F;
    float height = 1.0F;
};

// Pure presentation inputs. Crop and Layout retain their Domain semantics:
// Crop is normalized source UV, while positive Layout positionY is editor-up.
struct PresentationGeometryInput
{
    int sourceWidth = 0;
    int sourceHeight = 0;
    int canvasWidth = 0;
    int canvasHeight = 0;
    Domain::Crop crop{};
    Domain::Layout layout{};
    Domain::MediaPresentation presentation{};
};

// A resolved source-to-Canvas mapping. destination is normalized Canvas
// geometry; it is intentionally allowed to extend beyond the unit Canvas.
struct ResolvedPresentationGeometry
{
    NormalizedRectangle sourceUv{};
    NormalizedRectangle destination{};
};

namespace Detail
{

[[nodiscard]] inline bool isPositiveFinite(float value) noexcept
{
    return std::isfinite(value) && value > 0.0F;
}

[[nodiscard]] inline bool isFinite(float value) noexcept
{
    return std::isfinite(value);
}

[[nodiscard]] inline bool isNormalizedCrop(const Domain::Crop& crop) noexcept
{
    return isFinite(crop.x)
        && isFinite(crop.y)
        && isPositiveFinite(crop.width)
        && isPositiveFinite(crop.height)
        && crop.x >= 0.0F
        && crop.y >= 0.0F
        && crop.x + crop.width <= 1.0F
        && crop.y + crop.height <= 1.0F;
}

[[nodiscard]] inline NormalizedRectangle fitAspect(
    float contentAspect,
    float canvasAspect
) noexcept
{
    if (contentAspect > canvasAspect)
    {
        const float height = canvasAspect / contentAspect;
        return {0.0F, (1.0F - height) * 0.5F, 1.0F, height};
    }

    const float width = contentAspect / canvasAspect;
    return {(1.0F - width) * 0.5F, 0.0F, width, 1.0F};
}

[[nodiscard]] inline NormalizedRectangle cropToAspect(
    const Domain::Crop& crop,
    float sourceAspect,
    float targetAspect
) noexcept
{
    const float croppedAspect = sourceAspect * crop.width / crop.height;
    if (croppedAspect > targetAspect)
    {
        const float width = crop.height * targetAspect / sourceAspect;
        return {crop.x + (crop.width - width) * 0.5F, crop.y, width, crop.height};
    }

    const float height = crop.width * sourceAspect / targetAspect;
    return {crop.x, crop.y + (crop.height - height) * 0.5F, crop.width, height};
}

[[nodiscard]] inline NormalizedRectangle applyLayoutTransform(
    const NormalizedRectangle& baseline,
    const Domain::Layout& layout
) noexcept
{
    const float width = baseline.width * layout.scaleX;
    const float height = baseline.height * layout.scaleY;
    return {
        baseline.x + (baseline.width - width) * 0.5F + layout.positionX,
        baseline.y + (baseline.height - height) * 0.5F - layout.positionY,
        width,
        height};
}

}

// Resolves the established scene-presentation semantics without depending on
// a renderer. Crop is applied before mode resolution. Native contains the
// cropped source, Auto Fill centered-crops it to cover the Canvas, and Custom
// fits its logical aspect. Each policy baseline then receives Layout scale and
// position. The editor-up Layout Y coordinate becomes Canvas-down destination
// Y here.
[[nodiscard]] inline std::optional<ResolvedPresentationGeometry>
resolvePresentationGeometry(const PresentationGeometryInput& input) noexcept
{
    if (input.sourceWidth <= 0 || input.sourceHeight <= 0
        || input.canvasWidth <= 0 || input.canvasHeight <= 0
        || !Detail::isNormalizedCrop(input.crop)
        || !Detail::isFinite(input.layout.positionX)
        || !Detail::isFinite(input.layout.positionY)
        || !Detail::isPositiveFinite(input.layout.scaleX)
        || !Detail::isPositiveFinite(input.layout.scaleY))
    {
        return std::nullopt;
    }

    const float sourceAspect = static_cast<float>(input.sourceWidth)
        / static_cast<float>(input.sourceHeight);
    const float canvasAspect = static_cast<float>(input.canvasWidth)
        / static_cast<float>(input.canvasHeight);
    if (!Detail::isPositiveFinite(sourceAspect) || !Detail::isPositiveFinite(canvasAspect))
    {
        return std::nullopt;
    }

    switch (input.presentation.mode)
    {
    case Domain::MediaPresentationMode::Native:
    {
        const float croppedAspect = sourceAspect * input.crop.width / input.crop.height;
        return ResolvedPresentationGeometry{
            {input.crop.x, input.crop.y, input.crop.width, input.crop.height},
            Detail::applyLayoutTransform(
                Detail::fitAspect(croppedAspect, canvasAspect),
                input.layout)};
    }
    case Domain::MediaPresentationMode::AutoFill:
        return ResolvedPresentationGeometry{
            Detail::cropToAspect(input.crop, sourceAspect, canvasAspect),
            Detail::applyLayoutTransform({}, input.layout)};
    case Domain::MediaPresentationMode::Custom:
    {
        if (!input.presentation.customAspect.isValid())
        {
            return std::nullopt;
        }

        const float customAspect = static_cast<float>(input.presentation.customAspect.numerator)
            / static_cast<float>(input.presentation.customAspect.denominator);
        if (!Detail::isPositiveFinite(customAspect))
        {
            return std::nullopt;
        }

        const NormalizedRectangle fitted = Detail::fitAspect(customAspect, canvasAspect);
        return ResolvedPresentationGeometry{
            Detail::cropToAspect(input.crop, sourceAspect, customAspect),
            Detail::applyLayoutTransform(fitted, input.layout)};
    }
    }

    return std::nullopt;
}

}
