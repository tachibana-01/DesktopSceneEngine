#include "Audio/AudioMixer.h"
#include "Audio/AudioMixerInput.h"
#include "Music/SequentialAudioMixerInput.h"
#include "Resources/SequentialAudioSource.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <span>
#include <vector>

namespace
{

using DesktopSceneEngine::Audio::AudioInputHandle;
using DesktopSceneEngine::Audio::AudioInputState;
using DesktopSceneEngine::Audio::AudioMixStatus;
using DesktopSceneEngine::Audio::AudioMixer;
using DesktopSceneEngine::Audio::AudioMixerInputReadStatus;
using DesktopSceneEngine::Music::SequentialAudioMixerInput;
using DesktopSceneEngine::Resources::SequentialAudioSource;

constexpr std::size_t StereoChannelCount = 2U;
constexpr std::size_t StereoFrameCount = 1003U;
constexpr std::array<std::int16_t, 5U> LeftPattern{
    -24576, -8192, 4096, 16384, 28672
};
constexpr std::array<std::int16_t, 5U> RightPattern{
    24576, 6144, -12288, -28672, 20480
};
constexpr float Sentinel = -1234.5F;

bool require(bool condition, const char* message)
{
    if (condition)
    {
        return true;
    }

    std::cerr << "Test failed: " << message << std::endl;
    return false;
}

bool approximatelyEqual(float left, float right)
{
    return std::fabs(left - right) <= 0.000002F;
}

std::filesystem::path fixturePath()
{
    return std::filesystem::path{DESKTOP_SCENE_ENGINE_SEQUENTIAL_AUDIO_TEST_DATA_DIR}
        / "SequentialAudioSourceStereo48k.wav";
}

std::unique_ptr<SequentialAudioSource> openFixtureSource()
{
    auto source = std::make_unique<SequentialAudioSource>();
    return source->open(fixturePath()) ? std::move(source) : nullptr;
}

float expectedLeft(std::size_t frame)
{
    return static_cast<float>(LeftPattern[frame % LeftPattern.size()]) / 32768.0F;
}

float expectedRight(std::size_t frame)
{
    return static_cast<float>(RightPattern[frame % RightPattern.size()]) / 32768.0F;
}

bool matchesExpectedSamples(std::span<const float> samples)
{
    if (samples.size() != StereoFrameCount * StereoChannelCount)
    {
        return false;
    }

    for (std::size_t frame = 0U; frame < StereoFrameCount; ++frame)
    {
        if (!approximatelyEqual(samples[frame * StereoChannelCount], expectedLeft(frame))
            || !approximatelyEqual(samples[frame * StereoChannelCount + 1U], expectedRight(frame)))
        {
            return false;
        }
    }

    return true;
}

bool verifyBasicCanonicalPull()
{
    auto source = openFixtureSource();
    if (!require(source != nullptr, "The deterministic stereo WAV fixture must open"))
    {
        return false;
    }

    SequentialAudioMixerInput input{std::move(source)};
    std::vector<float> destination(4U, Sentinel);
    const auto result = input.readFrames(destination);

    return require(result.framesProduced == 2U
                && result.status == AudioMixerInputReadStatus::Data,
            "Adapter must expose canonical source frames as Data")
        && require(
            approximatelyEqual(destination[0U], -0.75F)
                && approximatelyEqual(destination[1U], 0.75F)
                && approximatelyEqual(destination[2U], -0.25F)
                && approximatelyEqual(destination[3U], 0.1875F),
            "Adapter must preserve canonical F32 stereo samples without gain");
}

bool verifyEmptyAndMalformedRequests()
{
    auto source = openFixtureSource();
    if (!require(source != nullptr, "The deterministic stereo WAV fixture must open"))
    {
        return false;
    }

    SequentialAudioMixerInput input{std::move(source)};
    const auto emptyResult = input.readFrames(std::span<float>{});
    std::vector<float> firstFrame(2U, Sentinel);
    const auto firstFrameResult = input.readFrames(firstFrame);
    if (!require(emptyResult.framesProduced == 0U
                && emptyResult.status == AudioMixerInputReadStatus::Data,
            "An empty active pull must be a neutral Data result")
        || !require(firstFrameResult.framesProduced == 1U
                && firstFrameResult.status == AudioMixerInputReadStatus::Data
                && approximatelyEqual(firstFrame[0U], expectedLeft(0U))
                && approximatelyEqual(firstFrame[1U], expectedRight(0U)),
            "An empty pull must not advance the source"))
    {
        return false;
    }

    auto malformedSource = openFixtureSource();
    if (!require(malformedSource != nullptr, "The deterministic stereo WAV fixture must open"))
    {
        return false;
    }

    SequentialAudioMixerInput malformedInput{std::move(malformedSource)};
    std::vector<float> malformedDestination(3U, Sentinel);
    const auto malformedResult = malformedInput.readFrames(malformedDestination);
    const auto repeatedFailure = malformedInput.readFrames(std::span<float>{});
    if (!require(malformedResult.framesProduced == 0U
                && malformedResult.status == AudioMixerInputReadStatus::Failure,
            "Odd sample destinations must fail")
        || !require(malformedDestination == std::vector<float>(3U, Sentinel),
            "Odd sample destinations must remain untouched")
        || !require(repeatedFailure.framesProduced == 0U
                && repeatedFailure.status == AudioMixerInputReadStatus::Failure,
            "Destination validation failure must remain durable"))
    {
        return false;
    }

    auto unopenedSource = std::make_unique<SequentialAudioSource>();
    SequentialAudioMixerInput unopenedInput{std::move(unopenedSource)};
    std::vector<float> unopenedDestination(2U, Sentinel);
    const auto firstFailure = unopenedInput.readFrames(unopenedDestination);
    const auto secondFailure = unopenedInput.readFrames(unopenedDestination);
    return require(firstFailure.framesProduced == 0U
                && firstFailure.status == AudioMixerInputReadStatus::Failure,
            "An unopened source must map to terminal Failure")
        && require(secondFailure.framesProduced == 0U
                && secondFailure.status == AudioMixerInputReadStatus::Failure,
            "An unopened source failure must remain stable")
        && require(unopenedDestination == std::vector<float>(2U, Sentinel),
            "A failed source must not write the destination");
}

std::vector<float> readAll(std::size_t blockFrames)
{
    auto source = openFixtureSource();
    if (source == nullptr || blockFrames == 0U)
    {
        return {};
    }

    SequentialAudioMixerInput input{std::move(source)};
    std::vector<float> output{};
    std::vector<float> block(blockFrames * StereoChannelCount, Sentinel);
    for (std::size_t iteration = 0U; iteration < StereoFrameCount + 2U; ++iteration)
    {
        std::fill(block.begin(), block.end(), Sentinel);
        const auto result = input.readFrames(block);
        if (result.status == AudioMixerInputReadStatus::Failure || result.framesProduced > blockFrames)
        {
            return {};
        }

        output.insert(
            output.end(),
            block.begin(),
            block.begin() + static_cast<std::ptrdiff_t>(result.framesProduced * StereoChannelCount));
        if (result.status == AudioMixerInputReadStatus::EndOfStream)
        {
            return output;
        }
    }

    return {};
}

bool verifyIrregularAndLargePullConservation()
{
    for (const std::size_t blockFrames : {1U, 2U, 17U, 511U, 2000U})
    {
        const std::vector<float> samples = readAll(blockFrames);
        if (!require(matchesExpectedSamples(samples),
                "Every block size must preserve the entire canonical stereo sample sequence"))
        {
            return false;
        }
    }

    return true;
}

bool verifyExactBoundaryAndStableEndOfStream()
{
    auto source = openFixtureSource();
    if (!require(source != nullptr, "The deterministic stereo WAV fixture must open"))
    {
        return false;
    }

    SequentialAudioMixerInput input{std::move(source)};
    std::vector<float> exactDestination(StereoFrameCount * StereoChannelCount, Sentinel);
    const auto exactResult = input.readFrames(exactDestination);
    std::vector<float> terminalDestination(2U, Sentinel);
    const auto firstEndOfStream = input.readFrames(terminalDestination);
    const auto repeatedEndOfStream = input.readFrames(terminalDestination);
    return require(exactResult.framesProduced == StereoFrameCount
                && exactResult.status == AudioMixerInputReadStatus::Data
                && matchesExpectedSamples(exactDestination),
            "An exact source boundary must return all frames as Data")
        && require(firstEndOfStream.framesProduced == 0U
                && firstEndOfStream.status == AudioMixerInputReadStatus::EndOfStream,
            "The pull after an exact source boundary must report EndOfStream")
        && require(repeatedEndOfStream.framesProduced == 0U
                && repeatedEndOfStream.status == AudioMixerInputReadStatus::EndOfStream,
            "EndOfStream must remain stable without automatic source reset")
        && require(terminalDestination == std::vector<float>(2U, Sentinel),
            "Terminal pulls must not modify caller storage");
}

bool verifyAudioMixerIntegration()
{
    auto source = openFixtureSource();
    if (!require(source != nullptr, "The deterministic stereo WAV fixture must open"))
    {
        return false;
    }

    AudioMixer mixer{};
    const AudioInputHandle handle = mixer.addInput(
        std::make_unique<SequentialAudioMixerInput>(std::move(source)));
    if (!require(static_cast<bool>(handle), "AudioMixer must accept the owned adapter"))
    {
        return false;
    }

    const std::size_t requestedFrames = StereoFrameCount + 2U;
    std::vector<float> mixed(requestedFrames * StereoChannelCount, Sentinel);
    const auto firstMix = mixer.mixFrames(mixed);
    AudioInputState terminalState = AudioInputState::Active;
    const bool firstTerminalFact = mixer.consumeTerminalState(handle, terminalState);
    const bool repeatedTerminalFact = mixer.consumeTerminalState(handle, terminalState);
    if (!require(firstMix.framesProduced == requestedFrames
                && firstMix.status == AudioMixStatus::Success,
            "AudioMixer must preserve its requested output block size")
        || !require(matchesExpectedSamples(
                std::span<const float>{mixed}.first(StereoFrameCount * StereoChannelCount)),
            "AudioMixer must receive the adapter's unchanged canonical samples")
        || !require(mixed[StereoFrameCount * StereoChannelCount] == 0.0F
                && mixed[StereoFrameCount * StereoChannelCount + 1U] == 0.0F
                && mixed[StereoFrameCount * StereoChannelCount + 2U] == 0.0F
                && mixed[StereoFrameCount * StereoChannelCount + 3U] == 0.0F,
            "AudioMixer must silence only the frames after the adapter's final PCM")
        || !require(firstTerminalFact && terminalState == AudioInputState::Ended,
            "AudioMixer must receive the adapter EndOfStream terminal fact")
        || !require(!repeatedTerminalFact,
            "AudioMixer terminal notification must remain exactly once"))
    {
        return false;
    }

    mixed.assign(4U, Sentinel);
    const auto secondMix = mixer.mixFrames(mixed);
    return require(secondMix.framesProduced == 2U && secondMix.status == AudioMixStatus::Success,
            "Mixer remains reusable after adapter EndOfStream")
        && require(mixed == std::vector<float>(4U, 0.0F),
            "Terminal adapter inputs must never be pulled again");
}

}

int main()
{
    const bool success = verifyBasicCanonicalPull()
        && verifyEmptyAndMalformedRequests()
        && verifyIrregularAndLargePullConservation()
        && verifyExactBoundaryAndStableEndOfStream()
        && verifyAudioMixerIntegration();
    return success ? 0 : 1;
}
