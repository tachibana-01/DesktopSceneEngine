#pragma once

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>

namespace DesktopSceneEngine::Player
{

inline constexpr int TimelineSliderMaximum = 10000;

[[nodiscard]] inline bool isTimelineEndIntent(int sliderValue) noexcept
{
    return sliderValue == TimelineSliderMaximum;
}

[[nodiscard]] inline bool hasValidTimelineDuration(float duration) noexcept
{
    return std::isfinite(duration) && duration > 0.0F;
}

[[nodiscard]] inline int timelineValueForTime(
    float mediaTime,
    float duration
) noexcept
{
    if (!hasValidTimelineDuration(duration))
    {
        return 0;
    }

    const float clampedTime = std::isfinite(mediaTime)
        ? std::clamp(mediaTime, 0.0F, duration)
        : (mediaTime > 0.0F ? duration : 0.0F);
    const float normalized = clampedTime / duration;
    return std::clamp(
        static_cast<int>(std::lround(
            static_cast<double>(normalized)
                * static_cast<double>(TimelineSliderMaximum))),
        0,
        TimelineSliderMaximum);
}

[[nodiscard]] inline float timelineTimeForValue(
    int sliderValue,
    float duration
) noexcept
{
    if (!hasValidTimelineDuration(duration))
    {
        return 0.0F;
    }

    const int clampedValue = std::clamp(
        sliderValue,
        0,
        TimelineSliderMaximum);
    return duration * static_cast<float>(clampedValue)
        / static_cast<float>(TimelineSliderMaximum);
}

[[nodiscard]] inline std::string formatTimelineTime(float seconds)
{
    const double safeSeconds = !std::isfinite(seconds) || seconds < 0.0F
        ? 0.0
        : (std::min)(
            static_cast<double>(seconds),
            static_cast<double>((std::numeric_limits<long long>::max)()));
    const auto totalSeconds = static_cast<long long>(safeSeconds);
    const long long minutes = (totalSeconds / 60) % 60;
    const long long hours = totalSeconds / 3600;
    const long long remainingSeconds = totalSeconds % 60;

    std::ostringstream result;
    result << std::setfill('0');
    if (hours > 0)
    {
        result << std::setw(2) << hours << ':';
    }
    result << std::setw(2) << minutes << ':'
           << std::setw(2) << remainingSeconds;
    return result.str();
}

}
