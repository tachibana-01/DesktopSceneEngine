#include "EngineAudioControl.h"

#include "Music/BackgroundMusicOrchestrator.h"
#include "Runtime/Playback/PlaybackRuntime.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <type_traits>

namespace DesktopSceneEngine
{
namespace
{
std::atomic<std::uint64_t> nextExecution{1};
using Lane = Audio::AudioLane;
using Command = Audio::AudioCommand;
using Kind = Audio::AudioCommandKind;
using Phase = Engine::AudioEffectPhase;
std::size_t index(Engine::AudioEffectScope scope) noexcept
{ return static_cast<std::size_t>(scope); }
std::uint8_t mask(Engine::AudioEffectScope scope) noexcept
{
    return scope == Engine::AudioEffectScope::Structural ? 0x0FU
        : static_cast<std::uint8_t>(1U << (index(scope) - 1U));
}
}

Engine::AudioControl::AudioControl() : incarnation(nextExecution.fetch_add(1)) {}
Engine::AudioControl::AudioControl(Audio::AudioExecutionTestDevice& device)
    : execution(device), incarnation(nextExecution.fetch_add(1)) {}
Engine::AudioControl::~AudioControl() = default;

Engine::AudioControl::Effect* Engine::AudioControl::find(AudioEffectToken token) noexcept
{
    if (token.execution != incarnation) return nullptr;
    for (auto& value : effects) if (value.result.token == token) return &value;
    return nullptr;
}
const Engine::AudioControl::Effect* Engine::AudioControl::find(AudioEffectToken token) const noexcept
{ return const_cast<AudioControl*>(this)->find(token); }

bool Engine::AudioControl::start() noexcept
{
    if (closing) return false;
    if (!started) started = execution.start();
    return started;
}

std::optional<Engine::AudioEffectToken> Engine::AudioControl::begin(AudioEffectScope scope) noexcept
{
    if (closing || capture || !start()) return std::nullopt;
    auto& value = effects[index(scope)];
    if (value.result.pending()) return std::nullopt;
    // Admission is a C decision as well as an A reservation. Keep the barrier
    // until preparations and their reliable results have also been consumed.
    if (scope == AudioEffectScope::Structural)
    {
        for (const auto& other : effects) if (other.result.pending()) return std::nullopt;
        // Structural admission reserves a possible successor in each lane.
        // A retiring producer may legitimately keep its old slot occupied.
        for (std::size_t lane = 0; lane < 3; ++lane)
            if ((sources[lane * 2].occupied || terminalFacts[lane * 2])
                && (sources[lane * 2 + 1].occupied || terminalFacts[lane * 2 + 1])) return std::nullopt;
    }
    else if (effects[0].result.pending()) return std::nullopt;
    Effect candidate{};
    candidate.result = {{incarnation, ++nextOperation}, scope, Phase::Preparing, {}, 0};
    candidate.open = true;
    const auto count = scope == AudioEffectScope::Structural ? 8U : 1U;
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto ticket = execution.reserve(static_cast<Audio::AudioEffectClass>(scope),
            mask(scope), candidate.result.token.sequence);
        if (!ticket)
        {
            for (std::size_t j = 0; j < candidate.reserved; ++j)
            {
                (void)execution.cancelReserved(candidate.tickets[j]);
                (void)execution.releaseReceipt(candidate.tickets[j]);
            }
            return std::nullopt;
        }
        candidate.tickets[candidate.reserved++] = *ticket;
    }
    value = std::move(candidate);
    capture = value.result.token;
    ++capacityRevision;
    return capture;
}

Audio::AudioEffectTicket Engine::AudioControl::takeTicket(AudioEffectScope scope)
{
    if (capture)
    {
        auto* value = find(*capture);
        if (value && (value->result.scope == scope || value->result.scope == AudioEffectScope::Structural)
            && value->submitted < value->reserved) return value->tickets[value->submitted++];
        if (value) { value->outcome = Phase::Failed; value->result.error = "audio-capacity"; }
        return {};
    }
    if (internalCount == internalTickets.size()) return {};
    if (scope == AudioEffectScope::Structural && internalGroup == 0)
        internalGroup = (std::uint64_t{1} << 63U) | ++nextOperation;
    const auto ticket = execution.reserve(static_cast<Audio::AudioEffectClass>(scope), mask(scope),
        scope == AudioEffectScope::Structural ? internalGroup : 0);
    if (!ticket) return {};
    internalTickets[internalCount++] = *ticket;
    ++capacityRevision;
    return *ticket;
}

Engine::AudioEffectResult Engine::AudioControl::finish(AudioEffectToken token, bool accepted)
{
    auto* value = find(token);
    if (!value) return effect(token);
    if (pendingBinding && capture == token)
    {
        // The existing C operation may bind then restore pause/rate/position.
        // Compile those finite restoration steps into one authorized binding
        // transition; a prepared future ticket is never an ordering barrier.
        (void)command(*pendingBinding, AudioEffectScope::Structural);
        plannedClock = pendingBinding->clock;
        for (auto& source : sources)
            if (source.occupied && !source.submitted && source.key.lane == Lane::Embedded
                && source.key.mediaBinding == binding && !source.retiring)
            {
                source.expectedTimingVersion = plannedClock.timingVersion;
                source.clock.mode = plannedClock.mode;
                source.clock.position = plannedClock.position;
            }
        pendingBinding.reset();
    }
    value->logicalAccepted = accepted;
    value->open = false;
    if (!accepted && value->submitted == 0) value->outcome = Phase::Rejected;
    for (std::size_t i = value->submitted; i < value->reserved; ++i)
    {
        (void)execution.cancelReserved(value->tickets[i]);
        (void)execution.releaseReceipt(value->tickets[i]);
        value->tickets[i] = {};
    }
    if (capture == token) capture.reset();
    value->result.phase = Phase::Pending;
    ++capacityRevision;
    settleEffects();
    return value->result;
}
void Engine::AudioControl::discardUnusedTicket(Audio::AudioEffectTicket ticket) noexcept
{
    // Only a C-owned, never-enqueued reservation: no prepared view escaped.
    if (!ticket.valid() || !execution.cancelReserved(ticket) || !execution.releaseReceipt(ticket)) return;
    ++capacityRevision;
    for (auto& operation : effects)
        for (auto& owned : operation.tickets) if (owned == ticket) owned = {};
    for (auto& owned : internalTickets) if (owned == ticket) owned = {};
}
Engine::AudioEffectResult Engine::AudioControl::effect(AudioEffectToken token) const
{
    if (const auto* value = find(token)) return value->result;
    return {token, AudioEffectScope::Structural, Phase::Expired, "audio-effect-expired", 0};
}
void Engine::AudioControl::settleEffects()
{
    for (auto& value : effects)
    {
        if (!value.result.pending() || value.open || value.awaitingSources != 0) continue;
        if (value.requiredRetirement && execution.retiredPlaybackLifetime() < value.requiredRetirement) continue;
        bool waiting = false;
        for (std::size_t i = 0; i < value.submitted; ++i) waiting |= value.tickets[i].valid();
        for (const auto& source : sources)
            waiting |= source.occupied && ((source.retiring && source.operation == value.result.token)
                || source.retirementOperation == value.result.token);
        if (!waiting)
        {
            value.result.phase = value.outcome;
            ++capacityRevision;
            if (value.result.scope == AudioEffectScope::Structural && value.outcome != Phase::Applied
                && value.outcome != Phase::DeferredUnavailable)
                plannedClock = clock; // A precommit rejection never creates a future timing authority.
        }
    }
}

bool Engine::AudioControl::command(const Command& commandValue, AudioEffectScope scope)
{
    if (!start()) return false;
    const auto ticket = takeTicket(scope);
    if (!ticket.valid()) return false;
    if (execution.enqueue(ticket, commandValue)) return true;
    (void)execution.cancelReserved(ticket);
    if (capture) if (auto* value = find(*capture))
    { value->outcome = Phase::Failed; value->result.error = "audio-command-rejected"; }
    return false;
}

Engine::AudioControl::Source* Engine::AudioControl::allocate(Lane lane, std::uint64_t sourceToken) noexcept
{
    for (std::size_t i = static_cast<std::size_t>(lane) * 2; i < static_cast<std::size_t>(lane) * 2 + 2; ++i)
    {
        auto& value = sources[i];
        if (value.occupied || terminalFacts[i]) continue;
        value = {};
        value.occupied = true;
        value.key = {lane, static_cast<std::uint8_t>(i), ++nextSource, playbackLifetime,
            sourceToken, lane == Lane::Embedded ? binding : 0};
        return &value;
    }
    return nullptr;
}

void Engine::AudioControl::retire(Lane lane) noexcept
{
    for (auto& source : sources)
    {
        if (!source.occupied || source.key.lane != lane) continue;
        if (capture) source.retirementOperation = *capture;
        if (source.retiring) continue;
        source.retiring = true;
        preparation.cancel(source.key);
        if (source.submitted) (void)execution.requestRetireSource(source.key);
        else
        {
            (void)preparation.markConsumerQuiesced(source.key);
            if (source.ticket.valid()) (void)execution.cancelReserved(source.ticket);
            if (auto* operation = find(source.operation); operation && operation->awaitingSources)
                --operation->awaitingSources;
            source.ticket = {};
        }
    }
}
void Engine::AudioControl::retireAll() noexcept
{ for (auto lane : {Lane::Embedded, Lane::PlaylistBgm, Lane::ItemBgm}) retire(lane); }

void Engine::AudioControl::configureClock(Engine& engine, double positionValue, bool paused)
{
    failureHeld = false;
    embeddedPath.clear(); // A visual Image/no-audio binding cannot inherit an old decoder path.
    lastConfirmedPosition = positionValue;
    plannedClock = {++binding, ++timing, positionValue, Audio::audioMonotonicNow(),
        engine.playbackRuntime_ ? engine.playbackRuntime_->configuration().speed : 1.0F, engine.effectiveDuration(),
        paused ? Audio::AudioClockMode::Paused : Audio::AudioClockMode::Running};
    // Bind is a real media transition already validated by C. A receives that
    // exact authorized transition, including video-only bindings.
    Command cmd{};
    cmd.kind = Kind::ReplaceEmbedded; cmd.replacement = Audio::AudioEmbeddedReplacement::Bind;
    cmd.playbackLifetime = playbackLifetime;
    cmd.clock = plannedClock; cmd.embeddedPresent = false;
    cmd.transport = paused ? Audio::AudioTransport::Paused : Audio::AudioTransport::Playing;
    pendingBinding = cmd;
}

void Engine::AudioControl::prepareEmbedded(Engine&, const std::filesystem::path& path,
    float rate, SourcePurpose purpose)
{
    if (pendingBinding)
    {
        pendingBinding->clock.rate = rate;
        plannedClock.rate = rate;
        for (auto& current : sources)
            if (current.occupied && !current.submitted && !current.retiring
                && current.key.lane == Lane::Embedded && current.key.mediaBinding == binding)
            {
                if (current.clock.rate != rate)
                { current.reprepareRate = rate; preparation.cancel(current.key); }
                current.clock.rate = rate;
                return;
            }
    }
    auto* source = allocate(Lane::Embedded, ++nextSource);
    const auto failure = [&]() {
        if (purpose == SourcePurpose::EmbeddedBind)
        {
            std::fprintf(stderr, "[Engine] optional embedded preparation unavailable; continuing video-only\n");
            return;
        }
        if (capture) if (auto* operation = find(*capture))
        { operation->outcome = Phase::Failed; operation->result.error = "audio-preparation-resource"; }
    };
    if (!source) { failure(); return; }
    source->purpose = purpose; source->path = path; source->clock = plannedClock;
    source->expectedTimingVersion = plannedClock.timingVersion;
    source->clock.rate = rate; source->clock.timingVersion = ++timing;
    source->gain = desiredEmbeddedGain;
    if (capture) source->operation = *capture;
    source->ticket = takeTicket(AudioEffectScope::Structural);
    if (!source->ticket.valid()) { source->occupied = false; failure(); return; }
    Audio::AudioSourceKey original{};
    for (const auto& current : sources)
        if (&current != source && current.occupied && !current.retiring && current.submitted
            && current.key.lane == Lane::Embedded && current.key.mediaBinding == binding)
            original = current.key;
    const auto admitted = preparation.requestEmbedded(source->key, path, rate, original);
    if (admitted != Audio::AudioPreparationAdmission::Admitted)
    {
        if (purpose == SourcePurpose::EmbeddedBind) discardUnusedTicket(source->ticket);
        else (void)execution.cancelReserved(source->ticket);
        source->occupied = false; failure(); return;
    }
    if (auto* operation = find(source->operation)) ++operation->awaitingSources;
    embeddedPath = path;
}

std::optional<Music::MusicRuntimeEvent> Engine::AudioControl::music(const Music::MusicRuntimeAction& action)
{
    return std::visit([&](const auto& value) -> std::optional<Music::MusicRuntimeEvent> {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, Music::StartSource>)
        {
            const auto lane = value.scope == Music::MusicScope::PlaylistGlobal ? Lane::PlaylistBgm : Lane::ItemBgm;
            auto* source = allocate(lane, value.token.value);
            if (!source) return Music::MusicSourceFailed{value.token, Music::MusicSourceFailureClass::TransientResourceFailure,
                BackgroundMusicObservation::FailureStage::ResourceAllocation, BackgroundMusicObservation::FailureCode::ResourceFailure};
            source->purpose = lane == Lane::PlaylistBgm ? SourcePurpose::Playlist : SourcePurpose::Item;
            source->path = value.sourcePath; source->gain = value.effectiveGain;
            if (capture) source->operation = *capture;
            source->ticket = takeTicket(AudioEffectScope::Structural);
            if (!source->ticket.valid()) { source->occupied = false;
                return Music::MusicSourceFailed{value.token, Music::MusicSourceFailureClass::TransientResourceFailure}; }
            const auto admitted = preparation.requestBgm(source->key, source->path);
            if (admitted != Audio::AudioPreparationAdmission::Admitted)
            {
                (void)execution.cancelReserved(source->ticket); source->occupied = false;
                return Music::MusicSourceFailed{value.token, Music::MusicSourceFailureClass::TransientResourceFailure};
            }
            if (auto* operation = find(source->operation)) ++operation->awaitingSources;
        }
        else if constexpr (std::is_same_v<T, Music::StopSource>)
        {
            for (auto& source : sources) if (source.occupied && source.key.sourceToken == value.token.value
                && source.key.lane != Lane::Embedded && !source.retiring)
                retire(source.key.lane);
        }
        else if constexpr (std::is_same_v<T, Music::SetSourceGain>)
        {
            for (auto& source : sources) if (source.occupied && !source.retiring && source.key.lane != Lane::Embedded
                && source.key.sourceToken == value.token.value)
            {
                if (source.gain == value.gain) continue;
                source.gain = value.gain;
                if (!source.submitted) continue;
                Command cmd{}; cmd.kind = Kind::SetSourceGain; cmd.source = source.key; cmd.gain = value.gain;
                source.gainDirty = !command(cmd, source.key.lane == Lane::PlaylistBgm ? AudioEffectScope::PlaylistBgmGain : AudioEffectScope::ItemBgmGain);
            }
        }
        return std::nullopt;
    }, action);
}

void Engine::AudioControl::requestTransport(Engine&, bool paused)
{
    if (!plannedClock.valid()) return;
    const auto mode = paused ? Audio::AudioClockMode::Paused : Audio::AudioClockMode::Running;
    if (pendingBinding)
    {
        pendingBinding->transport = paused ? Audio::AudioTransport::Paused : Audio::AudioTransport::Playing;
        pendingBinding->clock.mode = mode; plannedClock = pendingBinding->clock;
        return;
    }
    if (plannedClock.mode == mode) return;
    Command cmd{}; cmd.kind = Kind::SetTransport; cmd.clock = plannedClock;
    cmd.playbackLifetime = playbackLifetime;
    cmd.clock.timingVersion = ++timing;
    cmd.expectedTimingVersion = plannedClock.timingVersion;
    cmd.transport = paused ? Audio::AudioTransport::Paused : Audio::AudioTransport::Playing;
    cmd.clock.mode = mode;
    if (command(cmd, AudioEffectScope::Structural)) plannedClock = cmd.clock;
}
void Engine::AudioControl::requestSeek(Engine&, double positionValue)
{
    if (pendingBinding)
    { pendingBinding->clock.position = positionValue; plannedClock = pendingBinding->clock; return; }
    Command cmd{}; cmd.kind = Kind::ReplaceEmbedded;
    cmd.playbackLifetime = playbackLifetime;
    cmd.clock = plannedClock; cmd.clock.position = positionValue; cmd.clock.timingVersion = ++timing;
    cmd.clock.mode = plannedClock.mode == Audio::AudioClockMode::Paused ? Audio::AudioClockMode::Paused : Audio::AudioClockMode::Running;
    cmd.expectedTimingVersion = plannedClock.timingVersion;
    cmd.replacement = Audio::AudioEmbeddedReplacement::SeekFixed;
    cmd.embeddedPresent = false;
    cmd.transport = cmd.clock.mode == Audio::AudioClockMode::Paused ? Audio::AudioTransport::Paused : Audio::AudioTransport::Playing;
    for (const auto& source : sources) if (source.occupied && source.key.lane == Lane::Embedded && source.submitted && !source.retiring)
    {
        if (const auto view = preparation.view(source.key))
        { cmd.source = source.key; cmd.prepared = *view; cmd.embeddedPresent = true; cmd.gain = desiredEmbeddedGain; }
    }
    if (command(cmd, AudioEffectScope::Structural)) { failureHeld = false; plannedClock = cmd.clock; }
}

double Engine::AudioControl::position() const noexcept
{ return clock.binding == binding ? Audio::resolveAudioClockPosition(clock, Audio::audioMonotonicNow()) : 0; }

void Engine::AudioControl::holdFailure(Engine&, double positionValue, double target)
{
    if (!clock.valid() || clock.binding != binding || failureHeld) return;
    failureHeld = true; failedTarget = target;
    clock.position = positionValue; clock.time = Audio::audioMonotonicNow();
    clock.mode = Audio::AudioClockMode::FailureHold; clock.timingVersion = ++timing;
    plannedClock = clock;
    // This uses the independent safety cell, never an ordinary command slot.
    gateDesiredHold = true; gateDirty = true;
    if (!gateRequest) { gateRequest = execution.requestEmbeddedFailureGate(clock, target, true); gateDirty = !gateRequest; }
}
void Engine::AudioControl::recoverFailure(Engine& engine, double positionValue)
{
    if (!failureHeld || positionValue < failedTarget) return;
    failureHeld = false;
    clock.position = positionValue; clock.time = Audio::audioMonotonicNow();
    clock.mode = engine.isPaused() ? Audio::AudioClockMode::Paused : Audio::AudioClockMode::Running;
    clock.timingVersion = ++timing;
    plannedClock = clock;
    gateDesiredHold = false; gateDirty = true;
    if (!gateRequest) { gateRequest = execution.requestEmbeddedFailureGate(clock, failedTarget, false); gateDirty = !gateRequest; }
}

void Engine::AudioControl::poll(Engine& engine)
{
    if (polling) return;
    polling = true;
    struct PollExit { bool& flag; ~PollExit() { flag = false; } } pollExit{polling};
    observedExecution = execution.snapshot();
    observationTime = Audio::audioMonotonicNow();
    const auto frontier = observedExecution.lastEffectSequence;
    const auto adopt = [&](const Audio::AudioEffectReceipt& receipt, bool positionCommitted) {
        // Receipts carry the SAME authorized anchor stamped by A. Never infer
        // actual rate/pause from an enqueued request or a preparation result.
        if (receipt.clock.valid() && receipt.clock.binding == binding
            && receipt.clock.timingVersion >= clock.timingVersion)
        {
            clock = receipt.clock;
            if (plannedClock.binding == clock.binding && plannedClock.timingVersion <= clock.timingVersion)
                plannedClock = clock;
            const bool validatedPosition = positionCommitted && (receipt.result == Audio::AudioEffectResult::Applied
                || receipt.result == Audio::AudioEffectResult::DeferredUnavailable) && receipt.command == Kind::ReplaceEmbedded
                && receipt.replacement != Audio::AudioEmbeddedReplacement::RateContinuous;
            if (validatedPosition) lastConfirmedPosition = clock.position;
            if (engine.playbackRuntime_)
            {
                const bool wasPaused = engine.playbackRuntime_->isPaused();
                engine.playbackRuntime_->setSpeed(static_cast<float>(clock.rate));
                if (receipt.transport == Audio::AudioTransport::Paused) engine.playbackRuntime_->pause();
                else if (receipt.transport == Audio::AudioTransport::Playing) engine.playbackRuntime_->resume();
                if (validatedPosition) engine.playbackRuntime_->restoreTime(static_cast<float>(clock.position));
                if (wasPaused != engine.playbackRuntime_->isPaused() && engine.backgroundMusicOrchestrator_)
                {
                    if (engine.playbackRuntime_->isPaused())
                        (void)engine.backgroundMusicOrchestrator_->processMusicEvent(Music::PlaybackPaused{});
                    else (void)engine.backgroundMusicOrchestrator_->processMusicEvent(Music::PlaybackResumed{});
                }
            }
        }
    };
    if (gateRequest)
    {
        if (const auto receipt = execution.embeddedGateReceipt(*gateRequest); receipt && receipt->sequence <= frontier)
        {
            if (!gateDirty) adopt(*receipt, false);
            (void)execution.releaseEmbeddedGateReceipt(*gateRequest); gateRequest.reset();
        }
    }
    if (!gateRequest && gateDirty && !closing)
    { gateRequest = execution.requestEmbeddedFailureGate(clock, failedTarget, gateDesiredHold); gateDirty = !gateRequest; }
    const auto consume = [&](Audio::AudioEffectTicket& ticket, Effect* operation) {
        if (!ticket.valid()) return;
        const auto receipt = execution.receipt(ticket);
        if (!receipt) return;
        if (receipt->sequence > frontier) return;
        if (receipt->command == Kind::SetOutputMute) desiredMuted = receipt->muteDesired;
        if (operation)
        {
            operation->result.effectTimeNs = (std::max)(operation->result.effectTimeNs, receipt->effectTime);
            if (receipt->result == Audio::AudioEffectResult::Failed)
            { operation->outcome = Phase::Failed; operation->result.error = "audio-execution-" + std::to_string(static_cast<unsigned>(receipt->failure)); }
            else if (receipt->result == Audio::AudioEffectResult::DeferredUnavailable && operation->outcome == Phase::Applied)
                operation->outcome = Phase::DeferredUnavailable;
            else if (receipt->result == Audio::AudioEffectResult::CancelledByRetirement
                && operation->outcome == Phase::Applied)
                operation->outcome = Phase::Cancelled;
        }
        adopt(*receipt, true);
        if (operation && receipt->command == Kind::ReplaceEmbedded
            && (receipt->result == Audio::AudioEffectResult::Applied || receipt->result == Audio::AudioEffectResult::DeferredUnavailable))
            for (auto& old : sources)
                if (old.occupied && old.key.lane == Lane::Embedded && old.key != receipt->source)
                    if (const auto retired = execution.sourceObservation(old.key); retired && retired->consumerQuiesced)
                        old.retirementOperation = operation->result.token;
        for (auto& source : sources)
        {
            if (!source.occupied || source.ticket != ticket) continue;
            source.ticket = {};
            if (receipt->result == Audio::AudioEffectResult::Applied || receipt->result == Audio::AudioEffectResult::DeferredUnavailable)
            {
                if (source.key.lane != Lane::Embedded && !source.retiring && !source.startedDelivered)
                {
                    source.startedDelivered = true;
                    (void)engine.backgroundMusicOrchestrator_->processMusicEvent(Music::MusicSourceStarted{{source.key.sourceToken}});
                }
            }
            else if (!source.retiring)
            {
                source.retiring = true; preparation.cancel(source.key);
                (void)execution.requestRetireSource(source.key);
                if (source.key.lane != Lane::Embedded && !source.producerFailureDelivered)
                    source.controlEvent = Music::MusicSourceFailed{{source.key.sourceToken},
                        Music::MusicSourceFailureClass::TransientResourceFailure};
            }
        }
        if (execution.releaseReceipt(ticket)) ++capacityRevision;
        ticket = {};
    };
    for (auto& operation : effects)
        for (std::size_t i = 0; i < operation.submitted; ++i) consume(operation.tickets[i], &operation);
    for (std::size_t i = 0; i < internalCount; ++i) consume(internalTickets[i], nullptr);
    std::size_t remaining = 0;
    for (std::size_t i = 0; i < internalCount; ++i)
        if (internalTickets[i].valid()) internalTickets[remaining++] = internalTickets[i];
    internalCount = remaining;
    if (internalCount == 0) internalGroup = 0;

    for (auto& source : sources)
    {
        if (!source.occupied) continue;
        auto producer = preparation.snapshot(source.key);
        if (source.reprepareRate && !source.submitted && !source.retiring
            && producer.producerQuiesced && producer.consumerQuiesced)
        {
            const auto rate = *source.reprepareRate;
            if (preparation.reclaim(source.key))
            {
                source.key.incarnation = ++nextSource;
                source.reprepareRate.reset();
                if (preparation.requestEmbedded(source.key, source.path, rate) != Audio::AudioPreparationAdmission::Admitted)
                {
                    source.retiring = true;
                    (void)execution.cancelReserved(source.ticket);
                    if (auto* operation = find(source.operation))
                    { if (operation->awaitingSources) --operation->awaitingSources;
                        operation->outcome = Phase::Failed; operation->result.error = "audio-preparation-resource"; }
                    // No new producer record or A-visible reference escaped.
                    // The cancelled ticket remains owned by its parent slot.
                    source = {};
                    continue;
                }
                producer = preparation.snapshot(source.key);
            }
        }
        const auto* sourceOperation = find(source.operation);
        if (!source.submitted && !source.retiring && !source.reprepareRate
            && (!sourceOperation || !sourceOperation->open))
        {
            if (producer.state == Audio::AudioPreparationState::Ready)
            {
                auto view = preparation.view(source.key);
                if (view)
                {
                    Command cmd{}; cmd.source = source.key; cmd.prepared = *view; cmd.gain = source.gain;
                    cmd.playbackLifetime = source.key.playbackLifetime;
                    cmd.kind = source.key.lane == Lane::Embedded ? Kind::ReplaceEmbedded : Kind::InstallBgmPrepared;
                    cmd.clock = source.clock;
                    if (source.key.lane == Lane::Embedded) cmd.clock.timingVersion = ++timing;
                    cmd.expectedTimingVersion = source.key.lane == Lane::Embedded ? source.expectedTimingVersion : 0;
                    cmd.transport = clock.mode == Audio::AudioClockMode::Paused ? Audio::AudioTransport::Paused : Audio::AudioTransport::Playing;
                    cmd.replacement = Audio::AudioEmbeddedReplacement::RateContinuous;
                    const bool exposed = preparation.markAudioSubmitted(source.key);
                    if (exposed && execution.enqueue(source.ticket, cmd)) source.submitted = true;
                    else
                    {
                        if (exposed) (void)preparation.markConsumerQuiesced(source.key);
                        source.retiring = true; preparation.cancel(source.key);
                        (void)execution.cancelReserved(source.ticket);
                        if (auto* operation = find(source.operation))
                        { operation->outcome = Phase::Failed; operation->result.error = "audio-source-install-rejected"; }
                    }
                    if (auto* operation = find(source.operation); operation && operation->awaitingSources) --operation->awaitingSources;
                }
            }
            else if (producer.state == Audio::AudioPreparationState::Failed || producer.state == Audio::AudioPreparationState::Cancelled)
            {
                source.retiring = true;
                const bool optional = source.purpose == SourcePurpose::EmbeddedBind
                    && producer.state == Audio::AudioPreparationState::Failed && !closing;
                if (optional) { discardUnusedTicket(source.ticket); source.ticket = {}; }
                else (void)execution.cancelReserved(source.ticket);
                if (auto* operation = find(source.operation))
                { if (operation->awaitingSources) --operation->awaitingSources;
                    if (!optional)
                    { operation->outcome = Phase::Failed; operation->result.error = "audio-preparation-failed"; }
                }
                if (optional)
                {
                    if (source.key.mediaBinding == binding) embeddedPath.clear();
                    std::fprintf(stderr, "[Engine] optional embedded preparation failed (stage=%u code=%u); continuing video-only\n",
                        static_cast<unsigned>(producer.failureStage), static_cast<unsigned>(producer.failureCode));
                }
                if (source.key.lane != Lane::Embedded)
                    source.controlEvent = Music::MusicSourceFailed{{source.key.sourceToken},
                        producer.failureCode == BackgroundMusicObservation::FailureCode::ResourceFailure
                            || producer.failureCode == BackgroundMusicObservation::FailureCode::AllocationFailure
                            ? Music::MusicSourceFailureClass::TransientResourceFailure : Music::MusicSourceFailureClass::UnplayableTrack,
                        producer.failureStage, producer.failureCode};
            }
        }
        const auto observation = source.submitted && !source.audioRecordReleased ? execution.sourceObservation(source.key) : std::nullopt;
        if (observation && observation->terminal != Audio::AudioSourceTerminal::None
            && observation->terminalSequence <= frontier && !source.terminalDelivered)
        {
            // Consume the durable A fact now. Policy consequences remain in
            // its reserved C cell until the structural retirement completes.
            {
                source.terminalDelivered = true; source.retiring = true; preparation.cancel(source.key);
                if (source.key.lane != Lane::Embedded && !closing && !source.producerFailureDelivered)
                {
                    if (observation->terminal == Audio::AudioSourceTerminal::Ended)
                        source.controlEvent = Music::MusicSourceEnded{{source.key.sourceToken}};
                    else if (observation->terminal == Audio::AudioSourceTerminal::Failed)
                        source.controlEvent = Music::MusicSourceFailed{{source.key.sourceToken},
                            Music::MusicSourceFailureClass::UnplayableTrack, observation->failureStage, observation->failureCode};
                }
            }
        }
        if (observation && observation->consumerQuiesced)
        { source.consumerQuiesced = true; (void)preparation.markConsumerQuiesced(source.key); }
        producer = preparation.snapshot(source.key);
        if (source.retiring && !source.ticket.valid() && producer.producerQuiesced && producer.consumerQuiesced
            && (!source.submitted || (source.consumerQuiesced && source.terminalDelivered)))
        {
            const auto key = source.key;
            if (source.controlEvent)
            {
                if (terminalFacts[key.slot]) continue;
                terminalFacts[key.slot] = TerminalFact{key, std::move(*source.controlEvent)};
                source.controlEvent.reset();
            }
            // Retirement may overtake an ordinary queued seek/gain holding
            // this identity/view. A's release gate proves that reference drain
            // before C frees PCM. Keep the C record if that proof is not ready.
            if (source.submitted && !source.audioRecordReleased)
            {
                if (!execution.releaseSourceRecord(key)) continue;
                source.audioRecordReleased = true;
            }
            if (preparation.reclaim(key))
            {
                source = {};
                ++capacityRevision;
            }
        }
    }
    settleEffects();
    if (!effects[0].result.pending())
    {
        for (auto& terminal : terminalFacts)
        {
            if (!terminal) continue;
            const auto first = static_cast<std::size_t>(terminal->key.lane) * 2;
            // Keep the reliable fact if the predecessor producer still owns
            // both records. No synthetic resource failure changes BGM policy.
            const auto* musicSession = terminal->key.lane == Lane::PlaylistBgm
                ? engine.backgroundMusicRuntime_->playlistSession() : engine.backgroundMusicRuntime_->itemSession();
            const bool currentToken = musicSession && musicSession->sourceToken()
                && musicSession->sourceToken()->value == terminal->key.sourceToken;
            if (!closing && currentToken && sources[first].occupied && sources[first + 1].occupied) continue;
            auto event = std::move(terminal->event); terminal.reset();
            ++capacityRevision;
            if (!closing) (void)engine.backgroundMusicOrchestrator_->processMusicEvent(std::move(event));
        }
    }
    if (!effects[0].result.pending()) for (auto& source : sources)
    {
        if (!source.occupied || source.retiring || !source.submitted || !source.gainDirty) continue;
        Command cmd{}; cmd.kind = Kind::SetSourceGain; cmd.source = source.key; cmd.gain = source.gain;
        const auto scope = source.key.lane == Lane::Embedded ? AudioEffectScope::EmbeddedGain
            : source.key.lane == Lane::PlaylistBgm ? AudioEffectScope::PlaylistBgmGain : AudioEffectScope::ItemBgmGain;
        if (command(cmd, scope)) source.gainDirty = false;
    }
    const auto& snapshot = observedExecution;
    if (snapshot.health == Audio::AudioOutputHealth::Healthy && lastHealth != snapshot.health && !closing)
        (void)engine.backgroundMusicOrchestrator_->processMusicEvent(Music::MusicExecutionAvailable{});
    lastHealth = snapshot.health;
    if (closing && preparation.stopped() && execution.shutdownComplete())
    {
        bool live = false;
        for (const auto& source : sources) live |= source.occupied;
        for (const auto& fact : terminalFacts) live |= fact.has_value();
        bool undrained = gateRequest.has_value();
        for (const auto& operation : effects)
            for (std::size_t i = 0; i < operation.submitted; ++i) undrained |= operation.tickets[i].valid();
        if (!live && internalCount == 0 && !undrained)
            shutdownJoined = preparation.joinStopped() && execution.joinIfStopped();
    }
}

void Engine::AudioControl::beginShutdown() noexcept
{
    if (closing) return;
    closing = true; capture.reset(); pendingBinding.reset(); retireAll();
    preparation.beginShutdown(); execution.requestShutdown();
    for (auto& value : effects)
    {
        value.open = false;
        for (std::size_t i = value.submitted; i < value.reserved; ++i)
        { (void)execution.cancelReserved(value.tickets[i]); (void)execution.releaseReceipt(value.tickets[i]); value.tickets[i] = {}; }
    }
}
bool Engine::AudioControl::stopped() const noexcept { return shutdownJoined; }
Engine::AudioExecutionState Engine::AudioControl::state() const
{
    const auto& current = observedExecution;
    AudioExecutionState result{};
    result.incarnation = incarnation; result.started = started; result.closing = closing;
    result.capacityRevision = capacityRevision;
    result.stopped = shutdownJoined; result.desiredMuted = desiredMuted;
    result.appliedMuted = current.muteApplied; result.muteKnown = current.muteKnown;
    result.deviceHealth = static_cast<AudioDeviceHealth>(current.health);
    for (std::size_t i = 0; i < effects.size(); ++i) result.effects[i] = effects[i].result;
    return result;
}
}
