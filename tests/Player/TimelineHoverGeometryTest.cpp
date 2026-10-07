#include "TimelineHoverGeometry.h"
#include "TimelineUtils.h"

#include <QRect>

#include <cmath>
#include <iostream>

namespace
{

bool require(bool condition, const char* message)
{
    if (condition)
    {
        return true;
    }

    std::cerr << "Test failed: " << message << std::endl;
    return false;
}

}

int main()
{
    using DesktopSceneEngine::Player::clampedTimelineHoverPopupX;
    using DesktopSceneEngine::Player::timelineHoverValueForGroovePosition;

    const QRect previewBounds{100, 20, 801, 400};
    const QRect groove{20, 0, 101, 8};
    const int popupWidth = 240;
    constexpr float durationSeconds = 51.0F;

    const int centeredX = clampedTimelineHoverPopupX(500, popupWidth, previewBounds);
    const int leftClampedX = clampedTimelineHoverPopupX(110, popupWidth, previewBounds);
    const int fartherLeftX = clampedTimelineHoverPopupX(0, popupWidth, previewBounds);
    const int rightClampedX = clampedTimelineHoverPopupX(900, popupWidth, previewBounds);

    if (!require(centeredX == 380,
            "A popup with available space should center on the hover X position")
        || !require(leftClampedX == previewBounds.left()
                && fartherLeftX == previewBounds.left(),
            "Left-bound hover positions should clamp the popup to the preview bounds")
        || !require(rightClampedX == previewBounds.right() - popupWidth + 1,
            "Right-bound hover positions should clamp the popup to the preview bounds")
        || !require(timelineHoverValueForGroovePosition(
                groove.left(), groove, 0, 10000, false) == 0,
            "The left groove edge should map to the minimum timeline value")
        || !require(timelineHoverValueForGroovePosition(
                groove.center().x(), groove, 0, 10000, false) >= 4900
                && timelineHoverValueForGroovePosition(
                    groove.center().x(), groove, 0, 10000, false) <= 5100,
            "The groove midpoint should map near the timeline midpoint")
        || !require(timelineHoverValueForGroovePosition(
                groove.right(), groove, 0, 10000, false) == 10000,
            "The right groove edge should map to the maximum preview value")
        || !require(std::fabs(DesktopSceneEngine::Player::timelineTimeForValue(
                timelineHoverValueForGroovePosition(
                    groove.right(), groove, 0, 10000, false),
                durationSeconds)
                - durationSeconds)
                < 0.0001F,
            "The right groove edge should remain a preview of the effective duration")
        || !require(timelineHoverValueForGroovePosition(
                groove.left() - 50, groove, 0, 10000, false) == 0
                && timelineHoverValueForGroovePosition(
                    groove.right() + 50, groove, 0, 10000, false) == 10000,
            "Positions outside the groove should clamp safely")
        || !require(leftClampedX == fartherLeftX
                && timelineHoverValueForGroovePosition(
                    groove.left(), groove, 0, 10000, false)
                    != timelineHoverValueForGroovePosition(
                        groove.center().x(), groove, 0, 10000, false),
            "Timeline target calculation must remain independent from popup clamping"))
    {
        return 1;
    }

    return 0;
}
