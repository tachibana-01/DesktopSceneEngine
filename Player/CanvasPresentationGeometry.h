#pragma once

#include <DesktopSceneEngine/Domain/AspectRatio.h>

#include <QRect>
#include <QSize>

#include <cstdint>

namespace DesktopSceneEngine::Player
{

// Fits the logical Desktop Canvas inside editor-only Preview Viewport pixels.
// The constrained dimension is rounded down so the presentation rectangle
// never exceeds the available viewport.
[[nodiscard]] inline QRect fitCanvasInsidePreviewViewport(
    const QSize& viewportSize,
    const Domain::AspectRatio& aspectRatio) noexcept
{
    if (viewportSize.width() <= 0 || viewportSize.height() <= 0
        || !aspectRatio.isValid())
    {
        return {};
    }

    const std::int64_t canvasWidth = aspectRatio.numerator;
    const std::int64_t canvasHeight = aspectRatio.denominator;
    const std::int64_t viewportWidth = viewportSize.width();
    const std::int64_t viewportHeight = viewportSize.height();

    std::int64_t presentationWidth = viewportWidth;
    std::int64_t presentationHeight = viewportHeight;
    if (viewportWidth * canvasHeight > viewportHeight * canvasWidth)
    {
        presentationWidth = (viewportHeight * canvasWidth) / canvasHeight;
    }
    else
    {
        presentationHeight = (viewportWidth * canvasHeight) / canvasWidth;
    }

    const int width = static_cast<int>(presentationWidth);
    const int height = static_cast<int>(presentationHeight);
    return {
        (viewportSize.width() - width) / 2,
        (viewportSize.height() - height) / 2,
        width,
        height};
}

}
