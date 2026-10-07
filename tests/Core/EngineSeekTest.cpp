#include <DesktopSceneEngine/BackgroundMusicControl.h>
#include <DesktopSceneEngine/Domain/Crop.h>
#include <DesktopSceneEngine/Domain/EntityId.h>
#include <DesktopSceneEngine/Domain/Layout.h>
#include <DesktopSceneEngine/Domain/Library.h>
#include <DesktopSceneEngine/Domain/Media.h>
#include <DesktopSceneEngine/Domain/Playback.h>
#include <DesktopSceneEngine/Domain/Project.h>
#include <DesktopSceneEngine/Domain/Scene.h>
#include <DesktopSceneEngine/Engine.h>

#include "Core/AudioOutputRecoveryTestHooks.h"
#include "Core/SharedAudioOutputRecoveryPolicy.h"
#include "Music/BackgroundMusicExecutor.h"
#include "Music/BackgroundMusicRuntime.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace DesktopSceneEngine::Core
{

class BackgroundMusicEngineTestAccess final
{
public:
    [[nodiscard]] static const Music::BackgroundMusicRuntime& runtime(
        const Engine& engine
    ) noexcept
    {
        return *engine.backgroundMusicRuntime_;
    }

    [[nodiscard]] static const Music::BackgroundMusicExecutor& executor(
        const Engine& engine
    ) noexcept
    {
        return *engine.backgroundMusicExecutor_;
    }

    [[nodiscard]] static bool hasEmbeddedAudioInput(const Engine& engine) noexcept
    {
        return engine.embeddedAudioInputHandleValue_ != 0U;
    }
};

class AudioOutputRecoveryEngineTestAccess final
{
public:
    static void installHooks(Engine& engine, AudioOutputRecoveryTestHooks hooks)
    {
        engine.audioOutputRecoveryTestHooks_ = std::make_unique<AudioOutputRecoveryTestHooks>(
            std::move(hooks));
    }

    [[nodiscard]] static AudioOutputOperationalState operationalState(
        const Engine& engine
    ) noexcept
    {
        return engine.audioOutputOperationalState_;
    }

    [[nodiscard]] static std::uint64_t embeddedInputHandle(const Engine& engine) noexcept
    {
        return engine.embeddedAudioInputHandleValue_;
    }
};

}

namespace
{

using DesktopSceneEngine::Domain::EntityId;
using DesktopSceneEngine::Domain::Library;
using DesktopSceneEngine::Domain::Media;
using DesktopSceneEngine::Domain::Playback;
using DesktopSceneEngine::Domain::Project;
using DesktopSceneEngine::Domain::Scene;
using DesktopSceneEngine::BackgroundMusicControl::ActiveScopeSnapshot;
using DesktopSceneEngine::BackgroundMusicControl::Event;
using DesktopSceneEngine::BackgroundMusicControl::PlaybackSessionStarted;
using DesktopSceneEngine::BackgroundMusicControl::PlaylistId;
using DesktopSceneEngine::BackgroundMusicControl::QueueMode;
using DesktopSceneEngine::BackgroundMusicControl::QueueSnapshot;
using DesktopSceneEngine::BackgroundMusicControl::TrackId;
using DesktopSceneEngine::BackgroundMusicControl::TrackSnapshot;
using DesktopSceneEngine::Core::BackgroundMusicEngineTestAccess;
using DesktopSceneEngine::Core::AudioOutputRecoveryEngineTestAccess;
using DesktopSceneEngine::Core::AudioOutputRecoveryTestHooks;
using DesktopSceneEngine::Core::AudioOutputOperationalState;
using DesktopSceneEngine::Audio::MixedOutputBridgeResult;
using DesktopSceneEngine::Audio::MixedOutputBridgeStatus;

bool require(bool condition, const char* message)
{
    if (condition)
    {
        return true;
    }

    std::cerr << "Test failed: " << message << std::endl;
    return false;
}

bool nearlyEqual(float left, float right, float tolerance = 0.01F)
{
    return std::fabs(left - right) <= tolerance;
}

std::string utf8Path(const std::filesystem::path& path)
{
    const std::u8string value = path.u8string();
    return {
        reinterpret_cast<const char*>(value.data()),
        value.size()
    };
}

std::vector<std::filesystem::path> filesWithExtensions(
    const std::filesystem::path& directory,
    const std::vector<std::string>& extensions
)
{
    std::vector<std::filesystem::path> files{};
    for (const auto& entry : std::filesystem::directory_iterator(directory))
    {
        if (!entry.is_regular_file())
        {
            continue;
        }

        std::string extension = entry.path().extension().string();
        std::transform(
            extension.begin(),
            extension.end(),
            extension.begin(),
            [](unsigned char character)
            {
                return static_cast<char>(std::tolower(character));
            });
        if (std::find(extensions.begin(), extensions.end(), extension)
            != extensions.end())
        {
            files.push_back(entry.path());
        }
    }

    std::sort(files.begin(), files.end());
    return files;
}

Event makePlaylistBgmStartEvent()
{
    const std::filesystem::path source = std::filesystem::path{
        DESKTOP_SCENE_ENGINE_SEQUENTIAL_AUDIO_TEST_DATA_DIR}
        / "SequentialAudioSourceStereo48k.wav";
    return PlaybackSessionStarted{ActiveScopeSnapshot{
        PlaylistId{"seek-playlist"},
        QueueSnapshot{
            {TrackSnapshot{TrackId{"seek-bgm"}, source, 1.0}},
            QueueMode::ListLoop,
            1.0},
        std::nullopt,
        std::nullopt}};
}

struct EmbeddedRecoveryOutputScript
{
    std::size_t initializeCalls = 0U;
    std::size_t beginCalls = 0U;
    std::size_t pumpCalls = 0U;

    [[nodiscard]] AudioOutputRecoveryTestHooks hooks()
    {
        AudioOutputRecoveryTestHooks result{};
        result.initialize = [this]()
        {
            ++initializeCalls;
            return true;
        };
        result.beginBlockMode = [this](auto)
        {
            ++beginCalls;
            return DesktopSceneEngine::Audio::AudioBlockStatus::Success;
        };
        result.pump = [this]()
        {
            ++pumpCalls;
            return MixedOutputBridgeResult{
                0U,
                pumpCalls == 2U
                    ? MixedOutputBridgeStatus::DeviceFailure
                    : MixedOutputBridgeStatus::WroteFrames};
        };
        return result;
    }
};

Project makeMediaProject(
    const std::filesystem::path& resourcePath,
    const EntityId& sceneId,
    const EntityId& mediaId,
    float sceneDuration,
    float speed = 1.0F
)
{
    return Project{
        Library{{Media{mediaId, "Seek media", utf8Path(resourcePath)}}},
        {Scene{
            sceneId,
            mediaId,
            {},
            {},
            Playback{true, speed, 0.75F},
            sceneDuration}},
        {}};
}

Project makeZeroDurationProject()
{
    return Project{
        {},
        {Scene{
            EntityId{std::string{"zero-duration-scene"}},
            EntityId{std::string{"missing-media"}},
            {},
            {},
            Playback{true, 1.0F, 0.75F},
            0.0F}},
        {}};
}

bool testImageSeek(const std::filesystem::path& imagePath)
{
    const EntityId sceneId{std::string{"image-scene"}};
    const Project project = makeMediaProject(
        imagePath,
        sceneId,
        EntityId{std::string{"image-media"}},
        10.0F);

    DesktopSceneEngine::Engine engine{};
    if (!require(
            !engine.seek(1.0F),
            "Seek must fail before Engine initialization")
        || !require(
            engine.isInitialized() == false,
            "A new Engine should start uninitialized"))
    {
        return false;
    }

    engine.initialize();
    if (!require(engine.isInitialized(), "Engine initialization should succeed")
        || !require(
            !engine.seek(1.0F),
            "Seek must fail before a Project is bound")
        || !require(
            engine.bindProject(project, sceneId),
            "Image Project binding should succeed"))
    {
        engine.shutdown();
        return false;
    }

    engine.setPlaybackSpeed(2.0F);
    engine.setVolume(0.65F);
    engine.toggleMute();
    const float preservedSpeed = engine.playbackSpeed();
    const float preservedVolume = engine.volume();
    const bool preservedMute = engine.isMuted();
    const std::size_t preservedSceneIndex = engine.activeSceneIndex();

    if (!require(engine.seek(3.5F), "Image seek to the middle should succeed")
        || !require(
            nearlyEqual(engine.playbackTime(), 3.5F),
            "Image seek should commit scene-local playback time")
        || !require(
            engine.activeSceneIndex() == preservedSceneIndex,
            "Image seek must preserve the active scene")
        || !require(
            nearlyEqual(engine.playbackSpeed(), preservedSpeed)
                && nearlyEqual(engine.volume(), preservedVolume)
                && engine.isMuted() == preservedMute,
            "Image seek must preserve speed, volume, and mute"))
    {
        engine.shutdown();
        return false;
    }

    engine.update(0.25F);
    if (!require(
            nearlyEqual(engine.playbackTime(), 4.0F),
            "Update should advance from the sought image time at the preserved speed"))
    {
        engine.shutdown();
        return false;
    }

    engine.togglePause();
    if (!require(engine.isPaused(), "Image playback should pause")
        || !require(engine.seek(7.5F), "Paused image seek should succeed")
        || !require(
            engine.isPaused() && nearlyEqual(engine.playbackTime(), 7.5F),
            "Paused image seek must preserve pause state and commit target time"))
    {
        engine.shutdown();
        return false;
    }

    engine.update(10.0F);
    if (!require(
            nearlyEqual(engine.playbackTime(), 7.5F),
            "Paused playback must not advance after a seek"))
    {
        engine.shutdown();
        return false;
    }

    if (!require(engine.seek(-2.0F), "Negative image seek should clamp to zero")
        || !require(
            nearlyEqual(engine.playbackTime(), 0.0F),
            "Negative image seek should commit zero")
        || !require(
            engine.seek(100.0F),
            "Beyond-end image seek should clamp safely")
        || !require(
            engine.playbackTime() >= 0.0F
                && engine.playbackTime() < engine.effectiveDuration(),
            "Beyond-end seek must remain before the duration boundary")
        || !require(
            engine.activeSceneIndex() == preservedSceneIndex && engine.isPaused(),
            "Beyond-end paused seek must not transition or resume playback"))
    {
        engine.shutdown();
        return false;
    }

    const float timeBeforeInvalidSeek = engine.playbackTime();
    if (!require(
            !engine.seek((std::numeric_limits<float>::quiet_NaN)()),
            "NaN seek must fail")
        || !require(
            !engine.seek((std::numeric_limits<float>::infinity)()),
            "Positive infinite seek must fail")
        || !require(
            !engine.seek(-(std::numeric_limits<float>::infinity)()),
            "Negative infinite seek must fail")
        || !require(
            nearlyEqual(engine.playbackTime(), timeBeforeInvalidSeek)
                && engine.isPaused()
                && engine.activeSceneIndex() == preservedSceneIndex,
            "Invalid seek input must not mutate playback state"))
    {
        engine.shutdown();
        return false;
    }

    engine.shutdown();
    return true;
}

bool testZeroDurationSeek()
{
    const Project project = makeZeroDurationProject();
    const EntityId sceneId{std::string{"zero-duration-scene"}};

    DesktopSceneEngine::Engine engine{};
    engine.initialize();
    const bool passed = require(
        engine.bindProject(project, sceneId),
        "A zero-duration Project should bind")
        && require(
            !engine.seek(0.0F),
            "Seek must fail for a zero-duration active scene")
        && require(
            nearlyEqual(engine.playbackTime(), 0.0F),
            "Failed zero-duration seek must not alter playback time");
    engine.shutdown();
    return passed;
}

bool testVideoSeek(const std::filesystem::path& videoPath)
{
    const EntityId sceneId{std::string{"video-scene"}};
    const Project project = makeMediaProject(
        videoPath,
        sceneId,
        EntityId{std::string{"video-media"}},
        0.0F,
        1.25F);

    DesktopSceneEngine::Engine engine{};
    EmbeddedRecoveryOutputScript recoveryOutput{};
    AudioOutputRecoveryEngineTestAccess::installHooks(engine, recoveryOutput.hooks());
    engine.initialize();
    if (!require(
            engine.bindProject(project, sceneId),
            "Video Project binding should succeed")
        || !require(
            engine.mediaKind() == DesktopSceneEngine::Engine::MediaKind::Video,
            "Bound video Project should expose video media")
        || !require(
            engine.effectiveDuration() > 4.0F,
            "Seek test video must have sufficient duration for random access"))
    {
        engine.shutdown();
        return false;
    }

    const std::size_t sceneIndex = engine.activeSceneIndex();
    const float duration = engine.effectiveDuration();
    const float preservedSpeed = engine.playbackSpeed();
    const float forwardTarget = (std::min)(duration * 0.75F, duration - 0.25F);
    const float backwardTarget = 0.25F;

    if (!require(
            BackgroundMusicEngineTestAccess::hasEmbeddedAudioInput(engine),
            "The seek fixture must create the embedded input required to exercise V1 shared-stream flushes")
        || !require(
            engine.processBackgroundMusicControl(makePlaylistBgmStartEvent()),
            "Seek continuity setup must start a Playlist BGM source")
        || !require(
            BackgroundMusicEngineTestAccess::executor(engine).hasActiveSources(),
            "Playlist BGM setup must register an Executor-owned Mixer source"))
    {
        engine.shutdown();
        return false;
    }

    const auto* const playlistSession = BackgroundMusicEngineTestAccess::runtime(engine)
        .playlistSession();
    if (!require(
            playlistSession != nullptr && playlistSession->sourceToken().has_value(),
            "Playlist BGM setup must produce a durable source token"))
    {
        engine.shutdown();
        return false;
    }
    const auto bgmToken = playlistSession->sourceToken();

    const auto requireBgmContinuity = [&engine, &bgmToken](const char* message)
    {
        const auto* const currentSession = BackgroundMusicEngineTestAccess::runtime(engine)
            .playlistSession();
        return require(
            BackgroundMusicEngineTestAccess::executor(engine).hasActiveSources()
                && currentSession != nullptr
                && currentSession->sourceToken() == bgmToken,
            message);
    };

    const std::uint64_t embeddedHandleBeforeOutage =
        AudioOutputRecoveryEngineTestAccess::embeddedInputHandle(engine);
    engine.update(0.0F);
    if (!require(
            AudioOutputRecoveryEngineTestAccess::operationalState(engine)
                == AudioOutputOperationalState::DeviceUnavailable
                && AudioOutputRecoveryEngineTestAccess::embeddedInputHandle(engine)
                    == embeddedHandleBeforeOutage
                && requireBgmContinuity(
                    "Device outage entry must retain embedded and BGM mixer registrations"),
            "The scripted device failure must preserve embedded presence until recovery"))
    {
        engine.shutdown();
        return false;
    }

    engine.update(0.5F);
    if (!require(
            AudioOutputRecoveryEngineTestAccess::operationalState(engine)
                == AudioOutputOperationalState::Healthy
                && AudioOutputRecoveryEngineTestAccess::embeddedInputHandle(engine) != 0U
                && AudioOutputRecoveryEngineTestAccess::embeddedInputHandle(engine)
                    != embeddedHandleBeforeOutage
                && recoveryOutput.initializeCalls == 3U
                && recoveryOutput.beginCalls == 2U
                && recoveryOutput.pumpCalls == 3U
                && nearlyEqual(engine.playbackTime(), 0.625F, 0.02F)
                && requireBgmContinuity(
                    "Embedded rebuild must preserve the Playlist BGM registration and token"),
            "Recovery must rebuild embedded audio at the current visual logical time"))
    {
        engine.shutdown();
        return false;
    }

    if (!require(
            forwardTarget > 2.0F && engine.seek(forwardTarget),
            "Large-forward video seek should reuse the existing random-access path")
        || !requireBgmContinuity(
            "V1 seek flush must preserve the Playlist BGM registration and token")
        || !require(
            nearlyEqual(engine.playbackTime(), forwardTarget, 0.02F),
            "Large-forward video seek should commit the requested media time")
        || !require(
            engine.seek(backwardTarget),
            "Backward video seek should reuse the existing random-access path")
        || !require(
            nearlyEqual(engine.playbackTime(), backwardTarget, 0.02F),
            "Backward video seek should commit the requested media time")
        || !require(
            engine.activeSceneIndex() == sceneIndex,
            "Video seek must preserve the active scene identity")
        || !require(
            nearlyEqual(engine.playbackSpeed(), preservedSpeed),
            "Video seek must preserve the configured playback speed"))
    {
        engine.shutdown();
        return false;
    }

    const float timeBeforeEndSeek = engine.playbackTime();
    const bool endSeekSucceeded = engine.seek(duration + 10.0F);
    if (!require(
            engine.activeSceneIndex() == sceneIndex,
            "Video end seek must not switch scenes immediately")
        || !require(
            endSeekSucceeded
                ? engine.playbackTime() < duration
                : nearlyEqual(engine.playbackTime(), timeBeforeEndSeek, 0.02F),
            "Video end seek must either clamp before EOF or preserve prior logical time"))
    {
        engine.shutdown();
        return false;
    }

    const float timeBeforeSpeedChanges = engine.playbackTime();
    engine.setPlaybackSpeed(1.0F);
    if (!require(
            nearlyEqual(engine.playbackSpeed(), 1.0F),
            "Video speed change should support the 1.25x to 1.0x transition")
        || !requireBgmContinuity(
            "V1 speed flush must preserve the Playlist BGM registration and token"))
    {
        engine.shutdown();
        return false;
    }

    if (!require(
            !engine.seek((std::numeric_limits<float>::quiet_NaN)()),
            "Invalid video seek must fail without starting an embedded transaction")
        || !requireBgmContinuity(
            "A failed seek must not remove or recreate the surviving Playlist BGM source"))
    {
        engine.shutdown();
        return false;
    }

    engine.setPlaybackSpeed(2.0F);
    if (!require(
            nearlyEqual(engine.playbackSpeed(), 2.0F),
            "Video speed change should support the 1.0x to 2.0x transition"))
    {
        engine.shutdown();
        return false;
    }

    engine.setPlaybackSpeed(1.0F);
    engine.setPlaybackSpeed(0.5F);
    engine.setPlaybackSpeed(2.0F);
    engine.setPlaybackSpeed(1.0F);
    if (!require(
            nearlyEqual(engine.playbackSpeed(), 1.0F)
                && nearlyEqual(engine.playbackTime(), timeBeforeSpeedChanges, 0.02F),
            "Repeated video speed changes must preserve the logical media time"))
    {
        engine.shutdown();
        return false;
    }

    engine.togglePause();
    const float pausedTarget = (std::min)(1.0F, duration - 0.25F);
    if (!require(engine.isPaused(), "Video playback should pause")
        || !require(
            engine.seek(pausedTarget),
            "Paused video seek should decode and upload the target frame")
        || !require(
            engine.isPaused()
                && nearlyEqual(engine.playbackTime(), pausedTarget, 0.02F),
            "Paused video seek must preserve pause state and commit target time"))
    {
        engine.shutdown();
        return false;
    }

    engine.setPlaybackSpeed(1.0F);
    if (!require(
            engine.isPaused() && nearlyEqual(engine.playbackSpeed(), 1.0F),
            "Paused video speed changes must preserve pause state"))
    {
        engine.shutdown();
        return false;
    }

    engine.update(1.0F);
    if (!require(
            nearlyEqual(engine.playbackTime(), pausedTarget, 0.02F),
            "Paused video playback must not advance after a seek"))
    {
        engine.shutdown();
        return false;
    }

    engine.shutdown();
    return true;
}

}

int main()
{
    const std::filesystem::path resourceRoot{
        DESKTOP_SCENE_ENGINE_TEST_RESOURCES_DIR};
    const auto images = filesWithExtensions(
        resourceRoot / "Images", {".jpg", ".jpeg", ".png"});
    const auto videos = filesWithExtensions(
        resourceRoot / "Videos", {".mp4"});
    if (!require(
            !images.empty() && !videos.empty(),
            "Engine seek test requires one image and one MP4 test resource"))
    {
        return 1;
    }

    return testImageSeek(images.front())
            && testZeroDurationSeek()
            && testVideoSeek(videos.front())
        ? 0 : 1;
}
