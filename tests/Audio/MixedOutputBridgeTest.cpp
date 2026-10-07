#include "Audio/AudioBlockSink.h"
#include "Audio/AudioMixer.h"
#include "Audio/MixedOutputBridge.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace
{

using DesktopSceneEngine::Audio::AudioBlockAvailability;
using DesktopSceneEngine::Audio::AudioBlockSink;
using DesktopSceneEngine::Audio::AudioBlockStatus;
using DesktopSceneEngine::Audio::AudioMixer;
using DesktopSceneEngine::Audio::AudioMixerInput;
using DesktopSceneEngine::Audio::AudioMixerInputReadResult;
using DesktopSceneEngine::Audio::AudioMixerInputReadStatus;
using DesktopSceneEngine::Audio::MixedOutputBridge;
using DesktopSceneEngine::Audio::MixedOutputBridgeResult;
using DesktopSceneEngine::Audio::MixedOutputBridgeStatus;
using DesktopSceneEngine::Audio::MixedOutputBridgeTestAccess;

bool require(bool condition, const char* message)
{
    if (condition)
    {
        return true;
    }

    std::cerr << "Test failed: " << message << std::endl;
    return false;
}

class ConstantInput final : public AudioMixerInput
{
public:
    explicit ConstantInput(float value)
        : value_(value)
    {
    }

    [[nodiscard]] AudioMixerInputReadResult readFrames(
        std::span<float> interleavedDestination
    ) noexcept override
    {
        ++readCalls_;
        lastRequestedFrames_ = interleavedDestination.size() / 2U;
        std::fill(interleavedDestination.begin(), interleavedDestination.end(), value_);
        return {lastRequestedFrames_, AudioMixerInputReadStatus::Data};
    }

    [[nodiscard]] std::size_t readCalls() const noexcept
    {
        return readCalls_;
    }

    [[nodiscard]] std::size_t lastRequestedFrames() const noexcept
    {
        return lastRequestedFrames_;
    }

private:
    float value_ = 0.0F;
    std::size_t readCalls_ = 0U;
    std::size_t lastRequestedFrames_ = 0U;
};

class SequentialInput final : public AudioMixerInput
{
public:
    [[nodiscard]] AudioMixerInputReadResult readFrames(
        std::span<float> interleavedDestination
    ) noexcept override
    {
        const std::size_t requestedFrames = interleavedDestination.size() / 2U;
        requestStartFrames_.push_back(nextFrame_);
        ++readCalls_;

        for (std::size_t frame = 0U; frame < requestedFrames; ++frame)
        {
            const float sample = static_cast<float>(nextFrame_ + frame) / 16.0F;
            interleavedDestination[frame * 2U] = sample;
            interleavedDestination[frame * 2U + 1U] = sample;
        }

        nextFrame_ += requestedFrames;
        return {requestedFrames, AudioMixerInputReadStatus::Data};
    }

    [[nodiscard]] std::size_t readCalls() const noexcept
    {
        return readCalls_;
    }

    [[nodiscard]] std::size_t consumedFrames() const noexcept
    {
        return nextFrame_;
    }

    [[nodiscard]] const std::vector<std::size_t>& requestStartFrames() const noexcept
    {
        return requestStartFrames_;
    }

private:
    std::size_t nextFrame_ = 0U;
    std::size_t readCalls_ = 0U;
    std::vector<std::size_t> requestStartFrames_{};
};

class FakeAudioBlockSink final : public AudioBlockSink
{
public:
    AudioBlockAvailability availability{};
    AudioBlockStatus writeStatus = AudioBlockStatus::Success;
    std::vector<std::int16_t> capturedSamples{};
    std::size_t writableQueryCalls = 0U;
    std::size_t writeCalls = 0U;

    [[nodiscard]] AudioBlockAvailability writableFrames() noexcept override
    {
        ++writableQueryCalls;
        return availability;
    }

    [[nodiscard]] AudioBlockStatus writeFrames(
        std::span<const std::int16_t> interleavedSamples
    ) noexcept override
    {
        ++writeCalls;
        capturedSamples.assign(interleavedSamples.begin(), interleavedSamples.end());
        return writeStatus;
    }
};

bool verifyInactiveAndZeroWritableDoNotPull()
{
    AudioMixer mixer{};
    auto input = std::make_unique<ConstantInput>(0.5F);
    ConstantInput* const rawInput = input.get();
    (void)mixer.addInput(std::move(input));
    FakeAudioBlockSink sink{};
    MixedOutputBridge bridge{mixer, sink};

    sink.availability = {4U, AudioBlockStatus::Paused};
    if (!require(bridge.pump().status == MixedOutputBridgeStatus::OutputInactive,
            "Paused output must be inactive before Mixer pull")
        || !require(rawInput->readCalls() == 0U && sink.writeCalls == 0U,
            "Paused output must not advance input or write the sink"))
    {
        return false;
    }

    sink.availability = {4U, AudioBlockStatus::WrongMode};
    if (!require(bridge.pump().status == MixedOutputBridgeStatus::OutputInactive,
            "Wrong output mode must be inactive before Mixer pull")
        || !require(rawInput->readCalls() == 0U && sink.writeCalls == 0U,
            "Wrong output mode must not advance input or write the sink"))
    {
        return false;
    }

    sink.availability = {4U, AudioBlockStatus::NotInitialized};
    if (!require(bridge.pump().status == MixedOutputBridgeStatus::OutputInactive,
            "Uninitialized output must be inactive before Mixer pull")
        || !require(rawInput->readCalls() == 0U && sink.writeCalls == 0U,
            "Uninitialized output must not advance input or write the sink"))
    {
        return false;
    }

    sink.availability = {4U, AudioBlockStatus::DeviceFailure};
    if (!require(bridge.pump().status == MixedOutputBridgeStatus::DeviceFailure,
            "Availability device failures must surface without Mixer pull")
        || !require(rawInput->readCalls() == 0U
                && rawInput->lastRequestedFrames() == 0U
                && sink.writeCalls == 0U,
            "Availability device failures must not advance input or write the sink"))
    {
        return false;
    }

    sink.availability = {0U, AudioBlockStatus::Success};
    if (!require(bridge.pump().status == MixedOutputBridgeStatus::NoWritableFrames,
            "Zero writable capacity must be a no-op")
        || !require(rawInput->readCalls() == 0U && sink.writeCalls == 0U,
            "Zero writable capacity must not advance input or write the sink"))
    {
        return false;
    }

    sink.availability = {4U, AudioBlockStatus::InvalidRequest};
    return require(bridge.pump().status == MixedOutputBridgeStatus::OutputContractFailure,
            "Invalid availability responses must surface as output contract failures")
        && require(rawInput->readCalls() == 0U && sink.writeCalls == 0U,
            "Invalid availability responses must not advance input or write the sink");
}

bool verifyMixConversionAndWrite()
{
    AudioMixer mixer{};
    auto input = std::make_unique<ConstantInput>(0.5F);
    ConstantInput* const rawInput = input.get();
    (void)mixer.addInput(std::move(input));
    FakeAudioBlockSink sink{};
    sink.availability = {2U, AudioBlockStatus::Success};
    MixedOutputBridge bridge{mixer, sink};

    const MixedOutputBridgeResult result = bridge.pump();
    return require(result.status == MixedOutputBridgeStatus::WroteFrames
                && result.frames == 2U,
            "A writable block must report exact written frames")
        && require(rawInput->readCalls() == 1U && rawInput->lastRequestedFrames() == 2U,
            "The bridge must pull the Mixer exactly once for writable frames")
        && require(sink.writeCalls == 1U
                && sink.capturedSamples == std::vector<std::int16_t>{16384, 16384, 16384, 16384},
            "The bridge must convert and write the exact interleaved block");
}

bool verifySaturationAndOutputFailure()
{
    AudioMixer positiveMixer{};
    (void)positiveMixer.addInput(std::make_unique<ConstantInput>(1.5F));
    FakeAudioBlockSink positiveSink{};
    positiveSink.availability = {1U, AudioBlockStatus::Success};
    MixedOutputBridge positiveBridge{positiveMixer, positiveSink};
    if (!require(positiveBridge.pump().status == MixedOutputBridgeStatus::WroteFrames,
            "A positive saturated block should write successfully")
        || !require(positiveSink.capturedSamples
                == std::vector<std::int16_t>{32767, 32767},
            "The bridge must saturate positive mixed samples at S16 maximum"))
    {
        return false;
    }

    AudioMixer negativeMixer{};
    (void)negativeMixer.addInput(std::make_unique<ConstantInput>(-2.0F));
    FakeAudioBlockSink negativeSink{};
    negativeSink.availability = {1U, AudioBlockStatus::Success};
    MixedOutputBridge negativeBridge{negativeMixer, negativeSink};
    if (!require(negativeBridge.pump().status == MixedOutputBridgeStatus::WroteFrames,
            "A negative saturated block should write successfully")
        || !require(negativeSink.capturedSamples
                == std::vector<std::int16_t>{-32768, -32768},
            "The bridge must saturate negative mixed samples at S16 minimum"))
    {
        return false;
    }

    AudioMixer mixer{};
    (void)mixer.addInput(std::make_unique<ConstantInput>(1.5F));
    (void)mixer.addInput(std::make_unique<ConstantInput>(-2.0F));
    FakeAudioBlockSink sink{};
    sink.availability = {1U, AudioBlockStatus::Success};
    MixedOutputBridge bridge{mixer, sink};
    if (!require(bridge.pump().status == MixedOutputBridgeStatus::WroteFrames,
            "Signed mixed blocks should write successfully")
        || !require(sink.capturedSamples == std::vector<std::int16_t>{-16384, -16384},
            "The bridge must preserve signed mixed arithmetic before final conversion"))
    {
        return false;
    }

    sink.writeStatus = AudioBlockStatus::DeviceFailure;
    if (!require(bridge.pump().status == MixedOutputBridgeStatus::DeviceFailure,
            "Device write failures after a Mixer pull must surface as device failures"))
    {
        return false;
    }

    sink.writeStatus = AudioBlockStatus::NotWritable;
    if (!require(bridge.pump().status == MixedOutputBridgeStatus::OutputContractFailure,
            "Post-pull capacity rejection must be an output contract failure"))
    {
        return false;
    }

    sink.writeStatus = AudioBlockStatus::Paused;
    return require(bridge.pump().status == MixedOutputBridgeStatus::OutputContractFailure,
        "Post-pull state changes must be output contract failures");
}

bool verifyPostPullDeviceFailureLosesOnlyCurrentWritableBlock()
{
    AudioMixer mixer{};
    auto input = std::make_unique<SequentialInput>();
    SequentialInput* const rawInput = input.get();
    (void)mixer.addInput(std::move(input));

    FakeAudioBlockSink sink{};
    // V1 discards only this current writable block after a post-pull device
    // failure. Its time bound is 1000 * writableFrames / 48000 ms, not a
    // fixed 100 ms guarantee.
    sink.availability = {3U, AudioBlockStatus::Success};
    sink.writeStatus = AudioBlockStatus::DeviceFailure;
    MixedOutputBridge bridge{mixer, sink};

    if (!require(bridge.pump().status == MixedOutputBridgeStatus::DeviceFailure,
            "A post-pull write failure must remain a device failure")
        || !require(rawInput->readCalls() == 1U
                && rawInput->consumedFrames() == 3U
                && rawInput->requestStartFrames()
                    == std::vector<std::size_t>{0U},
            "A failing write may consume exactly its one current writable block")
        || !require(sink.writeCalls == 1U,
            "A failing write must not issue a hidden retry"))
    {
        return false;
    }

    sink.writeStatus = AudioBlockStatus::Success;
    if (!require(bridge.pump().status == MixedOutputBridgeStatus::WroteFrames,
            "The next writable block must remain usable after a failed write")
        || !require(rawInput->readCalls() == 2U
                && rawInput->consumedFrames() == 6U
                && rawInput->requestStartFrames()
                    == std::vector<std::size_t>{0U, 3U},
            "The next successful block must begin after the discarded block")
        || !require(sink.writeCalls == 2U,
            "The bridge must not retain and replay the failed mixed block"))
    {
        return false;
    }

    return true;
}

bool verifySilenceAndReusableBuffers()
{
    AudioMixer silentMixer{};
    FakeAudioBlockSink silentSink{};
    silentSink.availability = {2U, AudioBlockStatus::Success};
    MixedOutputBridge silentBridge{silentMixer, silentSink};
    if (!require(silentBridge.pump().status == MixedOutputBridgeStatus::WroteFrames,
            "A zero-input Mixer must still write a block")
        || !require(silentSink.capturedSamples == std::vector<std::int16_t>{0, 0, 0, 0},
            "A zero-input Mixer must write S16 silence"))
    {
        return false;
    }

    AudioMixer mixer{};
    auto input = std::make_unique<ConstantInput>(1.0F);
    ConstantInput* const rawInput = input.get();
    (void)mixer.addInput(std::move(input));
    FakeAudioBlockSink sink{};
    MixedOutputBridge bridge{mixer, sink};
    sink.availability = {1U, AudioBlockStatus::Success};
    if (!require(bridge.pump().status == MixedOutputBridgeStatus::WroteFrames,
            "The first bridge request must write"))
    {
        return false;
    }

    sink.availability = {3U, AudioBlockStatus::Success};
    return require(bridge.pump().status == MixedOutputBridgeStatus::WroteFrames,
            "A larger later request must safely reuse or grow bridge buffers")
        && require(rawInput->readCalls() == 2U && rawInput->lastRequestedFrames() == 3U,
            "The larger request must have one exact additional Mixer pull")
        && require(sink.capturedSamples.size() == 6U,
            "The larger request must write exactly its requested samples");
}

bool verifyMixerFailureFallbacks()
{
    AudioMixer mixer{};
    auto input = std::make_unique<ConstantInput>(0.75F);
    ConstantInput* const rawInput = input.get();
    (void)mixer.addInput(std::move(input));
    FakeAudioBlockSink sink{};
    MixedOutputBridge bridge{mixer, sink};

    const MixedOutputBridgeResult resourceFailure =
        MixedOutputBridgeTestAccess::submitMixerResult(
            bridge,
            {0U, DesktopSceneEngine::Audio::AudioMixStatus::ResourceFailure},
            2U);
    if (!require(resourceFailure.status
                == MixedOutputBridgeStatus::MixerResourceFailureSilenced,
            "Mixer resource failure must write a silence block")
        || !require(sink.capturedSamples == std::vector<std::int16_t>{0, 0, 0, 0},
            "Mixer resource failure fallback must clear every device sample")
        || !require(rawInput->readCalls() == 0U,
            "The A3a-local resource-failure seam must not advance Mixer inputs"))
    {
        return false;
    }

    const MixedOutputBridgeResult contractFailure =
        MixedOutputBridgeTestAccess::submitMixerResult(
            bridge,
            {1U, DesktopSceneEngine::Audio::AudioMixStatus::Success},
            2U);
    return require(contractFailure.status
                == MixedOutputBridgeStatus::MixerContractFailureSilenced,
        "A malformed Mixer result must write silence as a contract failure")
        && require(sink.capturedSamples == std::vector<std::int16_t>{0, 0, 0, 0},
            "Mixer contract failure fallback must clear every device sample");
}

}

int main()
{
    const bool success = verifyInactiveAndZeroWritableDoNotPull()
        && verifyMixConversionAndWrite()
        && verifySaturationAndOutputFailure()
        && verifyPostPullDeviceFailureLosesOnlyCurrentWritableBlock()
        && verifySilenceAndReusableBuffers()
        && verifyMixerFailureFallbacks();
    return success ? 0 : 1;
}
