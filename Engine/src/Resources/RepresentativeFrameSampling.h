#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace DesktopSceneEngine::Resources
{

inline constexpr std::size_t MaximumRepresentativeFramesPerSource = 120U;

struct RepresentativeFrameSamplingPlan
{
    std::int64_t intervalMilliseconds = 0;
    std::vector<std::int64_t> timestampsMilliseconds{};

    [[nodiscard]] bool operator==(const RepresentativeFrameSamplingPlan&) const = default;
};

[[nodiscard]] RepresentativeFrameSamplingPlan makeRepresentativeFrameSamplingPlan(
    std::int64_t durationMilliseconds,
    std::size_t maximumFrameCount = MaximumRepresentativeFramesPerSource
);

}
