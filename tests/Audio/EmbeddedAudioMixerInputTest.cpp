#include "Audio/AudioMixer.h"
#include "Audio/EmbeddedAudioMixerInput.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <vector>

namespace
{

using DesktopSceneEngine::Audio::AudioInputHandle;
using DesktopSceneEngine::Audio::AudioInputState;
using DesktopSceneEngine::Audio::AudioMixer;
using DesktopSceneEngine::Audio::AudioMixerInputReadStatus;
using DesktopSceneEngine::Audio::EmbeddedAudioMixerInput;

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

float normalize(std::int16_t sample)
{
    return sample < 0
        ? static_cast<float>(sample) / 32768.0F
        : static_cast<float>(sample) / 32767.0F;
}

bool verifyEmptyAndMalformedSources()
{
    EmbeddedAudioMixerInput empty{{}, 0U};
    std::vector<float> emptyDestination{Sentinel, Sentinel};
    const auto emptyResult = empty.readFrames(emptyDestination);
    if (!require(emptyResult.framesProduced == 0U
                && emptyResult.status == AudioMixerInputReadStatus::EndOfStream,
            "Empty source must immediately report EndOfStream")
        || !require(emptyDestination == std::vector<float>{Sentinel, Sentinel},
            "Empty source must not write destination"))
    {
        return false;
    }

    const std::vector<std::int16_t> malformedSource{1, 2, 3};
    EmbeddedAudioMixerInput malformed{malformedSource, 0U};
    std::vector<float> malformedDestination{Sentinel, Sentinel};
    const auto firstFailure = malformed.readFrames(malformedDestination);
    const auto secondFailure = malformed.readFrames(malformedDestination);
    return require(firstFailure.framesProduced == 0U
                && firstFailure.status == AudioMixerInputReadStatus::Failure,
            "Odd source sample counts must fail")
        && require(secondFailure.framesProduced == 0U
                && secondFailure.status == AudioMixerInputReadStatus::Failure,
            "Malformed source failure must remain durable")
        && require(malformedDestination == std::vector<float>{Sentinel, Sentinel},
            "Malformed source must not write destination");
}

bool verifyStartFramesAndChannelOrder()
{
    const std::vector<std::int16_t> source{
        -32768, 32767,
        -16384, 16384,
        -1, 1,
        0, 0,
        1000, -1000};

    EmbeddedAudioMixerInput middle{source, 1U};
    std::vector<float> destination(4U, Sentinel);
    const auto middleResult = middle.readFrames(destination);
    if (!require(middleResult.framesProduced == 2U
                && middleResult.status == AudioMixerInputReadStatus::Data,
            "Middle start must read the requested full block")
        || !require(destination == std::vector<float>{
                -0.5F,
                16384.0F / 32767.0F,
                -1.0F / 32768.0F,
                1.0F / 32767.0F},
            "Middle start must preserve S16 normalization and L/R order"))
    {
        return false;
    }

    EmbeddedAudioMixerInput startAtZero{source, 0U};
    destination.assign(2U, Sentinel);
    const auto zeroResult = startAtZero.readFrames(destination);
    if (!require(zeroResult.framesProduced == 1U
                && zeroResult.status == AudioMixerInputReadStatus::Data,
            "Start frame zero must be active")
        || !require(destination == std::vector<float>{-1.0F, 1.0F},
            "Start frame zero must expose the first stereo frame"))
    {
        return false;
    }

    for (const std::size_t startFrame : {source.size() / 2U, source.size() / 2U + 10U})
    {
        EmbeddedAudioMixerInput atEnd{source, startFrame};
        destination.assign(2U, Sentinel);
        const auto result = atEnd.readFrames(destination);
        if (!require(result.framesProduced == 0U
                    && result.status == AudioMixerInputReadStatus::EndOfStream,
                "At-EOF and past-EOF starts must remain stable EOF")
            || !require(destination == std::vector<float>{Sentinel, Sentinel},
                "At-EOF starts must not write destination"))
        {
            return false;
        }
    }

    return true;
}

bool verifyReadBoundariesAndDestinationWrites()
{
    const std::vector<std::int16_t> source{
        -32768, 32767,
        -16384, 16384,
        -1, 1,
        0, 0,
        1, -1};
    EmbeddedAudioMixerInput input{source, 0U};
    std::vector<float> destination(6U, Sentinel);

    const auto first = input.readFrames(destination);
    if (!require(first.framesProduced == 3U
                && first.status == AudioMixerInputReadStatus::Data,
            "A full non-terminal read must return Data")
        || !require(destination == std::vector<float>{
                -1.0F,
                1.0F,
                -0.5F,
                16384.0F / 32767.0F,
                -1.0F / 32768.0F,
                1.0F / 32767.0F},
            "A full read must convert exactly the requested source frames"))
    {
        return false;
    }

    destination.assign(6U, Sentinel);
    const auto final = input.readFrames(destination);
    if (!require(final.framesProduced == 2U
                && final.status == AudioMixerInputReadStatus::EndOfStream,
            "A short final read must report EndOfStream with valid frames")
        || !require(destination[0] == 0.0F
                && destination[1] == 0.0F
                && destination[2] == 1.0F / 32767.0F
                && destination[3] == -1.0F / 32768.0F
                && destination[4] == Sentinel
                && destination[5] == Sentinel,
            "A short read must leave unused destination samples untouched"))
    {
        return false;
    }

    destination.assign(2U, Sentinel);
    const auto repeatedEof = input.readFrames(destination);
    if (!require(repeatedEof.framesProduced == 0U
                && repeatedEof.status == AudioMixerInputReadStatus::EndOfStream,
            "EOF must remain stable")
        || !require(destination == std::vector<float>{Sentinel, Sentinel},
            "Repeated EOF must not modify destination"))
    {
        return false;
    }

    EmbeddedAudioMixerInput exactBoundary{source, 0U};
    destination.assign(source.size(), Sentinel);
    const auto exact = exactBoundary.readFrames(destination);
    const auto exactEof = exactBoundary.readFrames(destination);
    return require(exact.framesProduced == source.size() / 2U
                && exact.status == AudioMixerInputReadStatus::Data,
            "An exact source boundary must use Data-then-EOF semantics")
        && require(exactEof.framesProduced == 0U
                && exactEof.status == AudioMixerInputReadStatus::EndOfStream,
            "The read following an exact boundary must report EndOfStream");
}

bool verifyDestinationValidation()
{
    const std::vector<std::int16_t> source{100, -100, 200, -200};
    EmbeddedAudioMixerInput active{source, 0U};
    std::vector<float> empty{};
    const auto emptyResult = active.readFrames(empty);
    if (!require(emptyResult.framesProduced == 0U
                && emptyResult.status == AudioMixerInputReadStatus::Data,
            "An active adapter must accept an empty destination without advancing"))
    {
        return false;
    }

    std::vector<float> oddDestination{Sentinel, Sentinel, Sentinel};
    const auto oddResult = active.readFrames(oddDestination);
    if (!require(oddResult.framesProduced == 0U
                && oddResult.status == AudioMixerInputReadStatus::Failure,
            "Odd destinations must produce durable Failure")
        || !require(oddDestination == std::vector<float>{Sentinel, Sentinel, Sentinel},
            "Odd destinations must remain unchanged")
        || !require(active.readFrames(std::span<float>{}).status
                == AudioMixerInputReadStatus::Failure,
            "A failed adapter must remain Failed even for empty destinations"))
    {
        return false;
    }

    EmbeddedAudioMixerInput ended{{}, 0U};
    return require(ended.readFrames(std::span<float>{}).status
            == AudioMixerInputReadStatus::EndOfStream,
        "An ended adapter must remain EndOfStream for empty destinations");
}

std::vector<float> readAll(std::span<const std::int16_t> source, std::size_t blockFrames)
{
    EmbeddedAudioMixerInput input{source, 0U};
    std::vector<float> output{};
    std::vector<float> block(blockFrames * 2U, Sentinel);

    for (std::size_t iteration = 0U; iteration < 128U; ++iteration)
    {
        std::fill(block.begin(), block.end(), Sentinel);
        const auto result = input.readFrames(block);
        if (result.status == AudioMixerInputReadStatus::Failure)
        {
            return {};
        }

        output.insert(
            output.end(),
            block.begin(),
            block.begin() + static_cast<std::ptrdiff_t>(result.framesProduced * 2U));
        if (result.status == AudioMixerInputReadStatus::EndOfStream)
        {
            return output;
        }
    }

    return {};
}

bool verifyBlockSizeIndependenceAndMixerIntegration()
{
    std::vector<std::int16_t> source{};
    for (std::int16_t frame = 0; frame < 35; ++frame)
    {
        source.push_back(static_cast<std::int16_t>(frame * 97 - 1600));
        source.push_back(static_cast<std::int16_t>(1600 - frame * 71));
    }

    const std::vector<float> oneFrame = readAll(source, 1U);
    const std::vector<float> seventeenFrames = readAll(source, 17U);
    const std::vector<float> largeBlock = readAll(source, 64U);
    if (!require(!oneFrame.empty()
                && oneFrame == seventeenFrames
                && oneFrame == largeBlock,
            "Different block sizes must produce the same converted PCM sequence"))
    {
        return false;
    }

    AudioMixer mixer{};
    auto adapter = std::make_unique<EmbeddedAudioMixerInput>(source, 33U);
    const AudioInputHandle handle = mixer.addInput(std::move(adapter));
    std::vector<float> mixed(8U, Sentinel);
    const auto mixResult = mixer.mixFrames(mixed);
    AudioInputState terminalState = AudioInputState::Active;
    return require(mixResult.framesProduced == 4U,
            "Mixer integration must retain the requested output block size")
        && require(mixed[0] == normalize(source[66U])
                && mixed[1] == normalize(source[67U])
                && mixed[2] == normalize(source[68U])
                && mixed[3] == normalize(source[69U])
                && mixed[4] == 0.0F
                && mixed[5] == 0.0F
                && mixed[6] == 0.0F
                && mixed[7] == 0.0F,
            "Mixer integration must preserve final adapter frames then supply silence")
        && require(mixer.consumeTerminalState(handle, terminalState)
                && terminalState == AudioInputState::Ended,
            "Mixer must receive the adapter EndOfStream terminal fact");
}

}

int main()
{
    const bool success = verifyEmptyAndMalformedSources()
        && verifyStartFramesAndChannelOrder()
        && verifyReadBoundariesAndDestinationWrites()
        && verifyDestinationValidation()
        && verifyBlockSizeIndependenceAndMixerIntegration();
    return success ? 0 : 1;
}
