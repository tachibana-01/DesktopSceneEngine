#include <DesktopSceneEngine/Engine.h>

#include "../Audio/AudioMixer.h"
#include "../Audio/EmbeddedAudioMixerInput.h"
#include "../Audio/MixedOutputBridge.h"
#include "../Music/BackgroundMusicExecutor.h"
#include "../Music/BackgroundMusicOrchestrator.h"
#include "../Music/BackgroundMusicRuntime.h"
#include "../Music/MusicRuntimeTypes.h"
#include "../Platform/Windows/DesktopWindowHost.h"
#include "../Platform/Audio/AudioOutput.h"
#include "../Platform/Audio/DefaultAudioEndpointWatcher.h"
#include "EmbeddedAudioStartFrame.h"
#include "AudioOutputRecoveryTestHooks.h"
#include "ScenePresentationMath.h"
#include "SharedAudioOutputRecoveryPolicy.h"
#include "SharedAudioOutputPolicy.h"
#include "../Rendering/RenderData.h"
#include "../Rendering/Renderer.h"
#include "../Rendering/WindowTarget.h"
#include "../Resources/AudioResource.h"
#include "../Resources/MediaResource.h"
#include "../Resources/ImageResource.h"
#include "../Resources/VideoResource.h"
#include "../Runtime/Playback/PlaybackRuntime.h"
#include "../Runtime/Scene/SceneRuntime.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cassert>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <cmath>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <variant>

namespace
{

std::uint64_t sceneSwitches = 0;
std::uint64_t videoFrameUpdates = 0;

bool shouldLog(std::uint64_t count) noexcept
{
    return count == 1 || count % 25 == 0;
}

using DesktopSceneEngine::Domain::Media;
using DesktopSceneEngine::Audio::AudioBlockStatus;
using DesktopSceneEngine::Audio::AudioInputHandle;
using DesktopSceneEngine::Audio::AudioMixer;
using DesktopSceneEngine::Audio::EmbeddedAudioMixerInput;
using DesktopSceneEngine::Audio::MixedOutputBridge;
using DesktopSceneEngine::Audio::MixedOutputBridgeStatus;
namespace BackgroundMusicControl = DesktopSceneEngine::BackgroundMusicControl;
namespace Music = DesktopSceneEngine::Music;
using DesktopSceneEngine::Platform::Audio::AudioBlockStartMode;
using DesktopSceneEngine::Platform::Audio::AudioOutput;
using DesktopSceneEngine::Rendering::Detail::MediaUpdateResult;
using DesktopSceneEngine::Rendering::Renderer;
using DesktopSceneEngine::Resources::AudioResource;
using DesktopSceneEngine::Resources::AudioOpenStatus;
using DesktopSceneEngine::Resources::ImageResource;
using DesktopSceneEngine::Resources::MediaResource;
using DesktopSceneEngine::Resources::MediaType;
using DesktopSceneEngine::Resources::VideoResource;
using DesktopSceneEngine::Runtime::PlaybackRuntime;

bool isUnitInterval(double value) noexcept
{
    return std::isfinite(value) && value >= 0.0 && value <= 1.0;
}

std::optional<Music::MusicQueueMode> toInternalQueueMode(
    BackgroundMusicControl::QueueMode mode
) noexcept
{
    switch (mode)
    {
    case BackgroundMusicControl::QueueMode::CurrentSingleLoop:
        return Music::MusicQueueMode::CurrentSingleLoop;

    case BackgroundMusicControl::QueueMode::ListLoop:
        return Music::MusicQueueMode::ListLoop;

    case BackgroundMusicControl::QueueMode::Shuffle:
        return Music::MusicQueueMode::Shuffle;
    }

    return std::nullopt;
}

std::optional<Music::MusicQueueSnapshot> toInternalQueue(
    const BackgroundMusicControl::QueueSnapshot& queue
)
{
    const std::optional<Music::MusicQueueMode> mode = toInternalQueueMode(queue.mode);
    if (!mode.has_value() || !isUnitInterval(queue.queueVolume))
    {
        return std::nullopt;
    }

    try
    {
        Music::MusicQueueSnapshot converted{};
        converted.mode = *mode;
        converted.queueVolume = static_cast<float>(queue.queueVolume);
        converted.enabled = queue.enabled;
        converted.tracks.reserve(queue.tracks.size());
        std::unordered_set<std::string> trackIds{};
        trackIds.reserve(queue.tracks.size());

        for (const BackgroundMusicControl::TrackSnapshot& track : queue.tracks)
        {
            if (!track.id || track.sourcePath.empty() || !isUnitInterval(track.trackVolume)
                || !trackIds.insert(track.id.value).second)
            {
                return std::nullopt;
            }

            converted.tracks.push_back(Music::MusicTrackSnapshot{
                Music::TrackId{track.id.value},
                track.sourcePath,
                static_cast<float>(track.trackVolume)});
        }

        return Music::isValid(converted) ? std::optional{std::move(converted)} : std::nullopt;
    }
    catch (const std::bad_alloc&)
    {
        return std::nullopt;
    }
    catch (const std::length_error&)
    {
        return std::nullopt;
    }
}

std::optional<Music::ActiveMusicScopeSnapshot> toInternalScope(
    const BackgroundMusicControl::ActiveScopeSnapshot& scope,
    Music::PlaybackActivity activity
)
{
    if (!scope.playlistId || scope.itemId.has_value() != scope.itemMusic.has_value())
    {
        return std::nullopt;
    }

    const std::optional<Music::MusicQueueSnapshot> playlistMusic = toInternalQueue(
        scope.playlistMusic);
    if (!playlistMusic.has_value())
    {
        return std::nullopt;
    }

    std::optional<Music::ItemId> itemId{};
    std::optional<Music::MusicQueueSnapshot> itemMusic{};
    if (scope.itemId.has_value())
    {
        if (!*scope.itemId)
        {
            return std::nullopt;
        }

        itemMusic = toInternalQueue(*scope.itemMusic);
        if (!itemMusic.has_value())
        {
            return std::nullopt;
        }

        itemId.emplace(Music::ItemId{scope.itemId->value});
    }

    Music::ActiveMusicScopeSnapshot converted{
        Music::PlaylistId{scope.playlistId.value},
        std::move(itemId),
        *playlistMusic,
        std::move(itemMusic),
        activity};
    return Music::isValid(converted) ? std::optional{std::move(converted)} : std::nullopt;
}

std::optional<Music::MusicRuntimeEvent> toInternalMusicEvent(
    const BackgroundMusicControl::Event& event,
    Music::PlaybackActivity activity
)
{
    return std::visit(
        [activity](const auto& typedEvent) -> std::optional<Music::MusicRuntimeEvent>
        {
            using EventType = std::decay_t<decltype(typedEvent)>;

            if constexpr (std::is_same_v<EventType, BackgroundMusicControl::PlaybackSessionStarted>)
            {
                const auto scope = toInternalScope(typedEvent.scope, activity);
                return scope.has_value()
                    ? std::optional<Music::MusicRuntimeEvent>{Music::PlaybackSessionStarted{*scope}}
                    : std::nullopt;
            }
            else if constexpr (std::is_same_v<EventType, BackgroundMusicControl::ActivePlaybackScopeChanged>)
            {
                const auto scope = toInternalScope(typedEvent.scope, activity);
                return scope.has_value()
                    ? std::optional<Music::MusicRuntimeEvent>{Music::ActivePlaybackScopeChanged{*scope}}
                    : std::nullopt;
            }
            else if constexpr (std::is_same_v<EventType, BackgroundMusicControl::PlaybackPaused>)
            {
                return Music::PlaybackPaused{};
            }
            else if constexpr (std::is_same_v<EventType, BackgroundMusicControl::PlaybackResumed>)
            {
                return Music::PlaybackResumed{};
            }
            else if constexpr (std::is_same_v<EventType, BackgroundMusicControl::PlaybackStopped>)
            {
                return Music::PlaybackStopped{};
            }
            else if constexpr (std::is_same_v<EventType, BackgroundMusicControl::PlaylistMusicConfigChanged>)
            {
                if (!typedEvent.playlistId)
                {
                    return std::nullopt;
                }

                const auto config = toInternalQueue(typedEvent.config);
                return config.has_value()
                    ? std::optional<Music::MusicRuntimeEvent>{Music::PlaylistMusicConfigChanged{
                        Music::PlaylistId{typedEvent.playlistId.value}, *config}}
                    : std::nullopt;
            }
            else if constexpr (std::is_same_v<EventType, BackgroundMusicControl::ItemMusicConfigChanged>)
            {
                if (!typedEvent.playlistId || !typedEvent.itemId)
                {
                    return std::nullopt;
                }

                const auto config = toInternalQueue(typedEvent.config);
                return config.has_value()
                    ? std::optional<Music::MusicRuntimeEvent>{Music::ItemMusicConfigChanged{
                        Music::PlaylistId{typedEvent.playlistId.value},
                        Music::ItemId{typedEvent.itemId.value},
                        *config}}
                    : std::nullopt;
            }
            else if constexpr (
                std::is_same_v<EventType, BackgroundMusicControl::PlaylistMusicTrackSwitchRequested>)
            {
                if (!typedEvent.playlistId || !typedEvent.trackId)
                {
                    return std::nullopt;
                }

                return Music::PlaylistMusicTrackSwitchRequested{
                    Music::PlaylistId{typedEvent.playlistId.value},
                    Music::TrackId{typedEvent.trackId.value}};
            }
            else if constexpr (
                std::is_same_v<EventType, BackgroundMusicControl::ItemMusicTrackSwitchRequested>)
            {
                if (!typedEvent.playlistId || !typedEvent.itemId || !typedEvent.trackId)
                {
                    return std::nullopt;
                }

                return Music::ItemMusicTrackSwitchRequested{
                    Music::PlaylistId{typedEvent.playlistId.value},
                    Music::ItemId{typedEvent.itemId.value},
                    Music::TrackId{typedEvent.trackId.value}};
            }
        },
        event);
}

std::optional<Music::MusicProcessResult> processBackgroundMusicEventInternal(
    Music::BackgroundMusicOrchestrator* orchestrator,
    Music::MusicRuntimeEvent event
)
{
    if (orchestrator == nullptr)
    {
        return std::nullopt;
    }

    return orchestrator->processMusicEvent(std::move(event));
}

double presentedTimeOrFallback(
    const DesktopSceneEngine::Resources::DecodedFrame& frame,
    double fallback) noexcept
{
    if (frame.hasTimestamp
        && std::isfinite(frame.timestampSeconds)
        && frame.timestampSeconds >= 0.0)
    {
        return frame.timestampSeconds;
    }

    return fallback;
}

void logPlaybackSync(
    const PlaybackRuntime& playback,
    const AudioOutput* audioOutput,
    double videoPresentedTime,
    std::uint64_t& sampleCount) noexcept
{
    ++sampleCount;
    if (!shouldLog(sampleCount))
    {
        return;
    }

    const double logicalTime = static_cast<double>(playback.currentTime());
    const bool logicalValid = std::isfinite(logicalTime) && logicalTime >= 0.0;
    const bool videoValid = std::isfinite(videoPresentedTime)
        && videoPresentedTime >= 0.0;
    const double videoDrift = logicalValid && videoValid
        ? videoPresentedTime - logicalTime : 0.0;

    std::fprintf(
        stderr,
        "[Engine] playback-sync sample=%llu logical=%.6f video=%.6f video-valid=%s video-drift=%.6f audio-submitted=%llu audio-queued=%llu audio-played=%llu audio-underflows=%llu\n",
        static_cast<unsigned long long>(sampleCount),
        logicalTime,
        videoPresentedTime,
        videoValid ? "yes" : "no",
        videoDrift,
        static_cast<unsigned long long>(audioOutput != nullptr
            ? audioOutput->submittedFrames() : 0U),
        static_cast<unsigned long long>(audioOutput != nullptr
            ? audioOutput->queuedFrames() : 0U),
        static_cast<unsigned long long>(audioOutput != nullptr
            ? audioOutput->playedFrames() : 0U),
        static_cast<unsigned long long>(audioOutput != nullptr
            ? audioOutput->underflowCount() : 0U));
    std::fflush(stderr);
}

MediaType mediaTypeForPath(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    for (char& character : extension)
    {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }

    if (extension == ".mp4"
        || extension == ".avi"
        || extension == ".mov"
        || extension == ".mkv"
        || extension == ".webm"
        || extension == ".video")
    {
        return MediaType::Video;
    }

    if (extension == ".png"
        || extension == ".jpg"
        || extension == ".jpeg"
        || extension == ".webp"
        || extension.empty())
    {
        return MediaType::Image;
    }

    return MediaType::Unknown;
}

std::filesystem::path pathFromUtf8(const std::string& value)
{
    const auto* characters = reinterpret_cast<const char8_t*>(value.data());
    return std::filesystem::path{
        std::u8string{characters, characters + value.size()}
    };
}

std::unique_ptr<MediaResource> createMediaResource(const Media& media)
{
    const std::filesystem::path resourcePath = pathFromUtf8(
        media.resourceReference);
    const MediaType type = mediaTypeForPath(resourcePath);
    if (type == MediaType::Video)
    {
        auto video = std::make_unique<VideoResource>();
        if (!video->open(resourcePath))
        {
            return nullptr;
        }

        return video;
    }

    if (type == MediaType::Image || type == MediaType::Unknown)
    {
        auto image = std::make_unique<ImageResource>();
        if (!image->loadFromFile(resourcePath))
        {
            return nullptr;
        }

        return image;
    }

    return nullptr;
}

std::unique_ptr<AudioResource> createAudioResource(
    const Media& media,
    AudioOpenStatus& status)
{
    status = AudioOpenStatus::NotOpened;
    const std::filesystem::path resourcePath = pathFromUtf8(
        media.resourceReference);
    auto audio = std::make_unique<AudioResource>();
    if (!audio->open(resourcePath))
    {
        status = audio->openStatus();
        return nullptr;
    }

    status = audio->openStatus();
    return audio;
}

void reportAudioOpenFailure(AudioOpenStatus status) noexcept
{
    if (status == AudioOpenStatus::NoAudioStream)
    {
        std::fprintf(
            stderr,
            "[Engine] video has no embedded audio stream; continuing video-only\n");
    }
    else if (status == AudioOpenStatus::DecodeFailure)
    {
        std::fprintf(
            stderr,
            "[Engine] embedded audio decode failed; continuing video-only\n");
    }
    std::fflush(stderr);
}

bool effectiveSceneDuration(
    const DesktopSceneEngine::Domain::Scene& scene,
    const MediaResource& resource,
    float& duration) noexcept
{
    if (resource.type() == MediaType::Video)
    {
        const auto* video = dynamic_cast<const VideoResource*>(&resource);
        if (video == nullptr)
        {
            return false;
        }

        const float mediaDuration = video->duration();
        if (!std::isfinite(mediaDuration) || mediaDuration <= 0.0F)
        {
            return false;
        }

        duration = mediaDuration;
        return true;
    }

    if (!std::isfinite(scene.duration) || scene.duration < 0.0F)
    {
        return false;
    }

    duration = scene.duration;
    return true;
}

bool uploadInitialMedia(Renderer& renderer, MediaResource& resource, float time)
{
    if (resource.type() == MediaType::Image)
    {
        const auto* image = dynamic_cast<const ImageResource*>(&resource);
        return image != nullptr
            && renderer.updateImage(
                image->data(),
                image->width(),
                image->height(),
                image->channels(),
                image->rowStride(),
                image->byteSize()) != MediaUpdateResult::ResourceFailure;
    }

    if (resource.type() == MediaType::Video)
    {
        auto* video = dynamic_cast<VideoResource*>(&resource);
        if (video == nullptr || !video->frameAt(time))
        {
            return false;
        }

        const DesktopSceneEngine::Resources::DecodedFrame& frame = video->currentFrame();
        if (!video->verifyBufferGuard("before-renderer-update"))
        {
            return false;
        }

        const MediaUpdateResult updateResult = renderer.updateFrame(
            frame.data,
            frame.width,
            frame.height,
            frame.channels,
            frame.rowStride,
            frame.byteSize);
        if (!video->verifyBufferGuard("after-renderer-update"))
        {
            return false;
        }

        return updateResult != MediaUpdateResult::ResourceFailure;
    }

    return false;
}

std::optional<DesktopSceneEngine::Rendering::Detail::MediaView> currentMediaView(
    const MediaResource* resource) noexcept
{
    using DesktopSceneEngine::Rendering::Detail::MediaView;
    if (resource == nullptr)
        return std::nullopt;
    if (resource->type() == MediaType::Image)
    {
        const auto* image = dynamic_cast<const ImageResource*>(resource);
        if (!image)
            return std::nullopt;
        return MediaView{image->data(), image->width(), image->height(),
            image->channels(), image->rowStride(), image->byteSize()};
    }
    if (resource->type() == MediaType::Video)
    {
        const auto* video = dynamic_cast<const VideoResource*>(resource);
        if (!video)
            return std::nullopt;
        const auto& frame = video->currentFrame();
        return MediaView{frame.data, frame.width, frame.height, frame.channels,
            frame.rowStride, frame.byteSize};
    }
    return std::nullopt;
}

DesktopSceneEngine::Rendering::RenderData makeActiveSceneRenderData(
    const DesktopSceneEngine::Domain::Project* project,
    const DesktopSceneEngine::Runtime::SceneRuntime* sceneRuntime,
    const std::optional<DesktopSceneEngine::Engine::MediaDimensions>& mediaDimensions,
    const DesktopSceneEngine::Domain::AspectRatio& canvasAspect
)
{
    DesktopSceneEngine::Rendering::RenderData renderData{};
    if (project == nullptr || sceneRuntime == nullptr
        || !sceneRuntime->hasActiveScene() || !mediaDimensions.has_value()
        || !canvasAspect.isValid())
    {
        return renderData;
    }

    for (const DesktopSceneEngine::Domain::Scene& scene : project->scenes)
    {
        if (scene.id != sceneRuntime->activeSceneId())
        {
            continue;
        }

        const auto presentation =
            DesktopSceneEngine::Core::makeScenePresentationRenderState(
                mediaDimensions->width,
                mediaDimensions->height,
                canvasAspect,
                scene.crop,
                scene.layout,
                scene.presentation);
        if (presentation.has_value())
        {
            renderData.presentation = *presentation;
        }
        break;
    }

    return renderData;
}

}

namespace DesktopSceneEngine
{

Engine::Engine()
    : sceneRuntime_(std::make_unique<Runtime::SceneRuntime>()),
      renderer_(std::make_unique<Rendering::Renderer>()),
      audioMixer_(std::make_unique<AudioMixer>()),
      audioOutput_(std::make_unique<Platform::Audio::AudioOutput>()),
      defaultAudioEndpointWatcher_(std::make_unique<Platform::Audio::DefaultAudioEndpointWatcher>()),
      mixedOutputBridge_(std::make_unique<MixedOutputBridge>(
          *audioMixer_,
          *audioOutput_)),
      audioOutputOperationalState_(Core::AudioOutputOperationalState::Healthy)
{
    backgroundMusicRuntime_ = std::make_unique<Music::BackgroundMusicRuntime>();
    backgroundMusicExecutor_ = std::make_unique<Music::BackgroundMusicExecutor>(*audioMixer_);
    backgroundMusicOrchestrator_ = std::make_unique<Music::BackgroundMusicOrchestrator>(
        *backgroundMusicRuntime_,
        [executor = backgroundMusicExecutor_.get()](const Music::MusicRuntimeAction& action)
        {
            return executor->execute(action);
        });
}

Engine::~Engine()
{
    shutdown();
}

void Engine::removeEmbeddedAudioInput() noexcept
{
    if (embeddedAudioInputHandleValue_ == 0U)
    {
        return;
    }

    if (audioMixer_)
    {
        // AudioMixer::removeInput() returns false only when the handle is no
        // longer present, so the Mixer cannot retain this adapter on failure.
        (void)audioMixer_->removeInput(
            AudioInputHandle{embeddedAudioInputHandleValue_});
    }
    embeddedAudioInputHandleValue_ = 0U;
}

void Engine::stopSharedAudioOutput() noexcept
{
    if (audioOutput_)
    {
        audioOutput_->stop();
    }
    sharedAudioOutputState_ = static_cast<std::uint8_t>(
        Core::SharedAudioOutputState::Stopped);
    audioOutputInactiveDiagnosticReported_ = false;
}

bool Engine::hasAudioInputPresence() const noexcept
{
    return embeddedAudioInputHandleValue_ != 0U
        || (backgroundMusicExecutor_ && backgroundMusicExecutor_->hasActiveSources());
}

Core::SharedAudioOutputState Engine::desiredSharedAudioOutputState() const noexcept
{
    const Core::PlaybackActivity activity = !projectBound_
            || !playbackRuntime_
            || !playbackRuntime_->isConfigured()
        ? Core::PlaybackActivity::Stopped
        : playbackRuntime_->isPaused()
            ? Core::PlaybackActivity::Paused
            : Core::PlaybackActivity::Playing;
    return Core::resolveSharedAudioOutputTransition(
        activity,
        hasAudioInputPresence(),
        Core::SharedAudioOutputState::Stopped).desiredState;
}

void Engine::enterAudioOutputDeviceUnavailable() noexcept
{
    const bool newOutage = audioOutputOperationalState_
        != Core::AudioOutputOperationalState::DeviceUnavailable;
    audioOutputOperationalState_ = Core::AudioOutputOperationalState::DeviceUnavailable;
    audioOutputRetryElapsedSeconds_ = 0.0;
    embeddedAudioNeedsResyncAfterOutputRecovery_ = embeddedAudioInputHandleValue_ != 0U;
    stopSharedAudioOutput();

    if (newOutage)
    {
        std::fprintf(stderr, "[Engine] shared audio output device unavailable\n");
        std::fflush(stderr);
    }
}

void Engine::enterAudioOutputContractFaulted() noexcept
{
    const bool newFault = audioOutputOperationalState_
        != Core::AudioOutputOperationalState::ContractFaulted;
    audioOutputOperationalState_ = Core::AudioOutputOperationalState::ContractFaulted;
    audioOutputRetryElapsedSeconds_ = 0.0;
    embeddedAudioNeedsResyncAfterOutputRecovery_ = false;
    stopSharedAudioOutput();

    if (newFault)
    {
        std::fprintf(stderr, "[Engine] shared audio output contract faulted\n");
        std::fflush(stderr);
    }
}

bool Engine::reconcileAudioOutputState(bool allowPrime) noexcept
{
    const Core::SharedAudioOutputState desiredState = desiredSharedAudioOutputState();
    const Core::SharedAudioOutputTransition transition =
        Core::resolveSharedAudioOutputTransition(
            !projectBound_ || !playbackRuntime_ || !playbackRuntime_->isConfigured()
                ? Core::PlaybackActivity::Stopped
                : playbackRuntime_->isPaused()
                    ? Core::PlaybackActivity::Paused
                    : Core::PlaybackActivity::Playing,
            hasAudioInputPresence(),
            static_cast<Core::SharedAudioOutputState>(sharedAudioOutputState_));

    if (audioOutputOperationalState_ != Core::AudioOutputOperationalState::Healthy)
    {
        if (desiredState == Core::SharedAudioOutputState::Stopped)
        {
            stopSharedAudioOutput();
            audioOutputRetryElapsedSeconds_ = 0.0;
            if (!hasAudioInputPresence())
            {
                embeddedAudioNeedsResyncAfterOutputRecovery_ = false;
            }
            return true;
        }

        return false;
    }

    if (!audioOutput_)
    {
        sharedAudioOutputState_ = static_cast<std::uint8_t>(
            Core::SharedAudioOutputState::Stopped);
        return transition.desiredState == Core::SharedAudioOutputState::Stopped;
    }

    switch (transition.command)
    {
    case Core::SharedAudioOutputCommand::None:
        return true;

    case Core::SharedAudioOutputCommand::Stop:
        stopSharedAudioOutput();
        return true;

    case Core::SharedAudioOutputCommand::BeginRunning:
    case Core::SharedAudioOutputCommand::ConfigurePaused:
    {
        const bool initialized = audioOutputRecoveryTestHooks_
                && audioOutputRecoveryTestHooks_->initialize
            ? audioOutputRecoveryTestHooks_->initialize()
            : audioOutput_->initialize();
        if (!initialized)
        {
            enterAudioOutputDeviceUnavailable();
            return false;
        }

        audioOutput_->setVolume(1.0F);
        const AudioBlockStartMode startMode =
            transition.command == Core::SharedAudioOutputCommand::BeginRunning
                ? AudioBlockStartMode::StartImmediately
                : AudioBlockStartMode::ConfigurePaused;
        const AudioBlockStatus status = audioOutputRecoveryTestHooks_
                && audioOutputRecoveryTestHooks_->beginBlockMode
            ? audioOutputRecoveryTestHooks_->beginBlockMode(startMode)
            : audioOutput_->beginBlockMode(startMode);
        if (status != AudioBlockStatus::Success)
        {
            if (status == AudioBlockStatus::DeviceFailure)
            {
                enterAudioOutputDeviceUnavailable();
            }
            else
            {
                enterAudioOutputContractFaulted();
            }
            return false;
        }

        sharedAudioOutputState_ = static_cast<std::uint8_t>(transition.desiredState);
        break;
    }

    case Core::SharedAudioOutputCommand::Pause:
    {
        const AudioBlockStatus status = audioOutputRecoveryTestHooks_
                && audioOutputRecoveryTestHooks_->pause
            ? audioOutputRecoveryTestHooks_->pause()
            : audioOutput_->pause();
        if (status == AudioBlockStatus::DeviceFailure)
        {
            enterAudioOutputDeviceUnavailable();
            return false;
        }
        if (status != AudioBlockStatus::Success)
        {
            enterAudioOutputContractFaulted();
            return false;
        }
        sharedAudioOutputState_ = static_cast<std::uint8_t>(
            Core::SharedAudioOutputState::Paused);
        break;
    }

    case Core::SharedAudioOutputCommand::Resume:
    {
        const AudioBlockStatus status = audioOutputRecoveryTestHooks_
                && audioOutputRecoveryTestHooks_->resume
            ? audioOutputRecoveryTestHooks_->resume()
            : audioOutput_->resume();
        if (status == AudioBlockStatus::DeviceFailure)
        {
            enterAudioOutputDeviceUnavailable();
            return false;
        }
        if (status != AudioBlockStatus::Success)
        {
            enterAudioOutputContractFaulted();
            return false;
        }
        sharedAudioOutputState_ = static_cast<std::uint8_t>(
            Core::SharedAudioOutputState::Running);
        break;
    }
    }

    return !transition.shouldPrime || !allowPrime || pumpMixedAudio();
}

bool Engine::recoverAudioOutput(Core::SharedAudioOutputState desiredState) noexcept
{
    if (!audioOutput_ || desiredState == Core::SharedAudioOutputState::Stopped)
    {
        return false;
    }

    if (audioOutputRecoveryTestHooks_ && audioOutputRecoveryTestHooks_->shutdown)
    {
        audioOutputRecoveryTestHooks_->shutdown();
    }
    else
    {
        audioOutput_->shutdown();
    }
    const bool initialized = audioOutputRecoveryTestHooks_
            && audioOutputRecoveryTestHooks_->initialize
        ? audioOutputRecoveryTestHooks_->initialize()
        : audioOutput_->initialize();
    if (!initialized)
    {
        enterAudioOutputDeviceUnavailable();
        return false;
    }

    if (embeddedAudioNeedsResyncAfterOutputRecovery_)
    {
        removeEmbeddedAudioInput();
        embeddedAudioNeedsResyncAfterOutputRecovery_ = false;
        if (audioResource_ && playbackRuntime_)
        {
            (void)configureEmbeddedAudioInputAt(
                playbackRuntime_->currentTime(),
                playbackRuntime_->configuration().speed);
        }
    }

    desiredState = desiredSharedAudioOutputState();
    if (desiredState == Core::SharedAudioOutputState::Stopped)
    {
        stopSharedAudioOutput();
        audioOutputOperationalState_ = Core::AudioOutputOperationalState::Healthy;
        audioOutputRetryElapsedSeconds_ = 0.0;
        return true;
    }

    audioOutput_->setVolume(1.0F);
    const AudioBlockStartMode startMode = desiredState == Core::SharedAudioOutputState::Running
        ? AudioBlockStartMode::StartImmediately
        : AudioBlockStartMode::ConfigurePaused;
    const AudioBlockStatus status = audioOutputRecoveryTestHooks_
            && audioOutputRecoveryTestHooks_->beginBlockMode
        ? audioOutputRecoveryTestHooks_->beginBlockMode(startMode)
        : audioOutput_->beginBlockMode(startMode);
    if (status == AudioBlockStatus::DeviceFailure)
    {
        enterAudioOutputDeviceUnavailable();
        return false;
    }
    if (status != AudioBlockStatus::Success)
    {
        enterAudioOutputContractFaulted();
        return false;
    }

    audioOutputOperationalState_ = Core::AudioOutputOperationalState::Healthy;
    audioOutputRetryElapsedSeconds_ = 0.0;
    sharedAudioOutputState_ = static_cast<std::uint8_t>(desiredState);
    std::fprintf(stderr, "[Engine] shared audio output recovered\n");
    std::fflush(stderr);

    return desiredState != Core::SharedAudioOutputState::Running || pumpMixedAudio();
}

void Engine::advanceAudioOutputRecovery(float deltaTime) noexcept
{
    if (audioOutputOperationalState_ != Core::AudioOutputOperationalState::DeviceUnavailable)
    {
        return;
    }

    if (std::isfinite(deltaTime) && deltaTime > 0.0F)
    {
        const double elapsed = audioOutputRetryElapsedSeconds_
            + static_cast<double>(deltaTime);
        audioOutputRetryElapsedSeconds_ = std::isfinite(elapsed)
            ? elapsed
            : (std::numeric_limits<double>::max)();
    }

    const Core::SharedAudioOutputState desiredState = desiredSharedAudioOutputState();
    const double cooldownSeconds = std::chrono::duration<double>(
        Core::SharedAudioOutputRecoveryCooldown).count();
    constexpr double floatDeltaToleranceSeconds = 0.000001;
    const auto elapsedMilliseconds = audioOutputRetryElapsedSeconds_
            + floatDeltaToleranceSeconds >= cooldownSeconds
        ? Core::SharedAudioOutputRecoveryCooldown
        : std::chrono::milliseconds{
            static_cast<std::chrono::milliseconds::rep>(
                std::min(
                    audioOutputRetryElapsedSeconds_ * 1000.0,
                    static_cast<double>((std::numeric_limits<std::chrono::milliseconds::rep>::max)())))};
    const Core::SharedAudioOutputRecoveryAction action =
        Core::resolveSharedAudioOutputRecoveryAction(
            audioOutputOperationalState_,
            desiredState,
            elapsedMilliseconds);

    switch (action)
    {
    case Core::SharedAudioOutputRecoveryAction::None:
        return;

    case Core::SharedAudioOutputRecoveryAction::CancelRecovery:
        stopSharedAudioOutput();
        audioOutputRetryElapsedSeconds_ = 0.0;
        if (!hasAudioInputPresence())
        {
            embeddedAudioNeedsResyncAfterOutputRecovery_ = false;
        }
        return;

    case Core::SharedAudioOutputRecoveryAction::RetryRunning:
    case Core::SharedAudioOutputRecoveryAction::RetryPaused:
        (void)recoverAudioOutput(desiredState);
        return;
    }
}

void Engine::observeDefaultAudioEndpointGeneration() noexcept
{
    std::uint64_t observedGeneration = lastSeenDefaultAudioEndpointGeneration_;
    bool observationAvailable = false;
    if (audioOutputRecoveryTestHooks_
        && audioOutputRecoveryTestHooks_->defaultAudioEndpointGeneration)
    {
        observedGeneration = audioOutputRecoveryTestHooks_->defaultAudioEndpointGeneration();
        observationAvailable = true;
    }
    else if (defaultAudioEndpointWatcherAvailable_ && defaultAudioEndpointWatcher_)
    {
        observedGeneration = defaultAudioEndpointWatcher_->generation();
        observationAvailable = true;
    }

    if (observationAvailable && observedGeneration != lastSeenDefaultAudioEndpointGeneration_)
    {
        lastSeenDefaultAudioEndpointGeneration_ = observedGeneration;
        defaultAudioEndpointRerouteRequested_ = true;
    }
}

void Engine::rerouteDefaultAudioEndpoint() noexcept
{
    defaultAudioEndpointRerouteRequested_ = false;
    if (audioOutputOperationalState_ == Core::AudioOutputOperationalState::ContractFaulted)
    {
        return;
    }

    const Core::SharedAudioOutputState desiredState = desiredSharedAudioOutputState();
    if (desiredState == Core::SharedAudioOutputState::Stopped)
    {
        stopSharedAudioOutput();
        if (audioOutputRecoveryTestHooks_ && audioOutputRecoveryTestHooks_->shutdown)
        {
            audioOutputRecoveryTestHooks_->shutdown();
        }
        else if (audioOutput_)
        {
            audioOutput_->shutdown();
        }
        audioOutputRetryElapsedSeconds_ = 0.0;
        if (!hasAudioInputPresence())
        {
            embeddedAudioNeedsResyncAfterOutputRecovery_ = false;
        }
        return;
    }

    embeddedAudioNeedsResyncAfterOutputRecovery_ = embeddedAudioInputHandleValue_ != 0U;
    (void)recoverAudioOutput(desiredState);
}

void Engine::releaseEmbeddedAudioResource() noexcept
{
    assert(embeddedAudioInputHandleValue_ == 0U);
    audioResource_.reset();
}

bool Engine::setEmbeddedAudioTempo(float speed) noexcept
{
    assert(embeddedAudioInputHandleValue_ == 0U);
    return audioResource_ && audioResource_->setTempo(speed);
}

bool Engine::configureEmbeddedAudioInputAt(
    float mediaTime,
    float playbackSpeed
) noexcept
{
    if (!audioResource_ || !audioMixer_ || embeddedAudioInputHandleValue_ != 0U
        || audioResource_->channels() != 2)
    {
        return false;
    }

    const double mediaDuration = audioResource_->durationSeconds();
    if (!std::isfinite(mediaDuration)
        || mediaDuration <= 0.0
        || !std::isfinite(mediaTime)
        || mediaTime < 0.0F
        || static_cast<double>(mediaTime) >= mediaDuration)
    {
        return false;
    }

    const auto startFrame = Core::resolveEmbeddedAudioStartFrame(
        audioResource_->frameCount(),
        audioResource_->sampleRate(),
        mediaTime,
        playbackSpeed);
    const std::int16_t* const data = audioResource_->data();
    if (!startFrame.has_value() || data == nullptr)
    {
        return false;
    }

    std::unique_ptr<EmbeddedAudioMixerInput> input{};
    try
    {
        input = std::make_unique<EmbeddedAudioMixerInput>(
            std::span<const std::int16_t>{
                data,
                audioResource_->sampleCount()},
            *startFrame);
    }
    catch (const std::bad_alloc&)
    {
        return false;
    }
    const AudioInputHandle handle = audioMixer_->addInput(
        std::move(input),
        embeddedAudioGain_);
    if (!handle)
    {
        return false;
    }

    embeddedAudioInputHandleValue_ = handle.value;
    return true;
}

bool Engine::pumpMixedAudio() noexcept
{
    if (!mixedOutputBridge_)
    {
        return false;
    }

    const auto result = audioOutputRecoveryTestHooks_
            && audioOutputRecoveryTestHooks_->pump
        ? audioOutputRecoveryTestHooks_->pump()
        : mixedOutputBridge_->pump();
    if (result.status == MixedOutputBridgeStatus::OutputInactive)
    {
        if (!audioOutputInactiveDiagnosticReported_)
        {
            std::fprintf(stderr, "[Engine] shared audio output inactive while running\n");
            std::fflush(stderr);
            audioOutputInactiveDiagnosticReported_ = true;
        }
        return true;
    }
    audioOutputInactiveDiagnosticReported_ = false;
    if (result.status == MixedOutputBridgeStatus::DeviceFailure)
    {
        enterAudioOutputDeviceUnavailable();
        return false;
    }
    if (result.status == MixedOutputBridgeStatus::OutputContractFailure)
    {
        enterAudioOutputContractFaulted();
        return false;
    }

    return true;
}

void Engine::initialize()
{
    if (initialized_)
    {
        return;
    }

    desktopWindowHost_ = std::make_unique<Platform::Windows::DesktopWindowHost>();
    desktopWindowHost_->initialize();

    if (!desktopWindowHost_->isInitialized())
    {
        desktopWindowHost_.reset();
        return;
    }

    if (!initializeWithWindowHost())
    {
        desktopWindowHost_->shutdown();
        desktopWindowHost_.reset();
    }
}

PresentationTargetId Engine::initialize(const ExternalWindowTarget& target)
{
    if (initialized_)
    {
        return {};
    }

    desktopWindowHost_ = std::make_unique<Platform::Windows::DesktopWindowHost>();
    desktopWindowHost_->initializeExternal(target.handle);

    if (!desktopWindowHost_->isInitialized())
    {
        desktopWindowHost_.reset();
        return {};
    }

    if (!initializeWithWindowHost(target.transport))
    {
        desktopWindowHost_->shutdown();
        desktopWindowHost_.reset();
        return {};
    }

    return defaultPresentationTargetId_;
}

bool Engine::initializeWithWindowHost(PresentationTransport transport)
{
    if (!desktopWindowHost_ || !desktopWindowHost_->isInitialized())
    {
        return false;
    }

    Rendering::WindowTarget windowTarget = desktopWindowHost_->getWindowTarget();
    windowTarget.transport = transport;
    const PresentationTargetId targetId = renderer_->initialize(windowTarget);
    if (!targetId.isValid())
    {
        renderer_->shutdown();
        return false;
    }
    defaultPresentationTargetId_ = targetId;

    playbackRuntime_ = std::make_unique<Runtime::PlaybackRuntime>();
    if (audioOutput_)
    {
        audioOutput_->setVolume(1.0F);
        const bool audioInitialized = audioOutputRecoveryTestHooks_
                && audioOutputRecoveryTestHooks_->initialize
            ? audioOutputRecoveryTestHooks_->initialize()
            : audioOutput_->initialize();
        if (!audioInitialized)
        {
            enterAudioOutputDeviceUnavailable();
            std::fprintf(
                stderr,
                "[Engine] audio output initialization unavailable; continuing without audio\n");
            std::fflush(stderr);
        }
    }

    defaultAudioEndpointWatcherAvailable_ = false;
    lastSeenDefaultAudioEndpointGeneration_ = 0U;
    defaultAudioEndpointRerouteRequested_ = false;
    if (defaultAudioEndpointWatcher_ && defaultAudioEndpointWatcher_->initialize())
    {
        defaultAudioEndpointWatcherAvailable_ = true;
        lastSeenDefaultAudioEndpointGeneration_ = defaultAudioEndpointWatcher_->generation();
    }
    else
    {
        std::fprintf(
            stderr,
            "[Engine] default audio endpoint monitoring unavailable; continuing without hot-switch support\n");
        std::fflush(stderr);
    }
    initialized_ = true;

    std::cout
        << "Desktop Scene Engine initialized."
        << std::endl;

    return true;
}

PresentationTargetId Engine::attachPresentationTarget(
    const ExternalWindowTarget& target)
{
    if (!initialized_ || !renderer_ || target.handle == nullptr)
    {
        return {};
    }

    Rendering::WindowTarget windowTarget{};
    windowTarget.handle = target.handle;
    windowTarget.transport = target.transport;
    return renderer_->attachPresentationTarget(windowTarget);
}

bool Engine::detachPresentationTarget(PresentationTargetId id) noexcept
{
    if (!initialized_ || !renderer_
        || !renderer_->detachPresentationTarget(id))
    {
        return false;
    }

    if (id == defaultPresentationTargetId_)
    {
        defaultPresentationTargetId_ = {};
    }
    return true;
}

bool Engine::setPresentationPacingTarget(PresentationTargetId id) noexcept
{
    return initialized_ && renderer_
        && renderer_->setPresentationPacingTarget(id);
}

bool Engine::isInitialized() const noexcept
{
    return initialized_;
}

bool Engine::bindProject(
    const Domain::Project& project,
    const Domain::EntityId& activeSceneId
)
{
    if (!initialized_ || projectBound_)
    {
        return false;
    }

    Domain::Playback playback{};
    bool activeSceneFound = false;
    const Domain::Scene* activeScene = nullptr;
    const Media* activeMedia = nullptr;

    for (const Domain::Scene& scene : project.scenes)
    {
        if (scene.id == activeSceneId)
        {
            playback = scene.playback;
            activeSceneFound = true;
            activeScene = &scene;
            break;
        }
    }

    if (!activeSceneFound)
    {
        return false;
    }

    std::unique_ptr<MediaResource> mediaResource{};
    for (const Domain::Media& media : project.library.media)
    {
        if (activeScene != nullptr && media.id == activeScene->mediaId)
        {
            mediaResource = createMediaResource(media);
            activeMedia = &media;
            if (mediaResource == nullptr)
            {
                return false;
            }
            break;
        }
    }

    std::unique_ptr<AudioResource> audioResource{};
    AudioOpenStatus audioStatus = AudioOpenStatus::NotOpened;
    if (activeMedia != nullptr
        && mediaResource
        && mediaResource->type() == MediaType::Video
        && audioOutput_)
    {
        audioResource = createAudioResource(*activeMedia, audioStatus);
        if (!audioResource)
        {
            reportAudioOpenFailure(audioStatus);
        }
    }

    float effectiveDuration = 0.0F;
    if (mediaResource)
    {
        if (!effectiveSceneDuration(*activeScene, *mediaResource, effectiveDuration)
            || !uploadInitialMedia(*renderer_, *mediaResource, 0.0F))
        {
            return false;
        }
    }
    else if (!std::isfinite(activeScene->duration)
        || activeScene->duration < 0.0F)
    {
        return false;
    }

    if (!mediaResource)
    {
        effectiveDuration = activeScene->duration;
        renderer_->clearImage();
    }
    if (embeddedAudioInputHandleValue_ != 0U)
    {
        // V1 scene-replacement policy: queued samples for the previous
        // embedded track must not bleed into a newly bound Project. BGM
        // registrations remain in the Mixer and resume from their cursors.
        stopSharedAudioOutput();
    }
    embeddedAudioNeedsResyncAfterOutputRecovery_ = false;
    removeEmbeddedAudioInput();
    releaseEmbeddedAudioResource();
    audioResource_ = std::move(audioResource);
    if (audioResource_
        && (!setEmbeddedAudioTempo(playback.speed)
            || !configureEmbeddedAudioInputAt(
                0.0F,
                playback.speed)))
    {
        std::fprintf(
            stderr,
            "[Engine] audio output rejected decoded audio; continuing video-only\n");
        std::fflush(stderr);
        removeEmbeddedAudioInput();
        releaseEmbeddedAudioResource();
    }
    sceneRuntime_->setActiveScene(activeSceneId);
    playbackRuntime_->configure(playback, effectiveDuration);
    double initialVideoPresentedTime = -1.0;
    if (mediaResource && mediaResource->type() == MediaType::Video)
    {
        const auto* video = dynamic_cast<const VideoResource*>(mediaResource.get());
        if (video != nullptr)
        {
            initialVideoPresentedTime = presentedTimeOrFallback(
                video->currentFrame(),
                0.0);
        }
    }
    mediaResource_ = std::move(mediaResource);
    videoFrameTime_ = mediaResource_ && mediaResource_->type() == MediaType::Video
        ? 0.0F
        : -1.0F;
    videoPresentedTime_ = initialVideoPresentedTime;
    syncDiagnosticUpdates_ = 0U;
    boundProject_ = &project;
    projectBound_ = true;
    pendingNaturalSceneCompletion_ = false;
    naturalSceneCompletionHalted_ = false;

    (void)reconcileAudioOutputState(true);
    return true;
}

bool Engine::switchScene(
    const Domain::Project& project,
    const Domain::EntityId& sceneId
)
{
    if (!initialized_ || !projectBound_ || boundProject_ != &project)
    {
        return false;
    }

    Domain::Playback playback{};
    const Domain::Scene* requestedScene = nullptr;
    const Media* requestedMedia = nullptr;
    for (const Domain::Scene& scene : project.scenes)
    {
        if (scene.id == sceneId)
        {
            requestedScene = &scene;
            playback = scene.playback;
            break;
        }
    }

    if (requestedScene == nullptr)
    {
        return false;
    }

    std::unique_ptr<MediaResource> mediaResource{};
    for (const Domain::Media& media : project.library.media)
    {
        if (media.id == requestedScene->mediaId)
        {
            mediaResource = createMediaResource(media);
            requestedMedia = &media;
            if (mediaResource == nullptr)
            {
                return false;
            }
            break;
        }
    }

    std::unique_ptr<AudioResource> audioResource{};
    AudioOpenStatus audioStatus = AudioOpenStatus::NotOpened;
    if (requestedMedia != nullptr
        && mediaResource
        && mediaResource->type() == MediaType::Video
        && audioOutput_)
    {
        audioResource = createAudioResource(*requestedMedia, audioStatus);
        if (!audioResource)
        {
            reportAudioOpenFailure(audioStatus);
        }
    }

    float effectiveDuration = 0.0F;
    if (mediaResource)
    {
        if (!effectiveSceneDuration(
                *requestedScene,
                *mediaResource,
                effectiveDuration)
            || !uploadInitialMedia(*renderer_, *mediaResource, 0.0F))
        {
            return false;
        }
    }
    else if (!std::isfinite(requestedScene->duration)
        || requestedScene->duration < 0.0F)
    {
        return false;
    }
    else
    {
        effectiveDuration = requestedScene->duration;
        renderer_->clearImage();
    }
    if (embeddedAudioInputHandleValue_ != 0U)
    {
        // V1 scene-replacement policy: flush old embedded media samples while
        // preserving all BGM registrations and their source cursors.
        stopSharedAudioOutput();
    }
    embeddedAudioNeedsResyncAfterOutputRecovery_ = false;
    removeEmbeddedAudioInput();
    releaseEmbeddedAudioResource();
    audioResource_ = std::move(audioResource);
    if (audioResource_
        && (!setEmbeddedAudioTempo(playback.speed)
            || !configureEmbeddedAudioInputAt(
                0.0F,
                playback.speed)))
    {
        std::fprintf(
            stderr,
            "[Engine] audio output rejected decoded audio; continuing video-only\n");
        std::fflush(stderr);
        removeEmbeddedAudioInput();
        releaseEmbeddedAudioResource();
    }
    sceneRuntime_->setActiveScene(sceneId);
    playbackRuntime_->configure(playback, effectiveDuration);
    double initialVideoPresentedTime = -1.0;
    if (mediaResource && mediaResource->type() == MediaType::Video)
    {
        const auto* video = dynamic_cast<const VideoResource*>(mediaResource.get());
        if (video != nullptr)
        {
            initialVideoPresentedTime = presentedTimeOrFallback(
                video->currentFrame(),
                0.0);
        }
    }
    mediaResource_ = std::move(mediaResource);
    videoFrameTime_ = mediaResource_ && mediaResource_->type() == MediaType::Video
        ? 0.0F
        : -1.0F;
    videoPresentedTime_ = initialVideoPresentedTime;
    syncDiagnosticUpdates_ = 0U;
    pendingNaturalSceneCompletion_ = false;
    naturalSceneCompletionHalted_ = false;

    ++sceneSwitches;
    std::fprintf(
        stderr,
        "[Engine] scene-switch=%llu scene=%s media-type=%d\n",
        static_cast<unsigned long long>(sceneSwitches),
        sceneId.value().c_str(),
        static_cast<int>(mediaResource_ ? mediaResource_->type() : MediaType::Unknown));
    std::fprintf(
        stderr,
        "[Engine] effective-duration=%.6f configured-duration=%.6f\n",
        static_cast<double>(effectiveDuration),
        static_cast<double>(requestedScene->duration));
    std::fflush(stderr);

    (void)reconcileAudioOutputState(true);
    return true;
}

void Engine::detachProject()
{
    sceneRuntime_->clearActiveScene();

    if (playbackRuntime_)
    {
        playbackRuntime_->reset();
    }

    if (renderer_)
    {
        renderer_->clearImage();
    }

    stopSharedAudioOutput();
    removeEmbeddedAudioInput();
    embeddedAudioNeedsResyncAfterOutputRecovery_ = false;
    audioOutputRetryElapsedSeconds_ = 0.0;
    mediaResource_.reset();
    releaseEmbeddedAudioResource();
    videoFrameTime_ = -1.0F;
    videoPresentedTime_ = -1.0;
    syncDiagnosticUpdates_ = 0U;
    pendingNaturalSceneCompletion_ = false;
    naturalSceneCompletionHalted_ = false;

    boundProject_ = nullptr;
    projectBound_ = false;
    (void)reconcileAudioOutputState(false);
}

void Engine::update()
{
    update(0.0F);
}

void Engine::setSceneCompletionPolicy(SceneCompletionPolicy policy) noexcept
{
    sceneCompletionPolicy_ = policy;
    pendingNaturalSceneCompletion_ = false;
    naturalSceneCompletionHalted_ = false;
}

bool Engine::consumeNaturalSceneCompletion() noexcept
{
    const bool completed = pendingNaturalSceneCompletion_;
    pendingNaturalSceneCompletion_ = false;
    return completed;
}

void Engine::update(float deltaTime)
{
    observeDefaultAudioEndpointGeneration();
    const bool audioOutputWasUnavailable = audioOutputOperationalState_
        == Core::AudioOutputOperationalState::DeviceUnavailable;
    bool sharedOutputPumpSucceeded = true;
    if (!defaultAudioEndpointRerouteRequested_
        && audioOutputOperationalState_ == Core::AudioOutputOperationalState::Healthy
        && sharedAudioOutputState_ == static_cast<std::uint8_t>(
            Core::SharedAudioOutputState::Running))
    {
        sharedOutputPumpSucceeded = pumpMixedAudio();
    }

    if (backgroundMusicExecutor_ && backgroundMusicOrchestrator_)
    {
        bool processedTerminalEvent = false;
        for (Music::MusicRuntimeEvent& terminalEvent : backgroundMusicExecutor_->collectTerminalEvents())
        {
            processedTerminalEvent = true;
            (void)processBackgroundMusicEventInternal(
                backgroundMusicOrchestrator_.get(),
                std::move(terminalEvent));
        }
        // A failed device write is not an A4-8a recovery signal.  In
        // particular, do not let terminal processing immediately re-open a
        // stopped shared output in the same update.
        if (processedTerminalEvent && sharedOutputPumpSucceeded)
        {
            (void)reconcileAudioOutputState(false);
        }
    }

    const auto finishAudioOutputRouting = [this, audioOutputWasUnavailable, deltaTime]() noexcept
    {
        if (defaultAudioEndpointRerouteRequested_)
        {
            rerouteDefaultAudioEndpoint();
            return;
        }

        // A DeviceFailure detected during this update occurs after the
        // supplied deltaTime has elapsed. Do not credit that pre-failure
        // interval toward the new outage's cooldown.
        if (audioOutputWasUnavailable)
        {
            advanceAudioOutputRecovery(deltaTime);
        }
    };

    if (!initialized_
        || !projectBound_
        || boundProject_ == nullptr
        || !playbackRuntime_
        || !sceneRuntime_)
    {
        finishAudioOutputRouting();
        return;
    }

    const float previousPlaybackTime = playbackRuntime_->currentTime();
    const float previousVideoFrameTime = videoFrameTime_;
    const double previousVideoPresentedTime = videoPresentedTime_;
    playbackRuntime_->update(deltaTime);
    const auto logSync = [this]() noexcept
    {
        logPlaybackSync(
            *playbackRuntime_,
            audioOutput_.get(),
            videoPresentedTime_,
            syncDiagnosticUpdates_);
    };

    if (naturalSceneCompletionHalted_)
    {
        playbackRuntime_->pause();
        (void)reconcileAudioOutputState(false);
        logSync();
        finishAudioOutputRouting();
        return;
    }

    if (playbackRuntime_->shouldAdvanceScene())
    {
        if (sceneCompletionPolicy_
            == SceneCompletionPolicy::ExternalSceneCompletion)
        {
            playbackRuntime_->pause();
            pendingNaturalSceneCompletion_ = true;
            naturalSceneCompletionHalted_ = true;
            (void)reconcileAudioOutputState(false);
            logSync();
            finishAudioOutputRouting();
            return;
        }

        const Domain::EntityId& activeSceneId = sceneRuntime_->activeSceneId();
        std::size_t activeSceneIndex = boundProject_->scenes.size();
        for (std::size_t index = 0; index < boundProject_->scenes.size(); ++index)
        {
            if (boundProject_->scenes[index].id == activeSceneId)
            {
                activeSceneIndex = index;
                break;
            }
        }

        if (activeSceneIndex >= boundProject_->scenes.size())
        {
            playbackRuntime_->restoreTime(previousPlaybackTime);
            logSync();
            finishAudioOutputRouting();
            return;
        }

        const bool atEnd = activeSceneIndex + 1 >= boundProject_->scenes.size();
        if (atEnd && !playbackRuntime_->configuration().loop)
        {
            playbackRuntime_->pause();
            (void)reconcileAudioOutputState(false);
            logSync();
            finishAudioOutputRouting();
            return;
        }

        const std::size_t nextSceneIndex = atEnd ? 0 : activeSceneIndex + 1;
        if (!switchScene(*boundProject_, boundProject_->scenes[nextSceneIndex].id))
        {
            playbackRuntime_->restoreTime(previousPlaybackTime);

            if (mediaResource_ && mediaResource_->type() == MediaType::Video
                && previousVideoFrameTime >= 0.0F
                && previousVideoFrameTime != videoFrameTime_)
            {
                auto* video = dynamic_cast<VideoResource*>(mediaResource_.get());
                if (video != nullptr && video->frameAt(previousVideoFrameTime))
                {
                    const Resources::DecodedFrame& frame = video->currentFrame();
                    if (renderer_->updateFrame(
                            frame.data,
                            frame.width,
                            frame.height,
                            frame.channels,
                            frame.rowStride,
                            frame.byteSize)
                        != MediaUpdateResult::ResourceFailure)
                    {
                        videoFrameTime_ = previousVideoFrameTime;
                        videoPresentedTime_ = presentedTimeOrFallback(
                            frame,
                            previousVideoPresentedTime);
                    }
                }
            }
        }

        logSync();
        finishAudioOutputRouting();
        return;
    }

    if (mediaResource_ && mediaResource_->type() == MediaType::Video)
    {
        const float currentTime = playbackRuntime_->currentTime();
        if (currentTime != videoFrameTime_)
        {
            auto* video = dynamic_cast<VideoResource*>(mediaResource_.get());
            if (video == nullptr || !video->frameAt(currentTime))
            {
                playbackRuntime_->restoreTime(previousPlaybackTime);
                logSync();
                finishAudioOutputRouting();
                return;
            }

            const Resources::DecodedFrame& frame = video->currentFrame();
            if (!video->verifyBufferGuard("before-renderer-update"))
            {
                playbackRuntime_->restoreTime(previousPlaybackTime);
                logSync();
                finishAudioOutputRouting();
                return;
            }

            const MediaUpdateResult updateResult = renderer_->updateFrame(
                    frame.data,
                    frame.width,
                    frame.height,
                    frame.channels,
                    frame.rowStride,
                    frame.byteSize);
            if (updateResult == MediaUpdateResult::ResourceFailure)
            {
                playbackRuntime_->restoreTime(previousPlaybackTime);
                logSync();
                finishAudioOutputRouting();
                return;
            }

            if (!video->verifyBufferGuard("after-renderer-update"))
            {
                playbackRuntime_->restoreTime(previousPlaybackTime);
                logSync();
                finishAudioOutputRouting();
                return;
            }

            ++videoFrameUpdates;
            if (shouldLog(videoFrameUpdates))
            {
                std::fprintf(
                    stderr,
                    "[Engine] video-frame-update=%llu time=%.6f\n",
                    static_cast<unsigned long long>(videoFrameUpdates),
                    static_cast<double>(currentTime));
                std::fflush(stderr);
            }

            videoFrameTime_ = currentTime;
            videoPresentedTime_ = presentedTimeOrFallback(
                frame,
                static_cast<double>(currentTime));
        }
    }

    logSync();
    finishAudioOutputRouting();
}

bool Engine::seek(float sceneTimeSeconds)
{
    if (!initialized_
        || !projectBound_
        || boundProject_ == nullptr
        || !playbackRuntime_
        || !playbackRuntime_->isConfigured()
        || !sceneRuntime_
        || !sceneRuntime_->hasActiveScene()
        || !std::isfinite(sceneTimeSeconds))
    {
        return false;
    }

    const float duration = playbackRuntime_->duration();
    if (!std::isfinite(duration) || duration <= 0.0F)
    {
        return false;
    }

    float targetTime = sceneTimeSeconds < 0.0F ? 0.0F : sceneTimeSeconds;
    if (targetTime >= duration)
    {
        targetTime = std::nextafter(duration, 0.0F);
        if (targetTime < 0.0F)
        {
            targetTime = 0.0F;
        }
    }

    const Resources::DecodedFrame* targetFrame = nullptr;
    VideoResource* video = nullptr;
    if (mediaResource_ && mediaResource_->type() == MediaType::Video)
    {
        if (!renderer_)
        {
            return false;
        }

        video = dynamic_cast<VideoResource*>(mediaResource_.get());
        if (video == nullptr || !video->frameAt(targetTime))
        {
            return false;
        }

        const Resources::DecodedFrame& frame = video->currentFrame();
        if (!video->verifyBufferGuard("before-seek-renderer-update"))
        {
            return false;
        }

        targetFrame = &frame;
    }

    const bool requiresSharedOutputFlush = embeddedAudioInputHandleValue_ != 0U;
    if (requiresSharedOutputFlush)
    {
        // V1 flush policy: discard queued mixed embedded samples before a
        // seek. BGM inputs stay registered and resume from their cursors.
        stopSharedAudioOutput();
    }
    embeddedAudioNeedsResyncAfterOutputRecovery_ = false;
    removeEmbeddedAudioInput();
    if (audioResource_
        && !configureEmbeddedAudioInputAt(
            targetTime,
            playbackRuntime_->configuration().speed))
    {
        (void)reconcileAudioOutputState(requiresSharedOutputFlush);
        return false;
    }

    if (video != nullptr && targetFrame != nullptr)
    {
        const MediaUpdateResult updateResult = renderer_->updateFrame(
                targetFrame->data,
                targetFrame->width,
                targetFrame->height,
                targetFrame->channels,
                targetFrame->rowStride,
                targetFrame->byteSize);
        if (updateResult == MediaUpdateResult::ResourceFailure)
        {
            removeEmbeddedAudioInput();
            (void)reconcileAudioOutputState(requiresSharedOutputFlush);
            return false;
        }

        if (!video->verifyBufferGuard("after-seek-renderer-update"))
        {
            removeEmbeddedAudioInput();
            (void)reconcileAudioOutputState(requiresSharedOutputFlush);
            return false;
        }

        videoFrameTime_ = targetTime;
        videoPresentedTime_ = presentedTimeOrFallback(
            *targetFrame,
            static_cast<double>(targetTime));
    }

    playbackRuntime_->restoreTime(targetTime);
    pendingNaturalSceneCompletion_ = false;
    naturalSceneCompletionHalted_ = false;
    (void)reconcileAudioOutputState(requiresSharedOutputFlush);
    return true;
}

void Engine::render()
{
    const auto mediaView = currentMediaView(mediaResource_.get());
    if (!initialized_ || !renderer_
        || !renderer_->prepareFrame(mediaView ? &*mediaView : nullptr))
    {
        return;
    }

    const auto targetInfo = renderer_->presentationTargetInfo();
    const auto targetAspect = Domain::AspectRatio::from(
        targetInfo.viewportWidth,
        targetInfo.viewportHeight);
    const auto renderData = makeActiveSceneRenderData(
        projectBound_ ? boundProject_ : nullptr,
        sceneRuntime_.get(),
        currentMediaDimensions(),
        targetAspect.value_or(Domain::AspectRatio{}));
    renderer_->renderPrepared(renderData);
}

void Engine::render(const Domain::AspectRatio& canvasAspect)
{
    render(canvasAspect, std::nullopt);
}

void Engine::render(
    const Domain::AspectRatio& canvasAspect,
    std::optional<CanvasOutsideInstruction> canvasOutside)
{
    if (!initialized_ || !renderer_)
    {
        return;
    }

    auto renderData = makeActiveSceneRenderData(
        projectBound_ ? boundProject_ : nullptr,
        sceneRuntime_.get(),
        currentMediaDimensions(),
        canvasAspect);
    renderData.canvasAspect = canvasAspect.isValid()
        ? static_cast<double>(canvasAspect.numerator) / canvasAspect.denominator
        : 0.0;
    renderData.canvasOutside = canvasOutside;
    const auto mediaView = currentMediaView(mediaResource_.get());
    renderer_->render(renderData, mediaView ? &*mediaView : nullptr);
}

bool Engine::processEvents()
{
    if (!desktopWindowHost_ || !desktopWindowHost_->processMessages())
    {
        return false;
    }

    return true;
}

bool Engine::consumeKeyPress(unsigned int virtualKey)
{
    return desktopWindowHost_
        && desktopWindowHost_->consumeKeyPress(virtualKey);
}

bool Engine::consumeVolumeSlider(float& volume) noexcept
{
    return desktopWindowHost_
        && desktopWindowHost_->consumeVolumeSlider(volume);
}

void Engine::togglePause()
{
    if (naturalSceneCompletionHalted_)
    {
        return;
    }

    if (playbackRuntime_ && playbackRuntime_->isConfigured())
    {
        playbackRuntime_->togglePause();
        (void)reconcileAudioOutputState(true);
        if (desktopWindowHost_)
        {
            desktopWindowHost_->setStatusText(
                playbackRuntime_->isPaused() ? "Paused" : "Playing");
        }
        std::fprintf(
            stderr,
            "[Engine] playback-pause=%s logical=%.6f\n",
            playbackRuntime_->isPaused() ? "paused" : "resumed",
            static_cast<double>(playbackRuntime_->currentTime()));
        std::fflush(stderr);
    }
}

void Engine::setPlaybackSpeed(float speed)
{
    if (playbackRuntime_ && playbackRuntime_->isConfigured()
        && std::isfinite(speed) && speed >= 0.5F && speed <= 2.0F)
    {
        const float previousSpeed = playbackRuntime_->configuration().speed;
        const float currentTime = playbackRuntime_->currentTime();
        const bool requiresSharedOutputFlush = embeddedAudioInputHandleValue_ != 0U;
        if (requiresSharedOutputFlush)
        {
            // V1 flush policy: tempo replacement must not drain samples from
            // the previous embedded-audio configuration. BGM inputs persist.
            stopSharedAudioOutput();
        }
        embeddedAudioNeedsResyncAfterOutputRecovery_ = false;
        removeEmbeddedAudioInput();
        if (audioResource_
            && (!setEmbeddedAudioTempo(speed)
                || !configureEmbeddedAudioInputAt(
                    currentTime,
                    speed)))
        {
            removeEmbeddedAudioInput();
            const bool rollbackSucceeded = setEmbeddedAudioTempo(previousSpeed)
                && configureEmbeddedAudioInputAt(
                    currentTime,
                    previousSpeed);
            if (!rollbackSucceeded)
            {
                removeEmbeddedAudioInput();
            }

            std::fprintf(
                stderr,
                "[Engine] unsupported audio playback speed %.6f; keeping %.6f\n",
                static_cast<double>(speed),
                static_cast<double>(previousSpeed));
            std::fflush(stderr);
            (void)reconcileAudioOutputState(requiresSharedOutputFlush);
            return;
        }

        playbackRuntime_->setSpeed(speed);
        std::fprintf(
            stderr,
            "[Engine] playback-speed=%.6f logical=%.6f\n",
            static_cast<double>(playbackRuntime_->configuration().speed),
            static_cast<double>(playbackRuntime_->currentTime()));
        std::fflush(stderr);
        (void)reconcileAudioOutputState(requiresSharedOutputFlush);
    }
}

void Engine::setVolume(float volume) noexcept
{
    embeddedAudioGain_ = std::isfinite(volume)
        ? std::clamp(volume, 0.0F, 1.0F)
        : 1.0F;
    if (audioMixer_ && embeddedAudioInputHandleValue_ != 0U
        && !audioMixer_->setInputGain(
            AudioInputHandle{embeddedAudioInputHandleValue_},
            embeddedAudioGain_))
    {
        embeddedAudioInputHandleValue_ = 0U;
        embeddedAudioNeedsResyncAfterOutputRecovery_ = false;
        (void)reconcileAudioOutputState(false);
    }
    if (audioOutput_)
    {
        audioOutput_->setVolume(1.0F);
    }
    if (desktopWindowHost_)
    {
        desktopWindowHost_->setVolumeControl(this->volume());
    }
}

float Engine::volume() const noexcept
{
    return embeddedAudioGain_;
}

void Engine::toggleMute() noexcept
{
    if (audioOutput_)
    {
        audioOutput_->toggleMute();
    }
    if (desktopWindowHost_)
    {
        desktopWindowHost_->setMuteControl(isMuted());
    }
}

bool Engine::isMuted() const noexcept
{
    return audioOutput_ && audioOutput_->isMuted();
}

bool Engine::processBackgroundMusicControl(
    const BackgroundMusicControl::Event& event
)
{
    const bool playbackStopped = std::holds_alternative<
        BackgroundMusicControl::PlaybackStopped>(event);
    const Music::PlaybackActivity activity = isPaused()
        ? Music::PlaybackActivity::Paused
        : Music::PlaybackActivity::Playing;
    const std::optional<Music::MusicRuntimeEvent> internalEvent = toInternalMusicEvent(
        event,
        activity);
    if (!internalEvent.has_value())
    {
        return false;
    }

    const std::optional<Music::MusicProcessResult> result = processBackgroundMusicEventInternal(
        backgroundMusicOrchestrator_.get(),
        *internalEvent);
    if (!result.has_value())
    {
        return false;
    }

    if (*result == Music::MusicProcessResult::Completed)
    {
        if (playbackStopped)
        {
            stopSharedAudioOutput();
            removeEmbeddedAudioInput();
            embeddedAudioNeedsResyncAfterOutputRecovery_ = false;
            audioOutputRetryElapsedSeconds_ = 0.0;
        }
        (void)reconcileAudioOutputState(true);
    }

    return *result == Music::MusicProcessResult::Completed
        || *result == Music::MusicProcessResult::Enqueued;
}

std::optional<BackgroundMusicControl::TrackId>
Engine::currentPlaylistBackgroundMusicTrackId() const
{
    const Music::MusicSession* const session = backgroundMusicRuntime_
        ? backgroundMusicRuntime_->playlistSession()
        : nullptr;
    if (session == nullptr || !session->currentTrackId().has_value())
    {
        return std::nullopt;
    }

    return BackgroundMusicControl::TrackId{session->currentTrackId()->value};
}

std::optional<BackgroundMusicControl::TrackId>
Engine::currentItemBackgroundMusicTrackId() const
{
    const Music::MusicSession* const session = backgroundMusicRuntime_
        ? backgroundMusicRuntime_->itemSession()
        : nullptr;
    if (session == nullptr || !session->currentTrackId().has_value())
    {
        return std::nullopt;
    }

    return BackgroundMusicControl::TrackId{session->currentTrackId()->value};
}

bool Engine::isPaused() const noexcept
{
    return playbackRuntime_ != nullptr && playbackRuntime_->isPaused();
}

float Engine::playbackTime() const noexcept
{
    return playbackRuntime_ ? playbackRuntime_->currentTime() : 0.0F;
}

float Engine::effectiveDuration() const noexcept
{
    return playbackRuntime_ ? playbackRuntime_->duration() : 0.0F;
}

float Engine::playbackSpeed() const noexcept
{
    return playbackRuntime_ ? playbackRuntime_->configuration().speed : 1.0F;
}

std::size_t Engine::activeSceneIndex() const noexcept
{
    if (!projectBound_ || boundProject_ == nullptr || !sceneRuntime_
        || !sceneRuntime_->hasActiveScene())
    {
        return (std::numeric_limits<std::size_t>::max)();
    }

    for (std::size_t index = 0; index < boundProject_->scenes.size(); ++index)
    {
        if (boundProject_->scenes[index].id == sceneRuntime_->activeSceneId())
        {
            return index;
        }
    }

    return (std::numeric_limits<std::size_t>::max)();
}

std::size_t Engine::sceneCount() const noexcept
{
    return projectBound_ && boundProject_ != nullptr
        ? boundProject_->scenes.size() : 0U;
}

Engine::MediaKind Engine::mediaKind() const noexcept
{
    if (!mediaResource_)
    {
        return MediaKind::Unknown;
    }

    if (mediaResource_->type() == MediaType::Image)
    {
        return MediaKind::Image;
    }
    if (mediaResource_->type() == MediaType::Video)
    {
        return MediaKind::Video;
    }
    return MediaKind::Unknown;
}

std::optional<Engine::MediaDimensions> Engine::currentMediaDimensions() const noexcept
{
    if (!mediaResource_)
    {
        return std::nullopt;
    }

    int width = 0;
    int height = 0;
    if (mediaResource_->type() == MediaType::Image)
    {
        const auto* image = dynamic_cast<const ImageResource*>(mediaResource_.get());
        if (image == nullptr)
        {
            return std::nullopt;
        }

        width = image->width();
        height = image->height();
    }
    else if (mediaResource_->type() == MediaType::Video)
    {
        const auto* video = dynamic_cast<const VideoResource*>(mediaResource_.get());
        if (video == nullptr)
        {
            return std::nullopt;
        }

        const Resources::DecodedFrame& frame = video->currentFrame();
        width = frame.width;
        height = frame.height;
    }

    if (width <= 0 || height <= 0)
    {
        return std::nullopt;
    }

    return MediaDimensions{width, height};
}

void Engine::setStatusText(const std::string& text) noexcept
{
    if (desktopWindowHost_)
    {
        desktopWindowHost_->setStatusText(text);
    }
}

bool Engine::switchToNextScene()
{
    if (!initialized_ || !projectBound_ || boundProject_ == nullptr
        || !sceneRuntime_ || !sceneRuntime_->hasActiveScene())
    {
        return false;
    }

    std::size_t activeSceneIndex = boundProject_->scenes.size();
    for (std::size_t index = 0; index < boundProject_->scenes.size(); ++index)
    {
        if (boundProject_->scenes[index].id == sceneRuntime_->activeSceneId())
        {
            activeSceneIndex = index;
            break;
        }
    }

    if (activeSceneIndex >= boundProject_->scenes.size())
    {
        return false;
    }

    const std::size_t nextIndex = (activeSceneIndex + 1)
        % boundProject_->scenes.size();
    return switchScene(*boundProject_, boundProject_->scenes[nextIndex].id);
}

bool Engine::switchToPreviousScene()
{
    if (!initialized_ || !projectBound_ || boundProject_ == nullptr
        || !sceneRuntime_ || !sceneRuntime_->hasActiveScene()
        || boundProject_->scenes.empty())
    {
        return false;
    }

    std::size_t activeSceneIndex = boundProject_->scenes.size();
    for (std::size_t index = 0; index < boundProject_->scenes.size(); ++index)
    {
        if (boundProject_->scenes[index].id == sceneRuntime_->activeSceneId())
        {
            activeSceneIndex = index;
            break;
        }
    }

    if (activeSceneIndex >= boundProject_->scenes.size())
    {
        return false;
    }

    const std::size_t previousIndex = activeSceneIndex == 0U
        ? boundProject_->scenes.size() - 1U
        : activeSceneIndex - 1U;
    return switchScene(*boundProject_, boundProject_->scenes[previousIndex].id);
}

void Engine::shutdown()
{
    detachProject();
    playbackRuntime_.reset();

    if (defaultAudioEndpointWatcher_)
    {
        defaultAudioEndpointWatcher_->shutdown();
    }
    defaultAudioEndpointWatcherAvailable_ = false;
    lastSeenDefaultAudioEndpointGeneration_ = 0U;
    defaultAudioEndpointRerouteRequested_ = false;

    if (audioOutput_)
    {
        audioOutput_->shutdown();
    }
    audioOutputOperationalState_ = Core::AudioOutputOperationalState::Healthy;
    audioOutputRetryElapsedSeconds_ = 0.0;
    embeddedAudioNeedsResyncAfterOutputRecovery_ = false;

    if (renderer_)
    {
        renderer_->shutdown();
    }
    defaultPresentationTargetId_ = {};

    if (desktopWindowHost_)
    {
        desktopWindowHost_->shutdown();
        desktopWindowHost_.reset();
    }

    if (!initialized_)
    {
        return;
    }

    initialized_ = false;
}

}
