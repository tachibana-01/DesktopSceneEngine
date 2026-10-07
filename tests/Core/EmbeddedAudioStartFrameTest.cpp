#include "Core/EmbeddedAudioStartFrame.h"

#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>

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

bool verifyLegacyEquivalentMapping()
{
    using DesktopSceneEngine::Core::resolveEmbeddedAudioStartFrame;

    constexpr std::size_t TempoFrameCount = 480000U;
    return require(
               resolveEmbeddedAudioStartFrame(
                   TempoFrameCount,
                   48000,
                   0.0F,
                   1.0F) == 0U,
               "Zero media time must map to output frame zero")
        && require(
               resolveEmbeddedAudioStartFrame(
                   TempoFrameCount,
                   48000,
                   1.234F,
                   1.0F) == 59231U,
               "One-times speed must truncate fractional output frames")
        && require(
               resolveEmbeddedAudioStartFrame(
                   TempoFrameCount,
                   48000,
                   1.234F,
                   0.5F) == 118463U,
               "Half speed must use the legacy media-time to tempo-frame mapping")
        && require(
               resolveEmbeddedAudioStartFrame(
                   TempoFrameCount,
                   48000,
                   1.234F,
                   2.0F) == 29615U,
               "Double speed must use the legacy media-time to tempo-frame mapping")
        && require(
               resolveEmbeddedAudioStartFrame(
                   TempoFrameCount,
                   48000,
                   9.999F,
                   1.0F) == 479951U,
               "A frame before tempo PCM EOF must remain playable");
}

bool verifyRejectedConfigurations()
{
    using DesktopSceneEngine::Core::resolveEmbeddedAudioStartFrame;

    constexpr std::size_t TempoFrameCount = 480000U;
    return require(
               !resolveEmbeddedAudioStartFrame(
                   TempoFrameCount,
                   48000,
                   10.0F,
                   1.0F).has_value(),
               "Exact tempo PCM EOF must be rejected")
        && require(
               !resolveEmbeddedAudioStartFrame(
                   TempoFrameCount,
                   48000,
                   -0.1F,
                   1.0F).has_value(),
               "Negative media time must be rejected")
        && require(
               !resolveEmbeddedAudioStartFrame(
                   TempoFrameCount,
                   48000,
                   (std::numeric_limits<float>::quiet_NaN)(),
                   1.0F).has_value(),
               "Non-finite media time must be rejected")
        && require(
               !resolveEmbeddedAudioStartFrame(
                   TempoFrameCount,
                   48000,
                   1.0F,
                   0.49F).has_value(),
               "Unsupported playback speed must be rejected")
        && require(
               !resolveEmbeddedAudioStartFrame(
                   TempoFrameCount,
                   44100,
                   1.0F,
                   1.0F).has_value(),
               "Non-canonical sample rates must be rejected")
        && require(
               !resolveEmbeddedAudioStartFrame(
                   0U,
                   48000,
                   0.0F,
                   1.0F).has_value(),
               "Empty tempo PCM must be rejected");
}

}

int main()
{
    return verifyLegacyEquivalentMapping() && verifyRejectedConfigurations()
        ? 0
        : 1;
}
