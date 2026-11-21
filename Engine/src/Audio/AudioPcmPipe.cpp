#include "AudioPcmPipe.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace DesktopSceneEngine::Audio
{
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
static_assert(std::atomic<AudioPcmReadStatus>::is_always_lock_free);

AudioPcmPipe::AudioPcmPipe(std::size_t capacityFrames)
    : capacityFrames_(capacityFrames)
{
    if (!capacityFrames || capacityFrames > CapacityFrames)
        throw std::invalid_argument("audio-pcm-pipe-capacity");
    pcm_.resize(capacityFrames * Channels);
}

std::size_t AudioPcmPipe::writeFrames(std::span<const float> samples) noexcept
{
    if (samples.size() % Channels || terminal_.load(std::memory_order_relaxed) != AudioPcmReadStatus::Data)
        return 0;
    const auto written = written_.load(std::memory_order_relaxed);
    const auto read = read_.load(std::memory_order_acquire);
    if (written < read || written - read > capacityFrames_) return 0;
    const auto count = (std::min)(samples.size() / Channels,
        capacityFrames_ - static_cast<std::size_t>(written - read));
    if (count > (std::numeric_limits<std::uint64_t>::max)() - written) return 0;
    for (std::size_t frame = 0; frame < count; ++frame)
    {
        const auto slot = static_cast<std::size_t>((written + frame) % capacityFrames_) * Channels;
        pcm_[slot] = samples[frame * Channels];
        pcm_[slot + 1] = samples[frame * Channels + 1];
    }
    written_.store(written + count, std::memory_order_release);
    return count;
}

bool AudioPcmPipe::publishTerminal(AudioPcmReadStatus status,
    BackgroundMusicObservation::FailureStage stage,
    BackgroundMusicObservation::FailureCode code) noexcept
{
    if ((status != AudioPcmReadStatus::EndOfStream && status != AudioPcmReadStatus::Failure
            && status != AudioPcmReadStatus::Retired)
        || terminal_.load(std::memory_order_relaxed) != AudioPcmReadStatus::Data) return false;
    terminalPosition_ = written_.load(std::memory_order_relaxed);
    failureStage_ = stage;
    failureCode_ = code;
    terminal_.store(status, std::memory_order_release);
    return true;
}

AudioPcmReadResult AudioPcmPipe::readFrames(std::span<float> samples) noexcept
{
    if (samples.size() % Channels)
        return {0, AudioPcmReadStatus::Failure,
            BackgroundMusicObservation::FailureStage::Playback,
            BackgroundMusicObservation::FailureCode::InputFailed};
    const auto read = read_.load(std::memory_order_relaxed);
    // Observe terminal first. If it was not published yet, an exact-boundary
    // read may truthfully return Data then observe terminal on the next read.
    const auto terminal = terminal_.load(std::memory_order_acquire);
    const auto written = written_.load(std::memory_order_acquire);
    const auto available = written >= read ? written - read : 0;
    const auto count = (std::min)(samples.size() / Channels,
        static_cast<std::size_t>((std::min)(available, std::uint64_t{capacityFrames_})));
    for (std::size_t frame = 0; frame < count; ++frame)
    {
        const auto slot = static_cast<std::size_t>((read + frame) % capacityFrames_) * Channels;
        samples[frame * Channels] = pcm_[slot];
        samples[frame * Channels + 1] = pcm_[slot + 1];
    }
    read_.store(read + count, std::memory_order_release);
    if (terminal != AudioPcmReadStatus::Data && read + count >= terminalPosition_)
        return {count, terminal, failureStage_, failureCode_};
    return {count, count == samples.size() / Channels ? AudioPcmReadStatus::Data : AudioPcmReadStatus::TemporaryEmpty};
}

std::uint64_t AudioPcmPipe::consumerPosition() const noexcept { return read_.load(std::memory_order_acquire); }
std::uint64_t AudioPcmPipe::producedPosition() const noexcept { return written_.load(std::memory_order_acquire); }
std::size_t AudioPcmPipe::availableFrames() const noexcept
{
    // This is a detached observation, not a transactional two-counter snapshot.
    const auto read = consumerPosition();
    const auto written = producedPosition();
    return written >= read ? static_cast<std::size_t>((std::min)(written - read, std::uint64_t{capacityFrames_})) : 0;
}
bool AudioPcmPipe::producerTerminal() const noexcept { return terminal_.load(std::memory_order_acquire) != AudioPcmReadStatus::Data; }
}
