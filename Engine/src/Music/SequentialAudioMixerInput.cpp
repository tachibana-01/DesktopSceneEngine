#include "SequentialAudioMixerInput.h"

#include "Resources/SequentialAudioSource.h"

#include <utility>

namespace DesktopSceneEngine::Music
{

namespace
{

constexpr std::size_t StereoChannelCount = 2U;

}

SequentialAudioMixerInput::SequentialAudioMixerInput(
    std::unique_ptr<Resources::SequentialAudioSource> source
) noexcept
    : source_(std::move(source))
{
    if (source_ != nullptr && source_->isOpen()
        && source_->format().sampleRate == 48000
        && source_->format().channels == static_cast<int>(StereoChannelCount))
    {
        state_ = State::Active;
    }
}

SequentialAudioMixerInput::~SequentialAudioMixerInput() = default;

Audio::AudioMixerInputReadResult SequentialAudioMixerInput::readFrames(
    std::span<float> interleavedDestination
) noexcept
{
    if (state_ == State::Failed)
    {
        return {0U, Audio::AudioMixerInputReadStatus::Failure};
    }

    if (state_ == State::Ended)
    {
        return {0U, Audio::AudioMixerInputReadStatus::EndOfStream};
    }

    if (interleavedDestination.empty())
    {
        return {0U, Audio::AudioMixerInputReadStatus::Data};
    }

    if (interleavedDestination.size() % StereoChannelCount != 0U || source_ == nullptr)
    {
        state_ = State::Failed;
        return {0U, Audio::AudioMixerInputReadStatus::Failure};
    }

    const Resources::AudioReadResult result = source_->readFrames(interleavedDestination);
    switch (result.status)
    {
    case Resources::AudioReadStatus::Data:
        return {result.framesProduced, Audio::AudioMixerInputReadStatus::Data};

    case Resources::AudioReadStatus::EndOfStream:
        state_ = State::Ended;
        return {result.framesProduced, Audio::AudioMixerInputReadStatus::EndOfStream};

    case Resources::AudioReadStatus::NotReady:
    case Resources::AudioReadStatus::InvalidRequest:
    case Resources::AudioReadStatus::Failure:
        state_ = State::Failed;
        return {result.framesProduced, Audio::AudioMixerInputReadStatus::Failure};
    }

    state_ = State::Failed;
    return {0U, Audio::AudioMixerInputReadStatus::Failure};
}

}
