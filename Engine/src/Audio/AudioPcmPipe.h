#pragma once

#include <DesktopSceneEngine/BackgroundMusicObservation.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace DesktopSceneEngine::Audio
{
enum class AudioPcmReadStatus { Data, TemporaryEmpty, EndOfStream, Failure, Retired };

struct AudioPcmReadResult
{
    std::size_t framesProduced = 0;
    AudioPcmReadStatus status = AudioPcmReadStatus::TemporaryEmpty;
    BackgroundMusicObservation::FailureStage failureStage = BackgroundMusicObservation::FailureStage::Unknown;
    BackgroundMusicObservation::FailureCode failureCode = BackgroundMusicObservation::FailureCode::Unknown;
};

// One resource producer and one audio consumer. Construction/destruction and
// reclamation belong to the nondeadline owner, after both endpoints quiesce.
// Positions are absolute stereo frame positions; a live pipe is never reset.
class AudioPcmPipe final
{
public:
    static constexpr std::size_t CapacityFrames = 48000;
    static constexpr std::size_t Channels = 2;

    explicit AudioPcmPipe(std::size_t capacityFrames = CapacityFrames);
    AudioPcmPipe(const AudioPcmPipe&) = delete;
    AudioPcmPipe& operator=(const AudioPcmPipe&) = delete;

    // P only. Bounded partial write, no waiting; zero when full/closed.
    [[nodiscard]] std::size_t writeFrames(std::span<const float> samples) noexcept;
    // P only, exactly once. Terminal metadata is independent of ring capacity.
    [[nodiscard]] bool publishTerminal(AudioPcmReadStatus status,
        BackgroundMusicObservation::FailureStage stage = BackgroundMusicObservation::FailureStage::Unknown,
        BackgroundMusicObservation::FailureCode code = BackgroundMusicObservation::FailureCode::Unknown) noexcept;

    // A only. Never waits, allocates, frees, decodes, or calls another owner.
    [[nodiscard]] AudioPcmReadResult readFrames(std::span<float> samples) noexcept;
    [[nodiscard]] std::uint64_t consumerPosition() const noexcept;
    [[nodiscard]] std::uint64_t producedPosition() const noexcept;
    [[nodiscard]] std::size_t availableFrames() const noexcept;
    [[nodiscard]] std::size_t capacityFrames() const noexcept { return capacityFrames_; }
    [[nodiscard]] bool producerTerminal() const noexcept;

private:
    const std::size_t capacityFrames_;
    std::vector<float> pcm_;
    alignas(64) std::atomic<std::uint64_t> written_{0};
    alignas(64) std::atomic<std::uint64_t> read_{0};
    std::uint64_t terminalPosition_ = 0;
    BackgroundMusicObservation::FailureStage failureStage_ = BackgroundMusicObservation::FailureStage::Unknown;
    BackgroundMusicObservation::FailureCode failureCode_ = BackgroundMusicObservation::FailureCode::Unknown;
    std::atomic<AudioPcmReadStatus> terminal_{AudioPcmReadStatus::Data};
};
}
