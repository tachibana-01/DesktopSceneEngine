#include "TimelineUtils.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <string>

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
    using DesktopSceneEngine::Player::timelineTimeForValue;
    using DesktopSceneEngine::Player::timelineValueForTime;
    using DesktopSceneEngine::Player::formatTimelineTime;
    using DesktopSceneEngine::Player::isTimelineEndIntent;

    constexpr float duration = 120.0F;
    if (!require(timelineValueForTime(0.0F, duration) == 0,
            "Zero media time should map to the minimum slider value")
        || !require(timelineValueForTime(60.0F, duration) == 5000,
            "Half duration should map to the slider midpoint")
        || !require(timelineValueForTime(duration, duration) == 10000,
            "Full duration should map to the slider maximum")
        || !require(timelineValueForTime(-1.0F, duration) == 0,
            "Negative media time should clamp to the slider minimum")
        || !require(timelineValueForTime(240.0F, duration) == 10000,
            "Media time beyond duration should clamp to the slider maximum")
        || !require(timelineValueForTime(
                10.0F,
                (std::numeric_limits<float>::quiet_NaN)()) == 0,
            "Invalid duration should not produce a seekable slider value")
        || !require(std::fabs(timelineTimeForValue(0, duration)) < 0.0001F,
            "Minimum slider value should map to zero media time")
        || !require(std::fabs(timelineTimeForValue(5000, duration) - 60.0F) < 0.0001F,
            "Slider midpoint should map to half the media duration")
        || !require(std::fabs(timelineTimeForValue(10000, duration) - duration) < 0.0001F,
            "Slider maximum should map to total media duration")
        || !require(std::fabs(timelineTimeForValue(-1, duration)) < 0.0001F,
            "Negative slider values should clamp to zero media time")
        || !require(std::fabs(timelineTimeForValue(10001, duration) - duration) < 0.0001F,
            "Slider values beyond maximum should clamp to total media duration")
        || !require(std::fabs(timelineTimeForValue(5000, 0.0F)) < 0.0001F,
            "Zero duration should not produce a seek target")
        || !require(!isTimelineEndIntent(9999),
            "The value immediately before the maximum should remain a normal seek")
        || !require(isTimelineEndIntent(10000),
            "Only the exact slider maximum should request item completion")
        || !require(!isTimelineEndIntent(10001),
            "Values beyond the slider maximum should not broaden endpoint intent")
        || !require(formatTimelineTime(7.0F) == "00:07",
            "Sub-minute time should use MM:SS")
        || !require(formatTimelineTime(134.0F) == "02:14",
            "Minute time should use MM:SS")
        || !require(formatTimelineTime(3727.0F) == "01:02:07",
            "Hour time should use HH:MM:SS"))
    {
        return 1;
    }

    return 0;
}
