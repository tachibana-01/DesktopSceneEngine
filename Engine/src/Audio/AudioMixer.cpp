#include "AudioMixer.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace DesktopSceneEngine::Audio
{

namespace
{

constexpr std::size_t StereoChannelCount = 2U;

}

AudioInputHandle AudioMixer::addInput(
    std::unique_ptr<AudioMixerInput> input,
    float gain
) noexcept
{
    if (!input || !isValidGain(gain) || nextHandleValue_ == 0U)
    {
        return {};
    }

    const AudioInputHandle handle{nextHandleValue_};
    try
    {
        inputs_.push_back({
            handle,
            std::move(input),
            gain,
            AudioInputState::Active,
            false
        });
    }
    catch (...)
    {
        return {};
    }

    if (nextHandleValue_ == (std::numeric_limits<std::uint64_t>::max)())
    {
        nextHandleValue_ = 0U;
    }
    else
    {
        ++nextHandleValue_;
    }

    return handle;
}

bool AudioMixer::removeInput(AudioInputHandle handle) noexcept
{
    if (!handle)
    {
        return false;
    }

    const auto iterator = std::find_if(
        inputs_.begin(),
        inputs_.end(),
        [handle](const InputSlot& input)
        {
            return input.handle == handle;
        });
    if (iterator == inputs_.end())
    {
        return false;
    }

    inputs_.erase(iterator);
    return true;
}

bool AudioMixer::setInputGain(AudioInputHandle handle, float gain) noexcept
{
    InputSlot* const input = findInput(handle);
    if (input == nullptr || !isValidGain(gain))
    {
        return false;
    }

    input->gain = gain;
    return true;
}

AudioMixResult AudioMixer::mixFrames(
    std::span<float> interleavedDestination
) noexcept
{
    if (interleavedDestination.empty())
    {
        return {0U, AudioMixStatus::Success};
    }
    if (interleavedDestination.size() % StereoChannelCount != 0U)
    {
        return {0U, AudioMixStatus::InvalidRequest};
    }

    const std::size_t requestedFrames = interleavedDestination.size() / StereoChannelCount;
    try
    {
        scratch_.resize(interleavedDestination.size());
    }
    catch (...)
    {
        return {0U, AudioMixStatus::ResourceFailure};
    }

    std::fill(interleavedDestination.begin(), interleavedDestination.end(), 0.0F);
    for (InputSlot& input : inputs_)
    {
        if (input.state != AudioInputState::Active)
        {
            continue;
        }

        const AudioMixerInputReadResult result = input.input->readFrames(
            std::span<float>{scratch_});
        if (result.framesProduced > requestedFrames)
        {
            input.state = AudioInputState::Failed;
            input.terminalNotificationPending = true;
            continue;
        }

        const std::size_t producedSamples = result.framesProduced * StereoChannelCount;
        for (std::size_t sample = 0U; sample < producedSamples; ++sample)
        {
            interleavedDestination[sample] += scratch_[sample] * input.gain;
        }

        if (result.status == AudioMixerInputReadStatus::EndOfStream)
        {
            input.state = AudioInputState::Ended;
            input.terminalNotificationPending = true;
        }
        else if (result.status == AudioMixerInputReadStatus::Failure)
        {
            input.state = AudioInputState::Failed;
            input.terminalNotificationPending = true;
        }
    }

    return {requestedFrames, AudioMixStatus::Success};
}

bool AudioMixer::consumeTerminalState(
    AudioInputHandle handle,
    AudioInputState& terminalState
) noexcept
{
    InputSlot* const input = findInput(handle);
    if (input == nullptr || !input->terminalNotificationPending)
    {
        return false;
    }

    terminalState = input->state;
    input->terminalNotificationPending = false;
    return true;
}

AudioMixer::InputSlot* AudioMixer::findInput(AudioInputHandle handle) noexcept
{
    if (!handle)
    {
        return nullptr;
    }

    const auto iterator = std::find_if(
        inputs_.begin(),
        inputs_.end(),
        [handle](const InputSlot& input)
        {
            return input.handle == handle;
        });
    return iterator != inputs_.end() ? &*iterator : nullptr;
}

bool AudioMixer::isValidGain(float gain) noexcept
{
    return std::isfinite(gain) && gain >= 0.0F;
}

}
