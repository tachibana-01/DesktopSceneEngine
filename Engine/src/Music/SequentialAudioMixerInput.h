#pragma once

#include "Audio/AudioMixerInput.h"

#include <memory>
#include <span>

namespace DesktopSceneEngine::Resources
{

class SequentialAudioSource;

}

namespace DesktopSceneEngine::Music
{

class SequentialAudioMixerInput final : public Audio::AudioMixerInput
{
public:
    explicit SequentialAudioMixerInput(
        std::unique_ptr<Resources::SequentialAudioSource> source
    ) noexcept;
    ~SequentialAudioMixerInput() override;

    SequentialAudioMixerInput(const SequentialAudioMixerInput&) = delete;
    SequentialAudioMixerInput& operator=(const SequentialAudioMixerInput&) = delete;
    SequentialAudioMixerInput(SequentialAudioMixerInput&&) = delete;
    SequentialAudioMixerInput& operator=(SequentialAudioMixerInput&&) = delete;

    [[nodiscard]] Audio::AudioMixerInputReadResult readFrames(
        std::span<float> interleavedDestination
    ) noexcept override;

private:
    enum class State
    {
        Active,
        Ended,
        Failed
    };

    std::unique_ptr<Resources::SequentialAudioSource> source_{};
    State state_ = State::Failed;
};

}
