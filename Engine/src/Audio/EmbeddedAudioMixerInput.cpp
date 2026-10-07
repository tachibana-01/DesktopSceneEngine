#include "EmbeddedAudioMixerInput.h"

#include "AudioSampleConversion.h"

#include <algorithm>

namespace DesktopSceneEngine::Audio
{

namespace
{

constexpr std::size_t StereoChannelCount = 2U;

}

EmbeddedAudioMixerInput::EmbeddedAudioMixerInput(
    std::span<const std::int16_t> interleavedPcm,
    std::size_t startFrame
) noexcept
    : source_(interleavedPcm)
{
    if (source_.size() % StereoChannelCount != 0U)
    {
        state_ = State::Failed;
        return;
    }

    totalFrames_ = source_.size() / StereoChannelCount;
    cursorFrames_ = (std::min)(startFrame, totalFrames_);
    state_ = cursorFrames_ == totalFrames_ ? State::Ended : State::Active;
}

AudioMixerInputReadResult EmbeddedAudioMixerInput::readFrames(
    std::span<float> interleavedDestination
) noexcept
{
    if (state_ == State::Failed)
    {
        return {0U, AudioMixerInputReadStatus::Failure};
    }

    if (state_ == State::Ended)
    {
        return {0U, AudioMixerInputReadStatus::EndOfStream};
    }

    if (interleavedDestination.empty())
    {
        return {0U, AudioMixerInputReadStatus::Data};
    }

    if (interleavedDestination.size() % StereoChannelCount != 0U
        || cursorFrames_ > totalFrames_)
    {
        state_ = State::Failed;
        return {0U, AudioMixerInputReadStatus::Failure};
    }

    if (cursorFrames_ == totalFrames_)
    {
        state_ = State::Ended;
        return {0U, AudioMixerInputReadStatus::EndOfStream};
    }

    const std::size_t requestedFrames = interleavedDestination.size()
        / StereoChannelCount;
    const std::size_t remainingFrames = totalFrames_ - cursorFrames_;
    const std::size_t framesToRead = (std::min)(requestedFrames, remainingFrames);
    const std::size_t sourceSampleOffset = cursorFrames_ * StereoChannelCount;
    const std::size_t samplesToRead = framesToRead * StereoChannelCount;
    if (!convertS16SamplesToF32(
            source_.subspan(sourceSampleOffset, samplesToRead),
            interleavedDestination.first(samplesToRead)))
    {
        state_ = State::Failed;
        return {0U, AudioMixerInputReadStatus::Failure};
    }

    cursorFrames_ += framesToRead;
    if (framesToRead < requestedFrames)
    {
        state_ = State::Ended;
        return {framesToRead, AudioMixerInputReadStatus::EndOfStream};
    }

    return {framesToRead, AudioMixerInputReadStatus::Data};
}

}
