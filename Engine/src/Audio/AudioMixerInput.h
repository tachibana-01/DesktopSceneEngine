#pragma once

#include <cstddef>
#include <span>

namespace DesktopSceneEngine::Audio
{

enum class AudioMixerInputReadStatus
{
    Data,
    TemporaryEmpty,
    EndOfStream,
    Failure,
    Retired
};

struct AudioMixerInputReadResult
{
    std::size_t framesProduced = 0U;
    AudioMixerInputReadStatus status = AudioMixerInputReadStatus::Failure;
};

class AudioMixerInput
{
public:
    virtual ~AudioMixerInput() = default;

    [[nodiscard]] virtual AudioMixerInputReadResult readFrames(
        std::span<float> interleavedDestination
    ) noexcept = 0;
};

}
