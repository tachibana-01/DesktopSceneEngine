#include "TimelineRepresentativeTimestamp.h"

#include <cstdint>
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

bool verifyPrecedingRepresentativeTimestamp()
{
    using DesktopSceneEngine::Player::representativeTimestampForTimelineHover;

    constexpr std::int64_t durationMilliseconds = 20'000;
    return require(representativeTimestampForTimelineHover(durationMilliseconds, 0)
                    == std::int64_t{0},
               "Zero hover time must map to the zero representative timestamp")
        && require(representativeTimestampForTimelineHover(durationMilliseconds, 4'999)
                    == std::int64_t{0},
               "A hover inside the first bucket must map to zero")
        && require(representativeTimestampForTimelineHover(durationMilliseconds, 5'000)
                    == std::int64_t{5'000},
               "An exact bucket boundary must map to that representative timestamp")
        && require(representativeTimestampForTimelineHover(durationMilliseconds, 9'999)
                    == std::int64_t{5'000},
               "A hover before the next bucket must map to the preceding timestamp")
        && require(representativeTimestampForTimelineHover(durationMilliseconds, 20'000)
                    == std::int64_t{15'000},
               "Exact EOF must clamp to duration minus one millisecond before mapping")
        && require(representativeTimestampForTimelineHover(0, 0) == std::nullopt,
               "A non-positive duration must not produce a representative timestamp");
}

bool verifyCommittedSamplingPolicyIsUsed()
{
    using DesktopSceneEngine::Player::representativeTimestampForTimelineHover;

    constexpr std::int64_t fiveMinuteDurationMilliseconds = 5 * 60 * 1'000;
    return require(representativeTimestampForTimelineHover(
                       fiveMinuteDurationMilliseconds,
                       19'999)
                    == std::int64_t{10'000},
               "The mapping must use the committed ten-second policy at five minutes");
}

}

int main()
{
    return verifyPrecedingRepresentativeTimestamp() && verifyCommittedSamplingPolicyIsUsed()
        ? 0
        : 1;
}
