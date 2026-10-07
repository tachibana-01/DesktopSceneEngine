#include "Audio/AudioMixer.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace
{

using DesktopSceneEngine::Audio::AudioInputHandle;
using DesktopSceneEngine::Audio::AudioInputState;
using DesktopSceneEngine::Audio::AudioMixer;
using DesktopSceneEngine::Audio::AudioMixerInput;
using DesktopSceneEngine::Audio::AudioMixerInputReadResult;
using DesktopSceneEngine::Audio::AudioMixerInputReadStatus;
using DesktopSceneEngine::Audio::AudioMixStatus;

constexpr float Sentinel = -1234.5F;

bool require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "Test failed: " << message << std::endl;
        return false;
    }

    return true;
}

bool approximatelyEqual(float left, float right)
{
    return std::fabs(left - right) <= 0.00001F;
}

bool samplesEqual(const std::vector<float>& actual, const std::vector<float>& expected)
{
    if (actual.size() != expected.size())
    {
        return false;
    }

    for (std::size_t index = 0U; index < actual.size(); ++index)
    {
        if (!approximatelyEqual(actual[index], expected[index]))
        {
            return false;
        }
    }

    return true;
}

std::vector<float> stereoFrames(std::initializer_list<float> values)
{
    std::vector<float> samples{};
    samples.reserve(values.size() * 2U);
    for (const float value : values)
    {
        samples.push_back(value);
        samples.push_back(value);
    }

    return samples;
}

class FakeAudioMixerInput final : public AudioMixerInput
{
public:
    FakeAudioMixerInput(
        std::vector<float> samples,
        std::size_t maxFramesPerRead = (std::numeric_limits<std::size_t>::max)(),
        std::size_t terminalAfterFrames = (std::numeric_limits<std::size_t>::max)(),
        AudioMixerInputReadStatus terminalStatus = AudioMixerInputReadStatus::EndOfStream,
        bool malformedResult = false
    )
        : samples_(std::move(samples)),
          maxFramesPerRead_(maxFramesPerRead),
          terminalStatus_(terminalStatus),
          malformedResult_(malformedResult)
    {
        const std::size_t frameCount = samples_.size() / 2U;
        terminalAfterFrames_ = (std::min)(terminalAfterFrames, frameCount);
    }

    [[nodiscard]] AudioMixerInputReadResult readFrames(
        std::span<float> interleavedDestination
    ) noexcept override
    {
        ++readCallCount_;
        lastRequestedFrames_ = interleavedDestination.size() / 2U;
        if (malformedResult_)
        {
            return {lastRequestedFrames_ + 1U, AudioMixerInputReadStatus::Data};
        }

        if (cursorFrames_ >= terminalAfterFrames_)
        {
            return {0U, terminalStatus_};
        }

        const std::size_t available = terminalAfterFrames_ - cursorFrames_;
        const std::size_t framesProduced = (std::min)(
            available,
            (std::min)(lastRequestedFrames_, maxFramesPerRead_));
        const std::size_t sourceOffset = cursorFrames_ * 2U;
        const std::size_t producedSamples = framesProduced * 2U;
        std::copy_n(
            samples_.data() + sourceOffset,
            producedSamples,
            interleavedDestination.data());
        cursorFrames_ += framesProduced;

        return {
            framesProduced,
            cursorFrames_ >= terminalAfterFrames_
                ? terminalStatus_
                : AudioMixerInputReadStatus::Data
        };
    }

    [[nodiscard]] std::size_t readCallCount() const noexcept
    {
        return readCallCount_;
    }

    [[nodiscard]] std::size_t lastRequestedFrames() const noexcept
    {
        return lastRequestedFrames_;
    }

private:
    std::vector<float> samples_{};
    std::size_t maxFramesPerRead_ = 0U;
    std::size_t terminalAfterFrames_ = 0U;
    AudioMixerInputReadStatus terminalStatus_ = AudioMixerInputReadStatus::EndOfStream;
    bool malformedResult_ = false;
    std::size_t cursorFrames_ = 0U;
    std::size_t readCallCount_ = 0U;
    std::size_t lastRequestedFrames_ = 0U;
};

std::unique_ptr<FakeAudioMixerInput> makeInput(
    std::initializer_list<float> values,
    std::size_t maxFramesPerRead = (std::numeric_limits<std::size_t>::max)(),
    std::size_t terminalAfterFrames = (std::numeric_limits<std::size_t>::max)(),
    AudioMixerInputReadStatus terminalStatus = AudioMixerInputReadStatus::EndOfStream,
    bool malformedResult = false
)
{
    return std::make_unique<FakeAudioMixerInput>(
        stereoFrames(values),
        maxFramesPerRead,
        terminalAfterFrames,
        terminalStatus,
        malformedResult);
}

bool verifyRequestAndSilenceSemantics()
{
    AudioMixer mixer{};
    auto input = makeInput({0.25F, 0.5F});
    FakeAudioMixerInput* const rawInput = input.get();
    const AudioInputHandle handle = mixer.addInput(std::move(input));

    std::vector<float> empty{};
    std::vector<float> odd{Sentinel, Sentinel, Sentinel};
    std::vector<float> silence(8U, Sentinel);
    if (!require(static_cast<bool>(handle), "A valid input must receive a nonzero handle")
        || !require(
            mixer.mixFrames(std::span<float>{empty}).status == AudioMixStatus::Success,
            "An empty mix request must be a success")
        || !require(rawInput->readCallCount() == 0U,
            "An empty mix request must not pull inputs")
        || !require(
            mixer.mixFrames(std::span<float>{odd}).status == AudioMixStatus::InvalidRequest,
            "An odd sample count must be invalid")
        || !require(odd == std::vector<float>(3U, Sentinel),
            "An invalid mix request must not modify its destination")
        || !require(rawInput->readCallCount() == 0U,
            "An invalid mix request must not pull inputs"))
    {
        return false;
    }

    AudioMixer zeroInputMixer{};
    const auto zeroResult = zeroInputMixer.mixFrames(std::span<float>{silence});
    return require(
        zeroResult.status == AudioMixStatus::Success && zeroResult.framesProduced == 4U,
        "A valid zero-input mix must produce every requested frame")
        && require(samplesEqual(silence, std::vector<float>(8U, 0.0F)),
            "A zero-input mix must fully initialize destination silence");
}

bool verifyAllTerminalSilence()
{
    AudioMixer mixer{};
    const AudioInputHandle handle = mixer.addInput(makeInput({}));
    std::vector<float> destination(8U, Sentinel);
    AudioInputState terminalState = AudioInputState::Active;
    if (!require(static_cast<bool>(handle)
            && mixer.mixFrames(std::span<float>{destination}).status == AudioMixStatus::Success
            && samplesEqual(destination, std::vector<float>(8U, 0.0F)),
        "An all-terminal mix must produce complete silence")
        || !require(mixer.consumeTerminalState(handle, terminalState)
                && terminalState == AudioInputState::Ended,
            "An immediately terminal input must report Ended once"))
    {
        return false;
    }

    destination.assign(8U, Sentinel);
    return require(mixer.mixFrames(std::span<float>{destination}).status == AudioMixStatus::Success
            && samplesEqual(destination, std::vector<float>(8U, 0.0F)),
        "A mixer with retained terminal slots must remain a silent source");
}

bool verifySingleInputAndShortReadSemantics()
{
    AudioMixer mixer{};
    auto input = makeInput({0.8F, -0.4F}, 1U);
    FakeAudioMixerInput* const rawInput = input.get();
    const AudioInputHandle handle = mixer.addInput(std::move(input), 0.5F);
    std::vector<float> destination(8U, Sentinel);
    const auto result = mixer.mixFrames(std::span<float>{destination});
    AudioInputState terminalState = AudioInputState::Active;
    if (!require(
            result.status == AudioMixStatus::Success && result.framesProduced == 4U,
            "A short input must not shorten the mixed output")
        || !require(samplesEqual(destination, {
            0.4F, 0.4F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F}),
            "Only the valid short-read prefix may contribute to the mix")
        || !require(rawInput->readCallCount() == 1U && rawInput->lastRequestedFrames() == 4U,
            "Each active input must be pulled exactly once for the requested frame count")
        || !require(!mixer.consumeTerminalState(handle, terminalState),
            "A short Data read must keep its input active"))
    {
        return false;
    }

    AudioMixer mutedMixer{};
    auto mutedInput = makeInput({0.25F, 0.5F});
    FakeAudioMixerInput* const rawMutedInput = mutedInput.get();
    const AudioInputHandle mutedHandle = mutedMixer.addInput(std::move(mutedInput), 0.0F);
    std::vector<float> mutedOutput(4U, Sentinel);
    const auto mutedResult = mutedMixer.mixFrames(std::span<float>{mutedOutput});
    return require(static_cast<bool>(mutedHandle)
            && mutedResult.status == AudioMixStatus::Success
            && samplesEqual(mutedOutput, std::vector<float>(4U, 0.0F)),
        "Gain zero must silence only the contribution")
        && require(rawMutedInput->readCallCount() == 1U,
            "Gain zero must still advance the input through one pull");
}

bool verifyMultiInputGainAndOrderSemantics()
{
    AudioMixer mixer{};
    const AudioInputHandle first = mixer.addInput(makeInput({0.25F}));
    const AudioInputHandle second = mixer.addInput(makeInput({0.5F}));
    std::vector<float> destination(2U, Sentinel);
    if (!require(static_cast<bool>(first) && static_cast<bool>(second),
            "Two valid inputs must both register")
        || !require(mixer.mixFrames(std::span<float>{destination}).status == AudioMixStatus::Success,
            "Multiple inputs must mix successfully")
        || !require(samplesEqual(destination, {0.75F, 0.75F}),
            "Inputs must accumulate without normalization"))
    {
        return false;
    }

    AudioMixer unclippedMixer{};
    (void)unclippedMixer.addInput(makeInput({0.75F}));
    (void)unclippedMixer.addInput(makeInput({0.75F}));
    destination.assign(2U, Sentinel);
    if (!require(unclippedMixer.mixFrames(std::span<float>{destination}).status == AudioMixStatus::Success
            && samplesEqual(destination, {1.5F, 1.5F}),
        "The pure mixer must preserve output above one without clipping"))
    {
        return false;
    }

    AudioMixer signedMixer{};
    (void)signedMixer.addInput(makeInput({0.5F}));
    (void)signedMixer.addInput(makeInput({-0.25F}));
    destination.assign(2U, Sentinel);
    if (!require(signedMixer.mixFrames(std::span<float>{destination}).status == AudioMixStatus::Success
            && samplesEqual(destination, {0.25F, 0.25F}),
        "The mixer must preserve signed accumulation"))
    {
        return false;
    }

    AudioMixer orderedMixer{};
    (void)orderedMixer.addInput(makeInput({1.0e20F}));
    (void)orderedMixer.addInput(makeInput({-1.0e20F}));
    (void)orderedMixer.addInput(makeInput({1.0F}));
    destination.assign(2U, Sentinel);
    return require(orderedMixer.mixFrames(std::span<float>{destination}).status == AudioMixStatus::Success
            && samplesEqual(destination, {1.0F, 1.0F}),
        "Registration order must determine floating-point accumulation order");
}

bool verifyLifecycleAndHandleSemantics()
{
    AudioMixer mixer{};
    auto firstInput = makeInput({1.0F, 1.0F, 1.0F});
    FakeAudioMixerInput* const rawFirst = firstInput.get();
    const AudioInputHandle first = mixer.addInput(std::move(firstInput));
    std::vector<float> destination(2U, Sentinel);
    if (!require(static_cast<bool>(first)
            && mixer.mixFrames(std::span<float>{destination}).status == AudioMixStatus::Success
            && samplesEqual(destination, {1.0F, 1.0F}),
        "An initial registered input must contribute"))
    {
        return false;
    }

    auto secondInput = makeInput({0.5F, 0.5F, 0.5F});
    FakeAudioMixerInput* const rawSecond = secondInput.get();
    const AudioInputHandle second = mixer.addInput(std::move(secondInput));
    destination.assign(2U, Sentinel);
    if (!require(static_cast<bool>(second)
            && first != second
            && mixer.mixFrames(std::span<float>{destination}).status == AudioMixStatus::Success
            && samplesEqual(destination, {1.5F, 1.5F}),
        "An input added between calls must join the next mix")
        || !require(mixer.setInputGain(second, 0.5F), "A valid live gain update must succeed"))
    {
        return false;
    }

    destination.assign(2U, Sentinel);
    if (!require(mixer.mixFrames(std::span<float>{destination}).status == AudioMixStatus::Success
            && samplesEqual(destination, {1.25F, 1.25F}),
        "A live gain update must affect only subsequent blocks without resetting inputs"))
    {
        return false;
    }

    const std::size_t firstReadsBeforeRemoval = rawFirst->readCallCount();
    AudioInputState terminalState = AudioInputState::Active;
    if (!require(mixer.removeInput(first), "A valid input removal must succeed")
        || !require(!mixer.removeInput(first), "Removing the same handle twice must fail")
        || !require(!mixer.setInputGain(first, 1.0F), "A stale handle must reject gain changes")
        || !require(!mixer.consumeTerminalState(first, terminalState),
            "A stale handle must not expose a terminal state"))
    {
        return false;
    }

    destination.assign(2U, Sentinel);
    return require(mixer.mixFrames(std::span<float>{destination}).status == AudioMixStatus::Success
            && samplesEqual(destination, {0.25F, 0.25F}),
        "Removing one input must leave surviving inputs continuous")
        && require(rawSecond->readCallCount() == 3U,
            "The surviving input must continue without reset")
        && require(firstReadsBeforeRemoval == 3U,
            "The removed input must have no future read path");
}

bool verifyTerminalSemantics(
    AudioMixerInputReadStatus terminalStatus,
    AudioInputState expectedState
)
{
    AudioMixer mixer{};
    auto terminalInput = makeInput({0.25F, 0.25F, 0.25F}, 8U, 3U, terminalStatus);
    FakeAudioMixerInput* const rawTerminalInput = terminalInput.get();
    const AudioInputHandle terminalHandle = mixer.addInput(std::move(terminalInput));
    auto peerInput = makeInput({0.5F, 0.5F, 0.5F, 0.5F, 0.5F, 0.5F, 0.5F, 0.5F,
        0.5F, 0.5F, 0.5F, 0.5F, 0.5F, 0.5F, 0.5F, 0.5F});
    FakeAudioMixerInput* const rawPeerInput = peerInput.get();
    (void)mixer.addInput(std::move(peerInput));

    std::vector<float> destination(16U, Sentinel);
    if (!require(static_cast<bool>(terminalHandle)
            && mixer.mixFrames(std::span<float>{destination}).status == AudioMixStatus::Success,
        "A terminal input must not fail the complete mix"))
    {
        return false;
    }

    for (std::size_t frame = 0U; frame < 8U; ++frame)
    {
        const float expected = frame < 3U ? 0.75F : 0.5F;
        if (!require(approximatelyEqual(destination[frame * 2U], expected)
                && approximatelyEqual(destination[frame * 2U + 1U], expected),
            "Terminal input must preserve only its valid prefix while peer continues"))
        {
            return false;
        }
    }

    AudioInputState observedState = AudioInputState::Active;
    if (!require(mixer.consumeTerminalState(terminalHandle, observedState)
            && observedState == expectedState,
        "A terminal transition must be consumable once with the correct state")
        || !require(!mixer.consumeTerminalState(terminalHandle, observedState),
            "A terminal transition must not be reported twice"))
    {
        return false;
    }

    destination.assign(16U, Sentinel);
    return require(mixer.mixFrames(std::span<float>{destination}).status == AudioMixStatus::Success
            && samplesEqual(destination, std::vector<float>(16U, 0.5F)),
        "Terminal input must remain silent while its peer continues")
        && require(rawTerminalInput->readCallCount() == 1U,
            "A terminal input must never be pulled again")
        && require(rawPeerInput->readCallCount() == 2U,
            "A healthy peer must continue after another input terminates");
}

bool verifyZeroFrameTerminalAndMalformedInput()
{
    AudioMixer mixer{};
    auto eofInput = makeInput({}, 8U, 0U, AudioMixerInputReadStatus::EndOfStream);
    FakeAudioMixerInput* const rawEofInput = eofInput.get();
    const AudioInputHandle eofHandle = mixer.addInput(std::move(eofInput));
    (void)mixer.addInput(makeInput({0.5F, 0.5F}));
    std::vector<float> destination(4U, Sentinel);
    AudioInputState terminalState = AudioInputState::Active;
    if (!require(mixer.mixFrames(std::span<float>{destination}).status == AudioMixStatus::Success
            && samplesEqual(destination, {0.5F, 0.5F, 0.5F, 0.5F}),
        "A zero-frame EOF must leave peer audio intact")
        || !require(mixer.consumeTerminalState(eofHandle, terminalState)
                && terminalState == AudioInputState::Ended,
            "A zero-frame EOF must be reported as Ended"))
    {
        return false;
    }

    AudioMixer failedMixer{};
    const AudioInputHandle failedHandle = failedMixer.addInput(makeInput(
        {},
        8U,
        0U,
        AudioMixerInputReadStatus::Failure));
    destination.assign(4U, Sentinel);
    terminalState = AudioInputState::Active;
    if (!require(failedMixer.mixFrames(std::span<float>{destination}).status
                == AudioMixStatus::Success
            && samplesEqual(destination, std::vector<float>(4U, 0.0F)),
        "A zero-frame Failure must leave a valid silent output")
        || !require(failedMixer.consumeTerminalState(failedHandle, terminalState)
                && terminalState == AudioInputState::Failed,
            "A zero-frame Failure must report Failed once"))
    {
        return false;
    }

    AudioMixer malformedMixer{};
    auto malformedInput = makeInput({0.25F}, 8U, 1U,
        AudioMixerInputReadStatus::EndOfStream, true);
    FakeAudioMixerInput* const rawMalformedInput = malformedInput.get();
    const AudioInputHandle malformedHandle = malformedMixer.addInput(std::move(malformedInput));
    (void)malformedMixer.addInput(makeInput({0.5F, 0.5F}));
    destination.assign(4U, Sentinel);
    terminalState = AudioInputState::Active;
    if (!require(malformedMixer.mixFrames(std::span<float>{destination}).status == AudioMixStatus::Success
            && samplesEqual(destination, {0.5F, 0.5F, 0.5F, 0.5F}),
        "A malformed input result must be isolated without ambiguous mixing")
        || !require(malformedMixer.consumeTerminalState(malformedHandle, terminalState)
                && terminalState == AudioInputState::Failed,
            "A malformed input result must become a terminal failure"))
    {
        return false;
    }

    destination.assign(4U, Sentinel);
    return require(malformedMixer.mixFrames(std::span<float>{destination}).status
                == AudioMixStatus::Success
            && rawMalformedInput->readCallCount() == 1U
            && rawEofInput->readCallCount() == 1U,
        "Zero-frame and malformed terminal inputs must never be read again");
}

bool verifyGainAndHandleRejection()
{
    AudioMixer mixer{};
    if (!require(!mixer.addInput(nullptr), "A null input must be rejected")
        || !require(!mixer.addInput(makeInput({0.5F}), -0.1F),
            "A negative registration gain must be rejected")
        || !require(!mixer.addInput(makeInput({0.5F}), -std::numeric_limits<float>::infinity()),
            "A negative-infinite registration gain must be rejected")
        || !require(!mixer.addInput(makeInput({0.5F}), std::numeric_limits<float>::quiet_NaN()),
            "A non-finite registration gain must be rejected"))
    {
        return false;
    }

    const AudioInputHandle handle = mixer.addInput(makeInput({0.8F, 0.8F}));
    std::vector<float> destination(2U, Sentinel);
    if (!require(static_cast<bool>(handle)
            && !mixer.setInputGain(handle, -1.0F)
            && !mixer.setInputGain(handle, -std::numeric_limits<float>::infinity())
            && !mixer.setInputGain(handle, std::numeric_limits<float>::infinity())
            && !mixer.setInputGain(handle, std::numeric_limits<float>::quiet_NaN())
            && mixer.mixFrames(std::span<float>{destination}).status == AudioMixStatus::Success
            && samplesEqual(destination, {0.8F, 0.8F}),
        "Invalid gain updates must preserve the old gain"))
    {
        return false;
    }

    if (!require(mixer.removeInput(handle)
            && !mixer.setInputGain(AudioInputHandle{}, 1.0F),
        "Zero and removed handles must remain invalid"))
    {
        return false;
    }

    const AudioInputHandle replacement = mixer.addInput(makeInput({0.4F}));
    return require(static_cast<bool>(replacement) && replacement != handle,
        "A removed handle must never be reused during a mixer lifetime");
}

}

int main()
{
    const bool success = verifyRequestAndSilenceSemantics()
        && verifyAllTerminalSilence()
        && verifySingleInputAndShortReadSemantics()
        && verifyMultiInputGainAndOrderSemantics()
        && verifyLifecycleAndHandleSemantics()
        && verifyTerminalSemantics(
            AudioMixerInputReadStatus::EndOfStream,
            AudioInputState::Ended)
        && verifyTerminalSemantics(
            AudioMixerInputReadStatus::Failure,
            AudioInputState::Failed)
        && verifyZeroFrameTerminalAndMalformedInput()
        && verifyGainAndHandleRejection();
    return success ? 0 : 1;
}
