#include "RepresentativeFrameSampling.h"

#include <algorithm>
#include <limits>

namespace DesktopSceneEngine::Resources
{

namespace
{

constexpr std::int64_t SecondMilliseconds = 1000;
constexpr std::int64_t MinuteMilliseconds = 60 * SecondMilliseconds;
constexpr std::int64_t MinimumIntervalMilliseconds = 5 * SecondMilliseconds;
constexpr std::int64_t FiveMinuteMilliseconds = 5 * MinuteMilliseconds;
constexpr std::int64_t TenMinuteMilliseconds = 10 * MinuteMilliseconds;
constexpr std::int64_t TwentyMinuteMilliseconds = 20 * MinuteMilliseconds;
constexpr std::int64_t ThirtyMinuteMilliseconds = 30 * MinuteMilliseconds;
constexpr std::int64_t SixtyMinuteMilliseconds = 60 * MinuteMilliseconds;
constexpr std::int64_t OneHundredTwentyMinuteMilliseconds = 120 * MinuteMilliseconds;

std::int64_t piecewiseIntervalMilliseconds(std::int64_t durationMilliseconds) noexcept
{
    if (durationMilliseconds < FiveMinuteMilliseconds)
    {
        return MinimumIntervalMilliseconds;
    }

    if (durationMilliseconds < TenMinuteMilliseconds)
    {
        return 10 * SecondMilliseconds;
    }

    if (durationMilliseconds < TwentyMinuteMilliseconds)
    {
        return 15 * SecondMilliseconds;
    }

    if (durationMilliseconds < ThirtyMinuteMilliseconds)
    {
        return 20 * SecondMilliseconds;
    }

    if (durationMilliseconds < SixtyMinuteMilliseconds)
    {
        return 30 * SecondMilliseconds;
    }

    if (durationMilliseconds < OneHundredTwentyMinuteMilliseconds)
    {
        return 60 * SecondMilliseconds;
    }

    return 120 * SecondMilliseconds;
}

std::int64_t ceilToFiveSeconds(std::int64_t durationMilliseconds, std::size_t maximumFrameCount) noexcept
{
    if (durationMilliseconds <= 0 || maximumFrameCount == 0U)
    {
        return MinimumIntervalMilliseconds;
    }

    const std::int64_t maximumCount = maximumFrameCount
            > static_cast<std::size_t>((std::numeric_limits<std::int64_t>::max)())
        ? (std::numeric_limits<std::int64_t>::max)()
        : static_cast<std::int64_t>(maximumFrameCount);
    const std::int64_t requiredMilliseconds = durationMilliseconds / maximumCount
        + (durationMilliseconds % maximumCount == 0 ? 0 : 1);
    const std::int64_t chunks = requiredMilliseconds / MinimumIntervalMilliseconds
        + (requiredMilliseconds % MinimumIntervalMilliseconds == 0 ? 0 : 1);

    if (chunks > (std::numeric_limits<std::int64_t>::max)() / MinimumIntervalMilliseconds)
    {
        return (std::numeric_limits<std::int64_t>::max)();
    }

    return chunks * MinimumIntervalMilliseconds;
}

}

RepresentativeFrameSamplingPlan makeRepresentativeFrameSamplingPlan(
    std::int64_t durationMilliseconds,
    std::size_t maximumFrameCount
)
{
    RepresentativeFrameSamplingPlan plan{};
    plan.intervalMilliseconds = (std::max)(
        piecewiseIntervalMilliseconds(durationMilliseconds),
        ceilToFiveSeconds(durationMilliseconds, maximumFrameCount));

    if (durationMilliseconds <= 0 || plan.intervalMilliseconds <= 0)
    {
        return plan;
    }

    if (maximumFrameCount > 0U)
    {
        plan.timestampsMilliseconds.reserve(maximumFrameCount);
    }

    for (std::int64_t timestampMilliseconds = 0;
         timestampMilliseconds < durationMilliseconds;)
    {
        plan.timestampsMilliseconds.push_back(timestampMilliseconds);

        if (timestampMilliseconds > durationMilliseconds - plan.intervalMilliseconds)
        {
            break;
        }

        timestampMilliseconds += plan.intervalMilliseconds;
    }

    return plan;
}

}
