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
    if (!input || !isValidGain(gain) || nextHandleValue_ == 0U
        || (deadlineMaximumInputs_ != 0U && inputs_.size() >= deadlineMaximumInputs_))
    {
        return {};
    }

    const AudioInputHandle handle{nextHandleValue_};
    try
    {
        inputs_.push_back({
            handle,
            std::move(input),
            nullptr,
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

bool AudioMixer::prepareDeadlineMode(std::size_t maximumInputs,
    std::size_t maximumFrames) noexcept
{
    if (!inputs_.empty() || maximumInputs == 0U || maximumFrames == 0U
        || maximumFrames > (std::numeric_limits<std::size_t>::max)() / StereoChannelCount)
        return false;
    try
    {
        inputs_.reserve(maximumInputs);
        scratch_.resize(maximumFrames * StereoChannelCount);
    }
    catch (...) { return false; }
    deadlineMaximumInputs_ = maximumInputs;
    deadlineMaximumFrames_ = maximumFrames;
    return true;
}

AudioInputHandle AudioMixer::addBorrowedInput(AudioMixerInput& input, float gain) noexcept
{
    if (!isValidGain(gain) || nextHandleValue_ == 0U || deadlineMaximumInputs_ == 0U
        || inputs_.size() >= deadlineMaximumInputs_) return {};
    const AudioInputHandle handle{nextHandleValue_};
    // Capacity was reserved before the stream started; this move-only slot
    // contains no owning endpoint and removal has no resource destructor.
    inputs_.push_back({handle, {}, &input, gain, AudioInputState::Active, false});
    nextHandleValue_ = nextHandleValue_ == (std::numeric_limits<std::uint64_t>::max)()
        ? 0U : nextHandleValue_ + 1U;
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
    if (deadlineMaximumFrames_ != 0U && requestedFrames > deadlineMaximumFrames_)
        return {0U, AudioMixStatus::InvalidRequest};
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

        AudioMixerInput* const endpoint = input.borrowedInput != nullptr
            ? input.borrowedInput : input.input.get();
        if (endpoint == nullptr)
        {
            input.state = AudioInputState::Failed;
            input.terminalNotificationPending = true;
            continue;
        }
        const AudioMixerInputReadResult result = endpoint->readFrames(
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
        else if (result.status == AudioMixerInputReadStatus::Failure
            || result.status == AudioMixerInputReadStatus::Retired)
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
