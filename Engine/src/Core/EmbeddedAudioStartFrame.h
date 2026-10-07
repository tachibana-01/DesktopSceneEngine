#pragma once

#include <cmath>
#include <cstddef>
#include <optional>

namespace DesktopSceneEngine::Core
{

[[nodiscard]] inline std::optional<std::size_t> resolveEmbeddedAudioStartFrame(
    std::size_t tempoFrameCount,
    int sampleRate,
    float mediaTime,
    float playbackSpeed
) noexcept
{
    constexpr int CanonicalSampleRate = 48000;
    if (tempoFrameCount == 0U
        || sampleRate != CanonicalSampleRate
        || !std::isfinite(mediaTime) || mediaTime < 0.0F
        || !std::isfinite(playbackSpeed)
        || playbackSpeed < 0.5F || playbackSpeed > 2.0F)
    {
        return std::nullopt;
    }

    const double outputFrame = static_cast<double>(mediaTime)
        / static_cast<double>(playbackSpeed)
        * static_cast<double>(sampleRate);
    if (!std::isfinite(outputFrame) || outputFrame < 0.0
        || outputFrame >= static_cast<double>(tempoFrameCount))
    {
        return std::nullopt;
    }

    return static_cast<std::size_t>(outputFrame);
}

}
