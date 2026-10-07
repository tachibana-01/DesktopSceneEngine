#include "Audio/AudioSampleConversion.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <vector>

namespace
{

using DesktopSceneEngine::Audio::convertF32SamplesToS16;
using DesktopSceneEngine::Audio::convertS16SamplesToF32;

bool require(bool condition, const char* message)
{
    if (condition)
    {
        return true;
    }

    std::cerr << "Test failed: " << message << std::endl;
    return false;
}

bool verifyConversionContract()
{
    const std::vector<float> source{
        0.0F,
        1.0F,
        -1.0F,
        1.5F,
        -1.5F,
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::quiet_NaN(),
        0.5F,
        -0.5F,
        0.25F,
        -0.25F,
        0.5F / 32767.0F,
        -0.5F / 32768.0F};
    std::vector<std::int16_t> destination(source.size(), 0);

    if (!require(convertF32SamplesToS16(source, destination),
            "Equal-sized source and destination must convert"))
    {
        return false;
    }

    const std::vector<std::int16_t> expected{
        0,
        32767,
        -32768,
        32767,
        -32768,
        32767,
        -32768,
        0,
        16384,
        -16384,
        8192,
        -8192,
        1,
        -1};
    return require(destination == expected,
        "F32 to S16 conversion must use the frozen saturation and rounding contract");
}

bool verifySizeMismatchPreservesDestination()
{
    const std::vector<float> source{0.25F, -0.25F};
    std::vector<std::int16_t> destination{123, -456, 789};
    const std::vector<std::int16_t> expected = destination;

    return require(!convertF32SamplesToS16(source, destination),
            "A size mismatch must fail")
        && require(destination == expected,
            "A size mismatch must not partially modify the destination");
}

bool verifyS16ToF32ConversionContract()
{
    const std::vector<std::int16_t> source{
        (std::numeric_limits<std::int16_t>::min)(),
        -16384,
        -1,
        0,
        1,
        16384,
        (std::numeric_limits<std::int16_t>::max)()};
    std::vector<float> destination(source.size(), 0.0F);

    if (!require(convertS16SamplesToF32(source, destination),
            "Equal-sized S16 and F32 spans must convert"))
    {
        return false;
    }

    const std::vector<float> expected{
        -1.0F,
        -0.5F,
        -1.0F / 32768.0F,
        0.0F,
        1.0F / 32767.0F,
        16384.0F / 32767.0F,
        1.0F};
    return require(destination == expected,
        "S16 to F32 conversion must use the frozen asymmetric normalization")
        && require(convertS16SamplesToF32({}, {}),
            "Equal empty spans must convert without allocation");
}

bool verifyS16ToF32MismatchPreservesDestination()
{
    const std::vector<std::int16_t> source{1, -1};
    std::vector<float> destination{0.25F, -0.25F, 0.5F};
    const std::vector<float> expected = destination;

    return require(!convertS16SamplesToF32(source, destination),
            "A mismatched S16 to F32 conversion must fail")
        && require(destination == expected,
            "A mismatched S16 to F32 conversion must not partially modify output");
}

bool verifyExhaustiveS16RoundTrip()
{
    std::vector<std::int16_t> source{};
    source.reserve(65536U);
    for (int value = (std::numeric_limits<std::int16_t>::min)();
         value <= (std::numeric_limits<std::int16_t>::max)();
         ++value)
    {
        source.push_back(static_cast<std::int16_t>(value));
    }

    std::vector<float> normalized(source.size(), 0.0F);
    std::vector<std::int16_t> roundTripped(source.size(), 0);
    return require(convertS16SamplesToF32(source, normalized),
            "Every S16 value must normalize")
        && require(convertF32SamplesToS16(normalized, roundTripped),
            "Every normalized S16 value must convert back")
        && require(roundTripped == source,
            "S16 to F32 to S16 must round-trip all 65536 values exactly");
}

}

int main()
{
    return verifyConversionContract()
        && verifySizeMismatchPreservesDestination()
        && verifyS16ToF32ConversionContract()
        && verifyS16ToF32MismatchPreservesDestination()
        && verifyExhaustiveS16RoundTrip()
        ? 0
        : 1;
}
