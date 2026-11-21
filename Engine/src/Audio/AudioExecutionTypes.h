#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace DesktopSceneEngine::Audio
{

// Every timestamp crossing C/A/P is a QPC-derived nanosecond value. No epoch
// conversion to Qt or std::chrono clocks is permitted at these boundaries.
using AudioMonotonicTime = std::int64_t;
[[nodiscard]] AudioMonotonicTime audioMonotonicNow() noexcept;

enum class AudioLane : std::uint8_t { Embedded, PlaylistBgm, ItemBgm };
inline constexpr std::size_t AudioLaneCount = 3U;
inline constexpr std::size_t AudioSourceRecordCount = 6U;

struct AudioSourceKey
{
    AudioLane lane = AudioLane::Embedded;
    std::uint8_t slot = 0U;
    std::uint64_t incarnation = 0U;
    std::uint64_t playbackLifetime = 0U;
    std::uint64_t sourceToken = 0U;
    // Only Embedded uses mediaBinding. BGM tokens survive visual seek/rate.
    std::uint64_t mediaBinding = 0U;

    [[nodiscard]] bool valid() const noexcept
    {
        return static_cast<std::size_t>(lane) < AudioLaneCount
            && slot < AudioSourceRecordCount && incarnation != 0U
            && playbackLifetime != 0U
            && slot / 2U == static_cast<std::uint8_t>(lane);
    }
    bool operator==(const AudioSourceKey&) const = default;
};

enum class AudioClockMode : std::uint8_t { Running, Paused, FailureHold };

struct AudioClockAnchor
{
    std::uint64_t binding = 0U;
    std::uint64_t timingVersion = 0U;
    double position = 0.0;
    AudioMonotonicTime time = 0;
    double rate = 1.0;
    double duration = 0.0;
    AudioClockMode mode = AudioClockMode::Paused;

    [[nodiscard]] bool valid() const noexcept
    {
        return binding != 0U && timingVersion != 0U && time >= 0
            && std::isfinite(position) && position >= 0.0
            && std::isfinite(rate) && rate >= 0.5 && rate <= 2.0
            && std::isfinite(duration) && duration >= 0.0
            && position <= duration;
    }
};

[[nodiscard]] inline double resolveAudioClockPosition(
    const AudioClockAnchor& anchor, AudioMonotonicTime at) noexcept
{
    if (!anchor.valid()) return 0.0;
    const double elapsed = anchor.mode == AudioClockMode::Running && at > anchor.time
        ? static_cast<double>(at - anchor.time) / 1'000'000'000.0 : 0.0;
    return (std::clamp)(anchor.position + elapsed * anchor.rate, 0.0, anchor.duration);
}

class AudioPcmPipe;

// Borrowed *immutable* execution view. The stable source record, not this
// trivially-copyable view, owns PCM/pipe storage. C retains that record until
// A's ConsumerQuiesced, P's ProducerQuiesced, and queued-reference drainage.
struct PreparedAudioSourceView
{
    AudioSourceKey key{};
    const std::int16_t* pcm = nullptr;
    std::size_t sampleCount = 0U;
    double tempo = 1.0;
    double durationSeconds = 0.0;
    AudioPcmPipe* pipe = nullptr;

    [[nodiscard]] bool valid() const noexcept
    {
        if (!key.valid()) return false;
        if (key.lane != AudioLane::Embedded) return pipe != nullptr && pcm == nullptr;
        return pcm != nullptr && sampleCount > 0U && sampleCount % 2U == 0U
            && pipe == nullptr && std::isfinite(tempo) && tempo >= 0.5 && tempo <= 2.0
            && std::isfinite(durationSeconds) && durationSeconds > 0.0;
    }
};

}
