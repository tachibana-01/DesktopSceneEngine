// CP1 coverage migration: all former recovery scenarios remain below. Device
// failures/readiness are injected through the same A executor's external sink;
// GUI update deltas, raw mixer mutations and exact GUI pump counts are gone.
#include "AudioExecutionEngineFixture.h"
#include <DesktopSceneEngine/BackgroundMusicControl.h>
#include <filesystem>
#include <functional>
#include <iostream>

namespace
{
using namespace DesktopSceneEngine;
namespace T = Tests;
using Lane = Audio::AudioLane;
using Health = Audio::AudioOutputHealth;
using Status = Audio::AudioBlockStatus;
using Access = Core::AudioOutputRecoveryEngineTestAccess;
using namespace BackgroundMusicControl;

bool require(bool value, const char* message)
{ if (!value) std::cerr << "FAIL: " << message << '\n'; return value; }
std::filesystem::path fixturePath()
{ return std::filesystem::path{DESKTOP_SCENE_ENGINE_SEQUENTIAL_AUDIO_TEST_DATA_DIR} / "SequentialAudioSourceStereo48k.wav"; }
Event makeStartEvent()
{
    return PlaybackSessionStarted{ActiveScopeSnapshot{PlaylistId{"recovery-playlist"},
        QueueSnapshot{{TrackSnapshot{TrackId{"recovery-track"}, fixturePath(), 1.0}}, QueueMode::ListLoop, 1.0},
        std::nullopt, std::nullopt}};
}
auto token(const Engine& engine)
{ return Core::BackgroundMusicEngineTestAccess::runtime(engine).playlistSession()->sourceToken(); }
bool healthy(Engine& engine)
{ return T::audioState(engine).health == Health::Healthy; }
bool waitHealth(Engine& engine, Health health)
{ return T::waitUntil(engine, [&] { return T::audioState(engine).health == health; }); }
bool failDevice(T::AwaitAudioEngine& engine, Status failure = Status::DeviceFailure)
{
    engine.device.availabilityResult.store(failure);
    return waitHealth(engine, failure == Status::DeviceFailure ? Health::DeviceUnavailable : Health::ContractFaulted);
}
bool installEmbedded(T::AwaitAudioEngine& engine)
{
    const auto path = fixturePath().parent_path() / "SequentialAudioSourceCompressed.flac";
    return T::effect(engine, [&] {
        Access::prepareEmbedded(engine, path);
        return true;
    }) && T::lane(engine, Lane::Embedded).has_value();
}
template<class Check> bool withBgm(Check check)
{
    T::AwaitAudioEngine engine;
    if (!require(engine.processBackgroundMusicControl(makeStartEvent()) && T::lane(engine, Lane::PlaylistBgm),
        "real prepared BGM registers through A and durable Started receipt")) return false;
    const bool result = check(engine);
    return T::shutdown(engine) && result;
}

bool verifyDeviceFailureDoesNotRecoverBeforeCooldown()
{
    return withBgm([](T::AwaitAudioEngine& engine) {
        const auto original = token(engine); const auto opens = engine.device.openCalls.load();
        if (!require(failDevice(engine), "device failure is observed by A")) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds{250});
        if (!require(engine.device.openCalls.load() == opens && !healthy(engine)
            && token(engine) == original && T::lane(engine, Lane::PlaylistBgm),
            "before 500ms no retry, source/Current remain retained")) return false;
        if (!require(waitHealth(engine, Health::Healthy), "A autonomously recovers after cooldown")) return false;
        return require(engine.device.openCalls.load() == opens + 1
            && engine.device.lastOpenTime.load() - engine.device.lastFaultTime.load() >= 500000000
            && token(engine) == original && T::audioState(engine).recoveredCount == 1,
            "one real reopen follows >=500ms measured post-failure time without replacing BGM");
    });
}
bool verifyFailureDoesNotConsumePreFailureUpdateDelta()
{
    return withBgm([](T::AwaitAudioEngine& engine) {
        const auto opens = engine.device.openCalls.load();
        if (!failDevice(engine)) return false;
        engine.update(1000); // Must not shortcut A's native cooldown.
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
        return require(engine.device.openCalls.load() == opens && !healthy(engine),
            "arbitrary old GUI delta cannot count as post-failure native elapsed");
    });
}
bool verifyInitialAndBeginDeviceFailuresUseRecovery()
{
    for (const bool failOpen : {true, false})
    {
        T::AwaitAudioEngine engine;
        if (failOpen) engine.device.failOpen.store(1);
        else engine.device.configureResult.store(Status::DeviceFailure);
        if (!require(engine.processBackgroundMusicControl(makeStartEvent())
            && waitHealth(engine, Health::DeviceUnavailable), "initial open/configure failure enters recoverable unavailable")) return false;
        const auto original = token(engine);
        if (!require(waitHealth(engine, Health::Healthy) && token(engine) == original
            && T::lane(engine, Lane::PlaylistBgm), "recovery keeps native selected source identity")) return false;
    }
    return true;
}
bool verifyFailedRecoveryResetsCooldown()
{
    return withBgm([](T::AwaitAudioEngine& engine) {
        const auto opens = engine.device.openCalls.load();
        if (!failDevice(engine)) return false;
        engine.device.failOpen.store(1);
        if (!T::waitUntil(engine, [&] { return engine.device.openCalls.load() == opens + 1; })) return false;
        const auto firstAttempt = engine.device.lastOpenTime.load();
        std::this_thread::sleep_for(std::chrono::milliseconds{250});
        if (!require(engine.device.openCalls.load() == opens + 1 && !healthy(engine), "failed retry starts a fresh cooldown")) return false;
        return require(waitHealth(engine, Health::Healthy)
            && engine.device.lastOpenTime.load() - firstAttempt >= 500000000,
            "next recovery waits another full native 500ms");
    });
}
bool verifyStopCancelsUnavailableRecovery()
{
    return withBgm([](T::AwaitAudioEngine& engine) {
        if (!failDevice(engine)) return false;
        if (!require(engine.processBackgroundMusicControl(PlaybackStopped{}) && !T::lane(engine, Lane::PlaylistBgm),
            "Stop waits for actual BGM retirement")) return false;
        const auto opens = engine.device.openCalls.load();
        std::this_thread::sleep_for(std::chrono::milliseconds{550});
        return require(engine.device.openCalls.load() == opens && T::audioState(engine).activeVoices == 0,
            "no-source Stop cancels scheduled reopen");
    });
}
bool verifyContractFaultDoesNotRetry()
{
    return withBgm([](T::AwaitAudioEngine& engine) {
        const auto original = token(engine);
        if (!failDevice(engine, Status::InvalidRequest)) return false;
        const auto opens = engine.device.openCalls.load();
        std::this_thread::sleep_for(std::chrono::milliseconds{550});
        return require(T::audioState(engine).health == Health::ContractFaulted
            && engine.device.openCalls.load() == opens && token(engine) == original,
            "non-device contract fault stays latched without losing BGM identity");
    });
}
bool verifyNonDeviceBridgeStatusDoesNotRetryAndConfigRemainsLive()
{
    const Event gain = PlaylistMusicConfigChanged{PlaylistId{"recovery-playlist"},
        QueueSnapshot{{TrackSnapshot{TrackId{"recovery-track"}, fixturePath(), 0.4}}, QueueMode::ListLoop, 1.0}};
    if (!withBgm([&](T::AwaitAudioEngine& engine) {
        if (!failDevice(engine)) return false;
        const auto original = token(engine); const auto opens = engine.device.openCalls.load();
        return require(engine.processBackgroundMusicControl(gain) && token(engine) == original
            && engine.device.openCalls.load() == opens && !healthy(engine)
            && waitHealth(engine, Health::Healthy) && token(engine) == original,
            "gain configuration remains live during a real device outage and survives recovery");
    })) return false;
    if (!withBgm([&](T::AwaitAudioEngine& engine) {
        if (!failDevice(engine, Status::InvalidRequest)) return false;
        const auto original = token(engine);
        const auto opens = engine.device.openCalls.load();
        return require(engine.processBackgroundMusicControl(gain) && token(engine) == original
            && engine.device.openCalls.load() == opens && T::audioState(engine).health == Health::ContractFaulted,
            "C still accepts gain policy without clearing A contract fault or replacing source");
    })) return false;
    return withBgm([](T::AwaitAudioEngine& engine) {
        const auto original = token(engine); const auto opens = engine.device.openCalls.load();
        engine.device.availabilityResult.store(Status::NotInitialized);
        if (!T::waitUntil(engine, [&] { return engine.device.availabilityResult.load() == Status::Success; })) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds{30});
        return require(healthy(engine) && engine.device.openCalls.load() == opens && token(engine) == original,
            "OutputInactive remains a diagnostic mismatch without creating recovery or losing BGM");
    });
}
bool verifyPausedAndResumedRecovery()
{
    const bool paused = withBgm([](T::AwaitAudioEngine& engine) {
        const auto original = token(engine);
        engine.device.pauseResult.store(Status::DeviceFailure);
        if (!require(engine.processBackgroundMusicControl(PlaybackPaused{})
            && waitHealth(engine, Health::DeviceUnavailable), "actual A pause failure enters unavailable")) return false;
        const auto written = engine.device.written.load(); engine.device.grant(64);
        return require(waitHealth(engine, Health::Healthy) && engine.device.paused.load()
            && engine.device.written.load() == written && token(engine) == original
            && T::audioState(engine).transport == Audio::AudioTransport::Paused,
            "paused failure recovery configures paused and never consumes PCM");
    });
    if (!paused) return false;
    return withBgm([](T::AwaitAudioEngine& engine) {
        const auto original = token(engine);
        if (!engine.processBackgroundMusicControl(PlaybackPaused{})) return false;
        engine.device.resumeResult.store(Status::DeviceFailure);
        return require(engine.processBackgroundMusicControl(PlaybackResumed{})
            && waitHealth(engine, Health::DeviceUnavailable) && waitHealth(engine, Health::Healthy)
            && !engine.device.paused.load() && token(engine) == original
            && T::audioState(engine).transport == Audio::AudioTransport::Playing,
            "actual A resume failure recovers Running with the original session");
    });
}

bool verifyEmbeddedPresenceSurvivesOutageUntilRecoveryPreparation()
{
    return withBgm([](T::AwaitAudioEngine& engine) {
        if (!require(installEmbedded(engine), "real embedded PCM fixture prepared")) return false;
        const auto embedded = *T::lane(engine, Lane::Embedded);
        if (!failDevice(engine)) return false;
        return require(T::lane(engine, Lane::Embedded)->source == embedded.source
            && waitHealth(engine, Health::Healthy) && T::lane(engine, Lane::Embedded)->source == embedded.source,
            "outage/recovery retains immutable embedded registration and resyncs its cursor");
    });
}
bool verifyEmbeddedAndBgmRecoveryPreservesBgm()
{
    return withBgm([](T::AwaitAudioEngine& engine) {
        if (!installEmbedded(engine)) return false;
        const auto original = token(engine);
        const auto source = T::lane(engine, Lane::PlaylistBgm)->source;
        return require(failDevice(engine) && waitHealth(engine, Health::Healthy)
            && T::lane(engine, Lane::PlaylistBgm)->source == source && token(engine) == original,
            "embedded resync does not re-open/re-token surviving BGM");
    });
}
bool verifyDefaultEndpointChangeReroutesRunningBgm()
{
    return withBgm([](T::AwaitAudioEngine& engine) {
        const auto original = token(engine); const auto opens = engine.device.openCalls.load();
        engine.device.endpoint.fetch_add(1);
        return require(T::waitUntil(engine, [&] { return engine.device.openCalls.load() == opens + 1 && engine.device.configured.load() && healthy(engine); })
            && token(engine) == original && T::lane(engine, Lane::PlaylistBgm),
            "running endpoint notification reroutes same source on A");
    });
}
bool verifyDefaultEndpointChangeReroutesPausedOutputWithoutPull()
{
    return withBgm([](T::AwaitAudioEngine& engine) {
        const auto original = token(engine);
        if (!engine.processBackgroundMusicControl(PlaybackPaused{})) return false;
        const auto configure = engine.device.configureCalls.load(); const auto written = engine.device.written.load();
        engine.device.endpoint.fetch_add(1);
        if (!T::waitUntil(engine, [&] { return engine.device.configureCalls.load() == configure + 1 && engine.device.configured.load() && healthy(engine); })) return false;
        engine.device.grant(64); std::this_thread::sleep_for(std::chrono::milliseconds{20});
        return require(engine.device.written.load() == written && engine.device.paused.load()
            && token(engine) == original && T::audioState(engine).transport == Audio::AudioTransport::Paused,
            "paused endpoint reroute configures paused without PCM pulls or token replacement");
    });
}
bool verifyDefaultEndpointChangeWhileStoppedInvalidatesWithoutReopen()
{
    return withBgm([](T::AwaitAudioEngine& engine) {
        if (!engine.processBackgroundMusicControl(PlaybackStopped{})) return false;
        const auto opens = engine.device.openCalls.load(); engine.device.endpoint.fetch_add(1);
        std::this_thread::sleep_for(std::chrono::milliseconds{20});
        return require(engine.device.openCalls.load() == opens && T::audioState(engine).activeVoices == 0,
            "stopped endpoint notification never creates an unnecessary device");
    });
}
bool verifyDefaultEndpointChangeImmediatelyRetriesDeviceUnavailable()
{
    return withBgm([](T::AwaitAudioEngine& engine) {
        if (!failDevice(engine)) return false;
        const auto opens = engine.device.openCalls.load();
        const auto began = Audio::audioMonotonicNow(); engine.device.endpoint.fetch_add(1);
        if (!T::waitUntil(engine, [&] { return engine.device.openCalls.load() > opens && healthy(engine); })) return false;
        return require(Audio::audioMonotonicNow() - began < 500000000,
            "new endpoint notification triggers immediate reroute without waiting failed endpoint cooldown");
    });
}
bool verifyDefaultEndpointChangeDoesNotClearContractFault()
{
    return withBgm([](T::AwaitAudioEngine& engine) {
        if (!failDevice(engine, Status::InvalidRequest)) return false;
        const auto opens = engine.device.openCalls.load(); engine.device.endpoint.fetch_add(1);
        std::this_thread::sleep_for(std::chrono::milliseconds{20});
        return require(engine.device.openCalls.load() == opens && T::audioState(engine).health == Health::ContractFaulted,
            "endpoint notification cannot clear latched contract fault");
    });
}
bool verifyEmbeddedAndBgmDefaultEndpointReroutePreservesBgm()
{
    return withBgm([](T::AwaitAudioEngine& engine) {
        if (!installEmbedded(engine)) return false;
        const auto embedded = T::lane(engine, Lane::Embedded)->source;
        const auto original = token(engine); const auto opens = engine.device.openCalls.load();
        engine.device.endpoint.fetch_add(1);
        return require(T::waitUntil(engine, [&] { return engine.device.openCalls.load() == opens + 1 && engine.device.configured.load() && healthy(engine); })
            && T::lane(engine, Lane::Embedded)->source == embedded && token(engine) == original,
            "default reroute preserves BGM token and immutable embedded asset");
    });
}
bool verifyGenerationArrivingDuringRerouteWaitsForNextUpdate()
{
    return withBgm([](T::AwaitAudioEngine& engine) {
        const auto opens = engine.device.openCalls.load();
        engine.device.bumpEndpointOnConfigure.store(true); engine.device.endpoint.fetch_add(1);
        return require(T::waitUntil(engine, [&] { return engine.device.openCalls.load() == opens + 2 && engine.device.configured.load() && healthy(engine); }),
            "generation arriving during configure is handled by a later A turn, not recursive reroute");
    });
}
bool rerouteFailure(bool openFailure)
{
    return withBgm([&](T::AwaitAudioEngine& engine) {
        const auto original = token(engine);
        if (openFailure) engine.device.failOpen.store(1); else engine.device.configureResult.store(Status::DeviceFailure);
        engine.device.endpoint.fetch_add(1);
        return require(waitHealth(engine, Health::DeviceUnavailable) && waitHealth(engine, Health::Healthy)
            && token(engine) == original && T::lane(engine, Lane::PlaylistBgm),
            "reroute open/configure failure enters ordinary timed recovery without replacing source");
    });
}
bool verifyDefaultEndpointRerouteFailureUsesNormalRecoveryState() { return rerouteFailure(true); }
bool verifyDefaultEndpointRerouteBeginFailureUsesNormalRecoveryState() { return rerouteFailure(false); }

bool verifyFailureHoldAdoptsRateAndPauseAppliedAfterLastControlPoll()
{
    using EngineType = DesktopSceneEngine::Engine;
    Domain::Project project{}; T::AwaitAudioEngine engine;
    Access::preparePolicy(engine, project);
    if (!require(T::settle(engine) && installEmbedded(engine)
        && engine.processBackgroundMusicControl(makeStartEvent()), "Stale-C gate fixture uses real P PCM and live BGM")) return false;
    auto& control = Access::control(engine);
    const auto original = T::lane(engine, Lane::Embedded)->source;
    const auto bgm = token(engine);
    const auto stale = control.clock;
    const auto confirmed = control.lastConfirmedPosition;
    const auto operation = engine.beginAudioEffect(EngineType::AudioEffectScope::Structural);
    if (!require(operation.has_value(), "Rate preparation reserves the normal Engine operation")) return false;
    Access::prepareEmbeddedRate(engine, fixturePath().parent_path() / "SequentialAudioSourceCompressed.flac", 0.5F);
    (void)engine.finishAudioEffect(*operation);
    Audio::AudioEffectTicket rateTicket{};
    if (!T::waitUntil(engine, [&] {
        for (const auto& source : control.sources)
            if (source.occupied && source.submitted && !source.retiring
                && source.key.lane == Lane::Embedded && source.key != original)
            { rateTicket = source.ticket; return rateTicket.valid(); }
        return false;
    })) return false;
    // Stop polling C exactly after its real prepared view is queued. Observe
    // A receipts directly until both authorized effects have actually applied.
    const auto waitAudioOnly = [](auto predicate) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds{10};
        do { if (predicate()) return true; std::this_thread::sleep_for(std::chrono::milliseconds{1}); }
        while (std::chrono::steady_clock::now() < end);
        return false;
    };
    if (!require(waitAudioOnly([&] { return control.execution.receipt(rateTicket).has_value(); }),
        "A applies real prepared RateContinuous while C remains unpolled")) return false;
    const auto rateReceipt = *control.execution.receipt(rateTicket);
    if (!require(rateReceipt.result == Audio::AudioEffectResult::Applied && rateReceipt.clock.rate == 0.5,
        "The rate receipt proves an actual half-speed effect")) return false;
    const auto pauseTicket = control.execution.reserve(Audio::AudioEffectClass::Structural, 0x0F, operation->sequence);
    if (!require(pauseTicket.has_value(), "Same bounded A operation group admits the already-authorized Pause")) return false;
    Audio::AudioCommand pause{};
    pause.kind = Audio::AudioCommandKind::SetTransport; pause.clock = rateReceipt.clock;
    pause.clock.timingVersion = ++control.timing; pause.expectedTimingVersion = rateReceipt.clock.timingVersion;
    pause.clock.mode = Audio::AudioClockMode::Paused; pause.transport = Audio::AudioTransport::Paused;
    pause.playbackLifetime = control.playbackLifetime;
    if (!require(control.execution.enqueue(*pauseTicket, pause)
        && waitAudioOnly([&] { return control.execution.receipt(*pauseTicket).has_value(); }),
        "A applies Pause through its public command port before C resumes")) return false;
    const auto pauseReceipt = *control.execution.receipt(*pauseTicket);
    const bool paused = pauseReceipt.result == Audio::AudioEffectResult::Applied
        && pauseReceipt.transport == Audio::AudioTransport::Paused && pauseReceipt.clock.rate == 0.5;
    (void)control.execution.releaseReceipt(*pauseTicket);
    if (!require(paused && control.clock.timingVersion == stale.timingVersion && control.clock.rate == stale.rate,
        "C demonstrably still holds the older running/rate anchor after A Rate/Pause effects")) return false;
    control.holdFailure(engine, confirmed, confirmed + 0.5);
    if (!require(T::settle(engine), "Reserved failure gate and prior rate receipt are durably consumed")) return false;
    const auto actual = T::audioState(engine);
    return require(control.failureHeld && control.clock.mode == Audio::AudioClockMode::FailureHold
        && control.clock.rate == 0.5 && actual.clock.rate == 0.5
        && actual.clock.mode == Audio::AudioClockMode::FailureHold && actual.transport == Audio::AudioTransport::Paused
        && control.clock.position == confirmed && control.lastConfirmedPosition == confirmed
        && engine.playbackSpeed() == 0.5F && engine.isPaused()
        && Core::BackgroundMusicEngineTestAccess::runtime(engine).playbackState() == Music::BackgroundMusicPlaybackState::Paused
        && token(engine) == bgm,
        "FailureHold preserves A's applied rate/Pause and synchronizes actual rate, pause and BGM policy back to stale C");
}
}

int main()
{
    const bool passed = verifyDeviceFailureDoesNotRecoverBeforeCooldown()
        && verifyFailureDoesNotConsumePreFailureUpdateDelta()
        && verifyInitialAndBeginDeviceFailuresUseRecovery()
        && verifyFailedRecoveryResetsCooldown()
        && verifyStopCancelsUnavailableRecovery()
        && verifyContractFaultDoesNotRetry()
        && verifyNonDeviceBridgeStatusDoesNotRetryAndConfigRemainsLive()
        && verifyPausedAndResumedRecovery()
        && verifyEmbeddedPresenceSurvivesOutageUntilRecoveryPreparation()
        && verifyEmbeddedAndBgmRecoveryPreservesBgm()
        && verifyDefaultEndpointChangeReroutesRunningBgm()
        && verifyDefaultEndpointChangeReroutesPausedOutputWithoutPull()
        && verifyDefaultEndpointChangeWhileStoppedInvalidatesWithoutReopen()
        && verifyDefaultEndpointChangeImmediatelyRetriesDeviceUnavailable()
        && verifyDefaultEndpointChangeDoesNotClearContractFault()
        && verifyEmbeddedAndBgmDefaultEndpointReroutePreservesBgm()
        && verifyGenerationArrivingDuringRerouteWaitsForNextUpdate()
        && verifyDefaultEndpointRerouteFailureUsesNormalRecoveryState()
        && verifyDefaultEndpointRerouteBeginFailureUsesNormalRecoveryState()
        && verifyFailureHoldAdoptsRateAndPauseAppliedAfterLastControlPoll();
    if (passed) std::cout << "AudioOutputRecoveryEngineTest CP1 PASS (19 mapped scenarios + stale-C failure gate)\n";
    return passed ? 0 : 1;
}
