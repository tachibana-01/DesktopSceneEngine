#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace DesktopSceneEngine::Audio
{

enum class AudioBlockStatus
{
    Success,
    Paused,
    WrongMode,
    NotInitialized,
    InvalidRequest,
    NotWritable,
    DeviceFailure
};

struct AudioBlockAvailability
{
    std::size_t writableFrames = 0U;
    AudioBlockStatus status = AudioBlockStatus::NotInitialized;
};

class AudioBlockSink
{
public:
    virtual ~AudioBlockSink() = default;

    [[nodiscard]] virtual AudioBlockAvailability writableFrames() noexcept = 0;

    [[nodiscard]] virtual AudioBlockStatus writeFrames(
        std::span<const std::int16_t> interleavedSamples
    ) noexcept = 0;
};

}
