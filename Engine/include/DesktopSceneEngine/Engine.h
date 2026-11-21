#pragma once

#include <DesktopSceneEngine/BackgroundMusicControl.h>
#include <DesktopSceneEngine/BackgroundMusicObservation.h>
#include <DesktopSceneEngine/Domain/AspectRatio.h>
#include <DesktopSceneEngine/Domain/EntityId.h>
#include <DesktopSceneEngine/Domain/Project.h>
#include <DesktopSceneEngine/EnginePresentation.h>
#include <DesktopSceneEngine/HostedCompositionContent.h>
#include <DesktopSceneEngine/PresentationTarget.h>

#include <cstddef>
#include <cstdint>
#include <array>
#include <memory>
#include <optional>
#include <string>

namespace DesktopSceneEngine
{

namespace Runtime
{

class SceneRuntime;
class PlaybackRuntime;

}

namespace Rendering
{

class Renderer;
class CanvasTargetFitTestAccess;

namespace Detail { struct MediaView; }

}

namespace Resources
{

class MediaResource;
class AudioResource;
class DetachedMediaProducer;

}

namespace Audio
{

class AudioMixer;
class MixedOutputBridge;

}

namespace Music
{

class BackgroundMusicRuntime;
class BackgroundMusicExecutor;
class BackgroundMusicOrchestrator;

}

namespace Core
{

class BackgroundMusicEngineTestAccess;
class AudioOutputRecoveryEngineTestAccess;
struct AudioOutputRecoveryTestHooks;
enum class AudioOutputOperationalState : std::uint8_t;
enum class SharedAudioOutputState : std::uint8_t;

}

namespace Platform
{
namespace Audio
{

class AudioOutput;
class DefaultAudioEndpointWatcher;

}

}

class Engine
{
public:

    enum class PresentationMode { Inline, Detached };

    // Native control operations reserve their bounded completion before any
    // authored mutation. This is an execution receipt, never an audibility ACK.
    enum class AudioEffectScope : std::uint8_t
    {
        Structural, EmbeddedGain, PlaylistBgmGain, ItemBgmGain, OutputMute
    };
    enum class AudioEffectPhase : std::uint8_t
    {
        Rejected, Preparing, Pending, Applied, DeferredUnavailable, Failed,
        Cancelled, Expired
    };
    enum class AudioDeviceHealth : std::uint8_t
    {
        Healthy, DeviceUnavailable, ContractFaulted
    };
    struct AudioEffectToken
    {
        std::uint64_t execution = 0;
        std::uint64_t sequence = 0;
        [[nodiscard]] explicit operator bool() const noexcept
        { return execution != 0 && sequence != 0; }
        bool operator==(const AudioEffectToken&) const = default;
    };
    struct AudioEffectResult
    {
        AudioEffectToken token{};
        AudioEffectScope scope = AudioEffectScope::Structural;
        AudioEffectPhase phase = AudioEffectPhase::Expired;
        std::string error;
        std::int64_t effectTimeNs = 0;
        [[nodiscard]] bool pending() const noexcept
        { return phase == AudioEffectPhase::Preparing || phase == AudioEffectPhase::Pending; }
        [[nodiscard]] bool terminal() const noexcept { return !pending(); }
    };
    struct AudioExecutionState
    {
        std::uint64_t incarnation = 0;
        std::uint64_t capacityRevision = 0;
        bool started = false;
        bool closing = false;
        bool stopped = true;
        bool desiredMuted = false;
        bool appliedMuted = false;
        bool muteKnown = true;
        AudioDeviceHealth deviceHealth = AudioDeviceHealth::Healthy;
        std::array<AudioEffectResult, 5> effects{};
    };

    struct PlaybackClockSample
    {
        std::uint64_t asOfEffectSequence = 0;
        std::int64_t asOfTimeNs = 0;
        double position = 0, duration = 0, rate = 1;
        bool paused = false, outputMuted = false, muteKnown = true;
    };
    // One C receipt frontier and one QPC instant for a detached readback.
    [[nodiscard]] PlaybackClockSample playbackClockSample() const;

    [[nodiscard]] std::optional<AudioEffectToken> beginAudioEffect(AudioEffectScope scope) noexcept;
    [[nodiscard]] AudioEffectResult finishAudioEffect(AudioEffectToken token, bool logicalAccepted = true);
    [[nodiscard]] AudioEffectResult audioEffect(AudioEffectToken token) const;
    void pollAudioExecution();
    [[nodiscard]] AudioExecutionState audioExecutionState() const;
    void beginAudioShutdown() noexcept;
    [[nodiscard]] bool audioShutdownComplete() const noexcept;

    using ExternalWindowTarget = EnginePresentation::ExternalWindowTarget;

    enum class MediaKind
    {
        Unknown,
        Image,
        Video
    };

    // Selects whether a naturally completed scene advances within Engine or
    // reports one completion fact for an external sequence controller.
    enum class SceneCompletionPolicy
    {
        InternalSceneNavigation,
        ExternalSceneCompletion
    };

    struct MediaDimensions
    {
        int width = 0;
        int height = 0;
    };

    Engine();
    explicit Engine(PresentationMode presentationMode);

    ~Engine();

    void initialize();

    // Detached control owns no HWND, renderer, or composition content lease.
    [[nodiscard]] bool initializeRuntimeOnly();

    // Borrowed auto-reset event. Keep it alive until this Engine's audio and
    // producer retirement is complete. Set it before logical initialization.
    [[nodiscard]] bool setControlWakeEvent(void* borrowedEvent) noexcept;

    // Read on the control owner. The returned pixels and geometry can be
    // retained independently by an EnginePresentation on the UI thread.
    [[nodiscard]] PresentationInput presentationInput(const Domain::AspectRatio& canvasAspect) const;

    [[nodiscard]] PresentationTargetId initialize(
        const ExternalWindowTarget& target);

    [[nodiscard]] PresentationTargetId initialize(
        const HostedCompositionTarget& target);

    [[nodiscard]] PresentationTargetId attachPresentationTarget(
        const ExternalWindowTarget& target);

    [[nodiscard]] PresentationTargetId attachPresentationTarget(
        const HostedCompositionTarget& target);

    // Explicit allocation input for windowless content; placement belongs to
    // the application that attaches the leased composition content.
    [[nodiscard]] bool setHostedPresentationExtent(
        PresentationTargetId id, PresentationExtent extent) noexcept;

    [[nodiscard]] CompositionContentLease acquireHostedCompositionContent(
        PresentationTargetId id) const noexcept;

    [[nodiscard]] bool detachPresentationTarget(
        PresentationTargetId id) noexcept;

    [[nodiscard]] bool setPresentationPacingTarget(
        PresentationTargetId id) noexcept;

    [[nodiscard]] bool isInitialized() const noexcept;

    bool bindProject(
        const Domain::Project& project,
        const Domain::EntityId& activeSceneId
    );

    bool switchScene(
        const Domain::Project& project,
        const Domain::EntityId& sceneId
    );

    void detachProject();

    // CP3 control path. Preparation publishes resource facts without mutating
    // the active binding or playback clock. Commit consumes only the exact
    // ready ticket and owns an immutable runtime copy of the project.
    enum class MediaPreparationStatus { Pending, Ready, Failed, Expired };
    using MediaPreparationToken = std::uint64_t;
    [[nodiscard]] bool enableAsyncMediaPreparation();
    [[nodiscard]] std::optional<MediaPreparationToken> prepareSceneMedia(
        const Domain::Project& project, const Domain::EntityId& scene, float initialTime = 0.0F);
    [[nodiscard]] std::optional<MediaPreparationToken> prepareSeek(float seconds);
    [[nodiscard]] MediaPreparationStatus preparedMediaStatus(MediaPreparationToken ticket) const noexcept;
    [[nodiscard]] bool commitPreparedScene(const Domain::Project& project, const Domain::EntityId& scene,
        MediaPreparationToken ticket, bool paused = false, std::optional<float> playbackRate = std::nullopt);
    [[nodiscard]] bool commitPreparedSeek(MediaPreparationToken ticket);
    void cancelMediaPreparation(MediaPreparationToken ticket) noexcept;
    enum class MediaPresentationResolution { Ignored, Pending, Acknowledged, RolledBack };
    [[nodiscard]] std::optional<MediaPreparationToken> pendingMediaPresentation() const noexcept;
    [[nodiscard]] MediaPresentationResolution resolveMediaPresentation(MediaPreparationToken ticket,
        PresentationUploadResult result);
    void beginMediaShutdown() noexcept;
    [[nodiscard]] bool mediaShutdownComplete() const noexcept;

    void update();

    // Compatibility signature. Logical time uses the transition-anchored QPC
    // clock; caller/UI delta is never accumulated or used for time stepping.
    void update(float deltaTime);

    void setSceneCompletionPolicy(SceneCompletionPolicy policy) noexcept;

    // Returns a natural-completion fact at most once. Successful explicit
    // scene changes, seeks, project lifecycle changes, and policy changes
    // invalidate an unconsumed fact.
    [[nodiscard]] bool consumeNaturalSceneCompletion() noexcept;
    [[nodiscard]] bool naturalSceneCompletionPending() const noexcept;

    // Seeks within the active scene using scene-local media time. Finite
    // requests are clamped to the playable range; invalid lifecycle, duration,
    // or numeric input returns false without changing logical playback time.
    // A successful seek preserves the active scene, pause state, speed, volume,
    // and mute state.
    [[nodiscard]] bool seek(float sceneTimeSeconds);

    [[nodiscard]] bool processEvents();

    [[nodiscard]] bool consumeKeyPress(unsigned int virtualKey);

    [[nodiscard]] bool consumeVolumeSlider(float& volume) noexcept;

    void togglePause();

    void setPlaybackSpeed(float speed);

    [[nodiscard]] bool isPaused() const noexcept;

    [[nodiscard]] float playbackTime() const noexcept;

    [[nodiscard]] float effectiveDuration() const noexcept;

    [[nodiscard]] float playbackSpeed() const noexcept;

    void setVolume(float volume) noexcept;

    [[nodiscard]] float volume() const noexcept;

    void toggleMute() noexcept;

    [[nodiscard]] bool isMuted() const noexcept;

    // Engine/control-thread only. This admits validated application-level
    // background-music control facts; source execution details remain private.
    [[nodiscard]] bool processBackgroundMusicControl(
        const BackgroundMusicControl::Event& event
    );

    // Engine/control-thread only. These return the active logical track IDs
    // owned by the background-music Runtime; they do not report audio-device
    // sample emission.
    [[nodiscard]] std::optional<BackgroundMusicControl::TrackId>
        currentPlaylistBackgroundMusicTrackId() const;

    [[nodiscard]] std::optional<BackgroundMusicControl::TrackId>
        currentItemBackgroundMusicTrackId() const;

    [[nodiscard]] BackgroundMusicObservation::Snapshot backgroundMusicObservation() const;

    [[nodiscard]] std::size_t activeSceneIndex() const noexcept;

    [[nodiscard]] std::size_t sceneCount() const noexcept;

    [[nodiscard]] MediaKind mediaKind() const noexcept;

    [[nodiscard]] std::optional<MediaDimensions> currentMediaDimensions() const noexcept;

    // Forwards presentation text to the executable-owned native control bar.
    // This does not store UI state or affect playback behavior.
    void setStatusText(const std::string& text) noexcept;

    bool switchToNextScene();

    bool switchToPreviousScene();

    void render();

    void render(const Domain::AspectRatio& canvasAspect);

    // Unaddressed/missing targets retain Opaque. Transparent requires DComp;
    // unsupported or invalid matched instructions skip only that submission.
    void render(
        const Domain::AspectRatio& canvasAspect,
        std::optional<CanvasOutsideInstruction> canvasOutside);

    void shutdown();

private:

    struct AudioControl;
    std::unique_ptr<AudioControl> audioControl_;

    friend class Core::BackgroundMusicEngineTestAccess;
    friend class Core::AudioOutputRecoveryEngineTestAccess;
    friend class Rendering::CanvasTargetFitTestAccess;

    [[nodiscard]] bool initializeRuntime();
    [[nodiscard]] Rendering::Renderer& renderer() noexcept;
    [[nodiscard]] bool stageMedia(const Rendering::Detail::MediaView& media, PresentationFrameKind kind);
    [[nodiscard]] bool prepareInitialMedia(Resources::MediaResource& media);
    void clearPresentationMedia() noexcept;
    void updateDetachedVideo();
    struct MediaPresentationCheckpoint;
    [[nodiscard]] std::unique_ptr<MediaPresentationCheckpoint> captureMediaPresentation(
        MediaPreparationToken ticket, bool seek) const;

    struct PendingMediaPreparation
    {
        MediaPreparationToken ticket;
        Domain::EntityId scene;
        std::optional<std::string> source;
        float declaredDuration = 0;
        float position = 0;
        bool seek = false;
        bool seekWithoutVideo = false;
    };

    bool initialized_ = false;
    bool projectBound_ = false;
    SceneCompletionPolicy sceneCompletionPolicy_
        = SceneCompletionPolicy::InternalSceneNavigation;
    bool pendingNaturalSceneCompletion_ = false;
    bool naturalSceneCompletionHalted_ = false;
    // Inline compatibility binds caller-owned storage. CP3 binds detachedProject_.
    const Domain::Project* boundProject_ = nullptr;
    std::unique_ptr<const Domain::Project> detachedProject_;
    std::unique_ptr<MediaPresentationCheckpoint> mediaPresentation_;
    std::unique_ptr<Runtime::SceneRuntime> sceneRuntime_{};
    std::unique_ptr<Runtime::PlaybackRuntime> playbackRuntime_{};
    std::unique_ptr<EnginePresentation> presentation_{};
    std::shared_ptr<const PresentationFrame> detachedFrame_;
    void* controlWakeEvent_ = nullptr;
    std::unique_ptr<Resources::DetachedMediaProducer> mediaProducer_;
    std::optional<PendingMediaPreparation> pendingMediaPreparation_;
    MediaPreparationToken nextMediaTicket_ = 0;
    MediaPreparationToken detachedMediaBinding_ = 0;
    MediaKind detachedMediaKind_ = MediaKind::Unknown;
    bool detachedVideoAtEnd_ = false;
    bool mediaShuttingDown_ = false;
    std::unique_ptr<Resources::MediaResource> mediaResource_{};
    float videoFrameTime_ = -1.0F;
    double videoPresentedTime_ = -1.0;
    std::uint64_t syncDiagnosticUpdates_ = 0U;
    std::unique_ptr<Music::BackgroundMusicRuntime> backgroundMusicRuntime_{};
    std::unique_ptr<Music::BackgroundMusicOrchestrator> backgroundMusicOrchestrator_{};
};

}
