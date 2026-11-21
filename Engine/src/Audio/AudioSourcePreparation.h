#pragma once

#include "AudioExecutionTypes.h"
#include <DesktopSceneEngine/BackgroundMusicObservation.h>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>

namespace DesktopSceneEngine::Audio
{
enum class AudioPreparationAdmission { Admitted, Busy, ResourceLimit, InvalidRequest, Stopped };
enum class AudioPreparationState { Empty, Queued, Preparing, Ready, Failed, Cancelled };

struct AudioPreparationSnapshot
{
    AudioSourceKey key{};
    AudioPreparationState state = AudioPreparationState::Empty;
    bool cancellationRequested = false;
    bool producerQuiesced = true;
    bool consumerQuiesced = true;
    BackgroundMusicObservation::FailureStage failureStage = BackgroundMusicObservation::FailureStage::Unknown;
    BackgroundMusicObservation::FailureCode failureCode = BackgroundMusicObservation::FailureCode::Unknown;
};

// C-owned lifetime; exactly three serial producer contexts and six stable
// records (two per lane). No method is an audio-thread API. A receives only a
// borrowed PreparedAudioSourceView and must ACK ConsumerQuiesced before reclaim.
class AudioSourcePreparation final
{
public:
    static constexpr std::size_t MaximumPcmArtifactBytes = 512ULL * 1024 * 1024;
    static constexpr std::size_t MaximumEmbeddedPcmBytes = 2ULL * 1024 * 1024 * 1024;
    static constexpr std::size_t ReferencePrimeFrames = 9600;

    AudioSourcePreparation();
    ~AudioSourcePreparation();
    AudioSourcePreparation(const AudioSourcePreparation&) = delete;
    AudioSourcePreparation& operator=(const AudioSourcePreparation&) = delete;

    // Uses the same optional C-owned Windows auto-reset event as A. Attach
    // before admitting work and retain the handle until P has joined.
    [[nodiscard]] bool setControlWakeEvent(void* borrowedEvent) noexcept;

    // Reserve before native authored admission. originalSource is used only for
    // a same-binding tempo candidate and retains its immutable original PCM.
    [[nodiscard]] AudioPreparationAdmission requestEmbedded(const AudioSourceKey& key,
        const std::filesystem::path& path, float tempo,
        const AudioSourceKey& originalSource = {});
    [[nodiscard]] AudioPreparationAdmission requestBgm(const AudioSourceKey& key,
        const std::filesystem::path& path, std::size_t primeFrames = ReferencePrimeFrames);

    [[nodiscard]] AudioPreparationSnapshot snapshot(const AudioSourceKey& key) const noexcept;
    [[nodiscard]] std::optional<PreparedAudioSourceView> view(const AudioSourceKey& key) const noexcept;
    // Set BEFORE exposing the view to A or its queue. A rejected enqueue may be
    // discharged by C only when C proves that no A-visible reference escaped.
    [[nodiscard]] bool markAudioSubmitted(const AudioSourceKey& key) noexcept;
    [[nodiscard]] bool markConsumerQuiesced(const AudioSourceKey& key) noexcept;
    void cancel(const AudioSourceKey& key) noexcept;
    // C guarantees all queued install/result references and necessary terminal
    // facts were discharged. Returns false until both endpoint proofs exist.
    [[nodiscard]] bool reclaim(const AudioSourceKey& key) noexcept;

    void beginShutdown() noexcept;
    [[nodiscard]] bool stopped() const noexcept;
    // Nonblocking admission: joins only after every producer reports exit.
    [[nodiscard]] bool joinStopped() noexcept;
    [[nodiscard]] std::size_t reservedPcmBytes() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
