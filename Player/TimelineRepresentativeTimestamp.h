#pragma once

#include "Resources/RepresentativeFrameSampling.h"

#include <cstdint>
#include <optional>

namespace DesktopSceneEngine::Player
{

[[nodiscard]] inline std::optional<std::int64_t> representativeTimestampForTimelineHover(
    std::int64_t durationMilliseconds,
    std::int64_t hoverMilliseconds
) noexcept
{
    if (durationMilliseconds <= 0)
    {
        return std::nullopt;
    }

    const Resources::RepresentativeFrameSamplingPlan samplingPlan =
        Resources::makeRepresentativeFrameSamplingPlan(durationMilliseconds);
    if (samplingPlan.intervalMilliseconds <= 0)
    {
        return std::nullopt;
    }

    const std::int64_t clampedHoverMilliseconds = hoverMilliseconds < 0
        ? 0
        : (hoverMilliseconds >= durationMilliseconds
            ? durationMilliseconds - 1
            : hoverMilliseconds);
    return clampedHoverMilliseconds / samplingPlan.intervalMilliseconds
        * samplingPlan.intervalMilliseconds;
}

}
