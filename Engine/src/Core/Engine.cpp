#include <DesktopSceneEngine/Engine.h>
#include "EngineAudioControl.h"

#include "../Music/BackgroundMusicOrchestrator.h"
#include "../Music/BackgroundMusicRuntime.h"
#include "../Music/MusicRuntimeTypes.h"
#include "ScenePresentationMath.h"
#include "../Rendering/RenderData.h"
#include "../Rendering/Renderer.h"
#include "../Resources/MediaResource.h"
#include "../Resources/DetachedMediaProducer.h"
#include "../Resources/ImageResource.h"
#include "../Resources/VideoResource.h"
#include "../Runtime/Playback/PlaybackRuntime.h"
#include "../Runtime/Scene/SceneRuntime.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
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
#include <thread>

namespace
{

template<class F> struct ScopeExit { F action; ~ScopeExit() { action(); } };
std::uint64_t sceneSwitches = 0;
std::uint64_t videoFrameUpdates = 0;

bool shouldLog(std::uint64_t count) noexcept
{
    return count == 1 || count % 25 == 0;
}

using DesktopSceneEngine::Domain::Media;
namespace BackgroundMusicControl = DesktopSceneEngine::BackgroundMusicControl;
namespace Music = DesktopSceneEngine::Music;
using DesktopSceneEngine::Rendering::Detail::MediaUpdateResult;
using DesktopSceneEngine::Rendering::Renderer;
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
    const DesktopSceneEngine::Audio::AudioExecutionSnapshot& audio,
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
        "[Engine] playback-sync sample=%llu logical=%.6f video=%.6f video-valid=%s video-drift=%.6f audio-submitted=%llu audio-queued=%llu audio-written-minus-current-queue=%llu audio-underflows=%llu\n",
        static_cast<unsigned long long>(sampleCount),
        logicalTime,
        videoPresentedTime,
        videoValid ? "yes" : "no",
        videoDrift,
        static_cast<unsigned long long>(audio.writtenFrames),
        static_cast<unsigned long long>(audio.queuedFrames),
        static_cast<unsigned long long>(audio.writtenFrames >= audio.queuedFrames ? audio.writtenFrames - audio.queuedFrames : 0U),
        static_cast<unsigned long long>(audio.outputUnderflows));
    std::fprintf(stderr,
        "[Engine] audio-execution qpc-ns=%lld publication=%llu publication-ns=%lld effect-sequence=%llu clock-binding=%llu clock-version=%llu clock-position=%.9f clock-time-ns=%lld clock-mode=%u clock-duration=%.9f clock-rate=%.9f mmcss=%u health=%u transport=%u owner-thread=%u buffer=%llu prime=%llu refill=%llu written=%llu queued=%llu underflows=%llu missing=%llu underruns=%llu device-failures=%llu recovery-attempts=%llu recovered=%llu nonzero-submitted=%llu max-zero-run=%llu max-write-gap-ns=%lld max-wake-gap-ns=%lld max-service-ns=%lld embedded-token=%llu embedded-cursor=%llu embedded-tempo=%.9f playlist-token=%llu playlist-cursor=%llu item-token=%llu item-cursor=%llu\n",
        static_cast<long long>(DesktopSceneEngine::Audio::audioMonotonicNow()),
        static_cast<unsigned long long>(audio.publication), static_cast<long long>(audio.publicationTimeNs), static_cast<unsigned long long>(audio.lastEffectSequence),
        static_cast<unsigned long long>(audio.clock.binding), static_cast<unsigned long long>(audio.clock.timingVersion),
        audio.clock.position, static_cast<long long>(audio.clock.time), static_cast<unsigned>(audio.clock.mode), audio.clock.duration, audio.clock.rate, static_cast<unsigned>(audio.mmcssRegistered),
        static_cast<unsigned>(audio.health),
        static_cast<unsigned>(audio.transport), static_cast<unsigned>(audio.ownerThreadId), static_cast<unsigned long long>(audio.bufferFrames),
        static_cast<unsigned long long>(audio.primeFrames), static_cast<unsigned long long>(audio.refillCount),
        static_cast<unsigned long long>(audio.writtenFrames), static_cast<unsigned long long>(audio.queuedFrames),
        static_cast<unsigned long long>(audio.outputUnderflows), static_cast<unsigned long long>(audio.sourceMissingFrames),
        static_cast<unsigned long long>(audio.sourceUnderruns),
        static_cast<unsigned long long>(audio.deviceFailures), static_cast<unsigned long long>(audio.recoveryAttempts),
        static_cast<unsigned long long>(audio.recoveredCount),
        static_cast<unsigned long long>(audio.nonzeroSubmittedFrames), static_cast<unsigned long long>(audio.maximumConsecutiveZeroSubmittedFrames),
        static_cast<long long>(audio.maximumWriteGap),
        static_cast<long long>(audio.maximumWakeGap), static_cast<long long>(audio.maximumServiceDuration),
        static_cast<unsigned long long>(audio.laneSources[0].sourceToken),
        static_cast<unsigned long long>(audio.laneCursorFrames[0]), audio.embeddedTempo,
        static_cast<unsigned long long>(audio.laneSources[1].sourceToken),
        static_cast<unsigned long long>(audio.laneCursorFrames[1]),
        static_cast<unsigned long long>(audio.laneSources[2].sourceToken),
        static_cast<unsigned long long>(audio.laneCursorFrames[2]));
    std::fflush(stderr);
}

std::filesystem::path pathFromUtf8(const std::string& value)
{
    const auto* characters = reinterpret_cast<const char8_t*>(value.data());
    return std::filesystem::path{
        std::u8string{characters, characters + value.size()}
    };
}

const DesktopSceneEngine::Domain::Scene* findScene(
    const DesktopSceneEngine::Domain::Project& project, const DesktopSceneEngine::Domain::EntityId& id)
{
    const auto found = std::find_if(project.scenes.begin(), project.scenes.end(),
        [&](const auto& candidate) { return candidate.id == id; });
    return found != project.scenes.end() ? &*found : nullptr;
}

const Media* findSceneMedia(
    const DesktopSceneEngine::Domain::Project& project, const DesktopSceneEngine::Domain::Scene& scene)
{
    const auto found = std::find_if(project.library.media.begin(), project.library.media.end(),
        [&](const auto& candidate) { return candidate.id == scene.mediaId; });
    return found != project.library.media.end() ? &*found : nullptr;
}

float clampedSeek(float seconds, float duration) noexcept
{
    if (seconds <= 0) return 0;
    return (std::min)(seconds, std::nextafter(duration, 0.0F));
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

struct Engine::MediaPresentationCheckpoint
{
    MediaPreparationToken ticket = 0;
    bool seek = false;
    std::unique_ptr<const Domain::Project> project;
    Domain::EntityId scene;
    Domain::Playback playback;
    std::filesystem::path source;
    std::shared_ptr<const PresentationFrame> frame;
    Audio::AudioClockAnchor clock;
    MediaPreparationToken binding = 0;
    MediaKind kind = MediaKind::Unknown;
    float duration = 0;
    float frameTime = -1;
    double presentedTime = -1;
    bool paused = false;
    bool videoAtEnd = false;
    bool completionPending = false;
    bool completionHalted = false;
};

Engine::Engine() : Engine(PresentationMode::Inline) {}

Engine::Engine(PresentationMode presentationMode)
    : audioControl_(std::make_unique<AudioControl>()),
      sceneRuntime_(std::make_unique<Runtime::SceneRuntime>()),
      presentation_(presentationMode == PresentationMode::Inline
          ? std::make_unique<EnginePresentation>() : nullptr)
{
    backgroundMusicRuntime_ = std::make_unique<Music::BackgroundMusicRuntime>();
    backgroundMusicOrchestrator_ = std::make_unique<Music::BackgroundMusicOrchestrator>(
        *backgroundMusicRuntime_,
        [this](const Music::MusicRuntimeAction& action) { return audioControl_->music(action); });
}

Engine::~Engine()
{
    shutdown();
}

void Engine::initialize()
{
    if (initialized_ || !presentation_) return;
    presentation_->initialize();
    if (presentation_->isInitialized() && !initializeRuntime()) presentation_->shutdown();
}

bool Engine::initializeRuntimeOnly()
{
    return !presentation_ && (initialized_ || initializeRuntime());
}

bool Engine::setControlWakeEvent(void* borrowedEvent) noexcept
{
    if (!borrowedEvent || controlWakeEvent_ || initialized_
        || audioControl_->started || audioControl_->closing)
        return false;
    if (!audioControl_->execution.setControlWakeEvent(borrowedEvent)
        || !audioControl_->preparation.setControlWakeEvent(borrowedEvent))
        return false;
    controlWakeEvent_ = borrowedEvent;
    return true;
}

PresentationTargetId Engine::initialize(const ExternalWindowTarget& target)
{
    if (initialized_ || !presentation_) return {};
    const auto id = presentation_->initialize(target);
    if (id.isValid() && !initializeRuntime())
    {
        presentation_->shutdown();
        return {};
    }
    return id;
}

PresentationTargetId Engine::initialize(const HostedCompositionTarget& target)
{
    if (initialized_ || !presentation_) return {};
    const auto id = presentation_->initialize(target);
    if (id.isValid() && !initializeRuntime())
    {
        presentation_->shutdown();
        return {};
    }
    return id;
}

Renderer& Engine::renderer() noexcept
{
    assert(presentation_);
    return *presentation_->renderer_;
}

bool Engine::initializeRuntime()
{
    if (audioControl_->closing && audioControl_->stopped())
    {
        // Only a completed execution lifetime may create a new incarnation.
        // Scene/Stop/device transitions never clear ContractFaulted.
        audioControl_ = std::make_unique<AudioControl>();
        if (controlWakeEvent_ && (!audioControl_->execution.setControlWakeEvent(controlWakeEvent_)
            || !audioControl_->preparation.setControlWakeEvent(controlWakeEvent_)))
            return false;
        backgroundMusicOrchestrator_.reset();
        backgroundMusicRuntime_ = std::make_unique<Music::BackgroundMusicRuntime>();
        backgroundMusicOrchestrator_ = std::make_unique<Music::BackgroundMusicOrchestrator>(
            *backgroundMusicRuntime_, [this](const Music::MusicRuntimeAction& action) { return audioControl_->music(action); });
    }
    playbackRuntime_ = std::make_unique<Runtime::PlaybackRuntime>();
    // Only A opens WASAPI / installs endpoint notifications. C never services it.
    (void)audioControl_->start();
    initialized_ = true;
    std::cout << "Desktop Scene Engine initialized." << std::endl;
    return true;
}

PresentationTargetId Engine::attachPresentationTarget(
    const ExternalWindowTarget& target)
{
    return presentation_ ? presentation_->attachPresentationTarget(target) : PresentationTargetId{};
}

PresentationTargetId Engine::attachPresentationTarget(
    const HostedCompositionTarget& target)
{
    return presentation_ ? presentation_->attachPresentationTarget(target) : PresentationTargetId{};
}

bool Engine::setHostedPresentationExtent(
    PresentationTargetId id, PresentationExtent extent) noexcept
{
    return presentation_ && presentation_->setHostedPresentationExtent(id, extent);
}

CompositionContentLease Engine::acquireHostedCompositionContent(
    PresentationTargetId id) const noexcept
{
    return presentation_ ? presentation_->acquireHostedCompositionContent(id) : CompositionContentLease{};
}

bool Engine::detachPresentationTarget(PresentationTargetId id) noexcept
{
    return presentation_ && presentation_->detachPresentationTarget(id);
}

bool Engine::setPresentationPacingTarget(PresentationTargetId id) noexcept
{
    return presentation_ && presentation_->setPresentationPacingTarget(id);
}

bool Engine::isInitialized() const noexcept
{
    return initialized_;
}

PresentationInput Engine::presentationInput(const Domain::AspectRatio& canvasAspect) const
{
    const auto dimensions = detachedFrame_
        ? std::optional{MediaDimensions{detachedFrame_->width, detachedFrame_->height}}
        : std::nullopt;
    const auto data = makeActiveSceneRenderData(
        projectBound_ ? boundProject_ : nullptr, sceneRuntime_.get(),
        dimensions, canvasAspect);
    return {detachedFrame_, data.presentation, canvasAspect.isValid()
        ? static_cast<double>(canvasAspect.numerator) / canvasAspect.denominator : 0.0};
}

bool Engine::stageMedia(const Rendering::Detail::MediaView& media, PresentationFrameKind kind)
{
    if (presentation_)
    {
        const auto result = kind == PresentationFrameKind::Image
            ? renderer().updateImage(media.data, media.width, media.height, media.channels,
                media.rowStride, media.byteSize)
            : renderer().updateFrame(media.data, media.width, media.height, media.channels,
                media.rowStride, media.byteSize);
        return result != MediaUpdateResult::ResourceFailure;
    }

    if (!media.data || !Rendering::Detail::hasValidRgbaBuffer(
        media.width, media.height, media.channels, media.rowStride, media.byteSize))
        return false;
    const auto height = static_cast<std::size_t>(media.height);
    const auto size = media.rowStride * height;
    if (size > std::vector<std::byte>{}.max_size()) return false;

    try
    {
        auto frame = std::make_shared<PresentationFrame>();
        frame->kind = kind;
        frame->width = media.width;
        frame->height = media.height;
        frame->rowStride = media.rowStride;
        frame->pixels.resize(size);
        std::memcpy(frame->pixels.data(), media.data, size);
        detachedFrame_ = std::move(frame);
        return true;
    }
    catch (const std::bad_alloc&)
    {
        return false;
    }
}

bool Engine::prepareInitialMedia(MediaResource& media)
{
    if (media.type() == MediaType::Image)
    {
        const auto view = currentMediaView(&media);
        return view && stageMedia(*view, PresentationFrameKind::Image);
    }

    auto* video = dynamic_cast<VideoResource*>(&media);
    if (!video || !video->frameAt(0.0F) || !video->verifyBufferGuard("before-renderer-update"))
        return false;
    const auto view = currentMediaView(&media);
    const bool staged = view && stageMedia(*view, PresentationFrameKind::Video);
    return video->verifyBufferGuard("after-renderer-update") && staged;
}

void Engine::clearPresentationMedia() noexcept
{
    if (presentation_) renderer().clearImage();
    else detachedFrame_.reset();
}

bool Engine::bindProject(
    const Domain::Project& project,
    const Domain::EntityId& activeSceneId
)
{
    if (mediaProducer_) return false;
    pollAudioExecution();
    const bool needsAdmission = !audioControl_->capture;
    const auto implicit = needsAdmission ? beginAudioEffect(AudioEffectScope::Structural) : std::nullopt;
    if (needsAdmission && !implicit) return false;
    bool logicalAccepted = false;
    const auto operationExit = ScopeExit{[&] { if (implicit) (void)finishAudioEffect(*implicit, logicalAccepted); }};

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
            mediaResource = Resources::openPlaybackResource(pathFromUtf8(media.resourceReference));
            activeMedia = &media;
            if (mediaResource == nullptr)
            {
                return false;
            }
            break;
        }
    }

    float effectiveDuration = 0.0F;
    if (mediaResource)
    {
        if (!effectiveSceneDuration(*activeScene, *mediaResource, effectiveDuration)
            || !prepareInitialMedia(*mediaResource))
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
        clearPresentationMedia();
    }
    audioControl_->retire(Audio::AudioLane::Embedded);
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

    audioControl_->configureClock(*this, 0.0, false);
    const auto* audioMedia = activeMedia;
    if (audioMedia && mediaResource_ && mediaResource_->type() == MediaType::Video)
        audioControl_->prepareEmbedded(*this, pathFromUtf8(audioMedia->resourceReference),
            playback.speed, AudioControl::SourcePurpose::EmbeddedBind);
    logicalAccepted = true;
    return true;
}

bool Engine::switchScene(
    const Domain::Project& project,
    const Domain::EntityId& sceneId
)
{
    if (mediaProducer_) return false;
    pollAudioExecution();
    const bool needsAdmission = !audioControl_->capture;
    const auto implicit = needsAdmission ? beginAudioEffect(AudioEffectScope::Structural) : std::nullopt;
    if (needsAdmission && !implicit) return false;
    bool logicalAccepted = false;
    const auto operationExit = ScopeExit{[&] { if (implicit) (void)finishAudioEffect(*implicit, logicalAccepted); }};

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
            mediaResource = Resources::openPlaybackResource(pathFromUtf8(media.resourceReference));
            requestedMedia = &media;
            if (mediaResource == nullptr)
            {
                return false;
            }
            break;
        }
    }

    float effectiveDuration = 0.0F;
    if (mediaResource)
    {
        if (!effectiveSceneDuration(
                *requestedScene,
                *mediaResource,
                effectiveDuration)
            || !prepareInitialMedia(*mediaResource))
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
        clearPresentationMedia();
    }
    audioControl_->retire(Audio::AudioLane::Embedded);
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

    audioControl_->configureClock(*this, 0.0, false);
    const auto* audioMedia = requestedMedia;
    if (audioMedia && mediaResource_ && mediaResource_->type() == MediaType::Video)
        audioControl_->prepareEmbedded(*this, pathFromUtf8(audioMedia->resourceReference),
            playback.speed, AudioControl::SourcePurpose::EmbeddedBind);
    logicalAccepted = true;
    return true;
}

void Engine::detachProject()
{
    if (pendingMediaPreparation_) cancelMediaPreparation(pendingMediaPreparation_->ticket);
    if (mediaProducer_) mediaProducer_->clearBinding();
    detachedMediaBinding_ = 0;
    detachedMediaKind_ = MediaKind::Unknown;
    detachedVideoAtEnd_ = false;
    sceneRuntime_->clearActiveScene();

    if (playbackRuntime_)
    {
        playbackRuntime_->reset();
    }

    clearPresentationMedia();

    // Detaching a visual projection is also used for an Item rebind. BGM's
    // Session/token lifetime is independent and is retired only by the explicit
    // PlaybackStopped control (or execution shutdown), never by this binding.
    audioControl_->retire(Audio::AudioLane::Embedded);
    audioControl_->clock = {};
    audioControl_->plannedClock = {};
    audioControl_->pendingBinding.reset();
    audioControl_->embeddedPath.clear();
    mediaResource_.reset();
    videoFrameTime_ = -1.0F;
    videoPresentedTime_ = -1.0;
    syncDiagnosticUpdates_ = 0U;
    pendingNaturalSceneCompletion_ = false;
    naturalSceneCompletionHalted_ = false;

    boundProject_ = nullptr;
    projectBound_ = false;
    detachedProject_.reset();
    mediaPresentation_.reset();
}

bool Engine::enableAsyncMediaPreparation()
{
    if (!initialized_ || presentation_ || projectBound_
        || sceneCompletionPolicy_ != SceneCompletionPolicy::ExternalSceneCompletion)
        return false;
    if (mediaProducer_) return true;
    try
    {
        mediaProducer_ = std::make_unique<Resources::DetachedMediaProducer>(controlWakeEvent_);
        mediaShuttingDown_ = false;
        return true;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

std::optional<Engine::MediaPreparationToken> Engine::prepareSceneMedia(
    const Domain::Project& project, const Domain::EntityId& id, float initialTime)
{
    if (!mediaProducer_ || mediaShuttingDown_ || pendingMediaPreparation_ || !std::isfinite(initialTime)
        || nextMediaTicket_ == (std::numeric_limits<MediaPreparationToken>::max)())
        return std::nullopt;
    const auto* scene = findScene(project, id);
    if (!scene) return std::nullopt;
    const auto* media = findSceneMedia(project, *scene);
    if (!media && (!std::isfinite(scene->duration) || scene->duration < 0))
        return std::nullopt;
    try
    {
        PendingMediaPreparation pending{++nextMediaTicket_, id, std::nullopt, scene->duration};
        std::optional<std::filesystem::path> path;
        if (media)
        {
            pending.source = media->resourceReference;
            path = pathFromUtf8(*pending.source);
        }
        if (!mediaProducer_->prepareScene(pending.ticket, std::move(path), scene->duration, initialTime))
            return std::nullopt;
        pendingMediaPreparation_ = std::move(pending);
        return pendingMediaPreparation_->ticket;
    }
    catch (const std::exception&)
    {
        return std::nullopt;
    }
}

std::optional<Engine::MediaPreparationToken> Engine::prepareSeek(float seconds)
{
    const auto duration = effectiveDuration();
    if (!mediaProducer_ || mediaShuttingDown_ || pendingMediaPreparation_
        || !projectBound_ || !std::isfinite(seconds) || !std::isfinite(duration) || duration <= 0
        || nextMediaTicket_ == (std::numeric_limits<MediaPreparationToken>::max)())
        return std::nullopt;
    try
    {
        PendingMediaPreparation pending{++nextMediaTicket_, sceneRuntime_->activeSceneId(),
            std::nullopt, duration, clampedSeek(seconds, duration), true, detachedMediaKind_ != MediaKind::Video};
        if (!pending.seekWithoutVideo
            && !mediaProducer_->prepareSeek(pending.ticket, detachedMediaBinding_, pending.position, duration))
            return std::nullopt;
        pendingMediaPreparation_ = std::move(pending);
        return pendingMediaPreparation_->ticket;
    }
    catch (const std::exception&)
    {
        return std::nullopt;
    }
}

Engine::MediaPreparationStatus Engine::preparedMediaStatus(MediaPreparationToken ticket) const noexcept
{
    if (!pendingMediaPreparation_ || pendingMediaPreparation_->ticket != ticket || !mediaProducer_)
        return MediaPreparationStatus::Expired;
    if (pendingMediaPreparation_->seekWithoutVideo) return MediaPreparationStatus::Ready;
    switch (mediaProducer_->preparedState(ticket))
    {
    case Resources::DetachedMediaProducer::State::Pending: return MediaPreparationStatus::Pending;
    case Resources::DetachedMediaProducer::State::Ready: return MediaPreparationStatus::Ready;
    case Resources::DetachedMediaProducer::State::Failed: return MediaPreparationStatus::Failed;
    case Resources::DetachedMediaProducer::State::Expired: return MediaPreparationStatus::Expired;
    }
    return MediaPreparationStatus::Expired;
}

std::unique_ptr<Engine::MediaPresentationCheckpoint> Engine::captureMediaPresentation(
    MediaPreparationToken ticket, bool seek) const
{
    auto checkpoint = std::make_unique<MediaPresentationCheckpoint>();
    checkpoint->ticket = ticket;
    checkpoint->seek = seek;
    if (projectBound_)
    {
        checkpoint->scene = sceneRuntime_->activeSceneId();
        checkpoint->playback = playbackRuntime_->configuration();
        checkpoint->duration = playbackRuntime_->duration();
        checkpoint->paused = playbackRuntime_->isPaused();
        if (!seek && detachedMediaKind_ == MediaKind::Video)
        {
            const auto* scene = findScene(*boundProject_, checkpoint->scene);
            if (const auto* media = scene ? findSceneMedia(*boundProject_, *scene) : nullptr)
                checkpoint->source = pathFromUtf8(media->resourceReference);
        }
    }
    checkpoint->frame = detachedFrame_;
    checkpoint->binding = detachedMediaBinding_;
    checkpoint->kind = detachedMediaKind_;
    checkpoint->frameTime = videoFrameTime_;
    checkpoint->presentedTime = videoPresentedTime_;
    checkpoint->videoAtEnd = detachedVideoAtEnd_;
    checkpoint->completionPending = pendingNaturalSceneCompletion_;
    checkpoint->completionHalted = naturalSceneCompletionHalted_;
    checkpoint->clock = audioControl_->clock.binding == audioControl_->binding
        ? audioControl_->clock : audioControl_->plannedClock;
    return checkpoint;
}

bool Engine::commitPreparedScene(const Domain::Project& project, const Domain::EntityId& id,
    MediaPreparationToken ticket, bool paused, std::optional<float> playbackRate)
{
    if (!pendingMediaPreparation_ || pendingMediaPreparation_->seek || pendingMediaPreparation_->scene != id
        || preparedMediaStatus(ticket) != MediaPreparationStatus::Ready)
        return false;
    const auto* scene = findScene(project, id);
    const auto* media = scene ? findSceneMedia(project, *scene) : nullptr;
    const auto& pending = *pendingMediaPreparation_;
    if (!scene || bool(media) != pending.source.has_value() || (media && media->resourceReference != *pending.source)
        || (scene->duration != pending.declaredDuration
            && !(std::isnan(scene->duration) && std::isnan(pending.declaredDuration))))
        return false;
    if (playbackRate && (!std::isfinite(*playbackRate) || *playbackRate < 0.5F || *playbackRate > 2.0F))
        return false;
    auto playback = scene->playback;
    if (playbackRate) playback.speed = *playbackRate;
    std::filesystem::path mediaPath;
    pollAudioExecution();
    std::unique_ptr<const Domain::Project> detachedProject;
    std::unique_ptr<MediaPresentationCheckpoint> checkpoint;
    try
    {
        if (media) mediaPath = pathFromUtf8(media->resourceReference);
        detachedProject = std::make_unique<const Domain::Project>(project);
        checkpoint = captureMediaPresentation(ticket, false);
    }
    catch (const std::exception&)
    {
        return false;
    }
    const auto implicit = audioControl_->capture ? std::nullopt : beginAudioEffect(AudioEffectScope::Structural);
    if (!audioControl_->capture) return false;
    bool logicalAccepted = false;
    const auto exit = ScopeExit{[&] { if (implicit) (void)finishAudioEffect(*implicit, logicalAccepted); }};
    auto prepared = mediaProducer_->commit(ticket, true);
    if (!prepared) return false;
    checkpoint->project = std::move(detachedProject_);
    detachedProject_ = std::move(detachedProject);
    mediaPresentation_ = std::move(checkpoint);

    const bool video = prepared->type == MediaType::Video;
    const auto mediaKind = video ? MediaKind::Video
        : prepared->type == MediaType::Image ? MediaKind::Image : MediaKind::Unknown;
    audioControl_->retire(Audio::AudioLane::Embedded);
    sceneRuntime_->setActiveScene(id);
    playbackRuntime_->configure(playback, prepared->duration);
    playbackRuntime_->restoreTime(prepared->position);
    if (paused) playbackRuntime_->pause();
    detachedFrame_ = std::move(prepared->frame);
    detachedMediaBinding_ = ticket;
    detachedMediaKind_ = mediaKind;
    detachedVideoAtEnd_ = false;
    pendingMediaPreparation_.reset();
    videoFrameTime_ = video ? prepared->position : -1.0F;
    videoPresentedTime_ = prepared->timestamp;
    syncDiagnosticUpdates_ = 0U;
    boundProject_ = detachedProject_.get();
    projectBound_ = true;
    pendingNaturalSceneCompletion_ = false;
    naturalSceneCompletionHalted_ = false;

    audioControl_->configureClock(*this, prepared->position, paused);
    if (video)
        audioControl_->prepareEmbedded(*this, mediaPath, playback.speed, AudioControl::SourcePurpose::EmbeddedBind);
    logicalAccepted = true;
    return true;
}

bool Engine::commitPreparedSeek(MediaPreparationToken ticket)
{
    if (!pendingMediaPreparation_ || !pendingMediaPreparation_->seek
        || preparedMediaStatus(ticket) != MediaPreparationStatus::Ready || !projectBound_
        || pendingMediaPreparation_->scene != sceneRuntime_->activeSceneId())
        return false;

    pollAudioExecution();
    std::unique_ptr<MediaPresentationCheckpoint> checkpoint;
    // Image seek does not submit a new upload. Keep any unresolved binding
    // receipt: an independent clock move is not a frame replacement.
    if (!pendingMediaPreparation_->seekWithoutVideo)
    {
        try { checkpoint = captureMediaPresentation(ticket, true); }
        catch (const std::exception&) { return false; }
    }
    const auto implicit = audioControl_->capture ? std::nullopt : beginAudioEffect(AudioEffectScope::Structural);
    if (!audioControl_->capture) return false;
    bool logicalAccepted = false;
    const auto exit = ScopeExit{[&] { if (implicit) (void)finishAudioEffect(*implicit, logicalAccepted); }};
    const auto position = pendingMediaPreparation_->position;
    if (!pendingMediaPreparation_->seekWithoutVideo)
    {
        auto prepared = mediaProducer_->commit(ticket, false);
        if (!prepared) return false;
        mediaProducer_->releasePreviousBinding(detachedMediaBinding_);
        mediaPresentation_ = std::move(checkpoint);
        detachedFrame_ = std::move(prepared->frame);
        detachedVideoAtEnd_ = false;
        videoFrameTime_ = position;
        videoPresentedTime_ = prepared->timestamp;
    }
    pendingMediaPreparation_.reset();
    audioControl_->requestSeek(*this, position);
    pendingNaturalSceneCompletion_ = false;
    naturalSceneCompletionHalted_ = false;
    logicalAccepted = true;
    return true;
}

void Engine::cancelMediaPreparation(MediaPreparationToken ticket) noexcept
{
    if (!pendingMediaPreparation_ || pendingMediaPreparation_->ticket != ticket) return;
    if (!pendingMediaPreparation_->seekWithoutVideo) mediaProducer_->cancel(ticket);
    pendingMediaPreparation_.reset();
}

std::optional<Engine::MediaPreparationToken> Engine::pendingMediaPresentation() const noexcept
{
    return mediaPresentation_ ? std::optional{mediaPresentation_->ticket} : std::nullopt;
}

Engine::MediaPresentationResolution Engine::resolveMediaPresentation(
    MediaPreparationToken ticket, PresentationUploadResult result)
{
    using Resolution = MediaPresentationResolution;
    if (!mediaPresentation_ || mediaPresentation_->ticket != ticket || mediaShuttingDown_)
        return Resolution::Ignored;
    if (result == PresentationUploadResult::DeviceLost || result == PresentationUploadResult::GraphicsUnavailable)
        return Resolution::Pending;
    if (result == PresentationUploadResult::Updated)
    {
        mediaProducer_->releasePreviousBinding(detachedMediaBinding_);
        mediaPresentation_.reset();
        return Resolution::Acknowledged;
    }

    pollAudioExecution();
    const auto implicit = audioControl_->capture ? std::nullopt : beginAudioEffect(AudioEffectScope::Structural);
    if (!audioControl_->capture) return Resolution::Pending;
    const auto finish = ScopeExit{[&] { if (implicit) (void)finishAudioEffect(*implicit); }};
    if (pendingMediaPreparation_) cancelMediaPreparation(pendingMediaPreparation_->ticket);
    auto checkpoint = std::move(mediaPresentation_);

    if (!checkpoint->seek && !checkpoint->project)
    {
        detachProject();
        return Resolution::RolledBack;
    }

    const double position = checkpoint->clock.valid()
        ? Audio::resolveAudioClockPosition(checkpoint->clock, Audio::audioMonotonicNow()) : 0.0;
    detachedFrame_ = std::move(checkpoint->frame);
    detachedMediaBinding_ = checkpoint->binding;
    detachedMediaKind_ = checkpoint->kind;
    detachedVideoAtEnd_ = checkpoint->videoAtEnd;
    videoFrameTime_ = checkpoint->frameTime;
    videoPresentedTime_ = checkpoint->presentedTime;
    pendingNaturalSceneCompletion_ = checkpoint->completionPending;
    naturalSceneCompletionHalted_ = checkpoint->completionHalted;

    if (checkpoint->seek)
        audioControl_->requestSeek(*this, position);
    else
    {
        mediaProducer_->restorePreviousBinding(checkpoint->binding);
        detachedProject_ = std::move(checkpoint->project);
        boundProject_ = detachedProject_.get();
        projectBound_ = true;
        sceneRuntime_->setActiveScene(checkpoint->scene);
        playbackRuntime_->configure(checkpoint->playback, checkpoint->duration);
        playbackRuntime_->restoreTime(static_cast<float>(position));
        if (checkpoint->paused) playbackRuntime_->pause();
        audioControl_->retire(Audio::AudioLane::Embedded);
        audioControl_->configureClock(*this, position, checkpoint->paused);
        if (checkpoint->kind == MediaKind::Video && !checkpoint->source.empty())
            audioControl_->prepareEmbedded(*this, checkpoint->source,
                checkpoint->playback.speed, AudioControl::SourcePurpose::EmbeddedBind);
    }
    return Resolution::RolledBack;
}

void Engine::beginMediaShutdown() noexcept
{
    mediaShuttingDown_ = true;
    if (pendingMediaPreparation_) cancelMediaPreparation(pendingMediaPreparation_->ticket);
    if (mediaProducer_) mediaProducer_->beginShutdown();
}

bool Engine::mediaShutdownComplete() const noexcept
{
    return !mediaProducer_ || mediaProducer_->shutdownComplete();
}

void Engine::update()
{
    update(std::numeric_limits<float>::quiet_NaN());
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

bool Engine::naturalSceneCompletionPending() const noexcept
{ return pendingNaturalSceneCompletion_; }

void Engine::updateDetachedVideo()
{
    using State = Resources::DetachedMediaProducer::FrameState;
    auto frame = mediaProducer_->takeFrame();
    if (frame && frame->binding == detachedMediaBinding_)
    {
        if (frame->state == State::Ready)
        {
            detachedFrame_ = std::move(frame->pixels);
            videoFrameTime_ = frame->position;
            videoPresentedTime_ = frame->timestamp;
            audioControl_->lastConfirmedPosition = frame->position;
            audioControl_->recoverFailure(*this, frame->position);
        }
        else if (frame->state == State::EndOfStream && detachedFrame_
            && videoFrameTime_ >= 0.0F && videoPresentedTime_ >= 0.0
            && (frame->timestamp < 0 || frame->timestamp == videoPresentedTime_))
            detachedVideoAtEnd_ = true;
        else
            audioControl_->holdFailure(*this, audioControl_->lastConfirmedPosition, frame->position);
    }

    const auto target = audioControl_->failureHeld ? audioControl_->failedTarget : audioControl_->position();
    if (detachedVideoAtEnd_)
    {
        audioControl_->lastConfirmedPosition = target;
        return;
    }
    if (target < effectiveDuration() && static_cast<float>(target) != videoFrameTime_)
        (void)mediaProducer_->requestFrame(detachedMediaBinding_, static_cast<float>(target));
}

void Engine::update(float deltaTime)
{
    pollAudioExecution();
    if (!initialized_
        || !projectBound_
        || boundProject_ == nullptr
        || !playbackRuntime_
        || !sceneRuntime_)
    {
        return;
    }

    const bool asynchronousVideo = mediaProducer_ && detachedMediaKind_ == MediaKind::Video;
    if (asynchronousVideo && !naturalSceneCompletionHalted_ && !mediaShuttingDown_)
        updateDetachedVideo();

    const float previousPlaybackTime = static_cast<float>(audioControl_->lastConfirmedPosition);
    const float previousVideoFrameTime = videoFrameTime_;
    const double previousVideoPresentedTime = videoPresentedTime_;
    // Compatibility overload: elapsed GUI delta is never a timing authority.
    (void)deltaTime;
    const double progressionTarget = audioControl_->failureHeld ? audioControl_->failedTarget : audioControl_->position();
    playbackRuntime_->restoreTime(static_cast<float>(progressionTarget));
    const auto logSync = [this]() noexcept
    {
        logPlaybackSync(
            *playbackRuntime_,
            audioControl_->execution.snapshot(),
            videoPresentedTime_,
            syncDiagnosticUpdates_);
    };

    if (naturalSceneCompletionHalted_)
    {
        playbackRuntime_->pause();
        logSync();
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
            audioControl_->requestTransport(*this, true);
            logSync();
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
                return;
        }

        const bool atEnd = activeSceneIndex + 1 >= boundProject_->scenes.size();
        if (atEnd && !playbackRuntime_->configuration().loop)
        {
            playbackRuntime_->pause();
            audioControl_->requestTransport(*this, true);
            logSync();
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
                    const auto view = currentMediaView(mediaResource_.get());
                    if (view && stageMedia(*view, PresentationFrameKind::Video))
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
                const Resources::DecodedFrame* const heldFrame = video != nullptr
                    ? &video->currentFrame() : nullptr;
                const bool canHoldFinalFrame = video != nullptr
                    && video->lastFrameAtReachedEndOfStream()
                    && std::isfinite(previousVideoFrameTime)
                    && previousVideoFrameTime >= 0.0F
                    && std::isfinite(previousVideoPresentedTime)
                    && previousVideoPresentedTime >= 0.0
                    && heldFrame != nullptr && heldFrame->data != nullptr
                    && heldFrame->width > 0 && heldFrame->height > 0
                    && heldFrame->channels > 0 && heldFrame->rowStride > 0U
                    && heldFrame->byteSize > 0U
                    && presentedTimeOrFallback(*heldFrame, previousVideoPresentedTime)
                        == previousVideoPresentedTime
                    && video->verifyBufferGuard("at-playback-end-of-stream");
                // A drained decoder has no replacement for the last uploaded
                // frame. Keep it while the native clock reaches scene duration.
                if (!canHoldFinalFrame)
                {
                    playbackRuntime_->restoreTime(previousPlaybackTime);
                    audioControl_->holdFailure(*this, previousPlaybackTime, progressionTarget);
                }
                else audioControl_->lastConfirmedPosition = progressionTarget;
                logSync();
                        return;
            }

            const Resources::DecodedFrame& frame = video->currentFrame();
            if (!video->verifyBufferGuard("before-renderer-update"))
            {
                playbackRuntime_->restoreTime(previousPlaybackTime);
                    audioControl_->holdFailure(*this, previousPlaybackTime, progressionTarget);
                logSync();
                        return;
            }

            const auto view = currentMediaView(mediaResource_.get());
            if (!view || !stageMedia(*view, PresentationFrameKind::Video))
            {
                playbackRuntime_->restoreTime(previousPlaybackTime);
                    audioControl_->holdFailure(*this, previousPlaybackTime, progressionTarget);
                logSync();
                        return;
            }

            if (!video->verifyBufferGuard("after-renderer-update"))
            {
                playbackRuntime_->restoreTime(previousPlaybackTime);
                    audioControl_->holdFailure(*this, previousPlaybackTime, progressionTarget);
                logSync();
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

            audioControl_->recoverFailure(*this, progressionTarget);
            videoFrameTime_ = currentTime;
            videoPresentedTime_ = presentedTimeOrFallback(
                frame,
                static_cast<double>(currentTime));
        }
    }

    if (!asynchronousVideo)
        audioControl_->lastConfirmedPosition = progressionTarget;
    logSync();
}

bool Engine::seek(float sceneTimeSeconds)
{
    if (mediaProducer_) return false;
    pollAudioExecution();
    const bool needsAdmission = !audioControl_->capture;
    const auto implicit = needsAdmission ? beginAudioEffect(AudioEffectScope::Structural) : std::nullopt;
    if (needsAdmission && !implicit) return false;
    bool logicalAccepted = false;
    const auto operationExit = ScopeExit{[&] { if (implicit) (void)finishAudioEffect(*implicit, logicalAccepted); }};

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

    if (mediaResource_ && mediaResource_->type() == MediaType::Video)
    {
        auto* video = dynamic_cast<VideoResource*>(mediaResource_.get());
        if (video == nullptr || !video->frameAt(targetTime))
        {
            return false;
        }

        const Resources::DecodedFrame& frame = video->currentFrame();
        if (!video->verifyBufferGuard("before-seek-renderer-update"))
        {
            return false;
        }

        const auto view = currentMediaView(mediaResource_.get());
        if (!view || !stageMedia(*view, PresentationFrameKind::Video))
        {
            return false;
        }

        if (!video->verifyBufferGuard("after-seek-renderer-update"))
        {
            return false;
        }

        videoFrameTime_ = targetTime;
        videoPresentedTime_ = presentedTimeOrFallback(
            frame,
            static_cast<double>(targetTime));
    }

    audioControl_->requestSeek(*this, targetTime);
    pendingNaturalSceneCompletion_ = false;
    naturalSceneCompletionHalted_ = false;
    logicalAccepted = true;
    return true;
}

void Engine::render()
{
    const auto mediaView = currentMediaView(mediaResource_.get());
    if (!initialized_ || !presentation_
        || !renderer().prepareFrame(mediaView ? &*mediaView : nullptr))
    {
        return;
    }

    const auto targetInfo = renderer().presentationTargetInfo();
    const auto targetAspect = Domain::AspectRatio::from(
        targetInfo.viewportWidth,
        targetInfo.viewportHeight);
    const auto renderData = makeActiveSceneRenderData(
        projectBound_ ? boundProject_ : nullptr,
        sceneRuntime_.get(),
        currentMediaDimensions(),
        targetAspect.value_or(Domain::AspectRatio{}));
    renderer().renderPrepared(renderData);
}

void Engine::render(const Domain::AspectRatio& canvasAspect)
{
    render(canvasAspect, std::nullopt);
}

void Engine::render(
    const Domain::AspectRatio& canvasAspect,
    std::optional<CanvasOutsideInstruction> canvasOutside)
{
    if (!initialized_ || !presentation_)
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
    renderer().render(renderData, mediaView ? &*mediaView : nullptr);
}

bool Engine::processEvents()
{
    return presentation_ ? presentation_->processEvents() : initialized_;
}

bool Engine::consumeKeyPress(unsigned int virtualKey)
{
    return presentation_ && presentation_->consumeKeyPress(virtualKey);
}

bool Engine::consumeVolumeSlider(float& volume) noexcept
{
    return presentation_ && presentation_->consumeVolumeSlider(volume);
}

void Engine::togglePause()
{
    pollAudioExecution();
    if (naturalSceneCompletionHalted_ || !playbackRuntime_ || !playbackRuntime_->isConfigured()) return;
    const bool own = !audioControl_->capture;
    const auto token = own ? beginAudioEffect(AudioEffectScope::Structural) : std::nullopt;
    if (own && !token) return;
    audioControl_->requestTransport(*this, !isPaused());
    if (token) (void)finishAudioEffect(*token);
}

void Engine::setPlaybackSpeed(float speed)
{
    pollAudioExecution();
    if (!playbackRuntime_ || !playbackRuntime_->isConfigured()
        || !std::isfinite(speed) || speed < 0.5F || speed > 2.0F || speed == playbackSpeed()) return;
    const bool own = !audioControl_->capture;
    const auto token = own ? beginAudioEffect(AudioEffectScope::Structural) : std::nullopt;
    if (own && !token) return;
    if (!audioControl_->embeddedPath.empty())
        audioControl_->prepareEmbedded(*this, audioControl_->embeddedPath, speed, AudioControl::SourcePurpose::EmbeddedRate);
    else
    {
        Audio::AudioCommand cmd{};
        cmd.kind = Audio::AudioCommandKind::ReplaceEmbedded;
        cmd.playbackLifetime = audioControl_->playbackLifetime;
        cmd.replacement = Audio::AudioEmbeddedReplacement::RateContinuous;
        cmd.clock = audioControl_->plannedClock;
        cmd.clock.rate = speed; cmd.clock.timingVersion = ++audioControl_->timing;
        cmd.expectedTimingVersion = audioControl_->plannedClock.timingVersion;
        cmd.embeddedPresent = false;
        cmd.transport = isPaused() ? Audio::AudioTransport::Paused : Audio::AudioTransport::Playing;
        if (audioControl_->pendingBinding)
        { audioControl_->pendingBinding->clock.rate = speed; audioControl_->plannedClock.rate = speed; }
        else if (audioControl_->command(cmd, AudioEffectScope::Structural)) audioControl_->plannedClock = cmd.clock;
    }
    if (token) (void)finishAudioEffect(*token);
}

void Engine::setVolume(float value) noexcept
{
    pollAudioExecution();
    const bool own = !audioControl_->capture;
    const auto token = own ? beginAudioEffect(AudioEffectScope::EmbeddedGain) : std::nullopt;
    if (own && !token) return;
    audioControl_->desiredEmbeddedGain = std::isfinite(value) ? std::clamp(value, 0.0F, 1.0F) : 1.0F;
    for (auto& source : audioControl_->sources)
    {
        if (!source.occupied || source.retiring || source.key.lane != Audio::AudioLane::Embedded) continue;
        source.gain = audioControl_->desiredEmbeddedGain;
        if (!source.submitted) continue;
        Audio::AudioCommand cmd{}; cmd.kind = Audio::AudioCommandKind::SetSourceGain;
        cmd.source = source.key; cmd.gain = source.gain;
        (void)audioControl_->command(cmd, AudioEffectScope::EmbeddedGain);
    }
    if (token) (void)finishAudioEffect(*token);
    if (presentation_) presentation_->setVolumeControl(volume());
}

float Engine::volume() const noexcept { return audioControl_->desiredEmbeddedGain; }

void Engine::toggleMute() noexcept
{
    pollAudioExecution();
    const bool own = !audioControl_->capture;
    const auto token = own ? beginAudioEffect(AudioEffectScope::OutputMute) : std::nullopt;
    if (own && !token) return;
    Audio::AudioCommand cmd{}; cmd.kind = Audio::AudioCommandKind::SetOutputMute;
    cmd.muted = !audioControl_->desiredMuted;
    if (audioControl_->command(cmd, AudioEffectScope::OutputMute)) audioControl_->desiredMuted = cmd.muted;
    if (token) (void)finishAudioEffect(*token);
}
bool Engine::isMuted() const noexcept { return audioControl_->execution.snapshot().muteApplied; }

bool Engine::processBackgroundMusicControl(const BackgroundMusicControl::Event& event)
{
    pollAudioExecution();
    const bool stopped = std::holds_alternative<BackgroundMusicControl::PlaybackStopped>(event);
    const bool own = !audioControl_->capture && !stopped;
    const auto token = own ? beginAudioEffect(AudioEffectScope::Structural) : std::nullopt;
    if (own && !token) return false;
    const auto operationExit = ScopeExit{[&] { if (token) (void)finishAudioEffect(*token); }};
    const auto internal = toInternalMusicEvent(event,
        isPaused() ? Music::PlaybackActivity::Paused : Music::PlaybackActivity::Playing);
    if (!internal) return false;
    const auto result = processBackgroundMusicEventInternal(backgroundMusicOrchestrator_.get(), *internal);
    if (stopped)
    {
        audioControl_->retireAll();
        (void)audioControl_->execution.requestRetirePlayback(audioControl_->playbackLifetime);
        if (audioControl_->capture)
            for (auto& operation : audioControl_->effects)
                if (operation.result.token == *audioControl_->capture) operation.requiredRetirement = audioControl_->playbackLifetime;
        ++audioControl_->playbackLifetime;
    }
    // C's BGM policy and A's physical pause are distinct but ordered in the
    // same structural operation. Config/gain changes do not reset transport.
    if (std::holds_alternative<BackgroundMusicControl::PlaybackPaused>(event))
        audioControl_->requestTransport(*this, true);
    else if (std::holds_alternative<BackgroundMusicControl::PlaybackResumed>(event))
        audioControl_->requestTransport(*this, false);
    return result && (*result == Music::MusicProcessResult::Completed || *result == Music::MusicProcessResult::Enqueued);
}

std::optional<Engine::AudioEffectToken> Engine::beginAudioEffect(AudioEffectScope scope) noexcept
{ return audioControl_->begin(scope); }
Engine::AudioEffectResult Engine::finishAudioEffect(AudioEffectToken token, bool accepted)
{ return audioControl_->finish(token, accepted); }
Engine::AudioEffectResult Engine::audioEffect(AudioEffectToken token) const
{ return audioControl_->effect(token); }
void Engine::pollAudioExecution() { audioControl_->poll(*this); }
Engine::AudioExecutionState Engine::audioExecutionState() const { return audioControl_->state(); }
Engine::PlaybackClockSample Engine::playbackClockSample() const
{
    const_cast<Engine*>(this)->pollAudioExecution();
    const auto& control = *audioControl_;
    PlaybackClockSample sample{};
    sample.asOfEffectSequence = control.observedExecution.lastEffectSequence;
    sample.asOfTimeNs = control.observationTime;
    const bool confirmed = control.clock.valid() && control.clock.binding == control.binding;
    sample.position = projectBound_ && confirmed ? Audio::resolveAudioClockPosition(control.clock, sample.asOfTimeNs) : 0;
    sample.duration = effectiveDuration();
    sample.rate = confirmed ? control.clock.rate : projectBound_ ? 0 : 1;
    sample.paused = playbackRuntime_ && playbackRuntime_->isPaused();
    sample.outputMuted = control.observedExecution.muteApplied;
    sample.muteKnown = control.observedExecution.muteKnown;
    return sample;
}
void Engine::beginAudioShutdown() noexcept { audioControl_->beginShutdown(); }
bool Engine::audioShutdownComplete() const noexcept { return audioControl_->stopped(); }

BackgroundMusicObservation::Snapshot Engine::backgroundMusicObservation() const
{
    return backgroundMusicRuntime_ ? backgroundMusicRuntime_->backgroundMusicObservation()
        : BackgroundMusicObservation::Snapshot{};
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
    const_cast<Engine*>(this)->pollAudioExecution();
    return playbackRuntime_ != nullptr && playbackRuntime_->isPaused();
}

float Engine::playbackTime() const noexcept
{
    const_cast<Engine*>(this)->pollAudioExecution();
    return projectBound_ ? static_cast<float>(audioControl_->position()) : 0.0F;
}

float Engine::effectiveDuration() const noexcept
{
    return playbackRuntime_ ? playbackRuntime_->duration() : 0.0F;
}

float Engine::playbackSpeed() const noexcept
{
    const_cast<Engine*>(this)->pollAudioExecution();
    if (!projectBound_) return 1.0F;
    return audioControl_->clock.valid() && audioControl_->clock.binding == audioControl_->binding
        ? static_cast<float>(audioControl_->clock.rate) : 0.0F;
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
    if (mediaProducer_) return detachedMediaKind_;
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
    if (mediaProducer_)
        return detachedFrame_ ? std::optional{MediaDimensions{detachedFrame_->width, detachedFrame_->height}}
                              : std::nullopt;
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
    if (presentation_) presentation_->setStatusText(text);
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
    // Normal Session Close waits asynchronously before calling shutdown().
    // The synchronous RAII boundary is containment for exceptional callers;
    // it never frees a source still read by A/P or claims a timeout success.
    beginMediaShutdown();
    beginAudioShutdown();
    while (!audioShutdownComplete() || !mediaShutdownComplete())
    {
        pollAudioExecution();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (presentation_) renderer().invalidateHostedCompositionContent();
    detachProject();
    mediaProducer_.reset();
    playbackRuntime_.reset();
    if (presentation_) presentation_->shutdown();
    initialized_ = false;
}

}
