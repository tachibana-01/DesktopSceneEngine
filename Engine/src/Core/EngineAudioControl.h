#pragma once

#include <DesktopSceneEngine/Engine.h>
#include "Audio/AudioExecution.h"
#include "Audio/AudioSourcePreparation.h"
#include "Music/MusicRuntimeTypes.h"

#include <array>
#include <filesystem>
#include <optional>

namespace DesktopSceneEngine
{

// Existing C-domain audio orchestration. Physical state is exclusively inside
// AudioExecution's A thread; this object contains only requests and receipts.
struct Engine::AudioControl
{
    struct Effect
    {
        AudioEffectResult result{};
        bool open = false;
        bool logicalAccepted = true;
        std::array<Audio::AudioEffectTicket, 8> tickets{};
        std::size_t reserved = 0;
        std::size_t submitted = 0;
        std::size_t awaitingSources = 0;
        AudioEffectPhase outcome = AudioEffectPhase::Applied;
        std::uint64_t requiredRetirement = 0;
    };
    enum class SourcePurpose { EmbeddedBind, EmbeddedRate, Playlist, Item };
    struct Source
    {
        Audio::AudioSourceKey key{};
        SourcePurpose purpose = SourcePurpose::EmbeddedBind;
        AudioEffectToken operation{};
        AudioEffectToken retirementOperation{};
        Audio::AudioEffectTicket ticket{};
        Audio::AudioClockAnchor clock{};
        std::uint64_t expectedTimingVersion = 0;
        std::filesystem::path path;
        float gain = 1.0F;
        bool gainDirty = false;
        bool occupied = false;
        bool submitted = false;
        bool consumerQuiesced = false;
        bool audioRecordReleased = false;
        bool retiring = false;
        bool terminalDelivered = false;
        bool producerFailureDelivered = false;
        bool startedDelivered = false;
        std::optional<Music::MusicRuntimeEvent> controlEvent;
        std::optional<float> reprepareRate;
    };
    struct TerminalFact
    {
        Audio::AudioSourceKey key;
        Music::MusicRuntimeEvent event;
    };

    AudioControl();
    explicit AudioControl(Audio::AudioExecutionTestDevice& device);
    ~AudioControl();
    AudioControl(const AudioControl&) = delete;
    AudioControl& operator=(const AudioControl&) = delete;

    std::optional<AudioEffectToken> begin(AudioEffectScope scope) noexcept;
    AudioEffectResult finish(AudioEffectToken token, bool accepted);
    AudioEffectResult effect(AudioEffectToken token) const;
    bool start() noexcept;
    void poll(Engine& engine);
    void beginShutdown() noexcept;
    bool stopped() const noexcept;
    AudioExecutionState state() const;
    bool command(const Audio::AudioCommand& command, AudioEffectScope scope);
    void prepareEmbedded(Engine& engine, const std::filesystem::path& path,
        float rate, SourcePurpose purpose);
    std::optional<Music::MusicRuntimeEvent> music(const Music::MusicRuntimeAction& action);
    void retire(Audio::AudioLane lane) noexcept;
    void retireAll() noexcept;
    void configureClock(Engine& engine, double position, bool paused);
    void requestTransport(Engine& engine, bool paused);
    void requestSeek(Engine& engine, double position);
    void holdFailure(Engine& engine, double position, double failedTarget);
    void recoverFailure(Engine& engine, double position);
    double position() const noexcept;

    Audio::AudioExecution execution;
    Audio::AudioSourcePreparation preparation;
    std::array<Effect, 5> effects{};
    std::array<Source, 6> sources{};
    // Six source-owned C fact cells allow storage retirement before policy
    // consequences, without losing the original token or reusing its slot.
    std::array<std::optional<TerminalFact>, 6> terminalFacts{};
    std::array<Audio::AudioEffectTicket, 32> internalTickets{};
    std::size_t internalCount = 0;
    std::uint64_t internalGroup = 0;
    std::optional<AudioEffectToken> capture;
    std::uint64_t incarnation = 0;
    std::uint64_t nextOperation = 0;
    std::uint64_t nextSource = 0;
    std::uint64_t capacityRevision = 1;
    std::uint64_t playbackLifetime = 1;
    std::uint64_t binding = 0;
    std::uint64_t timing = 0;
    Audio::AudioClockAnchor clock{};
    Audio::AudioClockAnchor plannedClock{};
    std::optional<Audio::AudioCommand> pendingBinding;
    std::filesystem::path embeddedPath;
    float desiredEmbeddedGain = 1.0F;
    bool desiredMuted = false;
    bool closing = false;
    bool started = false;
    bool shutdownJoined = false;
    bool polling = false;
    bool failureHeld = false;
    double failedTarget = 0;
    double lastConfirmedPosition = 0;
    std::optional<std::uint64_t> gateRequest;
    bool gateDesiredHold = false;
    bool gateDirty = false;
    Audio::AudioOutputHealth lastHealth = Audio::AudioOutputHealth::Healthy;
    Audio::AudioExecutionSnapshot observedExecution{};
    Audio::AudioMonotonicTime observationTime = 0;

private:
    Source* allocate(Audio::AudioLane lane, std::uint64_t sourceToken) noexcept;
    Effect* find(AudioEffectToken token) noexcept;
    const Effect* find(AudioEffectToken token) const noexcept;
    Audio::AudioEffectTicket takeTicket(AudioEffectScope scope);
    void discardUnusedTicket(Audio::AudioEffectTicket ticket) noexcept;
    void settleEffects();
};

}
