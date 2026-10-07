#pragma once

#include <QRect>
#include <QStyle>

#include <algorithm>

namespace DesktopSceneEngine::Player
{

[[nodiscard]] inline int clampedTimelineHoverPopupX(
    int mouseXInCentral,
    int popupWidth,
    const QRect& previewBounds
) noexcept
{
    if (previewBounds.isEmpty() || popupWidth <= 0)
    {
        return previewBounds.left();
    }

    const int minimumX = previewBounds.left();
    const int maximumX = (std::max)(
        minimumX,
        previewBounds.right() - popupWidth + 1);
    return std::clamp(
        mouseXInCentral - popupWidth / 2,
        minimumX,
        maximumX);
}

[[nodiscard]] inline int timelineHoverValueForGroovePosition(
    int mouseXInSlider,
    const QRect& groove,
    int minimum,
    int maximum,
    bool upsideDown
) noexcept
{
    if (groove.width() <= 1 || minimum > maximum)
    {
        return minimum;
    }

    const int span = groove.width() - 1;
    const int position = std::clamp(
        mouseXInSlider - groove.left(),
        0,
        span);
    return QStyle::sliderValueFromPosition(
        minimum,
        maximum,
        position,
        span,
        upsideDown);
}

}
