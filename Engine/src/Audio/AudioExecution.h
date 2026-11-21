#pragma once

#include "AudioExecutionTypes.h"

#include <DesktopSceneEngine/BackgroundMusicObservation.h>

#include <cstddef>
#include <cstdint>
#include <array>
#include <memory>
#include <optional>

namespace DesktopSceneEngine::Audio
{

class AudioExecutionTestDevice;

enum class AudioCommandKind : std::uint8_t
{
    InstallBgmPrepared, RetireSource, ReplaceEmbedded, SetTransport,
    SetSourceGain, SetOutputMute, SetEmbeddedGate, RetirePlayback, Shutdown
};
enum class AudioEffectClass : std::uint8_t
{
    Structural, EmbeddedGain, PlaylistGain, ItemGain, OutputMute
};
enum class AudioTransport : std::uint8_t { Stopped, Playing, Paused };
enum class AudioEmbeddedReplacement : std::uint8_t { Bind, SeekFixed, RateContinuous };
enum class AudioOutputHealth : std::uint8_t { Healthy, DeviceUnavailable, ContractFaulted };
enum class AudioEffectResult : std::uint8_t
{
    Applied, Failed, CancelledByRetirement, DeferredUnavailable
};
enum class AudioEffectFailure : std::uint8_t
{
    None, InvalidCommand, StaleIdentity, SourceCapacity, DeviceUnavailable,
    DeviceContract, MuteUnavailable, TimingChanged, EndOfScene, Shutdown
};
enum class AudioSourceTerminal : std::uint8_t { None, Ended, Failed, Retired };

struct AudioEffectTicket
{
    std::uint8_t slot = 255U;
    std::uint64_t incarnation = 0U;
    [[nodiscard]] bool valid() const noexcept { return slot < 32U && incarnation != 0U; }
    bool operator==(const AudioEffectTicket&) const = default;
};

struct AudioCommand
{
    AudioCommandKind kind = AudioCommandKind::SetTransport;
    AudioSourceKey source{};
    PreparedAudioSourceView prepared{};
    // A nonzero expected value rejects a transition after another timing
    // effect committed. New binding may deliberately replace the old binding.
    std::uint64_t expectedTimingVersion = 0U;
    std::uint64_t playbackLifetime = 0U;
    AudioClockAnchor clock{};
    AudioTransport transport = AudioTransport::Paused;
    AudioEmbeddedReplacement replacement = AudioEmbeddedReplacement::Bind;
    float gain = 1.0F;
    bool muted = false;
    bool embeddedPresent = true;
    bool holdEmbedded = false;
};

struct AudioEffectReceipt
{
    AudioEffectTicket ticket{};
    AudioCommandKind command = AudioCommandKind::SetTransport;
    std::uint64_t sequence = 0U;
    AudioEffectResult result = AudioEffectResult::Failed;
    AudioEffectFailure failure = AudioEffectFailure::None;
    AudioMonotonicTime effectTime = 0;
    AudioSourceKey source{};
    AudioClockAnchor clock{};
    AudioTransport transport = AudioTransport::Stopped;
    AudioOutputHealth health = AudioOutputHealth::Healthy;
    bool muteDesired = false;
    bool muteApplied = false;
    bool muteKnown = true;
    AudioEmbeddedReplacement replacement = AudioEmbeddedReplacement::Bind;
};

struct AudioSourceObservation
{
    AudioSourceKey source{};
    bool registered = false;
    bool consumerQuiesced = false;
    AudioSourceTerminal terminal = AudioSourceTerminal::None;
    std::uint64_t terminalSequence = 0U;
    std::uint64_t cursorFrames = 0U;
    BackgroundMusicObservation::FailureStage failureStage = BackgroundMusicObservation::FailureStage::Unknown;
    BackgroundMusicObservation::FailureCode failureCode = BackgroundMusicObservation::FailureCode::Unknown;
};

struct AudioExecutionSnapshot
{
    std::uint64_t publication = 0U;
    // QPC ns at this publication's cursor/padding sample, for detached
    // alignment observations; this does not advance the logical clock.
    AudioMonotonicTime publicationTimeNs = 0;
    std::uint64_t lastEffectSequence = 0U;
    AudioClockAnchor clock{};
    AudioTransport transport = AudioTransport::Stopped;
    AudioOutputHealth health = AudioOutputHealth::Healthy;
    std::uint64_t activeVoices = 0U;
    std::uint64_t writtenFrames = 0U;
    // Successful post-mix/pre-stream-mute stereo PCM writes over this A
    // lifetime. A frame is nonzero if either S16 channel is nonzero. The
    // longest all-zero run spans writes/reopens; neither count is a DAC fact.
    std::uint64_t nonzeroSubmittedFrames = 0U;
    std::uint64_t maximumConsecutiveZeroSubmittedFrames = 0U;
    std::uint64_t queuedFrames = 0U;
    std::uint64_t bufferFrames = 0U;
    std::uint64_t primeFrames = 9'600U;
    std::uint64_t refillCount = 0U;
    std::uint64_t sourceUnderruns = 0U;
    std::uint64_t sourceMissingFrames = 0U;
    std::uint64_t outputUnderflows = 0U;
    std::uint64_t deviceFailures = 0U;
    std::uint64_t recoveryAttempts = 0U;
    std::uint64_t recoveredCount = 0U;
    AudioMonotonicTime lastWriteTime = 0;
    AudioMonotonicTime maximumWriteGap = 0;
    AudioMonotonicTime maximumServiceDuration = 0;
    AudioMonotonicTime maximumWakeGap = 0;
    // Consumption/submission positions, not DAC-played positions. Identity
    // and cursor are captured in the same detached publication.
    std::array<AudioSourceKey, AudioLaneCount> laneSources{};
    std::array<std::uint64_t, AudioLaneCount> laneCursorFrames{};
    // Actual active Embedded prepared PCM tempo, zero when absent. This
    // source fact is independent of the authorized logical clock rate.
    double embeddedTempo = 0.0;
    bool muteDesired = false;
    bool muteApplied = false;
    bool muteKnown = true;
    bool started = false;
    bool stopped = false;
    bool mmcssRegistered = false;
    std::uint32_t ownerThreadId = 0U;
};

// C-facing bounded execution port. Unless explicitly documented otherwise,
// public calls are made by the one existing control thread. None pumps audio.
class AudioExecution final
{
public:
    static constexpr std::size_t EffectCapacity = 32U;
    static constexpr std::size_t MaximumDeviceFrames = 48'000U;

    AudioExecution();
    explicit AudioExecution(AudioExecutionTestDevice& device);
    ~AudioExecution();
    AudioExecution(const AudioExecution&) = delete;
    AudioExecution& operator=(const AudioExecution&) = delete;

    // Optional Windows auto-reset event, attached by C before start. Its
    // owner keeps it alive until A has joined. Wakes carry no payload.
    [[nodiscard]] bool setControlWakeEvent(void* borrowedEvent) noexcept;
    [[nodiscard]] bool start() noexcept;
    // Reserve before authored commit or preparation. Mask bit0/1/2 is lane;
    // bit3 is output. Structural blocks affected later scalar admission.
    [[nodiscard]] std::optional<AudioEffectTicket> reserve(
        AudioEffectClass effectClass, std::uint8_t affectedMask = 0x0FU,
        std::uint64_t group = 0U) noexcept;
    [[nodiscard]] bool enqueue(AudioEffectTicket ticket, const AudioCommand& command) noexcept;
    [[nodiscard]] bool cancelReserved(AudioEffectTicket ticket) noexcept;
    [[nodiscard]] std::optional<AudioEffectReceipt> receipt(AudioEffectTicket ticket) const noexcept;
    [[nodiscard]] bool releaseReceipt(AudioEffectTicket ticket) noexcept;

    // Dedicated retirement cells do not consume ordinary queue/ticket space.
    // Retiring an enqueued candidate also cancels its future installation.
    [[nodiscard]] bool requestRetireSource(const AudioSourceKey& source) noexcept;
    [[nodiscard]] std::optional<AudioSourceObservation> sourceObservation(
        const AudioSourceKey& source) const noexcept;
    // C calls only after P quiescence and A's no-more-reads. This gate also
    // proves queued-reference drainage. C retains its owning record until the
    // following non-deadline storage reclamation has actually completed.
    [[nodiscard]] bool releaseSourceRecord(const AudioSourceKey& source) noexcept;
    // Reserved monotonic Stop barrier, independent of all ordinary tickets.
    [[nodiscard]] bool requestRetirePlayback(std::uint64_t playbackLifetime) noexcept;
    [[nodiscard]] std::uint64_t retiredPlaybackLifetime() const noexcept;

    // Reserved one-record internal safety port. A confirmed video failure
    // cannot be blocked behind ordinary preparation/admission capacity.
    [[nodiscard]] std::optional<std::uint64_t> requestEmbeddedFailureGate(
        const AudioClockAnchor& anchor, double failedTarget, bool hold) noexcept;
    [[nodiscard]] std::optional<AudioEffectReceipt> embeddedGateReceipt(
        std::uint64_t request) const noexcept;
    [[nodiscard]] bool releaseEmbeddedGateReceipt(std::uint64_t request) noexcept;

    void requestShutdown() noexcept;
    [[nodiscard]] bool shutdownComplete() const noexcept;
    // Nonblocking. The C/root remains alive until this succeeds.
    [[nodiscard]] bool joinIfStopped() noexcept;
    [[nodiscard]] AudioExecutionSnapshot snapshot() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
