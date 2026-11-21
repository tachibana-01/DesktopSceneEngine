#include "Audio/AudioExecution.h"
#include "Audio/AudioExecutionTestDevice.h"
#include "Audio/AudioPcmPipe.h"

#include <array>
#include <atomic>
#include <cmath>
#include <iostream>
#include <memory>
#include <span>
#include <string_view>
#include <thread>
#include <vector>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

namespace
{
using namespace DesktopSceneEngine::Audio;

bool require(bool value, const char* message)
{
    if (!value) std::cerr << "FAIL: " << message << '\n';
    return value;
}

template <typename Predicate>
bool await(Predicate predicate, unsigned milliseconds = 2000U)
{
    const auto end = audioMonotonicNow() + static_cast<AudioMonotonicTime>(milliseconds) * 1'000'000LL;
    do
    {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (audioMonotonicNow() < end);
    return predicate();
}

// Emulates bounded device queue consumption while exercising the production
// A loop, mixer, bridge, command records, clocks and retirement. It proves no
// real-device continuity claim; the actual Player proof is a separate gate.
class TestDevice final : public AudioExecutionTestDevice
{
public:
    enum class StopFault { None, StopDeviceFailure, ResetDeviceFailure, ResetContractFailure };
    std::atomic_bool ownerCorrect{true};
    std::atomic_bool failNextWrite{false};
    std::atomic_bool rejectOpen{false};
    std::atomic<std::uint64_t> opens{0U};
    std::atomic<AudioMonotonicTime> lastOpen{0};
    std::atomic<AudioDeviceMuteResult> muteResult{AudioDeviceMuteResult::Applied};
    std::atomic_bool mutedResume{false};
    std::atomic_bool muteWhilePausedWithQueue{false};
    std::atomic_uint64_t playedFrames{0}, discardedFrames{0}, stops{0};
    std::atomic_uint64_t maximumQueuedFrames{0};
    std::atomic_bool blockWrite{false}, writeEntered{false};
    std::atomic_bool failWriteAfterMix{false};
    std::atomic_bool requireMutedStart{false}, startedUnmutedWhileRequired{false};
    std::atomic<std::uint64_t> pauses{0U}, resumes{0U};
    std::atomic_bool resumedRetainedQueue{false};
    std::atomic<unsigned> configureDelayMs{0U};
    std::atomic<AudioMonotonicTime> lastConfigureCompleted{0};
    std::atomic<StopFault> nextStopFault{StopFault::None};
    std::atomic<AudioBlockStatus> availabilityCleanupFailure{AudioBlockStatus::Success};
    std::atomic<std::uint64_t> checkedStops{0U}, checkedResets{0U}, closes{0U};
    std::atomic<std::uint64_t> generation{0U};

    bool open() noexcept override
    {
        touch();
        ++opens;
        lastOpen.store(audioMonotonicNow());
        opened_ = !rejectOpen.load();
        queued_ = 0.0;
        last_ = audioMonotonicNow();
        return opened_;
    }
    void close() noexcept override { touch(); ++closes; opened_ = false; running_ = false; queued_ = 0.0; }
    AudioBlockStatus configure(bool paused) noexcept override
    {
        touch();
        if (!opened_) return AudioBlockStatus::NotInitialized;
        if (const auto delay = configureDelayMs.exchange(0U); delay != 0U)
            std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        if (!paused && requireMutedStart.load() && !muted_) startedUnmutedWhileRequired.store(true);
        paused_ = paused; running_ = !paused; last_ = audioMonotonicNow();
        lastConfigureCompleted.store(last_);
        return AudioBlockStatus::Success;
    }
    AudioBlockStatus pause() noexcept override
    {
        touch(); advance(); ++pauses; paused_ = true; running_ = false;
        return AudioBlockStatus::Success;
    }
    AudioBlockStatus resume() noexcept override
    {
        touch();
        ++resumes;
        if (paused_ && queued_ > 0.0) resumedRetainedQueue.store(true);
        if (muted_ && paused_ && queued_ > 0.0) mutedResume.store(true);
        paused_ = false; running_ = true; last_ = audioMonotonicNow();
        return AudioBlockStatus::Success;
    }
    void stop() noexcept override
    { touch(); advance(); ++stops; discardedFrames.fetch_add(static_cast<std::uint64_t>(queued_));
        running_ = false; paused_ = false; queued_ = 0.0; underflows_ = 0U; }
    AudioBlockStatus stopAndResetChecked() noexcept override
    {
        touch(); ++checkedStops;
        const auto fault = nextStopFault.exchange(StopFault::None);
        if (fault == StopFault::StopDeviceFailure) return AudioBlockStatus::DeviceFailure;
        advance(); running_ = false; paused_ = false;
        ++checkedResets;
        if (fault == StopFault::ResetDeviceFailure) return AudioBlockStatus::DeviceFailure;
        if (fault == StopFault::ResetContractFailure) return AudioBlockStatus::WrongMode;
        stop();
        return AudioBlockStatus::Success;
    }
    AudioDeviceMuteResult setMuted(bool muted) noexcept override
    {
        touch();
        const auto result = muteResult.load();
        if (result == AudioDeviceMuteResult::Applied)
        {
            if (muted && paused_ && queued_ > 0.0) muteWhilePausedWithQueue.store(true);
            muted_ = muted;
        }
        return result;
    }
    AudioBlockAvailability writableFrames() noexcept override
    {
        touch();
        if (const auto failure = availabilityCleanupFailure.exchange(AudioBlockStatus::Success);
            failure != AudioBlockStatus::Success)
        {
            nextStopFault.store(StopFault::ResetContractFailure);
            (void)stopAndResetChecked();
            close();
            return {0U, failure};
        }
        if (failNextWrite.exchange(false)) return {0U, AudioBlockStatus::DeviceFailure};
        if (!opened_) return {};
        if (paused_) return {0U, AudioBlockStatus::Paused};
        advance();
        return {4800U - static_cast<std::size_t>(queued_), AudioBlockStatus::Success};
    }
    AudioBlockStatus writeFrames(std::span<const std::int16_t> samples) noexcept override
    {
        if (blockWrite.load())
        {
            writeEntered.store(true);
            // A bounded fake native call lets the test place a Stop while A
            // is in that call. This is not a production control/producer wait.
            const auto end = audioMonotonicNow() + 500'000'000LL;
            while (blockWrite.load() && audioMonotonicNow() < end) std::this_thread::yield();
        }
        touch(); advance();
        if (failWriteAfterMix.exchange(false)) return AudioBlockStatus::DeviceFailure;
        const auto frames = samples.size() / 2U;
        if (frames > 4800U - static_cast<std::size_t>(queued_)) return AudioBlockStatus::NotWritable;
        if (queued_ == 0.0 && written_) ++underflows_;
        queued_ += static_cast<double>(frames);
        maximumQueuedFrames.store((std::max)(maximumQueuedFrames.load(), static_cast<std::uint64_t>(queued_)));
        written_ = true;
        return AudioBlockStatus::Success;
    }
    std::size_t bufferFrames() const noexcept override { return 4800U; }
    std::uint64_t queuedFrames() const noexcept override
    { touch(); advance(); return static_cast<std::uint64_t>(queued_); }
    std::uint64_t underflowCount() const noexcept override { return underflows_; }
    std::uint64_t endpointGeneration() const noexcept override { return generation.load(); }
private:
    void touch() const noexcept
    {
        const auto here = std::this_thread::get_id();
        if (owner_ == std::thread::id{}) owner_ = here;
        else if (owner_ != here) const_cast<TestDevice*>(this)->ownerCorrect.store(false);
    }
    void advance() const noexcept
    {
        const auto now = audioMonotonicNow();
        if (running_)
        {
            const auto consumed = (std::min)(queued_, static_cast<double>(now - last_) / 1'000'000'000.0 * 48'000.0);
            played_ += consumed; queued_ -= consumed;
            const_cast<TestDevice*>(this)->playedFrames.store(static_cast<std::uint64_t>(played_));
        }
        last_ = now;
    }
    mutable std::thread::id owner_{};
    mutable double queued_ = 0.0;
    mutable AudioMonotonicTime last_ = 0;
    mutable double played_ = 0;
    bool opened_ = false;
    bool running_ = false;
    bool paused_ = false;
    bool muted_ = false;
    bool written_ = false;
    std::uint64_t underflows_ = 0U;
};

struct ExecutionFixture
{
    TestDevice device;
    AudioExecution execution{device};
    std::vector<std::int16_t> pcm = std::vector<std::int16_t>(48'000U * 2U * 10U, 3276);
    const AudioSourceKey original{AudioLane::Embedded, 0U, 1U, 1U, 0U, 1U};

    bool start()
    {
        if (!execution.start()) return false;
        AudioCommand command{};
        command.kind = AudioCommandKind::ReplaceEmbedded;
        command.source = original;
        command.prepared = {original, pcm.data(), pcm.size(), 1.0, 10.0, nullptr};
        command.clock = {1U, 1U, 0.0, audioMonotonicNow(), 1.0, 10.0, AudioClockMode::Running};
        command.transport = AudioTransport::Playing;
        return send(command).has_value()
            && await([&] { return execution.snapshot().writtenFrames > 4800U; });
    }
    std::optional<AudioEffectReceipt> send(const AudioCommand& command,
        AudioEffectClass type = AudioEffectClass::Structural, std::uint8_t mask = 0x0FU)
    {
        auto identified = command;
        if ((identified.kind == AudioCommandKind::ReplaceEmbedded || identified.kind == AudioCommandKind::SetTransport)
            && identified.playbackLifetime == 0) identified.playbackLifetime = 1;
        const auto ticket = execution.reserve(type, mask);
        if (!ticket || !execution.enqueue(*ticket, identified)) return std::nullopt;
        if (!await([&] { return execution.receipt(*ticket).has_value(); })) return std::nullopt;
        const auto result = execution.receipt(*ticket);
        if (!execution.releaseReceipt(*ticket)) return std::nullopt;
        return result;
    }
    ~ExecutionFixture()
    {
        execution.requestShutdown();
        (void)await([&] { return execution.shutdownComplete(); });
        (void)execution.joinIfStopped();
    }
};

#if defined(_WIN32)
bool controlFactWake()
{
    const std::unique_ptr<void, decltype(&CloseHandle)> wake{CreateEventW(nullptr, FALSE, FALSE, nullptr), CloseHandle};
    AudioPcmPipe pipe;
    ExecutionFixture f;
    auto& execution = f.execution;
    if (!require(wake && execution.setControlWakeEvent(wake.get()) && execution.start(),
            "C installs its event before the physical owner starts")
        || !require(!execution.setControlWakeEvent(wake.get()), "A rejects a late event assignment")
        || !require(WaitForSingleObject(wake.get(), 2000) == WAIT_OBJECT_0 && execution.snapshot().started,
            "start wakes C after the detached snapshot is published")) return false;

    const auto sendAndWake = [&](const AudioCommand& command) {
        const auto ticket = execution.reserve(AudioEffectClass::Structural);
        if (!ticket || !execution.enqueue(*ticket, command)) return false;
        if (WaitForSingleObject(wake.get(), 2000) != WAIT_OBJECT_0) return false;
        const auto receipt = execution.receipt(*ticket);
        return receipt && receipt->result == AudioEffectResult::Applied
            && receipt->sequence <= execution.snapshot().lastEffectSequence && execution.releaseReceipt(*ticket);
    };
    AudioCommand bind{};
    bind.kind = AudioCommandKind::ReplaceEmbedded;
    bind.embeddedPresent = false;
    bind.playbackLifetime = 1;
    bind.clock = {1, 1, 0, audioMonotonicNow(), 1, 10, AudioClockMode::Running};
    bind.transport = AudioTransport::Playing;
    if (!require(sendAndWake(bind), "a command wake makes both the receipt and sequence frontier available"))
        return false;

    constexpr std::size_t frames = AudioPcmPipe::CapacityFrames;
    const std::vector<float> pcm(frames * 2U, 0.1F);
    const AudioSourceKey key{AudioLane::PlaylistBgm, 2, 1, 1, 71, 0};
    AudioCommand install{};
    install.kind = AudioCommandKind::InstallBgmPrepared;
    install.source = key;
    install.prepared = {key, nullptr, 0, 1, 0, &pipe};
    if (!require(pipe.writeFrames(pcm) == frames && sendAndWake(install),
        "prepared stream registration also wakes at the published frontier")) return false;
    const auto before = execution.snapshot().writtenFrames;
    if (!require(WaitForSingleObject(wake.get(), 70) == WAIT_TIMEOUT
        && execution.snapshot().writtenFrames > before && f.device.ownerCorrect.load(),
        "steady refill progresses on A without signaling C for each block")) return false;

    if (!require(pipe.publishTerminal(AudioPcmReadStatus::EndOfStream)
        && WaitForSingleObject(wake.get(), 2000) == WAIT_OBJECT_0,
        "A wakes C for a consumed terminal")) return false;
    const auto terminal = execution.sourceObservation(key);
    if (!require(terminal && terminal->consumerQuiesced && terminal->terminal == AudioSourceTerminal::Ended
        && terminal->terminalSequence <= execution.snapshot().lastEffectSequence
        && execution.releaseSourceRecord(key), "terminal wake carries durable Ended and consumer quiescence"))
        return false;
    if (!require(execution.requestRetirePlayback(1) && WaitForSingleObject(wake.get(), 2000) == WAIT_OBJECT_0
        && execution.retiredPlaybackLifetime() == 1, "Stop barrier wakes C even without an ordinary effect receipt"))
        return false;
    execution.requestShutdown();
    return require(WaitForSingleObject(wake.get(), 2000) == WAIT_OBJECT_0 && execution.shutdownComplete()
        && execution.joinIfStopped(), "control receives a wake for final A shutdown");
}
#endif

bool clockAndPartialInitialization()
{
    const AudioClockAnchor anchor{1U, 1U, 1.25, 1'000'000'000LL, 1.25, 20.0, AudioClockMode::Running};
    if (!require(std::abs(resolveAudioClockPosition(anchor, 5'000'000'000LL) - 6.25) < 1e-9,
        "logical time keeps full elapsed and non-step rate")) return false;
    AudioExecution neverStarted;
    neverStarted.requestShutdown();
    return require(neverStarted.shutdownComplete() && neverStarted.joinIfStopped(),
        "never-started execution can actually retire");
}

bool boundedAdmission()
{
    TestDevice device;
    AudioExecution execution(device);
    if (!require(execution.start(), "execution starts")) return false;
    std::array<AudioEffectTicket, 8U> group{};
    for (auto& ticket : group)
    {
        const auto reserved = execution.reserve(AudioEffectClass::Structural, 1U, 42U);
        if (!require(reserved.has_value(), "same structural group reserves all eight effects")) return false;
        ticket = *reserved;
    }
    if (!require(!execution.reserve(AudioEffectClass::Structural, 1U, 42U), "ninth group ticket is rejected")
        || !require(!execution.reserve(AudioEffectClass::Structural, 2U, 43U), "second structural group is rejected")
        || !require(!execution.reserve(AudioEffectClass::EmbeddedGain), "affected scalar cannot cross barrier")) return false;
    const auto unaffected = execution.reserve(AudioEffectClass::PlaylistGain);
    if (!require(unaffected.has_value() && !execution.reserve(AudioEffectClass::PlaylistGain),
        "unaffected lane has exactly one scalar credit")) return false;
    for (const auto ticket : group)
        if (!require(execution.cancelReserved(ticket) && execution.releaseReceipt(ticket), "unused group credit drains")) return false;
    if (!require(execution.cancelReserved(*unaffected) && execution.releaseReceipt(*unaffected), "scalar credit drains")) return false;
    execution.requestShutdown();
    return require(await([&] { return execution.shutdownComplete(); }) && execution.joinIfStopped(), "bounded owner exits");
}

bool unconsumedReceiptsNeverBlockRefill()
{
    ExecutionFixture f;
    if (!require(f.start(), "receipt pressure source starts")) return false;
    std::array<AudioEffectTicket, 8> tickets{};
    const auto clock = f.execution.snapshot().clock;
    AudioCommand command{}; command.kind = AudioCommandKind::SetTransport;
    command.clock = clock; command.clock.timingVersion += 1; command.playbackLifetime = 1;
    command.transport = AudioTransport::Playing;
    for (auto& ticket : tickets)
    {
        const auto credit = f.execution.reserve(AudioEffectClass::Structural, 1, 109);
        if (!require(credit.has_value(), "bounded full structural group reserves")) return false;
        ticket = *credit;
        if (!require(f.execution.enqueue(ticket, command), "pressure command enqueued")) return false;
    }
    if (!require(await([&] { for (const auto ticket : tickets) if (!f.execution.receipt(ticket)) return false; return true; }),
        "all eight durable receipts arrive without a separate completion queue")) return false;
    const auto before = f.execution.snapshot();
    std::this_thread::sleep_for(std::chrono::milliseconds(260)); // Do not drain C receipts.
    const auto after = f.execution.snapshot();
    const bool healthy = after.writtenFrames > before.writtenFrames + 9000
        && after.sourceUnderruns == 0 && after.maximumWriteGap < 90'000'000LL
        && !f.execution.reserve(AudioEffectClass::Structural, 1, 109);
    for (const auto ticket : tickets)
        if (!require(f.execution.receipt(ticket).has_value() && f.execution.releaseReceipt(ticket), "unconsumed terminal is retained exactly until C release")) return false;
    return require(healthy, "full undrained result credits cannot wait or starve A refill");
}

bool boundedRenderHorizon()
{
    ExecutionFixture fixture;
    if (!require(fixture.start(), "Render horizon fixture starts the audio owner")) return false;
    const auto start = fixture.execution.snapshot();
    if (!require(await([&] {
        return fixture.execution.snapshot().writtenFrames >= start.writtenFrames + 4800U;
    }), "The device continues to be refilled under its render occupancy target")) return false;

    const auto current = fixture.execution.snapshot();
    return require(current.bufferFrames == 4800U && fixture.device.maximumQueuedFrames.load() > 0U
        && fixture.device.maximumQueuedFrames.load() <= 4U * 480U && current.outputUnderflows == 0U,
        "Refill preserves physical capacity and never queues beyond four device periods");
}

bool independentServiceAndPauseMute()
{
    ExecutionFixture f;
    if (!require(f.start(), "prepared embedded source starts on A")) return false;
    const auto before = f.execution.snapshot();
    // C does no pumping, dispatching or fact consumption during this interval.
    std::this_thread::sleep_for(std::chrono::milliseconds(260));
    const auto after = f.execution.snapshot();
    if (!require(after.writtenFrames > before.writtenFrames + 9000U
        && after.sourceUnderruns == 0U && after.maximumWriteGap < 90'000'000LL,
        "A refills beyond device horizon while C is stalled")) return false;
    AudioCommand pause{};
    pause.kind = AudioCommandKind::SetTransport;
    pause.clock = after.clock; pause.clock.timingVersion = 2U;
    pause.expectedTimingVersion = 1U;
    pause.transport = AudioTransport::Paused;
    const auto paused = f.send(pause);
    if (!require(paused && paused->result == AudioEffectResult::Applied,
        "Pause commits a durable effect timestamp")) return false;
    const auto sourceBefore = f.execution.sourceObservation(f.original);
    std::this_thread::sleep_for(std::chrono::milliseconds(70));
    const auto sourceAfter = f.execution.sourceObservation(f.original);
    if (!require(sourceBefore && sourceAfter && sourceBefore->cursorFrames == sourceAfter->cursorFrames,
        "Pause retains input and freezes consumption")) return false;
    AudioCommand mute{}; mute.kind = AudioCommandKind::SetOutputMute; mute.muted = true;
    const auto muted = f.send(mute, AudioEffectClass::OutputMute);
    if (!require(muted && muted->result == AudioEffectResult::Applied
        && f.device.muteWhilePausedWithQueue.load(), "checked stream mute covers retained paused queue")) return false;
    AudioCommand resume = pause;
    resume.clock = paused->clock; resume.clock.timingVersion = 3U;
    resume.expectedTimingVersion = 2U; resume.transport = AudioTransport::Playing;
    const auto resumed = f.send(resume);
    return require(resumed && resumed->result == AudioEffectResult::Applied
        && f.device.mutedResume.load() && f.device.ownerCorrect.load(),
        "Resume preserves queue/session and all device mutations stay on A");
}

bool rateContinuityAndReservedFailureHold()
{
    std::vector<std::int16_t> tempo(48'000U * 2U * 5U, 3276);
    ExecutionFixture f;
    if (!require(f.start(), "rate fixture starts")) return false;
    const auto oldClock = f.execution.snapshot().clock;
    std::this_thread::sleep_for(std::chrono::milliseconds(180)); // preparation continues old playback.
    const AudioSourceKey replacement{AudioLane::Embedded, 1U, 1U, 1U, 0U, 1U};
    AudioCommand rate{};
    rate.kind = AudioCommandKind::ReplaceEmbedded;
    rate.prepared = {replacement, tempo.data(), tempo.size(), 2.0, 10.0, nullptr};
    rate.source = replacement;
    rate.expectedTimingVersion = oldClock.timingVersion;
    rate.clock = oldClock; rate.clock.timingVersion = 2U; rate.clock.rate = 2.0;
    rate.transport = AudioTransport::Playing;
    rate.replacement = AudioEmbeddedReplacement::RateContinuous;
    const auto changed = f.send(rate);
    if (!require(changed && changed->result == AudioEffectResult::Applied
        && changed->clock.position > oldClock.position + 0.15
        && std::abs(changed->clock.position - resolveAudioClockPosition(oldClock, changed->effectTime)) < 0.002,
        "RateContinuous uses old anchor at actual effect, not prepare start")) return false;
    if (!require(f.execution.sourceObservation(f.original)->consumerQuiesced,
        "embedded replacement acknowledges no future reads of old asset")) return false;

    std::array<AudioEffectTicket, 8U> full{};
    for (auto& ticket : full)
    {
        const auto reserve = f.execution.reserve(AudioEffectClass::Structural, 0x0FU, 99U);
        if (!require(reserve.has_value(), "full structural reservation succeeds")) return false;
        ticket = *reserve;
    }
    auto hold = changed->clock;
    hold.timingVersion = 3U; hold.position = 0.1; hold.mode = AudioClockMode::FailureHold;
    const auto request = f.execution.requestEmbeddedFailureGate(hold, 0.2, true);
    if (!require(request && await([&] { return f.execution.embeddedGateReceipt(*request).has_value(); }),
        "FailureHold bypasses exhausted ordinary admission")) return false;
    const auto held = f.execution.embeddedGateReceipt(*request);
    const auto cursor = f.execution.sourceObservation(replacement)->cursorFrames;
    std::this_thread::sleep_for(std::chrono::milliseconds(70));
    if (!require(held->result == AudioEffectResult::Applied
        && held->clock.mode == AudioClockMode::FailureHold
        && f.execution.snapshot().transport == AudioTransport::Playing
        && f.execution.sourceObservation(replacement)->cursorFrames == cursor,
        "FailureHold gates embedded without product Pause")) return false;
    AudioCommand stale = rate; stale.expectedTimingVersion = 2U;
    if (!require(f.execution.enqueue(full[0], stale)
        && await([&] { return f.execution.receipt(full[0]).has_value(); })
        && f.execution.receipt(full[0])->failure == AudioEffectFailure::TimingChanged,
        "pre-hold replacement cannot overwrite newer timing")) return false;
    if (!require(f.execution.releaseReceipt(full[0]) && f.execution.releaseEmbeddedGateReceipt(*request), "reserved receipts drain")) return false;
    for (std::size_t i = 1U; i < full.size(); ++i)
        if (!require(f.execution.cancelReserved(full[i]) && f.execution.releaseReceipt(full[i]), "remaining reservation releases")) return false;
    return true;
}

bool supplyAndDurableTerminal()
{
    AudioPcmPipe pipe;
    ExecutionFixture f;
    if (!require(f.start(), "BGM fixture starts")) return false;
    const std::vector<float> prime(9600U * 2U, 0.1F);
    if (!require(pipe.writeFrames(prime) == 9600U, "producer primes bounded pipe")) return false;
    const AudioSourceKey key{AudioLane::PlaylistBgm, 2U, 1U, 1U, 55U, 0U};
    AudioCommand install{}; install.kind = AudioCommandKind::InstallBgmPrepared;
    install.source = key; install.prepared = {key, nullptr, 0U, 1.0, 0.0, &pipe};
    const auto installed = f.send(install);
    if (!require(installed && installed->result == AudioEffectResult::Applied, "BGM registration commits")) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(350));
    const auto empty = f.execution.sourceObservation(key);
    if (!require(empty && empty->terminal == AudioSourceTerminal::None
        && !empty->consumerQuiesced && f.execution.snapshot().sourceUnderruns > 0U,
        "TemporaryEmpty is observable underrun, not Ended or track failure")) return false;
    const std::array<float, 200U> tail{};
    if (!require(pipe.writeFrames(tail) == 100U && pipe.publishTerminal(AudioPcmReadStatus::EndOfStream),
        "producer publishes finite tail and EOF")) return false;
    if (!require(await([&] { const auto s = f.execution.sourceObservation(key); return s && s->consumerQuiesced; }),
        "terminal consumption exits all A references")) return false;
    const auto terminal = f.execution.sourceObservation(key);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const auto retained = f.execution.sourceObservation(key);
    return require(terminal && retained && terminal->terminal == AudioSourceTerminal::Ended
        && terminal->cursorFrames == 9700U && retained->terminalSequence == terminal->terminalSequence
        && f.execution.releaseSourceRecord(key), "Ended/no-more-reads remain durable until explicit release");
}

bool deviceRecoveryAndDeferredMute()
{
    ExecutionFixture f;
    if (!require(f.start(), "recovery fixture starts")) return false;
    const auto opens = f.device.opens.load();
    const auto failAt = audioMonotonicNow();
    f.device.failNextWrite.store(true);
    if (!require(await([&] { return f.execution.snapshot().health == AudioOutputHealth::DeviceUnavailable; }),
        "device failure becomes unavailable")) return false;
    AudioCommand pause{}; pause.kind = AudioCommandKind::SetTransport;
    pause.clock = f.execution.snapshot().clock; pause.clock.timingVersion = 2U;
    pause.expectedTimingVersion = 1U; pause.transport = AudioTransport::Paused;
    if (!require(f.send(pause).has_value(), "Pause can commit while device is unavailable")) return false;
    AudioCommand mute{}; mute.kind = AudioCommandKind::SetOutputMute; mute.muted = true;
    const auto deferred = f.send(mute, AudioEffectClass::OutputMute);
    if (!require(deferred && deferred->result == AudioEffectResult::DeferredUnavailable,
        "no-device mute is DeferredUnavailable, not fake Applied")) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    if (!require(f.device.opens.load() == opens, "output does not retry before native cooldown")) return false;
    if (!require(await([&] { return f.execution.snapshot().recoveredCount != 0U; }), "output recovers without C pumping")) return false;
    const auto recovered = f.execution.snapshot();
    const auto first = f.execution.sourceObservation(f.original)->cursorFrames;
    std::this_thread::sleep_for(std::chrono::milliseconds(70));
    return require(f.device.lastOpen.load() - failAt >= 490'000'000LL
        && recovered.transport == AudioTransport::Paused && recovered.muteApplied && recovered.muteKnown
        && f.execution.sourceObservation(f.original)->cursorFrames == first,
        "recovery applies desired mute before Start and honors unread Pause receipt timing");
}

bool checkedMuteFaultAndNoRecovery()
{
    ExecutionFixture f;
    if (!require(f.start(), "mute fault fixture starts")) return false;
    f.device.muteResult.store(AudioDeviceMuteResult::ContractFailure);
    AudioCommand mute{}; mute.kind = AudioCommandKind::SetOutputMute; mute.muted = true;
    const auto failed = f.send(mute, AudioEffectClass::OutputMute);
    if (!require(failed && failed->result == AudioEffectResult::Failed
        && f.execution.snapshot().health == AudioOutputHealth::ContractFaulted,
        "unknown stream-mute failure safely stops untrusted output")) return false;
    const auto opens = f.device.opens.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(550));
    return require(f.device.opens.load() == opens && f.execution.sourceObservation(f.original).has_value(),
        "ContractFaulted remains latched without source destruction/retry");
}

bool reservedStopAndLateOldLifetime()
{
    ExecutionFixture f;
    if (!require(f.start(), "Stop fixture starts")) return false;
    const auto original = f.execution.snapshot();
    if (!require(original.laneSources[0] == f.original && original.laneCursorFrames[0] > 0U,
        "detached lane identity/cursor belong to one publication")) return false;
    if (!require(f.execution.requestRetirePlayback(1U)
        && await([&] { return f.execution.retiredPlaybackLifetime() >= 1U; }),
        "reserved Stop has real physical retirement acknowledgement")) return false;
    if (!require(f.execution.sourceObservation(f.original)->consumerQuiesced,
        "Stop acknowledgement follows no-more-reads")) return false;

    const AudioSourceKey next{AudioLane::Embedded, 1U, 2U, 2U, 0U, 2U};
    AudioCommand bind{}; bind.kind = AudioCommandKind::ReplaceEmbedded;
    bind.source = next; bind.prepared = {next, f.pcm.data(), f.pcm.size(), 1.0, 10.0, nullptr};
    bind.playbackLifetime = 2U;
    bind.clock = {2U, 2U, 0.0, audioMonotonicNow(), 1.0, 10.0, AudioClockMode::Running};
    bind.transport = AudioTransport::Playing;
    const auto applied = f.send(bind);
    if (!require(applied && applied->result == AudioEffectResult::Applied,
        "new playback lifetime installs")) return false;
    if (!require(f.execution.requestRetirePlayback(1U), "repeated old Stop is idempotent")) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    if (!require(f.execution.snapshot().transport == AudioTransport::Playing
        && f.execution.snapshot().laneSources[0] == next, "old Stop cannot retire new playback")) return false;
    const auto opens = f.device.opens.load();
    ++f.device.generation;
    if (!require(await([&] { return f.device.opens.load() > opens; }), "native A policy reroutes endpoint generation")) return false;
    return require(f.execution.snapshot().laneSources[0] == next,
        "endpoint reroute preserves source identity");
}

bool optionalMuteCapability()
{
    ExecutionFixture f;
    f.device.muteResult.store(AudioDeviceMuteResult::Unavailable);
    if (!require(f.start() && f.execution.snapshot().health == AudioOutputHealth::Healthy,
        "ordinary initial unmuted output does not require an optional mute capability")) return false;
    AudioCommand mute{}; mute.kind = AudioCommandKind::SetOutputMute; mute.muted = true;
    const auto result = f.send(mute, AudioEffectClass::OutputMute);
    return require(result && result->result == AudioEffectResult::Failed
        && result->failure == AudioEffectFailure::MuteUnavailable && !result->muteDesired && !result->muteApplied
        && f.execution.snapshot().health == AudioOutputHealth::Healthy,
        "missing mute-on capability is explicit Unavailable, without fake confirmed mute or unrelated output fault");
}

bool lastSourceTailDrains()
{
    AudioPcmPipe pipe;
    ExecutionFixture f;
    if (!require(f.execution.start(), "short source owner starts")) return false;
    AudioCommand bind{}; bind.kind = AudioCommandKind::ReplaceEmbedded; bind.embeddedPresent = false;
    bind.clock = {1, 1, 0, audioMonotonicNow(), 1, 10, AudioClockMode::Running};
    bind.transport = AudioTransport::Playing;
    if (!require(f.send(bind).has_value(), "video-only transport commits")) return false;
    const std::array<float, 960> pcm{};
    if (!require(pipe.writeFrames(pcm) == 480 && pipe.publishTerminal(AudioPcmReadStatus::EndOfStream), "short real prefix sealed")) return false;
    const AudioSourceKey key{AudioLane::PlaylistBgm, 2, 1, 1, 71, 0};
    AudioCommand start{}; start.kind = AudioCommandKind::InstallBgmPrepared; start.source = key;
    start.prepared = {key, nullptr, 0, 1, 0, &pipe};
    if (!require(f.send(start).has_value(), "last short source installed")) return false;
    if (!require(await([&] { const auto source = f.execution.sourceObservation(key);
        return source && source->terminal == AudioSourceTerminal::Ended && source->consumerQuiesced && f.device.stops.load() > 0; }),
        "last source terminal and eventual device drain are observable")) return false;
    return require(f.device.playedFrames.load() >= 480 && f.device.discardedFrames.load() == 0,
        "last source EOF must drain its submitted valid tail, rather than reset/drop it");
}

bool stopRevokesQueuedTiming()
{
    ExecutionFixture f;
    if (!require(f.start(), "queued Stop fixture starts")) return false;
    f.device.blockWrite.store(true);
    if (!require(await([&] { return f.device.writeEntered.load(); }), "bounded native write entered")) return false;
    const auto clock = f.execution.snapshot().clock;
    const auto first = f.execution.reserve(AudioEffectClass::Structural, 15, 77);
    const auto second = f.execution.reserve(AudioEffectClass::Structural, 15, 77);
    AudioCommand resume{}; resume.kind = AudioCommandKind::SetTransport; resume.clock = clock;
    resume.clock.timingVersion = clock.timingVersion + 1; resume.expectedTimingVersion = clock.timingVersion;
    resume.playbackLifetime = 1; resume.transport = AudioTransport::Playing;
    AudioCommand absent = resume; absent.kind = AudioCommandKind::ReplaceEmbedded; absent.embeddedPresent = false;
    absent.clock.timingVersion += 1; absent.expectedTimingVersion = 0;
    const bool queued = first && second && f.execution.enqueue(*first, resume) && f.execution.enqueue(*second, absent)
        && f.execution.requestRetirePlayback(1);
    f.device.blockWrite.store(false);
    if (!require(queued && await([&] { return f.execution.receipt(*first) && f.execution.receipt(*second)
        && f.execution.retiredPlaybackLifetime() >= 1; }), "Stop and both timing results complete")) return false;
    const auto a = f.execution.receipt(*first), b = f.execution.receipt(*second);
    const bool correct = a->result == AudioEffectResult::CancelledByRetirement
        && b->result == AudioEffectResult::CancelledByRetirement && f.execution.snapshot().transport == AudioTransport::Stopped;
    (void)f.execution.releaseReceipt(*first); (void)f.execution.releaseReceipt(*second);
    return require(correct, "reserved Stop revokes queued old Resume and absent-source Bind before either can report Applied");
}

bool terminalSurvivesWriteFailure()
{
    for (const auto terminal : {AudioPcmReadStatus::EndOfStream, AudioPcmReadStatus::Failure})
    {
        AudioPcmPipe pipe;
        ExecutionFixture f;
        if (!require(f.execution.start(), "failure terminal owner starts")) return false;
        AudioCommand bind{}; bind.kind = AudioCommandKind::ReplaceEmbedded; bind.embeddedPresent = false;
        bind.clock = {1, 1, 0, audioMonotonicNow(), 1, 10, AudioClockMode::Running};
        bind.transport = AudioTransport::Playing;
        if (!require(f.send(bind).has_value(), "failure terminal transport commits")) return false;
        const std::array<float, 960> prefix{};
        if (!require(pipe.writeFrames(prefix) == 480 && pipe.publishTerminal(terminal), "failed final prefix sealed")) return false;
        const AudioSourceKey key{AudioLane::PlaylistBgm, 2, 1, 1, 72, 0};
        AudioCommand install{}; install.kind = AudioCommandKind::InstallBgmPrepared; install.source = key;
        install.prepared = {key, nullptr, 0, 1, 0, &pipe};
        f.device.failWriteAfterMix.store(true);
        if (!require(f.send(install).has_value() && await([&] {
                return f.execution.snapshot().health == AudioOutputHealth::DeviceUnavailable; }), "write fails after final read")) return false;
        (void)f.execution.requestRetirePlayback(1);
        if (!require(await([&] { return f.execution.retiredPlaybackLifetime() >= 1; }), "failed source Stop completes")) return false;
        const auto fact = f.execution.sourceObservation(key);
        if (!require(fact && fact->consumerQuiesced && fact->cursorFrames == 480
            && fact->terminal == (terminal == AudioPcmReadStatus::EndOfStream ? AudioSourceTerminal::Ended : AudioSourceTerminal::Failed),
            "failed output write cannot let later retirement overwrite consumed Ended/Failed")) return false;
    }
    return true;
}

bool failureGatePreservesAppliedRateAndPause()
{
    std::vector<std::int16_t> tempo(48'000U * 2U * 5U, 3276);
    ExecutionFixture f;
    if (!require(f.start(), "late failure gate fixture starts")) return false;
    // Simulate C entering video decode with an old confirmed anchor. A is free
    // to finish already-authorized Rate/Pause while that decode is in flight.
    const auto staleControl = f.execution.snapshot().clock;
    const AudioSourceKey replacement{AudioLane::Embedded, 1U, 1U, 1U, 0U, 1U};
    AudioCommand rate{}; rate.kind = AudioCommandKind::ReplaceEmbedded;
    rate.source = replacement; rate.prepared = {replacement, tempo.data(), tempo.size(), 2.0, 10.0, nullptr};
    rate.clock = staleControl; rate.clock.timingVersion = 2U; rate.clock.rate = 2.0;
    rate.expectedTimingVersion = staleControl.timingVersion;
    rate.replacement = AudioEmbeddedReplacement::RateContinuous; rate.transport = AudioTransport::Playing;
    const auto changed = f.send(rate);
    if (!require(changed && changed->result == AudioEffectResult::Applied, "A commits the authorized 2x source")) return false;
    AudioCommand pause{}; pause.kind = AudioCommandKind::SetTransport;
    pause.clock = changed->clock; pause.clock.timingVersion = 3U;
    pause.expectedTimingVersion = 2U; pause.transport = AudioTransport::Paused;
    const auto paused = f.send(pause);
    if (!require(paused && paused->result == AudioEffectResult::Applied
        && paused->clock.rate == 2.0 && paused->transport == AudioTransport::Paused,
        "A finishes Pause before C receives its video failure")) return false;

    auto held = staleControl; held.timingVersion = 4U; held.position = 0.05;
    held.mode = AudioClockMode::FailureHold;
    const auto request = f.execution.requestEmbeddedFailureGate(held, 0.1, true);
    if (!require(request && await([&] { return f.execution.embeddedGateReceipt(*request).has_value(); }),
        "late C failure receives a durable safety gate effect")) return false;
    const auto receipt = f.execution.embeddedGateReceipt(*request);
    const bool preserved = receipt && receipt->result == AudioEffectResult::Applied
        && receipt->clock.rate == 2.0 && receipt->clock.duration == paused->clock.duration
        && receipt->clock.mode == AudioClockMode::FailureHold && receipt->clock.position == 0.05
        && receipt->transport == AudioTransport::Paused;
    if (!require(f.execution.releaseEmbeddedGateReceipt(*request), "late hold receipt releases")) return false;
    if (!require(preserved, "FailureHold is not authority to overwrite already-applied rate/duration/Pause")) return false;

    auto follow = staleControl; follow.timingVersion = 5U; follow.position = 0.05;
    follow.mode = AudioClockMode::Running; // C still has its old pre-Pause mode.
    const auto recovery = f.execution.requestEmbeddedFailureGate(follow, 0.1, false);
    if (!require(recovery && await([&] { return f.execution.embeddedGateReceipt(*recovery).has_value(); }),
        "validated video recovery clears the internal hold")) return false;
    const auto recovered = f.execution.embeddedGateReceipt(*recovery);
    const auto source = f.execution.sourceObservation(replacement);
    const bool resumed = recovered && source && recovered->clock.rate == 2.0
        && recovered->clock.mode == AudioClockMode::Paused && recovered->transport == AudioTransport::Paused
        && source->source == replacement && source->cursorFrames == 1200U;
    (void)f.execution.releaseEmbeddedGateReceipt(*recovery);
    return require(resumed, "Follow preserves real Pause and resyncs the same 2x immutable PCM");
}

bool rejectsMismatchedEmbeddedTempo()
{
    std::vector<std::int16_t> tempo(48'000U * 2U * 5U, 3276);
    ExecutionFixture f;
    if (!require(f.start(), "tempo mismatch fixture starts")) return false;
    AudioCommand rate{}; rate.kind = AudioCommandKind::ReplaceEmbedded;
    const AudioSourceKey candidate{AudioLane::Embedded, 1U, 1U, 1U, 0U, 1U};
    rate.source = candidate; rate.prepared = {candidate, tempo.data(), tempo.size(), 2.0, 10.0, nullptr};
    rate.clock = f.execution.snapshot().clock; rate.clock.timingVersion = 2U; rate.clock.rate = 1.0;
    rate.expectedTimingVersion = 1U; rate.replacement = AudioEmbeddedReplacement::RateContinuous;
    rate.transport = AudioTransport::Playing;
    const auto receipt = f.send(rate);
    const auto old = f.execution.sourceObservation(f.original);
    return require(receipt && receipt->result == AudioEffectResult::Failed
        && receipt->failure == AudioEffectFailure::InvalidCommand && old && !old->consumerQuiesced
        && f.execution.snapshot().laneSources[0] == f.original,
        "PCM tempo must match authorized clock rate before destructive replacement");
}

bool muteOnFailureRetainsIntentThroughRecovery()
{
    ExecutionFixture f;
    if (!require(f.start(), "mute-on device failure fixture starts")) return false;
    const auto opens = f.device.opens.load();
    f.device.requireMutedStart.store(true);
    f.device.muteResult.store(AudioDeviceMuteResult::DeviceFailure);
    AudioCommand mute{}; mute.kind = AudioCommandKind::SetOutputMute; mute.muted = true;
    const auto failed = f.send(mute, AudioEffectClass::OutputMute);
    if (!require(failed && failed->result == AudioEffectResult::Failed
        && failed->health == AudioOutputHealth::DeviceUnavailable
        && f.execution.snapshot().muteDesired, "failed mute-on retains authorized desired intent")) return false;
    const auto failureTime = failed->effectTime;
    f.device.muteResult.store(AudioDeviceMuteResult::Applied);
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    if (!require(f.device.opens.load() == opens, "mute device failure obeys native recovery cooldown")) return false;
    if (!require(await([&] { return f.execution.snapshot().recoveredCount > 0U; }),
        "mute failure recovers on A without control pumping")) return false;
    const auto state = f.execution.snapshot();
    return require(f.device.lastOpen.load() - failureTime >= 490'000'000LL
        && state.muteDesired && state.muteApplied && state.muteKnown
        && !f.device.startedUnmutedWhileRequired.load() && state.laneSources[0] == f.original,
        "recovery applies latest wanted mute before Start and retains source identity");
}

bool pureVisualTransitionsPreserveBgmQueue()
{
    AudioPcmPipe pipe;
    ExecutionFixture f;
    if (!require(f.execution.start(), "pure visual owner starts")) return false;
    AudioCommand visual{}; visual.kind = AudioCommandKind::ReplaceEmbedded;
    visual.embeddedPresent = false; visual.playbackLifetime = 1U;
    visual.clock = {1U, 1U, 0.0, audioMonotonicNow(), 1.0, 10.0, AudioClockMode::Running};
    visual.transport = AudioTransport::Playing;
    if (!require(f.send(visual).has_value(), "video-only binding starts")) return false;
    const std::vector<float> pcm(AudioPcmPipe::CapacityFrames * 2U, 0.25F);
    if (!require(pipe.writeFrames(pcm) == AudioPcmPipe::CapacityFrames, "BGM supply is bounded and prepared")) return false;
    const AudioSourceKey bgm{AudioLane::PlaylistBgm, 2U, 1U, 1U, 99U, 0U};
    AudioCommand install{}; install.kind = AudioCommandKind::InstallBgmPrepared;
    install.source = bgm; install.prepared = {bgm, nullptr, 0U, 1.0, 0.0, &pipe};
    if (!require(f.send(install).has_value()
        && await([&] { return f.execution.snapshot().writtenFrames >= 4800U; }), "real mixer queues BGM-only PCM")) return false;
    const auto stops = f.device.stops.load();
    const auto discarded = f.device.discardedFrames.load();

    // A visual Seek also changes actual transport here. It must Pause the
    // existing stream and retain its queue, rather than Stop/Reset it.
    visual.clock = f.execution.snapshot().clock;
    visual.expectedTimingVersion = visual.clock.timingVersion;
    visual.clock.timingVersion = 2U; visual.clock.position = 2.0; visual.clock.mode = AudioClockMode::Paused;
    visual.transport = AudioTransport::Paused; visual.replacement = AudioEmbeddedReplacement::SeekFixed;
    auto receipt = f.send(visual);
    if (!require(receipt && receipt->result == AudioEffectResult::Applied
        && await([&] { return f.execution.snapshot().clock.timingVersion == 2U; }), "pure visual Seek/Pause applies")) return false;
    auto held = f.execution.snapshot();
    if (!require(f.device.stops.load() == stops && f.device.discardedFrames.load() == discarded
        && f.device.pauses.load() == 1U && held.queuedFrames > 0U
        && held.laneSources[1] == bgm, "absent embedded Seek preserves queued BGM and uses checked Pause")) return false;
    const auto queued = held.queuedFrames;
    const auto cursor = held.laneCursorFrames[1];

    visual.clock = receipt->clock; visual.expectedTimingVersion = 2U;
    visual.clock.timingVersion = 3U; visual.clock.rate = 2.0;
    visual.replacement = AudioEmbeddedReplacement::RateContinuous;
    receipt = f.send(visual);
    if (!require(receipt && receipt->result == AudioEffectResult::Applied
        && await([&] { return f.execution.snapshot().clock.timingVersion == 3U; }), "pure visual Rate applies")) return false;
    held = f.execution.snapshot();
    if (!require(held.queuedFrames == queued && held.laneCursorFrames[1] == cursor
        && held.laneSources[1] == bgm && f.device.stops.load() == stops,
        "absent embedded Rate cannot flush, rewind or restart BGM")) return false;

    visual.clock = receipt->clock; visual.expectedTimingVersion = 3U;
    visual.clock.binding = 2U; visual.clock.timingVersion = 4U; visual.clock.position = 0.0;
    visual.replacement = AudioEmbeddedReplacement::Bind;
    receipt = f.send(visual);
    if (!require(receipt && receipt->result == AudioEffectResult::Applied
        && await([&] { return f.execution.snapshot().clock.timingVersion == 4U; }), "pure visual rebind applies")) return false;
    held = f.execution.snapshot();
    if (!require(held.queuedFrames == queued && held.laneCursorFrames[1] == cursor
        && held.laneSources[1] == bgm && f.device.stops.load() == stops,
        "image/video-only rebind retains Playlist BGM queue, token and cursor")) return false;

    visual.clock = receipt->clock; visual.expectedTimingVersion = 4U;
    visual.clock.timingVersion = 5U; visual.clock.mode = AudioClockMode::Running;
    visual.transport = AudioTransport::Playing; visual.replacement = AudioEmbeddedReplacement::RateContinuous;
    receipt = f.send(visual);
    if (!require(receipt && receipt->result == AudioEffectResult::Applied
        && f.device.resumes.load() == 1U && f.device.resumedRetainedQueue.load(),
        "pure visual transport Resume starts the retained queue")) return false;
    visual.clock = receipt->clock; visual.expectedTimingVersion = 5U;
    visual.clock.timingVersion = 6U; visual.clock.position = 3.0; visual.clock.mode = AudioClockMode::Paused;
    visual.transport = AudioTransport::Paused; visual.replacement = AudioEmbeddedReplacement::SeekFixed;
    receipt = f.send(visual);
    if (!require(receipt && receipt->result == AudioEffectResult::Applied
        && await([&] { return f.execution.snapshot().clock.timingVersion == 6U; }), "second pure visual Pause applies")) return false;
    held = f.execution.snapshot();
    if (!require(f.device.stops.load() == stops && f.device.discardedFrames.load() == discarded
        && f.device.pauses.load() == 2U && held.queuedFrames > 0U
        && held.laneSources[1] == bgm && held.laneCursorFrames[1] >= cursor,
        "real Pause/Resume changes do not reset or seek surviving BGM")) return false;

    // The protected exception is actual embedded input replacement. Its old
    // mixed queue still must be discarded under the established flush policy.
    const AudioSourceKey embedded{AudioLane::Embedded, 0U, 1U, 1U, 0U, 3U};
    AudioCommand withEmbedded = visual;
    withEmbedded.clock = receipt->clock; withEmbedded.expectedTimingVersion = 6U;
    withEmbedded.clock.binding = 3U; withEmbedded.clock.timingVersion = 7U;
    withEmbedded.clock.position = 0.0; withEmbedded.clock.rate = 1.0;
    withEmbedded.replacement = AudioEmbeddedReplacement::Bind; withEmbedded.embeddedPresent = true;
    withEmbedded.source = embedded; withEmbedded.prepared = {embedded, f.pcm.data(), f.pcm.size(), 1.0, 10.0, nullptr};
    const auto changed = f.send(withEmbedded);
    if (!require(changed && changed->result == AudioEffectResult::Applied
        && await([&] { return f.execution.snapshot().clock.timingVersion == 7U; }), "actual embedded replacement applies")) return false;
    const auto final = f.execution.snapshot();
    return require(f.device.stops.load() == stops + 1U && f.device.discardedFrames.load() > discarded
        && final.laneSources[1] == bgm && final.laneCursorFrames[1] == held.laneCursorFrames[1],
        "actual embedded replacement flushes mixed queue while retaining BGM registration/cursor");
}

bool retiredEmbeddedQueueAncestry(bool letTailDrain)
{
    AudioPcmPipe pipe;
    ExecutionFixture f;
    if (!require(f.start(), "queue ancestry fixture starts with embedded PCM")) return false;
    const std::vector<float> pcm(AudioPcmPipe::CapacityFrames * 2U, 0.25F);
    if (!require(pipe.writeFrames(pcm) == AudioPcmPipe::CapacityFrames, "queue ancestry BGM producer primes")) return false;
    const AudioSourceKey bgm{AudioLane::PlaylistBgm, 2U, 1U, 1U, 100U, 0U};
    AudioCommand install{}; install.kind = AudioCommandKind::InstallBgmPrepared;
    install.source = bgm; install.prepared = {bgm, nullptr, 0U, 1.0, 0.0, &pipe};
    if (!require(f.send(install).has_value()
        && await([&] { return f.execution.snapshot().laneCursorFrames[1] > 0U; }), "mixed embedded/BGM queue is submitted")) return false;
    auto current = f.execution.snapshot().clock;
    if (!letTailDrain)
    {
        AudioCommand pause{}; pause.kind = AudioCommandKind::SetTransport;
        pause.clock = current; pause.clock.timingVersion = current.timingVersion + 1U;
        pause.expectedTimingVersion = current.timingVersion; pause.transport = AudioTransport::Paused;
        const auto receipt = f.send(pause);
        if (!require(receipt && receipt->result == AudioEffectResult::Applied, "Pause retains an old embedded mixed tail")) return false;
        current = receipt->clock;
    }
    if (!require(f.execution.requestRetireSource(f.original)
        && await([&] { const auto s = f.execution.sourceObservation(f.original); return s && s->consumerQuiesced; }),
        "dedicated embedded retirement wins before the absent Bind")) return false;
    if (letTailDrain) std::this_thread::sleep_for(std::chrono::milliseconds(180));
    const auto before = f.execution.snapshot();
    if (!require(!before.laneSources[0].valid() && before.laneSources[1] == bgm && before.queuedFrames > 0U,
        "lane registry is empty for embedded while device queue remains live")) return false;
    const auto stops = f.device.stops.load(), discarded = f.device.discardedFrames.load();
    AudioCommand absent{}; absent.kind = AudioCommandKind::ReplaceEmbedded;
    absent.embeddedPresent = false; absent.playbackLifetime = 1U;
    absent.replacement = AudioEmbeddedReplacement::Bind; absent.clock = current;
    absent.expectedTimingVersion = current.timingVersion;
    absent.clock.binding = current.binding + 1U; absent.clock.timingVersion = current.timingVersion + 1U;
    absent.clock.position = 0.0;
    absent.transport = letTailDrain ? AudioTransport::Playing : AudioTransport::Paused;
    const auto receipt = f.send(absent);
    if (!require(receipt && receipt->result == AudioEffectResult::Applied
        && await([&] { return f.execution.snapshot().clock.timingVersion == absent.clock.timingVersion; }),
        "new image/video-only binding commits after old input retired")) return false;
    const auto after = f.execution.snapshot();
    if (letTailDrain)
        return require(f.device.stops.load() == stops && f.device.discardedFrames.load() == discarded
            && after.laneSources[1] == bgm && after.laneCursorFrames[1] >= before.laneCursorFrames[1],
            "drained embedded ancestry must not cause a later BGM-only image-to-image flush");
    return require(f.device.stops.load() == stops + 1U && f.device.discardedFrames.load() > discarded
        && after.laneSources[1] == bgm && after.laneCursorFrames[1] == before.laneCursorFrames[1],
        "unplayed embedded queue ancestry still requires flush after dedicated input retirement");
}

bool configureDelayResynchronizesEmbedded()
{
    std::vector<std::int16_t> tempo(48'000U * 2U * 5U, 3276);
    ExecutionFixture f;
    if (!require(f.start(), "configure latency fixture starts")) return false;
    const auto before = f.execution.snapshot();
    const auto oldConfigure = f.device.lastConfigureCompleted.load();
    f.device.configureDelayMs.store(200U);
    const AudioSourceKey candidate{AudioLane::Embedded, 1U, 1U, 1U, 0U, 1U};
    AudioCommand rate{}; rate.kind = AudioCommandKind::ReplaceEmbedded;
    rate.source = candidate; rate.prepared = {candidate, tempo.data(), tempo.size(), 2.0, 10.0, nullptr};
    rate.clock = before.clock; rate.clock.timingVersion += 1U; rate.clock.rate = 2.0;
    rate.expectedTimingVersion = before.clock.timingVersion;
    rate.transport = AudioTransport::Playing; rate.replacement = AudioEmbeddedReplacement::RateContinuous;
    const auto receipt = f.send(rate);
    AudioExecutionSnapshot sample{};
    if (!require(receipt && receipt->result == AudioEffectResult::Applied
        && await([&] {
            sample = f.execution.snapshot();
            const auto configured = f.device.lastConfigureCompleted.load();
            return configured > oldConfigure && sample.publicationTimeNs >= configured
                && sample.lastWriteTime >= configured && sample.laneSources[0] == candidate
                && sample.clock.timingVersion == rate.clock.timingVersion
                && sample.writtenFrames > before.writtenFrames;
        }),
        "replacement reaches the delayed native configure and then resumes actual writes")) return false;
    // Compare facts from the same A publication. C can be descheduled after
    // reading it; a later live timestamp would invent drift from that delay.
    const auto logical = resolveAudioClockPosition(sample.clock, sample.publicationTimeNs);
    const auto playedFrames = sample.laneCursorFrames[0] >= sample.queuedFrames
        ? sample.laneCursorFrames[0] - sample.queuedFrames : 0U;
    const auto embedded = static_cast<double>(playedFrames) / 48'000.0 * 2.0;
    if (sample.laneSources[0] != candidate || std::abs(logical - embedded) >= 0.03)
        std::cerr << "configure-resync diagnostic: logical=" << logical << " embedded=" << embedded
            << " cursor=" << sample.laneCursorFrames[0] << " queued=" << sample.queuedFrames
            << " lastWrite=" << sample.lastWriteTime << " sampled=" << sample.publicationTimeNs
            << " now=" << audioMonotonicNow()
            << " configured=" << f.device.lastConfigureCompleted.load() << '\n';
    return require(sample.laneSources[0] == candidate && std::abs(logical - embedded) < 0.03,
        "fresh embedded cursor alignment includes configure latency after shared reset");
}

bool checkedStopFailuresInReplacement(TestDevice::StopFault selected = TestDevice::StopFault::None)
{
    for (const auto fault : {TestDevice::StopFault::StopDeviceFailure,
        TestDevice::StopFault::ResetDeviceFailure, TestDevice::StopFault::ResetContractFailure})
    {
        if (selected != TestDevice::StopFault::None && selected != fault) continue;
        AudioPcmPipe pipe;
        std::vector<std::int16_t> tempo(48'000U * 2U * 5U, 3276);
        ExecutionFixture f;
        if (!require(f.start(), "checked reset replacement fixture starts")) return false;
        const std::vector<float> bgmPcm(AudioPcmPipe::CapacityFrames * 2U, 0.25F);
        if (!require(pipe.writeFrames(bgmPcm) == AudioPcmPipe::CapacityFrames, "checked reset BGM is prepared")) return false;
        const AudioSourceKey bgm{AudioLane::PlaylistBgm, 2U, 1U, 1U, 101U, 0U};
        AudioCommand install{}; install.kind = AudioCommandKind::InstallBgmPrepared;
        install.source = bgm; install.prepared = {bgm, nullptr, 0U, 1.0, 0.0, &pipe};
        if (!require(f.send(install).has_value(), "checked reset BGM installs")) return false;
        const auto before = f.execution.snapshot();
        const auto closes = f.device.closes.load(), opens = f.device.opens.load();
        f.device.nextStopFault.store(fault);
        const AudioSourceKey candidate{AudioLane::Embedded, 1U, 1U, 1U, 0U, 1U};
        AudioCommand rate{}; rate.kind = AudioCommandKind::ReplaceEmbedded;
        rate.source = candidate; rate.prepared = {candidate, tempo.data(), tempo.size(), 2.0, 10.0, nullptr};
        rate.clock = before.clock; rate.clock.timingVersion += 1U; rate.clock.rate = 2.0;
        rate.expectedTimingVersion = before.clock.timingVersion;
        rate.replacement = AudioEmbeddedReplacement::RateContinuous; rate.transport = AudioTransport::Playing;
        const auto receipt = f.send(rate);
        const auto health = fault == TestDevice::StopFault::ResetContractFailure
            ? AudioOutputHealth::ContractFaulted : AudioOutputHealth::DeviceUnavailable;
        if (!require(receipt && receipt->result == AudioEffectResult::Applied && receipt->health == health
            && receipt->clock.rate == 2.0 && f.device.closes.load() > closes,
            "failed Stop/Reset closes untrusted client and preserves truthful committed input/clock")) return false;
        if (!require(await([&] { return f.execution.snapshot().health == health; }), "checked failure state is detached")) return false;
        const auto unavailable = f.execution.snapshot();
        const auto old = f.execution.sourceObservation(f.original);
        if (!require(old && old->consumerQuiesced && unavailable.laneSources[0] == candidate
            && unavailable.laneSources[1] == bgm && unavailable.laneCursorFrames[1] >= before.laneCursorFrames[1],
            "reset failure retires old embedded reads without replacing or rewinding BGM token/cursor")) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (!require(f.device.opens.load() == opens && f.execution.snapshot().laneCursorFrames[1] == unavailable.laneCursorFrames[1],
            "untrusted queue cannot restart or pull BGM before recovery")) return false;
        if (health == AudioOutputHealth::DeviceUnavailable)
        {
            if (!require(await([&] { return f.execution.snapshot().recoveredCount > 0U; }), "checked device failure recovers on a new stream")) return false;
            const auto recovered = f.execution.snapshot();
            if (!require(recovered.laneSources[0] == candidate && recovered.laneSources[1] == bgm
                && recovered.clock.rate == 2.0, "recovery keeps actual binding/rate and BGM source identity")) return false;
        }
        else
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(450));
            if (!require(f.device.opens.load() == opens, "reset contract fault remains latched")) return false;
        }
    }
    return true;
}

bool checkedStopFailuresDuringRetirement(bool shutdown)
{
    for (const auto fault : {TestDevice::StopFault::StopDeviceFailure,
        TestDevice::StopFault::ResetDeviceFailure, TestDevice::StopFault::ResetContractFailure})
    {
        ExecutionFixture f;
        if (!require(f.start(), "checked reset retirement fixture starts")) return false;
        const auto closes = f.device.closes.load();
        f.device.nextStopFault.store(fault);
        if (shutdown) f.execution.requestShutdown();
        else if (!require(f.execution.requestRetirePlayback(1U), "reserved retirement admits despite reset fault")) return false;
        if (!require(await([&] { return shutdown ? f.execution.shutdownComplete()
            : f.execution.retiredPlaybackLifetime() >= 1U; }), "retirement reaches true consumer/client barrier")) return false;
        const auto source = f.execution.sourceObservation(f.original);
        const auto state = f.execution.snapshot();
        const auto health = fault == TestDevice::StopFault::ResetContractFailure
            ? AudioOutputHealth::ContractFaulted : AudioOutputHealth::DeviceUnavailable;
        if (!require(source && source->consumerQuiesced && source->terminal == AudioSourceTerminal::Retired
            && state.health == health && state.deviceFailures > 0U && f.device.closes.load() > closes,
            "retirement reports Stop/Reset failure while no-more-reads and closed-client facts remain true")) return false;
        if (shutdown && !require(f.execution.joinIfStopped() && f.device.ownerCorrect.load(),
            "checked failure does not fake exit or move cleanup away from A")) return false;
    }
    return true;
}

bool endpointStopFailureReroutesImmediately()
{
    for (const auto fault : {TestDevice::StopFault::StopDeviceFailure,
        TestDevice::StopFault::ResetDeviceFailure})
    {
        ExecutionFixture f;
        if (!require(f.start(), "endpoint failure fixture starts")) return false;
        const auto opens = f.device.opens.load();
        f.device.nextStopFault.store(fault);
        const auto changed = audioMonotonicNow();
        ++f.device.generation;
        if (!require(await([&] { return f.device.opens.load() > opens; })
            && f.device.lastOpen.load() - changed < 250'000'000LL,
            "endpoint change closes invalid old client and permits immediate new-endpoint reroute")) return false;
        if (!require(f.execution.snapshot().laneSources[0] == f.original,
            "endpoint Stop/Reset failure preserves active source identity")) return false;
    }
    return true;
}

bool availabilityCleanupContractFailure(AudioBlockStatus nativeStatus = AudioBlockStatus::InvalidRequest)
{
    ExecutionFixture f;
    if (!require(f.start(), "availability cleanup failure fixture starts")) return false;
    const auto opens = f.device.opens.load();
    f.device.availabilityCleanupFailure.store(nativeStatus);
    if (!require(await([&] { return f.execution.snapshot().health == AudioOutputHealth::ContractFaulted; }),
        "availability cleanup contract failure reaches A through the real bridge")) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(550));
    const auto source = f.execution.sourceObservation(f.original);
    return require(f.device.opens.load() == opens && source && !source->consumerQuiesced,
        "availability contract fault stays latched while retaining source ownership");
}

bool bgmNewLifetimeStartsAfterStop()
{
    AudioPcmPipe pipe;
    ExecutionFixture f;
    if (!require(f.start() && f.execution.requestRetirePlayback(1U)
        && await([&] { return f.execution.retiredPlaybackLifetime() >= 1U; }),
        "old playback lifetime reaches reserved Stop")) return false;
    const auto stopped = f.execution.snapshot();
    const std::vector<float> pcm(AudioPcmPipe::CapacityFrames * 2U, 0.25F);
    if (!require(pipe.writeFrames(pcm) == AudioPcmPipe::CapacityFrames, "new lifetime BGM is ready")) return false;
    const AudioSourceKey key{AudioLane::PlaylistBgm, 2U, 1U, 2U, 301U, 0U};
    AudioCommand install{}; install.kind = AudioCommandKind::InstallBgmPrepared;
    install.source = key; install.prepared = {key, nullptr, 0U, 1.0, 0.0, &pipe};
    install.playbackLifetime = 2U; install.transport = AudioTransport::Playing;
    const auto receipt = f.send(install);
    if (!require(receipt && receipt->result == AudioEffectResult::Applied
        && receipt->transport == AudioTransport::Playing
        && await([&] { const auto sample = f.execution.snapshot();
            return sample.laneSources[1] == key && sample.writtenFrames > stopped.writtenFrames
                && sample.nonzeroSubmittedFrames > stopped.nonzeroSubmittedFrames; }),
        "first BGM install in a new lifetime seeds authorized Playing and actually writes")) return false;
    return require(receipt->clock.binding == stopped.clock.binding && receipt->clock.rate == stopped.clock.rate
        && receipt->clock.timingVersion == stopped.clock.timingVersion && receipt->clock.time == stopped.clock.time,
        "BGM initial transport does not invent a logical clock transition");
}

bool bgmDelayedSameLifetimePreservesPause()
{
    AudioPcmPipe pipe;
    ExecutionFixture f;
    if (!require(f.start(), "delayed BGM pause fixture starts")) return false;
    AudioCommand pause{}; pause.kind = AudioCommandKind::SetTransport;
    pause.clock = f.execution.snapshot().clock; pause.clock.timingVersion += 1U;
    pause.playbackLifetime = 1U; pause.transport = AudioTransport::Paused;
    const auto paused = f.send(pause);
    if (!require(paused && paused->transport == AudioTransport::Paused, "Pause applies before delayed preparation")) return false;
    const std::vector<float> pcm(AudioPcmPipe::CapacityFrames * 2U, 0.25F);
    if (!require(pipe.writeFrames(pcm) == AudioPcmPipe::CapacityFrames, "delayed same-life BGM becomes ready")) return false;
    const AudioSourceKey key{AudioLane::PlaylistBgm, 2U, 1U, 1U, 302U, 0U};
    AudioCommand install{}; install.kind = AudioCommandKind::InstallBgmPrepared;
    install.source = key; install.prepared = {key, nullptr, 0U, 1.0, 0.0, &pipe};
    install.playbackLifetime = 1U; install.transport = AudioTransport::Playing;
    const auto receipt = f.send(install);
    if (!require(receipt && receipt->result == AudioEffectResult::Applied && receipt->transport == AudioTransport::Paused
        && await([&] { return f.execution.snapshot().laneSources[1] == key; }),
        "same-lifetime stale preparation transport cannot revert an applied Pause")) return false;
    const auto before = f.execution.snapshot();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    const auto after = f.execution.snapshot();
    return require(after.transport == AudioTransport::Paused && after.writtenFrames == before.writtenFrames
        && after.laneCursorFrames[1] == 0U, "delayed same-life install remains physically paused");
}

bool bgmNewLifetimeTransportFences(bool idempotent)
{
    AudioPcmPipe pipe;
    ExecutionFixture f;
    if (!require(f.start(), "new-life transport fence fixture starts")) return false;
    AudioCommand pause{}; pause.kind = AudioCommandKind::SetTransport;
    pause.clock = f.execution.snapshot().clock; pause.clock.timingVersion += 1U;
    pause.transport = AudioTransport::Paused; pause.playbackLifetime = idempotent ? 1U : 2U;
    auto paused = f.send(pause);
    if (idempotent)
    {
        if (!require(paused && paused->transport == AudioTransport::Paused, "old-life Pause applies")) return false;
        pause.clock = paused->clock; pause.clock.timingVersion += 1U; pause.playbackLifetime = 2U;
        paused = f.send(pause);
    }
    if (!require(paused && paused->result == AudioEffectResult::Applied && paused->transport == AudioTransport::Paused,
        "new-life Pause is applied before preparation, including idempotent transport")) return false;
    AudioCommand stale = pause; stale.clock = paused->clock; stale.clock.timingVersion += 1U;
    stale.playbackLifetime = 1U; stale.transport = AudioTransport::Playing;
    const auto staleTransport = f.send(stale);
    if (!require(staleTransport && staleTransport->result == AudioEffectResult::CancelledByRetirement
        && staleTransport->transport == AudioTransport::Paused,
        "old-lifetime transport cannot revert a newer applied lifetime")) return false;
    const std::vector<float> pcm(AudioPcmPipe::CapacityFrames * 2U, 0.25F);
    if (!require(pipe.writeFrames(pcm) == AudioPcmPipe::CapacityFrames, "fenced BGM is prepared")) return false;
    const AudioSourceKey old{AudioLane::PlaylistBgm, 2U, 1U, 1U, 303U, 0U};
    AudioCommand install{}; install.kind = AudioCommandKind::InstallBgmPrepared;
    install.source = old; install.prepared = {old, nullptr, 0U, 1.0, 0.0, &pipe};
    install.playbackLifetime = 1U; install.transport = AudioTransport::Playing;
    const auto staleSource = f.send(install);
    if (!require(staleSource && staleSource->result == AudioEffectResult::CancelledByRetirement
        && f.execution.sourceObservation(old)->consumerQuiesced,
        "old-lifetime prepared BGM is retired without live reads")) return false;
    const AudioSourceKey next{AudioLane::ItemBgm, 4U, 1U, 2U, 304U, 0U};
    install.source = next; install.prepared = {next, nullptr, 0U, 1.0, 0.0, &pipe}; install.playbackLifetime = 2U;
    const auto ready = f.send(install);
    if (!require(ready && ready->result == AudioEffectResult::Applied && ready->transport == AudioTransport::Paused
        && await([&] { return f.execution.snapshot().laneSources[2] == next; }),
        "new-life preparation cannot override its already-applied Pause")) return false;
    if (!require(f.execution.requestRetirePlayback(1U)
        && await([&] { return f.execution.retiredPlaybackLifetime() >= 1U; })
        && f.execution.snapshot().transport == AudioTransport::Paused,
        "older lifetime retirement cannot stop newer paused playback")) return false;
    if (!require(f.execution.requestRetirePlayback(2U)
        && await([&] { return f.execution.retiredPlaybackLifetime() >= 2U; }), "new lifetime retires")) return false;
    const AudioSourceKey late{AudioLane::PlaylistBgm, 3U, 1U, 2U, 305U, 0U};
    install.source = late; install.prepared = {late, nullptr, 0U, 1.0, 0.0, &pipe};
    const auto retired = f.send(install);
    return require(retired && retired->result == AudioEffectResult::CancelledByRetirement
        && retired->transport == AudioTransport::Stopped && f.execution.sourceObservation(late)->consumerQuiesced,
        "retirement fence rejects late prepared installation without restarting output");
}

bool submittedSignalDiagnostics()
{
    {
        ExecutionFixture f;
        for (std::size_t frame = 0U; frame < f.pcm.size() / 2U; ++frame)
        { f.pcm[frame * 2U] = 0; f.pcm[frame * 2U + 1U] = 3276; }
        if (!require(f.start(), "nonzero signal diagnostic fixture starts")) return false;
        auto sample = f.execution.snapshot();
        if (!require(sample.nonzeroSubmittedFrames == sample.writtenFrames
            && sample.nonzeroSubmittedFrames > 4800U && sample.maximumConsecutiveZeroSubmittedFrames == 0U,
            "either nonzero stereo channel is counted across successful writes")) return false;
        AudioCommand mute{}; mute.kind = AudioCommandKind::SetOutputMute; mute.muted = true;
        const auto muted = f.send(mute, AudioEffectClass::OutputMute);
        const auto beforeMute = sample.writtenFrames;
        if (!require(muted && muted->result == AudioEffectResult::Applied
            && await([&] { return f.execution.snapshot().writtenFrames > beforeMute + 4800U; }),
            "stream mute permits further pre-stream-mute PCM submission")) return false;
        sample = f.execution.snapshot();
        if (!require(sample.nonzeroSubmittedFrames == sample.writtenFrames
            && sample.maximumConsecutiveZeroSubmittedFrames == 0U,
            "signal counters describe mixed PCM before the native mute gate")) return false;
        f.device.failWriteAfterMix.store(true);
        if (!require(await([&] { return f.execution.snapshot().health == AudioOutputHealth::DeviceUnavailable; }),
            "failed native write enters unavailable state")) return false;
        const auto unavailable = f.execution.snapshot();
        if (!require(unavailable.nonzeroSubmittedFrames == unavailable.writtenFrames
            && unavailable.maximumConsecutiveZeroSubmittedFrames == 0U,
            "failed writes do not inflate successful signal counters")) return false;
        if (!require(await([&] { return f.execution.snapshot().recoveredCount > 0U
            && f.execution.snapshot().writtenFrames > unavailable.writtenFrames; }),
            "nonzero source resumes on a new stream")) return false;
        sample = f.execution.snapshot();
        if (!require(sample.nonzeroSubmittedFrames == sample.writtenFrames
            && sample.nonzeroSubmittedFrames > unavailable.nonzeroSubmittedFrames,
            "nonzero cumulative counter survives stream recovery")) return false;
    }
    {
        ExecutionFixture f;
        std::fill(f.pcm.begin(), f.pcm.end(), std::int16_t{0});
        if (!require(f.start(), "zero signal diagnostic fixture starts")) return false;
        const auto before = f.execution.snapshot();
        if (!require(before.refillCount >= 2U && before.nonzeroSubmittedFrames == 0U
            && before.maximumConsecutiveZeroSubmittedFrames == before.writtenFrames,
            "all-zero run accumulates across successful write boundaries")) return false;
        const auto opens = f.device.opens.load();
        ++f.device.generation;
        AudioExecutionSnapshot sample{};
        if (!require(await([&] {
            sample = f.execution.snapshot();
            return f.device.opens.load() > opens && sample.writtenFrames > before.writtenFrames;
        }), "zero signal stream reroutes and submits more frames")) return false;
        if (!require(sample.nonzeroSubmittedFrames == 0U
            && sample.maximumConsecutiveZeroSubmittedFrames == sample.writtenFrames
            && sample.maximumConsecutiveZeroSubmittedFrames > before.maximumConsecutiveZeroSubmittedFrames,
            "zero run and lifetime diagnostics survive native stream reopen")) return false;
    }
    {
        ExecutionFixture f;
        if (!require(f.start(), "embedded tempo diagnostic fixture starts")) return false;
        const auto before = f.execution.snapshot();
        const AudioSourceKey candidate{AudioLane::Embedded, 1U, 1U, 1U, 0U, 1U};
        AudioCommand rate{}; rate.kind = AudioCommandKind::ReplaceEmbedded;
        rate.source = candidate; rate.prepared = {candidate, f.pcm.data(), f.pcm.size(), 1.25, 10.0, nullptr};
        rate.clock = before.clock; rate.clock.timingVersion += 1U; rate.clock.rate = 1.25;
        rate.expectedTimingVersion = before.clock.timingVersion;
        rate.transport = AudioTransport::Playing; rate.replacement = AudioEmbeddedReplacement::RateContinuous;
        const auto receipt = f.send(rate);
        AudioExecutionSnapshot sample{};
        if (!require(receipt && receipt->result == AudioEffectResult::Applied && await([&] {
            sample = f.execution.snapshot();
            return sample.laneSources[0] == candidate && sample.clock.timingVersion == rate.clock.timingVersion
                && sample.lastWriteTime >= receipt->effectTime;
        }), "tempo replacement publishes one source/cursor/clock observation")) return false;
        const auto playedFrames = sample.laneCursorFrames[0] >= sample.queuedFrames
            ? sample.laneCursorFrames[0] - sample.queuedFrames : 0U;
        const auto embeddedPosition = static_cast<double>(playedFrames) / 48'000.0 * sample.embeddedTempo;
        if (!require(sample.embeddedTempo == 1.25 && sample.clock.rate == 1.25
            && std::abs(resolveAudioClockPosition(sample.clock, sample.publicationTimeNs) - embeddedPosition) < 0.03,
            "actual prepared tempo1.25 independently projects PCM cursor to logical media time")) return false;
        AudioCommand absent{}; absent.kind = AudioCommandKind::ReplaceEmbedded;
        absent.embeddedPresent = false; absent.clock = sample.clock; absent.clock.binding += 1U;
        absent.clock.timingVersion += 1U; absent.transport = AudioTransport::Paused;
        const auto absentReceipt = f.send(absent);
        if (!require(absentReceipt && absentReceipt->result == AudioEffectResult::Applied && await([&] {
            sample = f.execution.snapshot();
            return sample.clock.binding == absent.clock.binding && !sample.laneSources[0].valid();
        }) && sample.embeddedTempo == 0.0,
            "absent embedded binding publishes zero prepared tempo")) return false;
    }
    return true;
}
}

int main(int argc, char** argv)
{
#if defined(_MSC_VER) && defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif
    if (argc == 2)
    {
        const std::string_view mode = argv[1];
#if defined(_WIN32)
        if (mode == "control-fact-wake")
        {
            if (!controlFactWake()) return 1;
            std::cout << "AudioExecution control fact wake PASS\n";
            return 0;
        }
#endif
        return (mode == "optional-mute" ? optionalMuteCapability() : mode == "last-tail" ? lastSourceTailDrains()
            : mode == "stop-timing" ? stopRevokesQueuedTiming()
            : mode == "terminal-failure" ? terminalSurvivesWriteFailure()
            : mode == "failure-gate-clock" ? failureGatePreservesAppliedRateAndPause()
            : mode == "tempo-mismatch" ? rejectsMismatchedEmbeddedTempo()
            : mode == "mute-recovery" ? muteOnFailureRetainsIntentThroughRecovery()
            : mode == "visual-bgm-queue" ? pureVisualTransitionsPreserveBgmQueue()
            : mode == "retired-embedded-tail" ? retiredEmbeddedQueueAncestry(false)
            : mode == "drained-embedded-tail" ? retiredEmbeddedQueueAncestry(true)
            : mode == "configure-resync" ? configureDelayResynchronizesEmbedded()
            : mode == "stop-reset-replace" ? checkedStopFailuresInReplacement()
            : mode == "native-stop-failure" ? checkedStopFailuresInReplacement(TestDevice::StopFault::StopDeviceFailure)
            : mode == "native-reset-failure" ? checkedStopFailuresInReplacement(TestDevice::StopFault::ResetDeviceFailure)
            : mode == "reset-contract-failure" ? checkedStopFailuresInReplacement(TestDevice::StopFault::ResetContractFailure)
            : mode == "stop-reset-retire" ? checkedStopFailuresDuringRetirement(false)
            : mode == "stop-reset-shutdown" ? checkedStopFailuresDuringRetirement(true)
            : mode == "endpoint-stop-failure" ? endpointStopFailureReroutesImmediately()
            : mode == "availability-contract" ? availabilityCleanupContractFailure()
            : mode == "availability-contract-old-status" ? availabilityCleanupContractFailure(AudioBlockStatus::WrongMode)
            : mode == "signal-diagnostics" ? submittedSignalDiagnostics()
            : mode == "bgm-new-lifetime" ? bgmNewLifetimeStartsAfterStop()
            : mode == "bgm-delayed-pause" ? bgmDelayedSameLifetimePreservesPause()
            : mode == "bgm-transport-fences" ? bgmNewLifetimeTransportFences(false)
            : mode == "bgm-idempotent-fences" ? bgmNewLifetimeTransportFences(true)
            : false) ? 0 : 1;
    }
#if defined(_WIN32)
    if (!controlFactWake()) return 1;
#endif
    if (!clockAndPartialInitialization() || !boundedAdmission() || !unconsumedReceiptsNeverBlockRefill()
        || !boundedRenderHorizon()
        || !independentServiceAndPauseMute() || !rateContinuityAndReservedFailureHold()
        || !supplyAndDurableTerminal() || !deviceRecoveryAndDeferredMute()
        || !checkedMuteFaultAndNoRecovery() || !reservedStopAndLateOldLifetime()
        || !optionalMuteCapability() || !lastSourceTailDrains() || !stopRevokesQueuedTiming()
        || !terminalSurvivesWriteFailure() || !failureGatePreservesAppliedRateAndPause()
        || !rejectsMismatchedEmbeddedTempo() || !muteOnFailureRetainsIntentThroughRecovery()
        || !pureVisualTransitionsPreserveBgmQueue() || !retiredEmbeddedQueueAncestry(false)
        || !retiredEmbeddedQueueAncestry(true) || !configureDelayResynchronizesEmbedded()
        || !checkedStopFailuresInReplacement() || !checkedStopFailuresDuringRetirement(false)
        || !checkedStopFailuresDuringRetirement(true) || !endpointStopFailureReroutesImmediately()
        || !availabilityCleanupContractFailure() || !submittedSignalDiagnostics()
        || !bgmNewLifetimeStartsAfterStop() || !bgmDelayedSameLifetimePreservesPause()
        || !bgmNewLifetimeTransportFences(false) || !bgmNewLifetimeTransportFences(true)) return 1;
#if defined(_MSC_VER) && defined(_DEBUG)
    if (!require(_CrtCheckMemory() != 0, "Debug CRT heap remains valid")) return 1;
#endif
    std::cout << "AudioExecution: bounded admission, independent refill, pause/mute/resume, rate/effect-time, "
        "reserved FailureHold, supply/EOF, recovery and retirement PASS\n";
    return 0;
}
