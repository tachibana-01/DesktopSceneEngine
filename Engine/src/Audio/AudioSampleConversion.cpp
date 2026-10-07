#include "AudioSampleConversion.h"

#include <cmath>

namespace DesktopSceneEngine::Audio
{

namespace
{

std::int16_t convertSample(float value) noexcept
{
    if (std::isnan(value))
    {
        return 0;
    }

    if (value >= 1.0F)
    {
        return 32767;
    }

    if (value <= -1.0F)
    {
        return -32768;
    }

    if (value > 0.0F)
    {
        return static_cast<std::int16_t>(std::lround(value * 32767.0F));
    }

    if (value < 0.0F)
    {
        return static_cast<std::int16_t>(std::lround(value * 32768.0F));
    }

    return 0;
}

}

bool convertF32SamplesToS16(
    std::span<const float> source,
    std::span<std::int16_t> destination
) noexcept
{
    if (source.size() != destination.size())
    {
        return false;
    }

    for (std::size_t index = 0U; index < source.size(); ++index)
    {
        destination[index] = convertSample(source[index]);
    }

    return true;
}

bool convertS16SamplesToF32(
    std::span<const std::int16_t> source,
    std::span<float> destination
) noexcept
{
    if (source.size() != destination.size())
    {
        return false;
    }

    for (std::size_t index = 0U; index < source.size(); ++index)
    {
        const std::int16_t sample = source[index];
        destination[index] = sample < 0
            ? static_cast<float>(sample) / 32768.0F
            : static_cast<float>(sample) / 32767.0F;
    }

    return true;
}

}
