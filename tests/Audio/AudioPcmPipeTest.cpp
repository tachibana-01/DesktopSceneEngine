#include "Audio/AudioPcmPipe.h"

#include <array>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

using namespace DesktopSceneEngine::Audio;
namespace Observation = DesktopSceneEngine::BackgroundMusicObservation;

namespace
{
bool require(bool value, const char* reason)
{
    if (!value) std::cerr << "FAIL: " << reason << '\n';
    return value;
}

bool statesAndWrap()
{
    AudioPcmPipe pipe{3};
    std::array<float, 8> destination{};
    if (!require(pipe.readFrames(destination).status == AudioPcmReadStatus::TemporaryEmpty,
        "empty live pipe is TemporaryEmpty, never EOF/failure")) return false;
    const std::array<float, 8> input{1, -1, 2, -2, 3, -3, 4, -4};
    if (!require(pipe.writeFrames(input) == 3, "full pipe bounds a producer write")
        || !require(pipe.writeFrames(input) == 0, "full pipe never overwrites unread frames")) return false;
    auto read = pipe.readFrames(std::span{destination}.first(4));
    if (!require(read.framesProduced == 2 && read.status == AudioPcmReadStatus::Data,
        "read exact requested prefix") || !require(destination[0] == 1 && destination[3] == -2,
        "stereo order preserved")) return false;
    if (!require(pipe.writeFrames(std::span{input}.subspan(6)) == 1, "write wraps without reset")) return false;
    if (!require(pipe.publishTerminal(AudioPcmReadStatus::Failure,
        Observation::FailureStage::Playback, Observation::FailureCode::DecodeFailure),
        "failure marker publishes even without PCM capacity")) return false;
    read = pipe.readFrames(destination);
    if (!require(read.framesProduced == 2 && read.status == AudioPcmReadStatus::Failure,
        "valid prefix is consumed before failure boundary")
        || !require(destination[0] == 3 && destination[1] == -3 && destination[2] == 4 && destination[3] == -4,
        "wrapped valid data matches original")
        || !require(read.failureCode == Observation::FailureCode::DecodeFailure, "failure detail retained")
        || !require(!pipe.publishTerminal(AudioPcmReadStatus::EndOfStream), "terminal cannot overwrite failure")
        || !require(pipe.writeFrames(input) == 0, "no writes follow terminal publication")) return false;
    read = pipe.readFrames(destination);
    return require(read.framesProduced == 0 && read.status == AudioPcmReadStatus::Failure,
        "terminal remains durable") && require(pipe.consumerPosition() == 4 && pipe.producedPosition() == 4,
        "absolute cursors reflect actual PCM, not silent destination tail");
}

bool eofAndRetirement()
{
    AudioPcmPipe pipe{1};
    const std::array<float, 2> input{0.25F, -0.5F};
    std::array<float, 2> output{};
    if (!require(pipe.writeFrames(input) == 1 && pipe.publishTerminal(AudioPcmReadStatus::EndOfStream),
        "terminal is not queued in the full payload ring")) return false;
    const auto read = pipe.readFrames(output);
    if (!require(read.status == AudioPcmReadStatus::EndOfStream && read.framesProduced == 1 && output == input,
        "exact final read observes drained EOF")) return false;
    AudioPcmPipe cancelled{1};
    if (!require(cancelled.publishTerminal(AudioPcmReadStatus::Retired), "producer publishes retirement")) return false;
    return require(cancelled.readFrames(output).status == AudioPcmReadStatus::Retired,
        "retirement is not natural EOF");
}

bool concurrentTransfer()
{
    constexpr std::uint64_t Frames = 500003;
    AudioPcmPipe pipe{257};
    std::atomic_bool valid{true};
    std::thread producer{[&] {
        std::array<float, 46> input{};
        std::uint64_t next = 0;
        while (next < Frames)
        {
            const auto count = static_cast<std::size_t>((std::min)(std::uint64_t{23}, Frames - next));
            for (std::size_t index = 0; index < count; ++index)
            {
                input[index * 2] = static_cast<float>(next + index);
                input[index * 2 + 1] = -static_cast<float>(next + index);
            }
            const auto written = pipe.writeFrames(std::span{input}.first(count * 2));
            next += written;
            if (!written) std::this_thread::yield();
        }
        if (!pipe.publishTerminal(AudioPcmReadStatus::EndOfStream)) valid.store(false);
    }};
    std::array<float, 62> output{};
    std::uint64_t next = 0;
    for (;;)
    {
        const auto read = pipe.readFrames(output);
        for (std::size_t index = 0; index < read.framesProduced; ++index)
            if (output[index * 2] != static_cast<float>(next + index)
                || output[index * 2 + 1] != -static_cast<float>(next + index)) valid.store(false);
        next += read.framesProduced;
        if (read.status == AudioPcmReadStatus::EndOfStream) break;
        if (read.status != AudioPcmReadStatus::Data && read.status != AudioPcmReadStatus::TemporaryEmpty) valid.store(false);
        if (!read.framesProduced) std::this_thread::yield();
    }
    producer.join();
    return require(valid.load() && next == Frames, "SPSC transfer has no torn stereo frames, loss, duplication, or early EOF");
}
}

int main()
{
    if (!statesAndWrap() || !eofAndRetirement() || !concurrentTransfer()) return 1;
    std::cout << "AudioPcmPipeTest PASS\n";
}
