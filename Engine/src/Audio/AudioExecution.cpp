#include "AudioExecution.h"

#include "AudioExecutionTestDevice.h"
#include "AudioMixer.h"
#include "AudioSampleConversion.h"
#include "AudioPcmPipe.h"
#include "MixedOutputBridge.h"
#include "Platform/Audio/AudioOutput.h"
#include "Platform/Audio/DefaultAudioEndpointWatcher.h"

#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <limits>
#include <span>
#include <thread>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <avrt.h>
#endif

namespace DesktopSceneEngine::Audio
{
namespace
{
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
static_assert(std::atomic<bool>::is_always_lock_free);
constexpr AudioMonotonicTime RecoveryCooldown = 500'000'000LL;
constexpr std::uint8_t AllLanes = 0x07U;
constexpr unsigned OwnerWaitTimeoutMs = 10U;
constexpr std::size_t DefaultPeriodFrames = OwnerWaitTimeoutMs * 48U; // 48 kHz output
constexpr std::size_t RenderHorizonPeriods = 4U;
enum class TicketState : std::uint8_t { Free, Reserved, Queued, Done };
enum class SourceState : std::uint8_t { Free, Queued, Active, Quiesced };

std::size_t laneIndex(AudioLane lane) noexcept { return static_cast<std::size_t>(lane); }
std::uint8_t scalarMask(AudioEffectClass type) noexcept
{
    return type == AudioEffectClass::Structural ? 0U
        : static_cast<std::uint8_t>(1U << (static_cast<unsigned>(type) - 1U));
}

class DevicePort final : public AudioBlockSink
{
public:
    explicit DevicePort(AudioExecutionTestDevice* test) noexcept : test_(test) {}
    bool open() noexcept
    {
        opened_ = test_ ? test_->open() : native_.initializeEventDriven();
        resetQueueAncestry();
        return opened_;
    }
    void close() noexcept
    {
        if (test_) test_->close(); else native_.shutdown();
        opened_ = false;
        resetQueueAncestry();
    }
    bool opened() const noexcept { return opened_; }
    AudioBlockStatus configure(bool paused) noexcept
    {
        return test_ ? test_->configure(paused) : native_.beginBlockMode(paused
            ? Platform::Audio::AudioBlockStartMode::ConfigurePaused
            : Platform::Audio::AudioBlockStartMode::StartImmediately);
    }
    AudioBlockStatus pause() noexcept { return test_ ? test_->pause() : native_.pause(); }
    AudioBlockStatus resume() noexcept { return test_ ? test_->resume() : native_.resume(); }
    [[nodiscard]] AudioBlockStatus stop() noexcept
    {
        const auto result = test_ ? test_->stopAndResetChecked() : native_.stopAndResetChecked();
        if (result == AudioBlockStatus::Success) resetQueueAncestry();
        return result;
    }
    AudioDeviceMuteResult mute(bool muted) noexcept
    {
        if (test_) return test_->setMuted(muted);
        switch (native_.setMutedChecked(muted))
        {
        case Platform::Audio::CheckedMuteStatus::Applied: return AudioDeviceMuteResult::Applied;
        case Platform::Audio::CheckedMuteStatus::Unavailable: return AudioDeviceMuteResult::Unavailable;
        case Platform::Audio::CheckedMuteStatus::DeviceFailure: return AudioDeviceMuteResult::DeviceFailure;
        case Platform::Audio::CheckedMuteStatus::ContractFailure: return AudioDeviceMuteResult::ContractFailure;
        }
        return AudioDeviceMuteResult::ContractFailure;
    }
    AudioBlockAvailability writableFrames() noexcept override
    {
        const auto available = test_ ? test_->writableFrames() : native_.writableFrames();
        if (available.status != AudioBlockStatus::Success) return available;
        const auto capacity = bufferFrames();
        if (available.writableFrames > capacity) return {0U, AudioBlockStatus::InvalidRequest};
        const auto measured = measuredPeriodFrames();
        if (measured == 0U) return available;
        const auto period = (std::max)(measured, DefaultPeriodFrames);
        if (period > capacity / RenderHorizonPeriods) return available;

        // Keep physical capacity for the endpoint, but mix only the next
        // few periods. Account for the owner's timeout if readiness is late.
        // A control change cannot alter PCM already submitted.
        const auto target = period * RenderHorizonPeriods;
        const auto padding = capacity - available.writableFrames;
        return {padding < target ? target - padding : 0U, AudioBlockStatus::Success};
    }
    AudioBlockStatus writeFrames(std::span<const std::int16_t> samples) noexcept override
    {
        const auto frames = samples.size() / 2U;
        if (frames > (std::numeric_limits<std::uint64_t>::max)() - submittedFrames_)
            return AudioBlockStatus::InvalidRequest;
        const auto result = test_ ? test_->writeFrames(samples) : native_.writeFrames(samples);
        if (result == AudioBlockStatus::Success)
        {
            submittedFrames_ += frames;
            lastWriteFrames_ = frames;
            auto nonzero = nonzeroSubmittedFrames_;
            auto zeroRun = consecutiveZeroSubmittedFrames_;
            auto maximumZeroRun = maximumConsecutiveZeroSubmittedFrames_;
            constexpr auto maximum = (std::numeric_limits<std::uint64_t>::max)();
            for (std::size_t frame = 0U; frame < frames; ++frame)
            {
                if (samples[frame * 2U] != 0 || samples[frame * 2U + 1U] != 0)
                {
                    if (nonzero < maximum) ++nonzero;
                    zeroRun = 0U;
                }
                else
                {
                    if (zeroRun < maximum) ++zeroRun;
                    maximumZeroRun = (std::max)(maximumZeroRun, zeroRun);
                }
            }
            nonzeroSubmittedFrames_ = nonzero;
            consecutiveZeroSubmittedFrames_ = zeroRun;
            maximumConsecutiveZeroSubmittedFrames_ = maximumZeroRun;
        }
        return result;
    }
    std::uint64_t nonzeroSubmittedFrames() const noexcept { return nonzeroSubmittedFrames_; }
    std::uint64_t maximumConsecutiveZeroSubmittedFrames() const noexcept
    { return maximumConsecutiveZeroSubmittedFrames_; }
    void markLastWriteEmbeddedPrefix(std::uint64_t frames) noexcept
    {
        if (frames != 0U)
            embeddedSubmittedThrough_ = submittedFrames_ - lastWriteFrames_
                + (std::min)(frames, lastWriteFrames_);
    }
    bool queueContainsEmbedded() const noexcept
    {
        if (!opened_ || embeddedSubmittedThrough_ == 0U) return false;
        const auto padding = queuedFrames();
        const auto played = submittedFrames_ >= padding ? submittedFrames_ - padding : 0U;
        return played < embeddedSubmittedThrough_;
    }
    std::size_t bufferFrames() const noexcept
    { return test_ ? test_->bufferFrames() : native_.bufferFrameCount(); }
    std::size_t periodFrames() const noexcept
    {
        const auto period = measuredPeriodFrames();
        return period ? period : DefaultPeriodFrames;
    }
    std::uint64_t queuedFrames() const noexcept
    { return test_ ? test_->queuedFrames() : native_.queuedFrames(); }
    std::uint64_t underflows() const noexcept
    { return test_ ? test_->underflowCount() : native_.underflowCount(); }
    void* event() const noexcept
    { return test_ ? test_->readinessHandle() : native_.readinessHandle(); }
    std::uint64_t endpointGeneration() const noexcept
    { return test_ ? test_->endpointGeneration() : 0U; }
private:
    std::size_t measuredPeriodFrames() const noexcept
    { return test_ ? DefaultPeriodFrames : native_.devicePeriodFrames(); }

    void resetQueueAncestry() noexcept
    {
        submittedFrames_ = 0U;
        lastWriteFrames_ = 0U;
        embeddedSubmittedThrough_ = 0U;
    }
    AudioExecutionTestDevice* test_ = nullptr;
    Platform::Audio::AudioOutput native_{};
    bool opened_ = false;
    // Device-queue provenance, not a media clock. A retiring source does not
    // erase PCM already copied into the stream. Padding advances this finite
    // watermark out of the queue without a permanent "had embedded" flag.
    std::uint64_t submittedFrames_ = 0U;
    std::uint64_t lastWriteFrames_ = 0U;
    std::uint64_t embeddedSubmittedThrough_ = 0U;
    // Successful mixed S16 stereo submissions over this A lifetime. Stream
    // replacement/queue ancestry reset must not erase these diagnostics.
    std::uint64_t nonzeroSubmittedFrames_ = 0U;
    std::uint64_t consecutiveZeroSubmittedFrames_ = 0U;
    std::uint64_t maximumConsecutiveZeroSubmittedFrames_ = 0U;
};

// Inline endpoints have no ownership/destructor work. Producer/storage lifetime
// is retained by C's source records until both retirement directions complete.
class PreparedInput final : public AudioMixerInput
{
public:
    PreparedAudioSourceView view{};
    std::uint64_t embeddedCursor = 0U;
    std::uint64_t underruns = 0U;
    std::uint64_t missingFrames = 0U;
    bool held = false;
    bool retiredByProducer = false;
    BackgroundMusicObservation::FailureStage failureStage = BackgroundMusicObservation::FailureStage::Unknown;
    BackgroundMusicObservation::FailureCode failureCode = BackgroundMusicObservation::FailureCode::Unknown;

    AudioMixerInputReadResult readFrames(std::span<float> destination) noexcept override
    {
        if (held) return {0U, AudioMixerInputReadStatus::Data};
        if (view.key.lane == AudioLane::Embedded)
        {
            const auto total = view.sampleCount / 2U;
            if (embeddedCursor >= total) return {0U, AudioMixerInputReadStatus::EndOfStream};
            const auto frames = (std::min)(destination.size() / 2U,
                total - static_cast<std::size_t>(embeddedCursor));
            const auto offset = static_cast<std::size_t>(embeddedCursor) * 2U;
            // Preserve the existing positive/negative full-scale convention.
            // Reuse the bounded conversion, rather than changing one lane by
            // an S16 LSB during the ownership migration.
            if (!convertS16SamplesToF32({view.pcm + offset, frames * 2U}, destination.first(frames * 2U)))
                return {0U, AudioMixerInputReadStatus::Failure};
            embeddedCursor += frames;
            return {frames, frames < destination.size() / 2U
                ? AudioMixerInputReadStatus::EndOfStream : AudioMixerInputReadStatus::Data};
        }
        if (!view.pipe) return {0U, AudioMixerInputReadStatus::Failure};
        const auto read = view.pipe->readFrames(destination);
        failureStage = read.failureStage;
        failureCode = read.failureCode;
        switch (read.status)
        {
        case AudioPcmReadStatus::Data: return {read.framesProduced, AudioMixerInputReadStatus::Data};
        case AudioPcmReadStatus::TemporaryEmpty:
            ++underruns;
            missingFrames += destination.size() / 2U - read.framesProduced;
            return {read.framesProduced, AudioMixerInputReadStatus::TemporaryEmpty};
        case AudioPcmReadStatus::EndOfStream: return {read.framesProduced, AudioMixerInputReadStatus::EndOfStream};
        case AudioPcmReadStatus::Failure: return {read.framesProduced, AudioMixerInputReadStatus::Failure};
        case AudioPcmReadStatus::Retired:
            retiredByProducer = true;
            return {read.framesProduced, AudioMixerInputReadStatus::Retired};
        }
        return {0U, AudioMixerInputReadStatus::Failure};
    }
    std::uint64_t cursor() const noexcept
    { return view.key.lane == AudioLane::Embedded ? embeddedCursor : view.pipe ? view.pipe->consumerPosition() : 0U; }
};

// All fields in the latest observation are atomic, including seqlock payload:
// unlike a seqlock over plain C++ fields, concurrent reads have no data race.
struct AtomicSnapshot
{
    std::atomic<std::uint64_t> version{0U};
    std::array<std::atomic<std::uint64_t>, 56U> fields{};

    void publish(const AudioExecutionSnapshot& s) noexcept
    {
        version.fetch_add(1U, std::memory_order_acq_rel);
        std::array<std::uint64_t, 56U> values{
            s.publication, s.lastEffectSequence, s.clock.binding, s.clock.timingVersion,
            std::bit_cast<std::uint64_t>(s.clock.position), static_cast<std::uint64_t>(s.clock.time),
            std::bit_cast<std::uint64_t>(s.clock.rate), std::bit_cast<std::uint64_t>(s.clock.duration),
            static_cast<std::uint64_t>(s.clock.mode), static_cast<std::uint64_t>(s.transport),
            static_cast<std::uint64_t>(s.health), s.activeVoices, s.writtenFrames, s.queuedFrames,
            s.bufferFrames, s.refillCount, s.sourceUnderruns, s.sourceMissingFrames,
            s.outputUnderflows, s.deviceFailures, s.recoveryAttempts, s.recoveredCount,
            static_cast<std::uint64_t>(s.lastWriteTime), static_cast<std::uint64_t>(s.maximumWriteGap),
            static_cast<std::uint64_t>(s.maximumServiceDuration), s.muteDesired, s.muteApplied,
            s.muteKnown, s.started, s.stopped, s.primeFrames, 0U, 0U, 0U, 0U, 0U};
        for (std::size_t lane = 0U; lane < AudioLaneCount; ++lane)
        {
            const auto& key = s.laneSources[lane];
            const auto offset = 31U + lane * 6U;
            values[offset] = key.slot;
            values[offset + 1U] = key.incarnation;
            values[offset + 2U] = key.playbackLifetime;
            values[offset + 3U] = key.sourceToken;
            values[offset + 4U] = key.mediaBinding;
            values[offset + 5U] = s.laneCursorFrames[lane];
        }
        values[49U] = static_cast<std::uint64_t>(s.maximumWakeGap);
        values[50U] = s.mmcssRegistered;
        values[51U] = s.ownerThreadId;
        values[52U] = static_cast<std::uint64_t>(s.publicationTimeNs);
        values[53U] = s.nonzeroSubmittedFrames;
        values[54U] = s.maximumConsecutiveZeroSubmittedFrames;
        values[55U] = std::bit_cast<std::uint64_t>(s.embeddedTempo);
        for (std::size_t i = 0U; i < values.size(); ++i) fields[i].store(values[i], std::memory_order_relaxed);
        version.fetch_add(1U, std::memory_order_release);
    }
    AudioExecutionSnapshot read() const noexcept
    {
        std::array<std::uint64_t, 56U> v{};
        for (;;)
        {
            const auto before = version.load(std::memory_order_acquire);
            if ((before & 1U) != 0U) continue;
            for (std::size_t i = 0U; i < v.size(); ++i) v[i] = fields[i].load(std::memory_order_relaxed);
            if (before == version.load(std::memory_order_acquire)) break;
        }
        AudioExecutionSnapshot s{};
        s.publication = v[0]; s.lastEffectSequence = v[1];
        s.clock = {v[2], v[3], std::bit_cast<double>(v[4]), static_cast<AudioMonotonicTime>(v[5]),
            std::bit_cast<double>(v[6]), std::bit_cast<double>(v[7]), static_cast<AudioClockMode>(v[8])};
        s.transport = static_cast<AudioTransport>(v[9]); s.health = static_cast<AudioOutputHealth>(v[10]);
        s.activeVoices = v[11]; s.writtenFrames = v[12]; s.queuedFrames = v[13]; s.bufferFrames = v[14];
        s.refillCount = v[15]; s.sourceUnderruns = v[16]; s.sourceMissingFrames = v[17];
        s.outputUnderflows = v[18]; s.deviceFailures = v[19]; s.recoveryAttempts = v[20]; s.recoveredCount = v[21];
        s.lastWriteTime = static_cast<AudioMonotonicTime>(v[22]);
        s.maximumWriteGap = static_cast<AudioMonotonicTime>(v[23]);
        s.maximumServiceDuration = static_cast<AudioMonotonicTime>(v[24]);
        s.muteDesired = v[25] != 0U; s.muteApplied = v[26] != 0U; s.muteKnown = v[27] != 0U;
        s.started = v[28] != 0U; s.stopped = v[29] != 0U;
        s.primeFrames = v[30];
        for (std::size_t lane = 0U; lane < AudioLaneCount; ++lane)
        {
            const auto offset = 31U + lane * 6U;
            s.laneSources[lane] = {static_cast<AudioLane>(lane), static_cast<std::uint8_t>(v[offset]),
                v[offset + 1U], v[offset + 2U], v[offset + 3U], v[offset + 4U]};
            s.laneCursorFrames[lane] = v[offset + 5U];
        }
        s.maximumWakeGap = static_cast<AudioMonotonicTime>(v[49U]);
        s.mmcssRegistered = v[50U] != 0U;
        s.ownerThreadId = static_cast<std::uint32_t>(v[51U]);
        s.publicationTimeNs = static_cast<AudioMonotonicTime>(v[52U]);
        s.nonzeroSubmittedFrames = v[53U];
        s.maximumConsecutiveZeroSubmittedFrames = v[54U];
        s.embeddedTempo = std::bit_cast<double>(v[55U]);
        return s;
    }
};
}

struct AudioExecution::Impl
{
    struct TicketRecord
    {
        std::atomic<TicketState> state{TicketState::Free};
        std::uint64_t incarnation = 0U; // C changes only while Free.
        AudioEffectClass type = AudioEffectClass::Structural;
        std::uint8_t affectedMask = 0U;
        std::uint64_t group = 0U;
        AudioCommand command{};
        AudioEffectReceipt receipt{}; // A publishes immutable through Done.
    };
    struct SourceRecord
    {
        std::atomic<SourceState> state{SourceState::Free};
        AudioSourceKey key{}; // C owns; immutable until released Quiesced.
        std::atomic_bool retireRequested{false};
        std::atomic_bool registered{false};
        std::atomic_bool consumerQuiesced{false};
        std::atomic<AudioSourceTerminal> terminal{AudioSourceTerminal::None};
        std::atomic<std::uint64_t> cursor{0U};
        std::uint64_t terminalSequence = 0U;
        BackgroundMusicObservation::FailureStage failureStage = BackgroundMusicObservation::FailureStage::Unknown;
        BackgroundMusicObservation::FailureCode failureCode = BackgroundMusicObservation::FailureCode::Unknown;
    };

    explicit Impl(AudioExecutionTestDevice* test)
        : device(test), bridge(mixer, device), testDevice(test != nullptr) {}
    ~Impl()
    {
        // Defensive lifetime barrier; ordinary UI Close uses asynchronous
        // shutdownComplete/joinIfStopped before destroying this owner.
        shutdownRequested.store(true, std::memory_order_release);
        signal();
        if (thread.joinable()) thread.join();
#if defined(_WIN32)
        if (controlEvent) CloseHandle(controlEvent);
#endif
    }

    DevicePort device;
    AudioMixer mixer{};
    MixedOutputBridge bridge;
    Platform::Audio::DefaultAudioEndpointWatcher endpointWatcher{};
    bool testDevice = false;
    std::array<TicketRecord, EffectCapacity> tickets{};
    std::array<SourceRecord, AudioSourceRecordCount> sources{};
    std::array<PreparedInput, AudioSourceRecordCount> endpoints{};
    std::array<AudioInputHandle, AudioSourceRecordCount> handles{};
    std::array<int, AudioLaneCount> laneSlots{-1, -1, -1};
    std::array<std::uint8_t, EffectCapacity> queue{};
    std::atomic<std::uint64_t> queueWrite{0U};
    std::atomic<std::uint64_t> queueRead{0U};
    std::atomic_bool shutdownRequested{false};
    std::atomic_bool stopped{false};
    std::thread thread{};
#if defined(_WIN32)
    HANDLE controlEvent = nullptr;
    HANDLE mmcssTask = nullptr;
#endif
    // C-only admission state. A never reads or writes these fields.
    std::uint64_t nextTicket = 1U;
    std::uint64_t structuralGroup = 0U;
    std::size_t structuralReferences = 0U;
    std::uint8_t structuralMask = 0U;
    std::uint8_t scalarBusy = 0U;
    // A-only state, published through durable records / atomic snapshot.
    AudioExecutionSnapshot status{};
    AtomicSnapshot observation{};
    std::uint64_t sequence = 0U;
    void* controlWakeEvent = nullptr; // C installs before A starts.
    bool controlWakePending = false;
    AudioOutputHealth publishedHealth = AudioOutputHealth::Healthy;
    bool publishedMuteKnown = true;
    std::uint64_t retiredPlaybackLifetime = 0U;
    std::uint64_t currentPlaybackLifetime = 0U;
    std::atomic<std::uint64_t> playbackRetirementRequest{0U};
    std::atomic<std::uint64_t> playbackRetirementAck{0U};
    std::uint64_t endpointGeneration = 0U;
    AudioMonotonicTime outageBegan = 0;
    bool configured = false;
    bool embeddedNeedsResync = false;
    bool embeddedHeld = false;
    int shutdownTicket = -1;
    std::uint64_t lastDeviceUnderflows = 0U;
    AudioMonotonicTime lastWakeTime = 0;
    struct FailureGateRecord
    {
        std::atomic<TicketState> state{TicketState::Free};
        std::uint64_t request = 0U;
        AudioClockAnchor anchor{};
        double failedTarget = 0.0;
        bool hold = true;
        AudioEffectReceipt receipt{};
    } failureGate;
    std::uint64_t nextFailureGate = 1U;

    void signal() noexcept
    {
#if defined(_WIN32)
        if (controlEvent) SetEvent(controlEvent);
#endif
    }

    void wakeControl() const noexcept
    {
#if defined(_WIN32)
        if (controlWakeEvent) SetEvent(static_cast<HANDLE>(controlWakeEvent));
#endif
    }

    bool ticketMatches(AudioEffectTicket ticket) const noexcept
    {
        return ticket.valid() && tickets[ticket.slot].state.load(std::memory_order_acquire) != TicketState::Free
            && tickets[ticket.slot].incarnation == ticket.incarnation;
    }
    bool sourceMatches(const AudioSourceKey& key) const noexcept
    {
        return key.valid() && sources[key.slot].state.load(std::memory_order_acquire) != SourceState::Free
            && sources[key.slot].key == key;
    }
    bool reserveSource(const AudioSourceKey& key) noexcept
    {
        if (!key.valid()) return false;
        auto& s = sources[key.slot];
        if (s.state.load(std::memory_order_acquire) != SourceState::Free)
            return s.key == key && !s.consumerQuiesced.load(std::memory_order_acquire)
                && !s.retireRequested.load(std::memory_order_acquire);
        s.key = key;
        s.retireRequested.store(false, std::memory_order_relaxed);
        s.registered.store(false, std::memory_order_relaxed);
        s.consumerQuiesced.store(false, std::memory_order_relaxed);
        s.terminal.store(AudioSourceTerminal::None, std::memory_order_relaxed);
        s.cursor.store(0U, std::memory_order_relaxed);
        s.terminalSequence = 0U;
        s.failureStage = BackgroundMusicObservation::FailureStage::Unknown;
        s.failureCode = BackgroundMusicObservation::FailureCode::Unknown;
        s.state.store(SourceState::Queued, std::memory_order_release);
        return true;
    }
    void publish() noexcept
    {
        const bool controlFactChanged = controlWakeEvent
            && (std::exchange(controlWakePending, false) || status.publication == 0
                || status.lastEffectSequence != sequence || publishedHealth != status.health
                || publishedMuteKnown != status.muteKnown);
        status.activeVoices = 0U;
        status.embeddedTempo = 0.0;
        for (std::size_t lane = 0U; lane < laneSlots.size(); ++lane)
        {
            const auto slot = laneSlots[lane];
            status.laneSources[lane] = {};
            status.laneCursorFrames[lane] = 0U;
            if (slot < 0) continue;
            ++status.activeVoices;
            const auto index = static_cast<std::size_t>(slot);
            status.laneSources[lane] = endpoints[index].view.key;
            status.laneCursorFrames[lane] = endpoints[index].cursor();
            if (lane == laneIndex(AudioLane::Embedded)) status.embeddedTempo = endpoints[index].view.tempo;
        }
        status.queuedFrames = device.opened() ? device.queuedFrames() : 0U;
        status.publicationTimeNs = audioMonotonicNow();
        status.bufferFrames = device.opened() ? device.bufferFrames() : 0U;
        status.nonzeroSubmittedFrames = device.nonzeroSubmittedFrames();
        status.maximumConsecutiveZeroSubmittedFrames = device.maximumConsecutiveZeroSubmittedFrames();
        ++status.publication;
        status.lastEffectSequence = sequence;
        observation.publish(status);
        if (controlFactChanged)
        {
            publishedHealth = status.health;
            publishedMuteKnown = status.muteKnown;
            // C validates receipts and terminals against the published frontier.
            wakeControl();
        }
    }
    void terminal(std::size_t slot, AudioSourceTerminal kind) noexcept
    {
        auto& s = sources[slot];
        if (s.terminal.load(std::memory_order_relaxed) != AudioSourceTerminal::None) return;
        s.cursor.store(endpoints[slot].cursor(), std::memory_order_relaxed);
        s.terminalSequence = ++sequence;
        s.failureStage = endpoints[slot].failureStage;
        s.failureCode = endpoints[slot].failureCode;
        s.terminal.store(kind, std::memory_order_release);
    }
    void retire(std::size_t slot, bool explicitRetirement) noexcept
    {
        auto& s = sources[slot];
        const auto phase = s.state.load(std::memory_order_acquire);
        if (phase == SourceState::Free || phase == SourceState::Quiesced) return;
        if (handles[slot]) { (void)mixer.removeInput(handles[slot]); handles[slot] = {}; }
        const auto lane = laneIndex(s.key.lane);
        if (laneSlots[lane] == static_cast<int>(slot)) laneSlots[lane] = -1;
        if (explicitRetirement) terminal(slot, AudioSourceTerminal::Retired);
        endpoints[slot].view = {}; // Last A reference is gone before the ACK.
        s.registered.store(false, std::memory_order_relaxed);
        s.state.store(SourceState::Quiesced, std::memory_order_release);
        s.consumerQuiesced.store(true, std::memory_order_release);
        controlWakePending = true;
    }
    void processRetirements() noexcept
    {
        for (std::size_t i = 0U; i < sources.size(); ++i)
        {
            const auto state = sources[i].state.load(std::memory_order_acquire);
            if ((state == SourceState::Queued || state == SourceState::Active)
                && sources[i].retireRequested.load(std::memory_order_acquire)) retire(i, true);
        }
    }
    void processPlaybackRetirement() noexcept
    {
        const auto request = playbackRetirementRequest.load(std::memory_order_acquire);
        if (request <= playbackRetirementAck.load(std::memory_order_relaxed)) return;
        for (std::size_t slot = 0U; slot < sources.size(); ++slot)
        {
            const auto phase = sources[slot].state.load(std::memory_order_acquire);
            if ((phase == SourceState::Queued || phase == SourceState::Active)
                && sources[slot].key.playbackLifetime <= request) retire(slot, true);
        }
        retiredPlaybackLifetime = (std::max)(retiredPlaybackLifetime, request);
        // An older Stop request may race a newer queued Bind. It cancels old
        // sources, but is not authority to stop the newer playback lifetime.
        if (currentPlaybackLifetime <= request)
        {
            (void)stopOutput();
            configured = false;
            status.transport = AudioTransport::Stopped;
            status.lastWriteTime = 0;
            lastDeviceUnderflows = 0U;
        }
        playbackRetirementAck.store(request, std::memory_order_release);
        controlWakePending = true;
    }
    void closeFailedOutput(bool contract) noexcept
    {
        device.close();
        configured = false;
        status.health = contract || status.health == AudioOutputHealth::ContractFaulted
            ? AudioOutputHealth::ContractFaulted : AudioOutputHealth::DeviceUnavailable;
        status.muteKnown = false;
        outageBegan = audioMonotonicNow();
        status.lastWriteTime = 0;
        ++status.deviceFailures;
        embeddedNeedsResync = true;
        lastDeviceUnderflows = 0U;
    }
    bool stopOutput() noexcept
    {
        const auto result = device.stop();
        if (result != AudioBlockStatus::Success)
            closeFailedOutput(result != AudioBlockStatus::DeviceFailure);
        configured = false;
        lastDeviceUnderflows = 0U;
        return result == AudioBlockStatus::Success;
    }
    void outputFailure(bool contract) noexcept
    {
        const auto resetStatus = device.stop();
        closeFailedOutput(contract || (resetStatus != AudioBlockStatus::Success
            && resetStatus != AudioBlockStatus::DeviceFailure));
    }
    bool hasSources() const noexcept
    {
        for (const auto slot : laneSlots) if (slot >= 0) return true;
        return false;
    }
    bool positionEmbedded(std::size_t slot, double position) noexcept
    {
        auto& input = endpoints[slot];
        const double frame = position / input.view.tempo * 48'000.0;
        if (!std::isfinite(frame) || frame < 0.0
            || frame >= static_cast<double>(input.view.sampleCount / 2U)) return false;
        input.embeddedCursor = static_cast<std::uint64_t>(frame);
        sources[slot].cursor.store(input.embeddedCursor, std::memory_order_release);
        return true;
    }
    bool applyEmbeddedGate(const AudioClockAnchor& authorized, bool hold,
        AudioMonotonicTime effectTime) noexcept
    {
        if (!status.clock.valid() || authorized.binding != status.clock.binding
            || authorized.position > status.clock.duration) return false;
        // A failure result may arrive after A finished an already-authorized
        // Rate/Pause while C was decoding. This gate authorizes a hold/resync
        // position, not another rate, duration or transport transition.
        auto next = status.clock;
        next.position = authorized.position;
        next.time = effectTime;
        next.timingVersion = authorized.timingVersion;
        next.mode = hold ? AudioClockMode::FailureHold
            : status.transport == AudioTransport::Playing ? AudioClockMode::Running : AudioClockMode::Paused;
        status.clock = next;
        embeddedHeld = hold;
        if (const auto slot = laneSlots[0]; slot >= 0)
        {
            auto& endpoint = endpoints[static_cast<std::size_t>(slot)];
            endpoint.held = hold;
            if (!hold && !positionEmbedded(static_cast<std::size_t>(slot), next.position)) endpoint.held = true;
        }
        return true;
    }
    void reconcileOutput() noexcept
    {
        if (status.health == AudioOutputHealth::ContractFaulted) return;
        if (!hasSources() || status.transport == AudioTransport::Stopped)
        {
            // Natural terminal consumption retires the borrowed input, not
            // PCM already copied into the device. Explicit Stop is separate.
            if (!hasSources() && configured && status.transport != AudioTransport::Stopped
                && device.queuedFrames() > 0U)
            { status.lastWriteTime = 0; return; }
            if (configured) (void)stopOutput();
            configured = false;
            lastDeviceUnderflows = 0U;
            status.lastWriteTime = 0;
            return;
        }
        const auto now = audioMonotonicNow();
        if (status.health == AudioOutputHealth::DeviceUnavailable && now - outageBegan < RecoveryCooldown) return;
        if (!device.opened())
        {
            const bool recovering = status.health == AudioOutputHealth::DeviceUnavailable;
            if (recovering) ++status.recoveryAttempts;
            if (!device.open()) { outputFailure(false); return; }
            if (device.bufferFrames() == 0U || device.bufferFrames() > MaximumDeviceFrames)
            { outputFailure(true); return; }
            // Never Start a recovered stream before the latest authorized
            // mute has actually been applied to the new stream.
            const auto mute = device.mute(status.muteDesired);
            const bool initialOptionalMute = !recovering && !status.muteDesired
                && mute == AudioDeviceMuteResult::Unavailable;
            if (mute != AudioDeviceMuteResult::Applied && !initialOptionalMute)
            { outputFailure(mute != AudioDeviceMuteResult::DeviceFailure); return; }
            status.muteApplied = status.muteDesired;
            status.muteKnown = !initialOptionalMute;
            status.health = AudioOutputHealth::Healthy;
            embeddedNeedsResync = true;
            status.primeFrames = device.bufferFrames() + 2U * device.periodFrames();
            if (status.primeFrames > AudioPcmPipe::CapacityFrames)
            { outputFailure(true); return; }
            if (recovering) ++status.recoveredCount;
        }
        if (!configured)
        {
            // P keeps filling while the GUI is blocked. Startup/recovery may
            // wait on bounded already-published supply, never on a P callback.
            for (std::size_t lane = 1U; lane < laneSlots.size(); ++lane)
            {
                const auto slot = laneSlots[lane];
                if (slot < 0) continue;
                const auto* pipe = endpoints[static_cast<std::size_t>(slot)].view.pipe;
                if (pipe && pipe->availableFrames() < status.primeFrames
                    && !pipe->producerTerminal()) return;
            }
            const auto result = device.configure(status.transport == AudioTransport::Paused);
            if (result != AudioBlockStatus::Success)
            { outputFailure(result != AudioBlockStatus::DeviceFailure); return; }
            configured = true;
        }
        if (embeddedNeedsResync)
        {
            const auto slot = laneSlots[laneIndex(AudioLane::Embedded)];
            if (slot >= 0 && status.clock.valid())
            {
                auto& input = endpoints[static_cast<std::size_t>(slot)];
                input.held = embeddedHeld || status.clock.mode == AudioClockMode::FailureHold;
                // Open, prime readiness and native Start may take time after
                // the authorized effect. Align at actual service readiness,
                // never at a timestamp captured before those native calls.
                if (!positionEmbedded(static_cast<std::size_t>(slot),
                    resolveAudioClockPosition(status.clock, audioMonotonicNow())))
                    input.held = true; // At duration, never wrap to zero.
            }
            embeddedNeedsResync = false;
        }
    }
    void finish(std::size_t slot, AudioEffectResult result, AudioEffectFailure failure,
        AudioMonotonicTime effectTime) noexcept
    {
        auto& record = tickets[slot];
        if (result != AudioEffectResult::Applied && record.command.prepared.key.valid())
        {
            const auto candidate = record.command.prepared.key.slot;
            if (sources[candidate].state.load(std::memory_order_acquire) == SourceState::Queued
                && sources[candidate].key == record.command.prepared.key) retire(candidate, true);
        }
        const auto target = record.command.source.valid() ? record.command.source : record.command.prepared.key;
        record.receipt = {{static_cast<std::uint8_t>(slot), record.incarnation}, record.command.kind,
            ++sequence, result, failure, effectTime, target, status.clock,
            status.transport, status.health, status.muteDesired, status.muteApplied, status.muteKnown};
        record.receipt.replacement = record.command.replacement;
        record.state.store(TicketState::Done, std::memory_order_release);
    }
    bool timingMatches(const AudioCommand& cmd) const noexcept
    {
        if (cmd.expectedTimingVersion != 0U && cmd.expectedTimingVersion != status.clock.timingVersion) return false;
        if ((cmd.kind == AudioCommandKind::ReplaceEmbedded || cmd.kind == AudioCommandKind::SetTransport
            || cmd.kind == AudioCommandKind::SetEmbeddedGate) && status.clock.valid()
            && cmd.clock.valid() && cmd.clock.timingVersion <= status.clock.timingVersion) return false;
        return true;
    }
    bool install(const PreparedAudioSourceView& view, float gain, double position) noexcept
    {
        const auto slot = view.key.slot;
        const auto lane = laneIndex(view.key.lane);
        if (laneSlots[lane] >= 0 && laneSlots[lane] != slot) return false;
        auto& endpoint = endpoints[slot];
        endpoint.view = view;
        endpoint.held = view.key.lane == AudioLane::Embedded && embeddedHeld;
        endpoint.underruns = 0U; endpoint.missingFrames = 0U;
        endpoint.retiredByProducer = false;
        endpoint.failureStage = BackgroundMusicObservation::FailureStage::Unknown;
        endpoint.failureCode = BackgroundMusicObservation::FailureCode::Unknown;
        if (view.key.lane == AudioLane::Embedded && !positionEmbedded(slot, position))
        { endpoint.view = {}; return false; }
        handles[slot] = mixer.addBorrowedInput(endpoint, gain);
        if (!handles[slot]) { endpoint.view = {}; return false; }
        laneSlots[lane] = slot;
        auto& source = sources[slot];
        source.registered.store(true, std::memory_order_relaxed);
        source.state.store(SourceState::Active, std::memory_order_release);
        return true;
    }
    void apply(std::size_t index) noexcept
    {
        auto& record = tickets[index];
        const auto& cmd = record.command;
        auto now = audioMonotonicNow();
        auto effectTime = now;
        if (shutdownRequested.load(std::memory_order_acquire))
        { finish(index, AudioEffectResult::CancelledByRetirement, AudioEffectFailure::Shutdown, now); return; }
        if (cmd.kind == AudioCommandKind::Shutdown)
        {
            shutdownTicket = static_cast<int>(index);
            shutdownRequested.store(true, std::memory_order_release);
            return;
        }
        processRetirements();
        processPlaybackRetirement();
        processFailureGate();
        now = audioMonotonicNow(); effectTime = now;
        const auto retiredThrough = (std::max)(retiredPlaybackLifetime,
            playbackRetirementRequest.load(std::memory_order_acquire));
        const bool timingCommand = cmd.kind == AudioCommandKind::ReplaceEmbedded || cmd.kind == AudioCommandKind::SetTransport;
        const auto lifetime = cmd.kind == AudioCommandKind::ReplaceEmbedded && cmd.embeddedPresent
            ? cmd.prepared.key.playbackLifetime : cmd.playbackLifetime;
        if (timingCommand && (lifetime == 0 || lifetime <= retiredThrough || lifetime < currentPlaybackLifetime))
        {
            finish(index, lifetime == 0 ? AudioEffectResult::Failed : AudioEffectResult::CancelledByRetirement,
                lifetime == 0 ? AudioEffectFailure::InvalidCommand : AudioEffectFailure::StaleIdentity, now);
            return;
        }
        if (cmd.kind == AudioCommandKind::SetSourceGain && cmd.source.playbackLifetime <= retiredThrough)
        { finish(index, AudioEffectResult::CancelledByRetirement, AudioEffectFailure::StaleIdentity, now); return; }
        // Absolute transport is idempotent. Legacy C forwarding may repeat a
        // Pause/Resume fact before it has consumed the first real receipt.
        // Preserve that receipt's anchor rather than inventing another clock
        // transition or converting the already-applied request into failure.
        if (cmd.kind == AudioCommandKind::SetTransport && status.clock.valid()
            && cmd.clock.binding == status.clock.binding && cmd.transport == status.transport)
        {
            currentPlaybackLifetime = (std::max)(currentPlaybackLifetime, lifetime);
            finish(index, AudioEffectResult::Applied, AudioEffectFailure::None, now);
            return;
        }
        if (!timingMatches(cmd))
        { finish(index, AudioEffectResult::Failed, AudioEffectFailure::TimingChanged, now); return; }
        auto result = AudioEffectResult::Applied;
        auto failure = AudioEffectFailure::None;
        switch (cmd.kind)
        {
        case AudioCommandKind::InstallBgmPrepared:
            if (!cmd.prepared.valid() || cmd.prepared.key.lane == AudioLane::Embedded
                || !sourceMatches(cmd.prepared.key) || sources[cmd.prepared.key.slot].consumerQuiesced.load(std::memory_order_acquire)
                || sources[cmd.prepared.key.slot].retireRequested.load(std::memory_order_acquire)
                || cmd.prepared.key.playbackLifetime <= retiredThrough
                || cmd.prepared.key.playbackLifetime < currentPlaybackLifetime)
            { result = AudioEffectResult::CancelledByRetirement; failure = AudioEffectFailure::StaleIdentity; }
            else if (!install(cmd.prepared, cmd.gain, 0.0))
            { result = AudioEffectResult::Failed; failure = AudioEffectFailure::SourceCapacity; }
            else if (cmd.prepared.key.playbackLifetime > currentPlaybackLifetime)
            {
                // The first applied effect in a new playback lifetime owns
                // its initial transport. Later prepared sources in that same
                // lifetime must preserve an already-applied Pause/Resume.
                const bool changed = cmd.transport != status.transport;
                if (configured && changed)
                {
                    const auto native = cmd.transport == AudioTransport::Paused ? device.pause()
                        : cmd.transport == AudioTransport::Playing ? device.resume() : AudioBlockStatus::Success;
                    if (cmd.transport == AudioTransport::Stopped) { (void)stopOutput(); }
                    else if (native != AudioBlockStatus::Success) outputFailure(native != AudioBlockStatus::DeviceFailure);
                }
                effectTime = audioMonotonicNow();
                status.transport = cmd.transport;
                currentPlaybackLifetime = cmd.prepared.key.playbackLifetime;
                if (changed) status.lastWriteTime = 0;
            }
            break;
        case AudioCommandKind::RetireSource:
            if (sourceMatches(cmd.source)) retire(cmd.source.slot, true);
            break;
        case AudioCommandKind::ReplaceEmbedded:
        {
            if (!cmd.clock.valid() || (cmd.embeddedPresent && (!cmd.prepared.valid()
                || std::abs(cmd.prepared.tempo - cmd.clock.rate) > 0.000001
                || cmd.prepared.key.lane != AudioLane::Embedded || !sourceMatches(cmd.prepared.key)
                || sources[cmd.prepared.key.slot].retireRequested.load(std::memory_order_acquire)
                || sources[cmd.prepared.key.slot].consumerQuiesced.load(std::memory_order_acquire)
                || cmd.prepared.key.playbackLifetime <= retiredPlaybackLifetime)))
            { result = AudioEffectResult::Failed; failure = AudioEffectFailure::InvalidCommand; break; }
            auto next = cmd.clock;
            next.time = now;
            if (cmd.replacement == AudioEmbeddedReplacement::RateContinuous)
            {
                if (!status.clock.valid() || status.clock.binding != cmd.clock.binding)
                { result = AudioEffectResult::Failed; failure = AudioEffectFailure::TimingChanged; break; }
                next.position = resolveAudioClockPosition(status.clock, now);
            }
            if (next.position >= next.duration
                && (cmd.replacement != AudioEmbeddedReplacement::Bind || next.duration > 0.0))
            { result = AudioEffectResult::Failed; failure = AudioEffectFailure::EndOfScene; break; }
            if (cmd.embeddedPresent)
            {
                const auto frame = next.position / cmd.prepared.tempo * 48'000.0;
                if (!std::isfinite(frame) || frame < 0.0 || frame >= static_cast<double>(cmd.prepared.sampleCount / 2U))
                { result = AudioEffectResult::Failed; failure = AudioEffectFailure::EndOfScene; break; }
            }
            const auto old = laneSlots[laneIndex(AudioLane::Embedded)];
            const bool flushEmbedded = old >= 0 || cmd.embeddedPresent || device.queueContainsEmbedded();
            const bool transportChanged = cmd.transport != status.transport;
            if (flushEmbedded)
            {
                // Only old/new embedded PCM requires discarding a mixed queue.
                // A pure visual transition must not discard surviving BGM.
                if (configured) (void)stopOutput();
                configured = false;
                lastDeviceUnderflows = 0U;
            }
            else if (configured && transportChanged)
            {
                const auto native = cmd.transport == AudioTransport::Paused ? device.pause()
                    : cmd.transport == AudioTransport::Playing ? device.resume() : AudioBlockStatus::Success;
                if (cmd.transport == AudioTransport::Stopped)
                { (void)stopOutput(); }
                else if (native != AudioBlockStatus::Success) outputFailure(native != AudioBlockStatus::DeviceFailure);
            }
            effectTime = audioMonotonicNow();
            next.time = effectTime;
            if (cmd.replacement == AudioEmbeddedReplacement::RateContinuous)
            {
                next.position = resolveAudioClockPosition(status.clock, effectTime);
                if (next.position >= next.duration
                    || (cmd.embeddedPresent && next.position / cmd.prepared.tempo * 48'000.0
                        >= static_cast<double>(cmd.prepared.sampleCount / 2U)))
                {
                    if (!flushEmbedded)
                    {
                        // A checked transport change may already have happened
                        // while the old clock reached D. Report that real state
                        // with the old rate, rather than restarting the stream
                        // or claiming that the requested new rate applied.
                        auto actual = status.clock;
                        actual.position = resolveAudioClockPosition(status.clock, effectTime);
                        actual.time = effectTime;
                        actual.timingVersion = cmd.clock.timingVersion;
                        actual.mode = embeddedHeld ? AudioClockMode::FailureHold
                            : cmd.transport == AudioTransport::Playing ? AudioClockMode::Running : AudioClockMode::Paused;
                        status.clock = actual;
                        status.transport = cmd.transport;
                        if (transportChanged) status.lastWriteTime = 0;
                    }
                    result = AudioEffectResult::Failed; failure = AudioEffectFailure::EndOfScene; break;
                }
            }
            const bool same = old >= 0 && cmd.embeddedPresent && static_cast<std::uint8_t>(old) == cmd.prepared.key.slot;
            if (same)
            {
                (void)mixer.removeInput(handles[static_cast<std::size_t>(old)]);
                handles[static_cast<std::size_t>(old)] = {};
                laneSlots[0] = -1;
            }
            else if (old >= 0) retire(static_cast<std::size_t>(old), true);
            status.clock = next;
            status.transport = cmd.transport;
            currentPlaybackLifetime = (std::max)(currentPlaybackLifetime, cmd.embeddedPresent
                ? cmd.prepared.key.playbackLifetime : cmd.playbackLifetime);
            embeddedHeld = next.mode == AudioClockMode::FailureHold;
            if (cmd.embeddedPresent && !install(cmd.prepared, cmd.gain, next.position))
            { result = AudioEffectResult::Failed; failure = AudioEffectFailure::SourceCapacity; }
            embeddedNeedsResync = cmd.embeddedPresent;
            if (flushEmbedded || transportChanged) status.lastWriteTime = 0;
            break;
        }
        case AudioCommandKind::SetTransport:
        {
            if (!cmd.clock.valid() || (status.clock.valid() && cmd.clock.binding != status.clock.binding))
            { result = AudioEffectResult::Failed; failure = AudioEffectFailure::InvalidCommand; break; }
            auto next = cmd.clock;
            next.mode = embeddedHeld ? AudioClockMode::FailureHold
                : cmd.transport == AudioTransport::Playing ? AudioClockMode::Running : AudioClockMode::Paused;
            if (configured && cmd.transport != status.transport)
            {
                const auto native = cmd.transport == AudioTransport::Paused ? device.pause()
                    : cmd.transport == AudioTransport::Playing ? device.resume() : AudioBlockStatus::Success;
                if (cmd.transport == AudioTransport::Stopped) { (void)stopOutput(); }
                else if (native != AudioBlockStatus::Success) outputFailure(native != AudioBlockStatus::DeviceFailure);
            }
            effectTime = audioMonotonicNow();
            next.position = status.clock.valid() ? resolveAudioClockPosition(status.clock, effectTime) : cmd.clock.position;
            next.time = effectTime;
            status.clock = next;
            status.transport = cmd.transport;
            currentPlaybackLifetime = (std::max)(currentPlaybackLifetime, lifetime);
            status.lastWriteTime = 0;
            break;
        }
        case AudioCommandKind::SetSourceGain:
            if (!sourceMatches(cmd.source) || !handles[cmd.source.slot]
                || !std::isfinite(cmd.gain) || cmd.gain < 0.0F || cmd.gain > 1.0F
                || !mixer.setInputGain(handles[cmd.source.slot], cmd.gain))
            { result = AudioEffectResult::Failed; failure = AudioEffectFailure::StaleIdentity; }
            break;
        case AudioCommandKind::SetOutputMute:
            if (!device.opened())
            {
                status.muteDesired = cmd.muted;
                status.muteKnown = false;
                result = AudioEffectResult::DeferredUnavailable;
                failure = AudioEffectFailure::DeviceUnavailable;
            }
            else
            {
                const auto mute = device.mute(cmd.muted);
                effectTime = audioMonotonicNow();
                if (mute == AudioDeviceMuteResult::Applied)
                { status.muteDesired = cmd.muted; status.muteApplied = cmd.muted; status.muteKnown = true; }
                else
                {
                    // A failed application does not turn a requested mute-on
                    // into an authorized unmute on a subsequent new stream.
                    // Missing capability is a pre-state rejection instead.
                    if (mute != AudioDeviceMuteResult::Unavailable) status.muteDesired = cmd.muted;
                    result = AudioEffectResult::Failed;
                    failure = mute == AudioDeviceMuteResult::Unavailable
                        ? AudioEffectFailure::MuteUnavailable : AudioEffectFailure::DeviceContract;
                    if (mute != AudioDeviceMuteResult::Unavailable)
                        outputFailure(mute != AudioDeviceMuteResult::DeviceFailure);
                }
            }
            break;
        case AudioCommandKind::SetEmbeddedGate:
            if (!cmd.clock.valid() || !applyEmbeddedGate(cmd.clock, cmd.holdEmbedded, now))
            { result = AudioEffectResult::Failed; failure = AudioEffectFailure::InvalidCommand; break; }
            break;
        case AudioCommandKind::RetirePlayback:
            retiredPlaybackLifetime = (std::max)(retiredPlaybackLifetime, cmd.playbackLifetime);
            for (std::size_t slot = 0U; slot < sources.size(); ++slot)
            {
                const auto phase = sources[slot].state.load(std::memory_order_acquire);
                if ((phase == SourceState::Active || phase == SourceState::Queued)
                    && (cmd.playbackLifetime == 0U || sources[slot].key.playbackLifetime == cmd.playbackLifetime))
                    retire(slot, true);
            }
            (void)stopOutput();
            lastDeviceUnderflows = 0U;
            status.transport = AudioTransport::Stopped;
            status.lastWriteTime = 0;
            break;
        case AudioCommandKind::Shutdown: break;
        }
        finish(index, result, failure, effectTime);
    }
    void processCommands() noexcept
    {
        std::size_t processed = 0U;
        std::uint64_t batchGroup = 0U;
        bool structural = false;
        while (processed < 8U)
        {
            const auto read = queueRead.load(std::memory_order_relaxed);
            if (read == queueWrite.load(std::memory_order_acquire)) break;
            const auto index = queue[read % EffectCapacity];
            auto& record = tickets[index];
            if (processed == 0U) { structural = record.type == AudioEffectClass::Structural; batchGroup = record.group; }
            else if (structural ? record.type != AudioEffectClass::Structural || record.group != batchGroup
                : record.type == AudioEffectClass::Structural || processed >= 4U) break;
            queueRead.store(read + 1U, std::memory_order_release);
            apply(index);
            ++processed;
            if (shutdownRequested.load(std::memory_order_acquire)) break;
        }
    }
    void processFailureGate() noexcept
    {
        auto& gate = failureGate;
        if (gate.state.load(std::memory_order_acquire) != TicketState::Queued) return;
        const auto now = audioMonotonicNow();
        auto result = AudioEffectResult::Applied;
        auto failure = AudioEffectFailure::None;
        if (shutdownRequested.load(std::memory_order_acquire))
        { result = AudioEffectResult::CancelledByRetirement; failure = AudioEffectFailure::Shutdown; }
        else if (!gate.anchor.valid() || !status.clock.valid() || gate.anchor.binding != status.clock.binding
            || gate.anchor.timingVersion <= status.clock.timingVersion)
        { result = AudioEffectResult::Failed; failure = AudioEffectFailure::TimingChanged; }
        else if (!applyEmbeddedGate(gate.anchor, gate.hold, now))
        { result = AudioEffectResult::Failed; failure = AudioEffectFailure::InvalidCommand; }
        gate.receipt = {};
        gate.receipt.command = AudioCommandKind::SetEmbeddedGate;
        gate.receipt.sequence = ++sequence;
        gate.receipt.effectTime = now;
        gate.receipt.result = result;
        gate.receipt.failure = failure;
        gate.receipt.clock = status.clock;
        gate.receipt.transport = status.transport;
        gate.receipt.health = status.health;
        gate.state.store(TicketState::Done, std::memory_order_release);
    }
    void service() noexcept
    {
        if (shutdownRequested.load(std::memory_order_acquire)) return;
        processRetirements();
        processPlaybackRetirement();
        processFailureGate();
        if (!configured || status.health != AudioOutputHealth::Healthy
            || status.transport != AudioTransport::Playing || !hasSources()) return;
        const auto began = audioMonotonicNow();
        const auto embeddedSlot = laneSlots[laneIndex(AudioLane::Embedded)];
        const auto embeddedBefore = embeddedSlot >= 0
            ? endpoints[static_cast<std::size_t>(embeddedSlot)].embeddedCursor : 0U;
        const auto result = bridge.pump();
        const auto ended = audioMonotonicNow();
        status.maximumServiceDuration = (std::max)(status.maximumServiceDuration, ended - began);
        if (result.status == MixedOutputBridgeStatus::WroteFrames)
        {
            if (embeddedSlot >= 0)
            {
                const auto consumed = endpoints[static_cast<std::size_t>(embeddedSlot)].embeddedCursor - embeddedBefore;
                device.markLastWriteEmbeddedPrefix(consumed);
            }
            ++status.refillCount;
            status.writtenFrames += result.frames;
            if (status.lastWriteTime > 0) status.maximumWriteGap = (std::max)(status.maximumWriteGap, ended - status.lastWriteTime);
            status.lastWriteTime = ended;
            const auto underflows = device.underflows();
            if (underflows >= lastDeviceUnderflows) status.outputUnderflows += underflows - lastDeviceUnderflows;
            lastDeviceUnderflows = underflows;
        }
        for (std::size_t slot = 0U; slot < sources.size(); ++slot)
        {
            if (!handles[slot]) continue;
            auto& endpoint = endpoints[slot];
            status.sourceUnderruns += std::exchange(endpoint.underruns, 0U);
            status.sourceMissingFrames += std::exchange(endpoint.missingFrames, 0U);
            sources[slot].cursor.store(endpoint.cursor(), std::memory_order_release);
            AudioInputState terminalState{};
            if (mixer.consumeTerminalState(handles[slot], terminalState))
            {
                terminal(slot, endpoint.retiredByProducer ? AudioSourceTerminal::Retired
                    : terminalState == AudioInputState::Ended ? AudioSourceTerminal::Ended : AudioSourceTerminal::Failed);
                retire(slot, false);
            }
        }
        // Mixing may already have consumed a final valid prefix even if the
        // device rejected its write. Harvest its durable terminal/cursor facts
        // before reporting output failure; later retirement cannot replace it.
        if (result.status == MixedOutputBridgeStatus::DeviceFailure) { outputFailure(false); return; }
        if (result.status == MixedOutputBridgeStatus::OutputContractFailure
            || result.status == MixedOutputBridgeStatus::MixerContractFailureSilenced
            || result.status == MixedOutputBridgeStatus::MixerResourceFailureSilenced
            || result.status == MixedOutputBridgeStatus::ResourceFailure)
        { outputFailure(true); return; }
    }
    void endpointChanges() noexcept
    {
        const auto observed = testDevice ? device.endpointGeneration() : endpointWatcher.generation();
        if (observed == endpointGeneration) return;
        endpointGeneration = observed;
        if (status.health == AudioOutputHealth::ContractFaulted) return;
        (void)stopOutput();
        if (status.health == AudioOutputHealth::ContractFaulted) return;
        device.close(); configured = false;
        status.lastWriteTime = 0;
        embeddedNeedsResync = true;
        // An endpoint-change signal permits immediate reroute, matching the
        // existing policy; repeated ordinary failures still use cooldown.
        status.health = AudioOutputHealth::DeviceUnavailable;
        outageBegan = audioMonotonicNow() - RecoveryCooldown;
        status.muteKnown = false;
    }
    bool shutdown() noexcept
    {
        processFailureGate();
        processPlaybackRetirement();
        for (std::size_t slot = 0U; slot < sources.size(); ++slot) retire(slot, true);
        while (queueRead.load(std::memory_order_relaxed) != queueWrite.load(std::memory_order_acquire))
        {
            const auto read = queueRead.load(std::memory_order_relaxed);
            const auto index = queue[read % EffectCapacity];
            queueRead.store(read + 1U, std::memory_order_release);
            finish(index, AudioEffectResult::CancelledByRetirement, AudioEffectFailure::Shutdown, audioMonotonicNow());
        }
        (void)stopOutput(); device.close(); configured = false;
        if (!testDevice && !endpointWatcher.shutdownAndQuiesce()) return false;
#if defined(_WIN32)
        if (mmcssTask != nullptr)
        {
            (void)AvRevertMmThreadCharacteristics(mmcssTask);
            mmcssTask = nullptr;
        }
#endif
        status.transport = AudioTransport::Stopped;
        status.lastWriteTime = 0;
        status.stopped = true;
        if (shutdownTicket >= 0)
        {
            finish(static_cast<std::size_t>(shutdownTicket), AudioEffectResult::Applied, AudioEffectFailure::None, audioMonotonicNow());
            shutdownTicket = -1;
        }
        publish();
        stopped.store(true, std::memory_order_release);
        wakeControl();
        return true;
    }
    void wait() noexcept
    {
#if defined(_WIN32)
        HANDLE events[2] = {controlEvent, static_cast<HANDLE>(device.event())};
        const DWORD count = events[1] ? 2U : 1U;
        (void)WaitForMultipleObjects(count, events, FALSE, testDevice ? 1U : OwnerWaitTimeoutMs);
#else
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
    }
    void run() noexcept
    {
#if defined(_WIN32)
        status.ownerThreadId = GetCurrentThreadId();
        DWORD taskIndex = 0U;
        mmcssTask = AvSetMmThreadCharacteristicsW(L"Audio", &taskIndex);
        status.mmcssRegistered = mmcssTask != nullptr;
#endif
        if (!mixer.prepareDeadlineMode(AudioLaneCount, MaximumDeviceFrames)
            || !bridge.prepareDeadlineMode(MaximumDeviceFrames))
        {
            status.health = AudioOutputHealth::ContractFaulted;
            shutdownRequested.store(true, std::memory_order_release);
        }
        if (!testDevice)
        {
            (void)endpointWatcher.initialize();
            endpointGeneration = endpointWatcher.generation();
        }
        else endpointGeneration = device.endpointGeneration();
        status.started = true;
        publish();
        for (;;)
        {
            if (shutdownRequested.load(std::memory_order_acquire))
            { if (shutdown()) return; wait(); continue; }
            const auto wake = audioMonotonicNow();
            if (configured && status.transport == AudioTransport::Playing
                && status.health == AudioOutputHealth::Healthy && hasSources())
            {
                if (lastWakeTime != 0) status.maximumWakeGap = (std::max)(status.maximumWakeGap, wake - lastWakeTime);
                lastWakeTime = wake;
            }
            else lastWakeTime = 0;
            processRetirements();
            processPlaybackRetirement();
            processFailureGate();
            endpointChanges();
            reconcileOutput();
            service(); // Deadline work precedes ordinary command batches.
            processCommands();
            reconcileOutput();
            service();
            publish();
            wait();
        }
    }
};

AudioExecution::AudioExecution() : impl_(std::make_unique<Impl>(nullptr)) {}
AudioExecution::AudioExecution(AudioExecutionTestDevice& device) : impl_(std::make_unique<Impl>(&device)) {}
AudioExecution::~AudioExecution() = default;

bool AudioExecution::setControlWakeEvent(void* borrowedEvent) noexcept
{
#if defined(_WIN32)
    if (!borrowedEvent || impl_->controlWakeEvent || impl_->thread.joinable()
        || impl_->shutdownRequested.load(std::memory_order_acquire)
        || impl_->stopped.load(std::memory_order_acquire))
        return false;
    impl_->controlWakeEvent = borrowedEvent;
    return true;
#else
    (void)borrowedEvent;
    return false;
#endif
}

bool AudioExecution::start() noexcept
{
    if (impl_->thread.joinable() || impl_->stopped.load(std::memory_order_acquire)) return false;
#if defined(_WIN32)
    impl_->controlEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!impl_->controlEvent)
    {
        impl_->status.stopped = true;
        impl_->observation.publish(impl_->status);
        impl_->stopped.store(true, std::memory_order_release);
        impl_->wakeControl();
        return false;
    }
#endif
    try { impl_->thread = std::thread([state = impl_.get()] { state->run(); }); }
    catch (...)
    {
        impl_->status.stopped = true;
        impl_->observation.publish(impl_->status);
        impl_->stopped.store(true, std::memory_order_release);
        impl_->wakeControl();
        return false;
    }
    return true;
}

std::optional<AudioEffectTicket> AudioExecution::reserve(AudioEffectClass type,
    std::uint8_t mask, std::uint64_t group) noexcept
{
    auto& s = *impl_;
    if (!s.thread.joinable() || s.shutdownRequested.load(std::memory_order_acquire) || s.nextTicket == 0U) return std::nullopt;
    const auto bit = scalarMask(type);
    if (type == AudioEffectClass::Structural)
    {
        if (s.structuralReferences != 0U && (group == 0U || s.structuralGroup != group)) return std::nullopt;
        if (s.structuralReferences >= 8U) return std::nullopt;
    }
    else if ((s.scalarBusy & bit) != 0U || (s.structuralMask & bit) != 0U) return std::nullopt;
    for (std::size_t index = 0U; index < s.tickets.size(); ++index)
    {
        auto& record = s.tickets[index];
        if (record.state.load(std::memory_order_acquire) != TicketState::Free) continue;
        record.incarnation = s.nextTicket++;
        record.type = type;
        record.affectedMask = type == AudioEffectClass::Structural ? mask : bit;
        record.group = group != 0U ? group : record.incarnation;
        record.command = {};
        record.receipt = {};
        if (type == AudioEffectClass::Structural)
        {
            s.structuralGroup = record.group;
            ++s.structuralReferences;
            s.structuralMask |= mask;
        }
        else s.scalarBusy |= bit;
        record.state.store(TicketState::Reserved, std::memory_order_release);
        return AudioEffectTicket{static_cast<std::uint8_t>(index), record.incarnation};
    }
    return std::nullopt;
}

bool AudioExecution::enqueue(AudioEffectTicket ticket, const AudioCommand& command) noexcept
{
    auto& s = *impl_;
    if (!s.ticketMatches(ticket) || s.shutdownRequested.load(std::memory_order_acquire)) return false;
    auto& record = s.tickets[ticket.slot];
    if (record.state.load(std::memory_order_acquire) != TicketState::Reserved) return false;
    const auto write = s.queueWrite.load(std::memory_order_relaxed);
    if (write - s.queueRead.load(std::memory_order_acquire) >= EffectCapacity) return false;
    if ((command.kind == AudioCommandKind::InstallBgmPrepared
        || (command.kind == AudioCommandKind::ReplaceEmbedded && command.embeddedPresent))
        && (!command.prepared.valid() || !s.reserveSource(command.prepared.key))) return false;
    record.command = command;
    record.state.store(TicketState::Queued, std::memory_order_release);
    s.queue[write % EffectCapacity] = ticket.slot;
    s.queueWrite.store(write + 1U, std::memory_order_release);
    s.signal();
    return true;
}

bool AudioExecution::cancelReserved(AudioEffectTicket ticket) noexcept
{
    auto& s = *impl_;
    if (!s.ticketMatches(ticket)) return false;
    auto& record = s.tickets[ticket.slot];
    if (record.state.load(std::memory_order_acquire) != TicketState::Reserved) return false;
    record.receipt.ticket = ticket;
    record.receipt.result = AudioEffectResult::CancelledByRetirement;
    record.receipt.failure = AudioEffectFailure::None;
    record.state.store(TicketState::Done, std::memory_order_release);
    return true;
}

std::optional<AudioEffectReceipt> AudioExecution::receipt(AudioEffectTicket ticket) const noexcept
{
    if (!impl_->ticketMatches(ticket)) return std::nullopt;
    const auto& record = impl_->tickets[ticket.slot];
    if (record.state.load(std::memory_order_acquire) != TicketState::Done) return std::nullopt;
    return record.receipt;
}

bool AudioExecution::releaseReceipt(AudioEffectTicket ticket) noexcept
{
    auto& s = *impl_;
    if (!s.ticketMatches(ticket)) return false;
    auto& record = s.tickets[ticket.slot];
    if (record.state.load(std::memory_order_acquire) != TicketState::Done) return false;
    if (record.type == AudioEffectClass::Structural)
    {
        if (--s.structuralReferences == 0U) { s.structuralMask = 0U; s.structuralGroup = 0U; }
    }
    else s.scalarBusy &= static_cast<std::uint8_t>(~scalarMask(record.type));
    record.state.store(TicketState::Free, std::memory_order_release);
    return true;
}

bool AudioExecution::requestRetireSource(const AudioSourceKey& key) noexcept
{
    if (!impl_->sourceMatches(key)) return false;
    impl_->sources[key.slot].retireRequested.store(true, std::memory_order_release);
    impl_->signal();
    return true;
}

std::optional<AudioSourceObservation> AudioExecution::sourceObservation(const AudioSourceKey& key) const noexcept
{
    if (!impl_->sourceMatches(key)) return std::nullopt;
    const auto& s = impl_->sources[key.slot];
    AudioSourceObservation result{};
    result.source = key;
    result.registered = s.registered.load(std::memory_order_acquire);
    result.consumerQuiesced = s.consumerQuiesced.load(std::memory_order_acquire);
    result.terminal = s.terminal.load(std::memory_order_acquire);
    result.cursorFrames = s.cursor.load(std::memory_order_acquire);
    if (result.terminal != AudioSourceTerminal::None)
    {
        result.terminalSequence = s.terminalSequence;
        result.failureStage = s.failureStage;
        result.failureCode = s.failureCode;
    }
    return result;
}

bool AudioExecution::releaseSourceRecord(const AudioSourceKey& key) noexcept
{
    auto& s = *impl_;
    if (!s.sourceMatches(key) || !s.sources[key.slot].consumerQuiesced.load(std::memory_order_acquire)) return false;
    for (const auto& record : s.tickets)
    {
        if (record.state.load(std::memory_order_acquire) != TicketState::Queued) continue;
        if (record.command.source == key || record.command.prepared.key == key) return false;
    }
    s.sources[key.slot].state.store(SourceState::Free, std::memory_order_release);
    return true;
}

bool AudioExecution::requestRetirePlayback(std::uint64_t lifetime) noexcept
{
    if (lifetime == 0U) return false;
    auto& s = *impl_;
    if (s.shutdownRequested.load(std::memory_order_acquire))
        return s.playbackRetirementAck.load(std::memory_order_acquire) >= lifetime;
    const auto requested = s.playbackRetirementRequest.load(std::memory_order_relaxed);
    if (lifetime > requested) s.playbackRetirementRequest.store(lifetime, std::memory_order_release);
    if (!s.thread.joinable())
        s.playbackRetirementAck.store(lifetime, std::memory_order_release);
    s.signal();
    return true;
}

std::uint64_t AudioExecution::retiredPlaybackLifetime() const noexcept
{
    return impl_->playbackRetirementAck.load(std::memory_order_acquire);
}

std::optional<std::uint64_t> AudioExecution::requestEmbeddedFailureGate(
    const AudioClockAnchor& anchor, double failedTarget, bool hold) noexcept
{
    auto& s = *impl_;
    if (!anchor.valid() || !std::isfinite(failedTarget) || failedTarget < 0.0
        || s.shutdownRequested.load(std::memory_order_acquire)
        || s.failureGate.state.load(std::memory_order_acquire) != TicketState::Free
        || s.nextFailureGate == 0U) return std::nullopt;
    auto& gate = s.failureGate;
    gate.request = s.nextFailureGate++;
    gate.anchor = anchor;
    gate.failedTarget = failedTarget;
    gate.hold = hold;
    gate.receipt = {};
    gate.state.store(TicketState::Queued, std::memory_order_release);
    s.signal();
    return gate.request;
}

std::optional<AudioEffectReceipt> AudioExecution::embeddedGateReceipt(std::uint64_t request) const noexcept
{
    const auto& gate = impl_->failureGate;
    if (gate.state.load(std::memory_order_acquire) != TicketState::Done || gate.request != request)
        return std::nullopt;
    return gate.receipt;
}

bool AudioExecution::releaseEmbeddedGateReceipt(std::uint64_t request) noexcept
{
    auto& gate = impl_->failureGate;
    if (gate.state.load(std::memory_order_acquire) != TicketState::Done || gate.request != request) return false;
    gate.state.store(TicketState::Free, std::memory_order_release);
    return true;
}

void AudioExecution::requestShutdown() noexcept
{
    impl_->shutdownRequested.store(true, std::memory_order_release);
    if (!impl_->thread.joinable())
    {
        impl_->status.stopped = true;
        impl_->observation.publish(impl_->status);
        impl_->stopped.store(true, std::memory_order_release);
        impl_->wakeControl();
    }
    impl_->signal();
}
bool AudioExecution::shutdownComplete() const noexcept { return impl_->stopped.load(std::memory_order_acquire); }
bool AudioExecution::joinIfStopped() noexcept
{
    if (!shutdownComplete()) return false;
    if (impl_->thread.joinable()) impl_->thread.join();
    return true;
}
AudioExecutionSnapshot AudioExecution::snapshot() const noexcept { return impl_->observation.read(); }

}
