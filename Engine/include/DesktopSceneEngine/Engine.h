#pragma once

#include <DesktopSceneEngine/BackgroundMusicControl.h>
#include <DesktopSceneEngine/Domain/AspectRatio.h>
#include <DesktopSceneEngine/Domain/EntityId.h>
#include <DesktopSceneEngine/Domain/Project.h>
#include <DesktopSceneEngine/PresentationTarget.h>

#include <cstddef>
#include <cstdint>
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

}

namespace Resources
{

class MediaResource;
class AudioResource;

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

namespace Windows
{

class DesktopWindowHost;

}
}

class Engine
{
public:

    // Borrowed presentation target supplied by an external application window.
    // The caller owns the native window and must keep it alive until its
    // presentation target is detached or Engine shutdown completes.
    struct ExternalWindowTarget
    {
        void* handle = nullptr;
        PresentationTransport transport = PresentationTransport::HwndSwapChain;
    };

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

    ~Engine();

    void initialize();

    [[nodiscard]] PresentationTargetId initialize(
        const ExternalWindowTarget& target);

    [[nodiscard]] PresentationTargetId attachPresentationTarget(
        const ExternalWindowTarget& target);

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

    void update();

    void update(float deltaTime);

    void setSceneCompletionPolicy(SceneCompletionPolicy policy) noexcept;

    // Returns a natural-completion fact at most once. Successful explicit
    // scene changes, seeks, project lifecycle changes, and policy changes
    // invalidate an unconsumed fact.
    [[nodiscard]] bool consumeNaturalSceneCompletion() noexcept;

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

    friend class Core::BackgroundMusicEngineTestAccess;
    friend class Core::AudioOutputRecoveryEngineTestAccess;
    friend class Rendering::CanvasTargetFitTestAccess;

    bool initializeWithWindowHost(
        PresentationTransport transport = PresentationTransport::HwndSwapChain);

    void removeEmbeddedAudioInput() noexcept;

    void stopSharedAudioOutput() noexcept;

    [[nodiscard]] bool hasAudioInputPresence() const noexcept;

    [[nodiscard]] Core::SharedAudioOutputState desiredSharedAudioOutputState() const noexcept;

    [[nodiscard]] bool reconcileAudioOutputState(bool allowPrime) noexcept;

    void enterAudioOutputDeviceUnavailable() noexcept;

    void enterAudioOutputContractFaulted() noexcept;

    void advanceAudioOutputRecovery(float deltaTime) noexcept;

    void observeDefaultAudioEndpointGeneration() noexcept;

    void rerouteDefaultAudioEndpoint() noexcept;

    [[nodiscard]] bool recoverAudioOutput(
        Core::SharedAudioOutputState desiredState
    ) noexcept;

    void releaseEmbeddedAudioResource() noexcept;

    [[nodiscard]] bool setEmbeddedAudioTempo(float speed) noexcept;

    [[nodiscard]] bool configureEmbeddedAudioInputAt(
        float mediaTime,
        float playbackSpeed
    ) noexcept;

    [[nodiscard]] bool pumpMixedAudio() noexcept;

    bool initialized_ = false;
    bool projectBound_ = false;
    SceneCompletionPolicy sceneCompletionPolicy_
        = SceneCompletionPolicy::InternalSceneNavigation;
    bool pendingNaturalSceneCompletion_ = false;
    bool naturalSceneCompletionHalted_ = false;
    // Application owns the project and must keep it alive until detachProject().
    const Domain::Project* boundProject_ = nullptr;
    std::unique_ptr<Runtime::SceneRuntime> sceneRuntime_{};
    std::unique_ptr<Runtime::PlaybackRuntime> playbackRuntime_{};
    std::unique_ptr<Rendering::Renderer> renderer_{};
    std::unique_ptr<Resources::MediaResource> mediaResource_{};
    std::unique_ptr<Resources::AudioResource> audioResource_{};
    float videoFrameTime_ = -1.0F;
    double videoPresentedTime_ = -1.0;
    std::uint64_t syncDiagnosticUpdates_ = 0U;
    std::unique_ptr<Audio::AudioMixer> audioMixer_{};
    std::unique_ptr<Platform::Audio::AudioOutput> audioOutput_{};
    std::unique_ptr<Platform::Audio::DefaultAudioEndpointWatcher> defaultAudioEndpointWatcher_{};
    std::unique_ptr<Audio::MixedOutputBridge> mixedOutputBridge_{};
    std::unique_ptr<Music::BackgroundMusicRuntime> backgroundMusicRuntime_{};
    std::unique_ptr<Music::BackgroundMusicExecutor> backgroundMusicExecutor_{};
    std::unique_ptr<Music::BackgroundMusicOrchestrator> backgroundMusicOrchestrator_{};
    std::uint8_t sharedAudioOutputState_ = 0U;
    Core::AudioOutputOperationalState audioOutputOperationalState_;
    double audioOutputRetryElapsedSeconds_ = 0.0;
    std::uint64_t embeddedAudioInputHandleValue_ = 0U;
    float embeddedAudioGain_ = 1.0F;
    bool embeddedAudioNeedsResyncAfterOutputRecovery_ = false;
    bool audioOutputInactiveDiagnosticReported_ = false;
    bool defaultAudioEndpointWatcherAvailable_ = false;
    std::uint64_t lastSeenDefaultAudioEndpointGeneration_ = 0U;
    bool defaultAudioEndpointRerouteRequested_ = false;
    std::unique_ptr<Core::AudioOutputRecoveryTestHooks> audioOutputRecoveryTestHooks_{};
    std::unique_ptr<Platform::Windows::DesktopWindowHost> desktopWindowHost_{};
    PresentationTargetId defaultPresentationTargetId_{};
};

}
