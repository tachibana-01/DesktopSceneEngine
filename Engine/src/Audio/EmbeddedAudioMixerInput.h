#pragma once

#include "AudioMixerInput.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace DesktopSceneEngine::Audio
{

// Borrows canonical S16 stereo PCM. The source must remain valid and unchanged
// for this adapter's lifetime. For a speed/tempo change, its owner must first
// remove the old Mixer input and destroy this adapter, then call
// AudioResource::setTempo(), obtain the new tempo PCM span, resolve the start
// frame, and create/register a replacement adapter. For resource replacement
// or destruction, the owner must likewise remove and destroy this adapter
// before replacing or destroying the AudioResource.
class EmbeddedAudioMixerInput final : public AudioMixerInput
{
public:
    EmbeddedAudioMixerInput(
        std::span<const std::int16_t> interleavedPcm,
        std::size_t startFrame
    ) noexcept;

    EmbeddedAudioMixerInput(const EmbeddedAudioMixerInput&) = delete;
    EmbeddedAudioMixerInput& operator=(const EmbeddedAudioMixerInput&) = delete;
    EmbeddedAudioMixerInput(EmbeddedAudioMixerInput&&) = delete;
    EmbeddedAudioMixerInput& operator=(EmbeddedAudioMixerInput&&) = delete;

    [[nodiscard]] AudioMixerInputReadResult readFrames(
        std::span<float> interleavedDestination
    ) noexcept override;

private:
    enum class State
    {
        Active,
        Ended,
        Failed
    };

    std::span<const std::int16_t> source_{};
    std::size_t cursorFrames_ = 0U;
    std::size_t totalFrames_ = 0U;
    State state_ = State::Ended;
};

}
