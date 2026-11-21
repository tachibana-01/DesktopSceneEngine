#pragma once

#include "Core/EngineAudioControl.h"
#include "Audio/AudioExecutionTestDevice.h"
#include "Music/BackgroundMusicRuntime.h"
#include "Music/BackgroundMusicOrchestrator.h"
#include "Runtime/Playback/PlaybackRuntime.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <thread>
#include <vector>

namespace DesktopSceneEngine::Core
{
class AudioOutputRecoveryEngineTestAccess
{
public:
    static void installDevice(Engine& engine, Audio::AudioExecutionTestDevice& device)
    { engine.audioControl_ = std::make_unique<Engine::AudioControl>(device); }
    static auto& control(Engine& engine) { return *engine.audioControl_; }
    static const auto& control(const Engine& engine) { return *engine.audioControl_; }
    static float confirmedPosition(const Engine& engine)
    { return static_cast<float>(engine.audioControl_->lastConfirmedPosition); }
    static void preparePolicy(Engine& engine, const Domain::Project& project, float duration = 600, bool paused = false)
    {
        const auto operation = engine.beginAudioEffect(Engine::AudioEffectScope::Structural);
        if (!operation) { std::cerr << "Fixture policy admission failed\n"; return; }
        engine.initialized_ = true;
        engine.projectBound_ = true;
        engine.boundProject_ = &project;
        engine.playbackRuntime_ = std::make_unique<Runtime::PlaybackRuntime>();
        Domain::Playback playback{}; playback.speed = 1; playback.loop = true;
        engine.playbackRuntime_->configure(playback, duration);
        if (paused) engine.playbackRuntime_->pause();
        engine.audioControl_->configureClock(engine, 0, paused);
        (void)engine.finishAudioEffect(*operation);
    }
    static void prepareEmbedded(Engine& engine, const std::filesystem::path& path, float rate = 1)
    { engine.audioControl_->prepareEmbedded(engine, path, rate, Engine::AudioControl::SourcePurpose::EmbeddedBind); }
    static void prepareEmbeddedRate(Engine& engine, const std::filesystem::path& path, float rate)
    { engine.audioControl_->prepareEmbedded(engine, path, rate, Engine::AudioControl::SourcePurpose::EmbeddedRate); }
    static std::optional<Engine::AudioEffectToken> prepareHeadlessAudio(Engine& engine)
    {
        const auto operation = engine.beginAudioEffect(Engine::AudioEffectScope::Structural);
        if (!operation) { std::cerr << "Fixture bootstrap admission failed\n"; return std::nullopt; }
        auto& control = *engine.audioControl_;
        control.configureClock(engine, 0, false);
        // Headless BGM tests have no visual document. Their explicit C-domain
        // test scene clock is authorized through the same captured Bind path.
        control.plannedClock.duration = 600;
        if (control.pendingBinding) control.pendingBinding->clock.duration = 600;
        (void)engine.finishAudioEffect(*operation);
        return operation;
    }
};
class BackgroundMusicEngineTestAccess
{
public:
    static auto& mutableRuntime(Engine& engine) { return *engine.backgroundMusicRuntime_; }
    static const auto& runtime(const Engine& engine) { return *engine.backgroundMusicRuntime_; }
    static auto processOriginalOrchestrator(Engine& engine, Music::MusicRuntimeEvent event)
    { return engine.backgroundMusicOrchestrator_->processMusicEvent(std::move(event)); }
};
}

namespace DesktopSceneEngine::Tests
{
// A bounded external device seam. The production A loop, mixer, prepared-source
// inputs and receipts run unchanged. Test grants are device readiness, never C
// calls into the mixer. This fixture makes no physical audibility claim.
class AudioCaptureDevice final : public Audio::AudioExecutionTestDevice
{
public:
    static constexpr std::size_t CaptureFrames = 262144;
    AudioCaptureDevice() : capture_(CaptureFrames * 2) {}
    bool open() noexcept override
    {
        ++openCalls;
        lastOpenTime.store(Audio::audioMonotonicNow());
        if (consume(failOpen)) return false;
        opened.store(true); return true;
    }
    void close() noexcept override { ++closeCalls; opened.store(false); configured.store(false); }
    Audio::AudioBlockStatus configure(bool pausedValue) noexcept override
    {
        ++configureCalls;
        if (bumpEndpointOnConfigure.exchange(false)) ++endpoint;
        const auto result = configureResult.exchange(Audio::AudioBlockStatus::Success);
        if (result == Audio::AudioBlockStatus::Success)
        { configured.store(true); paused.store(pausedValue); }
        return result;
    }
    Audio::AudioBlockStatus pause() noexcept override
    {
        ++pauseCalls;
        const auto result = pauseResult.exchange(Audio::AudioBlockStatus::Success);
        if (result == Audio::AudioBlockStatus::Success) paused.store(true);
        return result;
    }
    Audio::AudioBlockStatus resume() noexcept override
    {
        ++resumeCalls;
        const auto result = resumeResult.exchange(Audio::AudioBlockStatus::Success);
        if (result == Audio::AudioBlockStatus::Success) paused.store(false);
        return result;
    }
    void stop() noexcept override { ++stopCalls; configured.store(false); credits.store(0); }
    Audio::AudioDeviceMuteResult setMuted(bool value) noexcept override
    {
        const auto result = muteResult.exchange(Audio::AudioDeviceMuteResult::Applied);
        if (result == Audio::AudioDeviceMuteResult::Applied) muted.store(value);
        return result;
    }
    std::size_t bufferFrames() const noexcept override { return 4800; }
    std::uint64_t queuedFrames() const noexcept override { return 0; }
    std::uint64_t underflowCount() const noexcept override { return 0; }
    std::uint64_t endpointGeneration() const noexcept override { return endpoint.load(); }
    Audio::AudioBlockAvailability writableFrames() noexcept override
    {
        ++availabilityCalls;
        const auto fault = availabilityResult.exchange(Audio::AudioBlockStatus::Success);
        if (fault != Audio::AudioBlockStatus::Success)
        { lastFaultTime.store(Audio::audioMonotonicNow()); return {0, fault}; }
        if (!opened.load() || !configured.load()) return {0, Audio::AudioBlockStatus::NotInitialized};
        if (paused.load()) return {0, Audio::AudioBlockStatus::Paused};
        return {static_cast<std::size_t>((std::min)(credits.load(), std::uint64_t{4800})), Audio::AudioBlockStatus::Success};
    }
    Audio::AudioBlockStatus writeFrames(std::span<const std::int16_t> samples) noexcept override
    {
        const auto fault = writeResult.exchange(Audio::AudioBlockStatus::Success);
        if (fault != Audio::AudioBlockStatus::Success) return fault;
        const auto frames = samples.size() / 2;
        const auto offset = written.load(std::memory_order_relaxed);
        if (samples.size() % 2 || frames > credits.load() || offset + frames > CaptureFrames)
            return Audio::AudioBlockStatus::InvalidRequest;
        if (blockNextWrite.exchange(false))
        {
            blockedWriteEntered.store(true,std::memory_order_release);
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{5};
            while (!releaseBlockedWrite.load(std::memory_order_acquire))
            {
                if (std::chrono::steady_clock::now()>=deadline)
                { blockedWriteTimedOut.store(true); return Audio::AudioBlockStatus::DeviceFailure; }
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
            }
        }
        for (std::size_t i = 0; i < samples.size(); ++i) capture_[offset * 2 + i] = muted.load() ? 0 : samples[i];
        credits.fetch_sub(frames);
        ++writeCalls;
        written.store(offset + frames, std::memory_order_release);
        return Audio::AudioBlockStatus::Success;
    }
    void grant(std::size_t frames) noexcept { credits.fetch_add(frames); }
    void armBlockedWrite() noexcept
    { blockedWriteEntered.store(false);blockedWriteTimedOut.store(false);releaseBlockedWrite.store(false);blockNextWrite.store(true); }
    void releaseWrite() noexcept { releaseBlockedWrite.store(true,std::memory_order_release); }
    std::vector<std::int16_t> samples(std::uint64_t offset, std::size_t count) const
    {
        if (offset + count > written.load(std::memory_order_acquire)) return {};
        return {capture_.begin() + offset * 2, capture_.begin() + (offset + count) * 2};
    }

    std::atomic_uint64_t openCalls{0}, closeCalls{0}, configureCalls{0}, pauseCalls{0}, resumeCalls{0},
        stopCalls{0}, availabilityCalls{0}, writeCalls{0}, written{0}, credits{0};
    std::atomic_uint failOpen{0};
    std::atomic_uint64_t endpoint{0};
    std::atomic_bool bumpEndpointOnConfigure{false};
    std::atomic_bool blockNextWrite{false},blockedWriteEntered{false},releaseBlockedWrite{true},blockedWriteTimedOut{false};
    std::atomic<Audio::AudioMonotonicTime> lastOpenTime{0}, lastFaultTime{0};
    std::atomic_bool opened{false}, configured{false}, paused{false}, muted{false};
    std::atomic<Audio::AudioBlockStatus> configureResult{Audio::AudioBlockStatus::Success},
        pauseResult{Audio::AudioBlockStatus::Success}, resumeResult{Audio::AudioBlockStatus::Success},
        availabilityResult{Audio::AudioBlockStatus::Success}, writeResult{Audio::AudioBlockStatus::Success};
    std::atomic<Audio::AudioDeviceMuteResult> muteResult{Audio::AudioDeviceMuteResult::Applied};
private:
    static bool consume(std::atomic_uint& value) noexcept
    {
        auto count = value.load();
        while (count) if (value.compare_exchange_weak(count, count - 1)) return true;
        return false;
    }
    std::vector<std::int16_t> capture_;
};

template<class Predicate> bool waitUntil(Engine& engine, Predicate predicate, std::chrono::milliseconds timeout = std::chrono::seconds{10})
{
    const auto end = std::chrono::steady_clock::now() + timeout;
    do
    {
        engine.pollAudioExecution();
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    } while (std::chrono::steady_clock::now() < end);
    return false;
}
inline bool settle(Engine& engine)
{
    return waitUntil(engine, [&] {
        const auto& control = Core::AudioOutputRecoveryEngineTestAccess::control(engine);
        if (control.capture || control.pendingBinding || control.internalCount || control.gateRequest || control.gateDirty) return false;
        for (const auto& operation : control.effects) if (operation.result.pending()) return false;
        for (const auto& source : control.sources)
            if (source.occupied && (source.retiring || !source.submitted || !source.startedDelivered))
            {
                if (source.key.lane == Audio::AudioLane::Embedded && source.submitted && !source.retiring) continue;
                return false;
            }
        return true;
    });
}
inline std::optional<Audio::AudioSourceObservation> lane(Engine& engine, Audio::AudioLane value)
{
    auto& control = Core::AudioOutputRecoveryEngineTestAccess::control(engine);
    for (const auto& source : control.sources)
        if (source.occupied && !source.retiring && source.key.lane == value)
            if (auto observation = control.execution.sourceObservation(source.key); observation && observation->registered)
                return observation;
    return std::nullopt;
}
inline Audio::AudioExecutionSnapshot audioState(const Engine& engine)
{ return Core::AudioOutputRecoveryEngineTestAccess::control(engine).execution.snapshot(); }

template<class Action> bool effect(Engine& engine, Action action, Engine::AudioEffectScope scope = Engine::AudioEffectScope::Structural)
{
    if (!settle(engine)) return false;
    const auto operation = engine.beginAudioEffect(scope);
    if (!operation) return false;
    const bool accepted = action();
    (void)engine.finishAudioEffect(*operation, accepted);
    if (!waitUntil(engine, [&] { return engine.audioEffect(*operation).terminal(); }) || !settle(engine)) return false;
    const auto receipt = engine.audioEffect(*operation);
    if (accepted && receipt.phase == Engine::AudioEffectPhase::Failed)
        std::cerr << "Fixture observed real failed effect sequence=" << operation->sequence
            << " scope=" << static_cast<unsigned>(scope) << " error=" << receipt.error << '\n';
    return accepted;
}
inline bool shutdown(Engine& engine)
{
    engine.beginAudioShutdown();
    if (!waitUntil(engine, [&] { return engine.audioShutdownComplete(); })) return false;
    engine.shutdown();
    return true;
}
inline bool readOutput(Engine& engine, AudioCaptureDevice& device, std::span<float> output)
{
    const auto start = device.written.load(std::memory_order_acquire);
    const auto frames = output.size() / 2;
    if (output.size() % 2 || start + frames > AudioCaptureDevice::CaptureFrames) return false;
    device.grant(frames);
    if (!waitUntil(engine, [&] {
        return device.written.load(std::memory_order_acquire) >= start + frames
            && audioState(engine).writtenFrames >= start + frames;
    })) return false;
    const auto samples = device.samples(start, frames);
    for (std::size_t i = 0; i < samples.size(); ++i)
        output[i] = samples[i] < 0 ? static_cast<float>(samples[i]) / 32768.0F : static_cast<float>(samples[i]) / 32767.0F;
    return true;
}

// Sequential assertion adapter: every call waits native durable terminal facts.
// Its bool preserves logical admission; tests inspect actual sources/output and
// clock receipts separately rather than interpreting admission as an effect.
class AwaitAudioEngine : public Engine
{
public:
    AudioCaptureDevice device;
    AwaitAudioEngine()
    {
        Core::AudioOutputRecoveryEngineTestAccess::installDevice(*this, device);
        const auto bootstrap=Core::AudioOutputRecoveryEngineTestAccess::prepareHeadlessAudio(*this);
        const bool applied=bootstrap && settle(*this) && audioEffect(*bootstrap).phase==AudioEffectPhase::Applied;
        const bool matching=applied && waitUntil(*this,[&] {
            const auto& authorized=Core::AudioOutputRecoveryEngineTestAccess::control(*this).clock;
            const auto actual=audioState(*this).clock;
            return authorized.valid() && actual.valid() && actual.binding==authorized.binding
                && actual.timingVersion==authorized.timingVersion && actual.position==authorized.position
                && actual.time==authorized.time && actual.rate==authorized.rate
                && actual.duration==authorized.duration && actual.mode==authorized.mode;
        });
        if (!matching)
        {
            std::cerr << "Fixture bootstrap did not produce an Applied, valid matching C/A anchor\n";
            // A must stop while this external device member is still alive;
            // constructor unwinding otherwise destroys it before Engine's base.
            if (!Tests::shutdown(*this)) std::terminate();
            throw std::runtime_error("Actual audio execution bootstrap failed");
        }
    }
    ~AwaitAudioEngine() { (void)Tests::shutdown(*this); }
    bool processBackgroundMusicControl(const BackgroundMusicControl::Event& event)
    { return effect(*this, [&] { return Engine::processBackgroundMusicControl(event); }); }
    bool bindProject(const Domain::Project& project, const Domain::EntityId& scene)
    { return effect(*this, [&] { return Engine::bindProject(project, scene); }); }
    bool seek(float time)
    { return effect(*this, [&] { return Engine::seek(time); }); }
    void setPlaybackSpeed(float rate)
    { (void)effect(*this, [&] { Engine::setPlaybackSpeed(rate); return true; }); }
    void setVolume(float gain)
    { (void)effect(*this, [&] { Engine::setVolume(gain); return true; }, AudioEffectScope::EmbeddedGain); }
    void togglePause()
    { (void)effect(*this, [&] { Engine::togglePause(); return true; }); }
    void toggleMute()
    { (void)effect(*this, [&] { Engine::toggleMute(); return true; }, AudioEffectScope::OutputMute); }
    void shutdown() { (void)Tests::shutdown(*this); }
};
}
