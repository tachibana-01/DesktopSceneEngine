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

#include "AudioExecutionEngineFixture.h"
#include "Resources/AudioResource.h"
#include "Resources/VideoResource.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>


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
namespace Tests = DesktopSceneEngine::Tests;
using DesktopSceneEngine::Audio::AudioLane;
using DesktopSceneEngine::Audio::AudioOutputHealth;

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

bool readMatchesNativeClock(DesktopSceneEngine::Engine& engine,
    const DesktopSceneEngine::Audio::AudioClockAnchor& anchor)
{
    const auto before = DesktopSceneEngine::Audio::audioMonotonicNow();
    const double actual = engine.playbackTime();
    const auto after = DesktopSceneEngine::Audio::audioMonotonicNow();
    const double lower = DesktopSceneEngine::Audio::resolveAudioClockPosition(anchor, before);
    const double upper = DesktopSceneEngine::Audio::resolveAudioClockPosition(anchor, after);
    // Only the float public return conversion is tolerated. The scheduling
    // interval is measured in the same native domain, not a guessed delay.
    constexpr double FloatRounding = 0.000002;
    const bool matches = actual >= lower - FloatRounding && actual <= upper + FloatRounding;
    if (!matches) std::cerr << "Seek clock read actual=" << actual << " envelope=[" << lower << ',' << upper
        << "] anchor=" << anchor.position << " version=" << anchor.timingVersion << '\n';
    return matches;
}

bool committedSeekAnchorIs(DesktopSceneEngine::Engine& engine, float target)
{
    if (!Tests::waitUntil(engine, [&] {
        const auto& clock = AudioOutputRecoveryEngineTestAccess::control(engine).clock;
        const auto applied = Tests::audioState(engine).clock;
        return clock.valid() && applied.binding == clock.binding && applied.timingVersion == clock.timingVersion;
    })) return false;
    const auto clock = AudioOutputRecoveryEngineTestAccess::control(engine).clock;
    const auto applied = Tests::audioState(engine).clock;
    const bool matching = clock.position == static_cast<double>(target)
        && applied.position == static_cast<double>(target)
        && applied.timingVersion == clock.timingVersion
        && applied.binding == clock.binding;
    if (!matching) std::cerr << "Seek anchor target=" << target << " C=" << clock.position
        << " A=" << applied.position << " versions=" << clock.timingVersion << ',' << applied.timingVersion << '\n';
    return matching && readMatchesNativeClock(engine, clock);
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

    Tests::AwaitAudioEngine engine{};
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
            committedSeekAnchorIs(engine, 3.5F),
            "Image seek commits the exact matching C/A target anchor and reads its QPC progression")
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

    const auto beforeElapsed = DesktopSceneEngine::Audio::audioMonotonicNow();
    const auto beforePosition = engine.playbackTime();
    std::this_thread::sleep_for(std::chrono::milliseconds{250});
    engine.update(0.25F); // Compatibility argument is not the clock authority.
    const double elapsed = static_cast<double>(DesktopSceneEngine::Audio::audioMonotonicNow() - beforeElapsed) / 1e9;
    if (!require(
            std::fabs(engine.playbackTime() - (beforePosition + elapsed * preservedSpeed)) < 0.03,
            "Update evaluates elapsed QPC time from the sought position at the preserved speed"))
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

    Tests::AwaitAudioEngine engine{};
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

    Tests::AwaitAudioEngine engine{};
    auto& recoveryOutput = engine.device;
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
            Tests::lane(engine, AudioLane::Embedded).has_value(),
            "The seek fixture must create the embedded input required to exercise V1 shared-stream flushes")
        || !require(
            engine.processBackgroundMusicControl(makePlaylistBgmStartEvent()),
            "Seek continuity setup must start a Playlist BGM source")
        || !require(
            Tests::lane(engine, AudioLane::PlaylistBgm).has_value(),
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
            Tests::lane(engine, AudioLane::PlaylistBgm).has_value()
                && currentSession != nullptr
                && currentSession->sourceToken() == bgmToken,
            message);
    };

    const auto embeddedBeforeOutage = *Tests::lane(engine, AudioLane::Embedded);
    const auto opensBeforeOutage = recoveryOutput.openCalls.load();
    const auto configuresBeforeOutage = recoveryOutput.configureCalls.load();
    const auto recoveriesBeforeOutage = Tests::audioState(engine).recoveryAttempts;
    recoveryOutput.availabilityResult.store(DesktopSceneEngine::Audio::AudioBlockStatus::DeviceFailure);
    if (!require(Tests::waitUntil(engine, [&] {
            return Tests::audioState(engine).health == AudioOutputHealth::DeviceUnavailable;
        }), "A must observe the injected sink failure independently of Engine update")
        || !require(Tests::lane(engine, AudioLane::Embedded).has_value()
            && Tests::lane(engine, AudioLane::Embedded)->source == embeddedBeforeOutage.source
            && requireBgmContinuity("Device outage retains embedded and BGM source identity"),
            "Device outage does not retire registered sources")) return false;
    if (!require(Tests::waitUntil(engine, [&] {
            return Tests::audioState(engine).health == AudioOutputHealth::Healthy
                && Tests::audioState(engine).recoveryAttempts > recoveriesBeforeOutage;
        }), "A recovers after native elapsed cooldown without GUI delta")
        || !require(recoveryOutput.openCalls.load() == opensBeforeOutage + 1
            && recoveryOutput.configureCalls.load() == configuresBeforeOutage + 1
            && Tests::audioState(engine).recoveryAttempts == recoveriesBeforeOutage + 1,
            "Outage causes exactly one reopen/configure attempt")
        || !require(Tests::lane(engine, AudioLane::Embedded).has_value()
            && Tests::lane(engine, AudioLane::Embedded)->source == embeddedBeforeOutage.source
            && requireBgmContinuity("Recovery preserves Playlist BGM registration and token"),
            "Recovery resynchronizes the same immutable embedded allocation and surviving BGM")) return false;
    const auto recovered = *Tests::lane(engine, AudioLane::Embedded);
    const double expectedCursor = engine.playbackTime() / engine.playbackSpeed() * 48000.0;
    if (!require(std::fabs(static_cast<double>(recovered.cursorFrames) - expectedCursor) < 4800,
        "Recovered embedded cursor agrees with latest committed clock within the device horizon")) return false;

    if (!require(
            forwardTarget > 2.0F && engine.seek(forwardTarget),
            "Large-forward video seek should reuse the existing random-access path")
        || !requireBgmContinuity(
            "V1 seek flush must preserve the Playlist BGM registration and token")
        || !require(
            committedSeekAnchorIs(engine, forwardTarget),
            "Large-forward video seek commits the exact C/A media anchor and its native elapsed position")
        || !require(
            engine.seek(backwardTarget),
            "Backward video seek should reuse the existing random-access path")
        || !require(
            committedSeekAnchorIs(engine, backwardTarget),
            "Backward video seek commits the exact C/A media anchor and its native elapsed position")
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

    const auto clockBeforeEndSeek = AudioOutputRecoveryEngineTestAccess::control(engine).clock;
    const bool endSeekSucceeded = engine.seek(duration + 10.0F);
    if (!require(
            engine.activeSceneIndex() == sceneIndex,
            "Video end seek must not switch scenes immediately")
        || !require(
            endSeekSucceeded
                ? engine.playbackTime() < duration
                : AudioOutputRecoveryEngineTestAccess::control(engine).clock.timingVersion == clockBeforeEndSeek.timingVersion
                    && readMatchesNativeClock(engine, clockBeforeEndSeek),
            "Video end seek must clamp before EOF or preserve the committed native clock and its progression"))
    {
        engine.shutdown();
        return false;
    }

    const float timeBeforeSpeedChanges = engine.playbackTime();
    const auto speedChangesBegan = DesktopSceneEngine::Audio::audioMonotonicNow();
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
                && engine.playbackTime() >= timeBeforeSpeedChanges
                && engine.playbackTime() <= timeBeforeSpeedChanges + 2.0 * static_cast<double>(DesktopSceneEngine::Audio::audioMonotonicNow() - speedChangesBegan) / 1e9 + 0.03,
            "RateContinuous preparations never rewind or add time beyond actual elapsed progression"))
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

bool testRejectedEmbeddedSeekDoesNotCreateTimingAuthority(const std::filesystem::path& videoPath)
{
    using Engine = DesktopSceneEngine::Engine;
    const EntityId sceneId{std::string{"short-audio-video-scene"}};
    const auto project = makeMediaProject(videoPath, sceneId,
        EntityId{std::string{"short-audio-video-media"}}, 0.0F);
    Tests::AwaitAudioEngine engine;
    engine.initialize();
    if (!require(engine.bindProject(project, sceneId) && engine.effectiveDuration() > 2,
        "Precommit rejection fixture binds a real video with a valid later frame")) return false;
    engine.togglePause();
    if (!require(engine.isPaused() && engine.seek(0), "Short embedded fixture is positioned while paused")) return false;
    auto& control = AudioOutputRecoveryEngineTestAccess::control(engine);
    // This fixture substitutes a different real PCM asset under the existing
    // video clock. Retire the old lease first: same-binding replacement otherwise
    // deliberately reuses its immutable original for tempo preparation.
    if (!require(Tests::effect(engine, [&] {
        control.retire(AudioLane::Embedded); return true;
    }) && !Tests::lane(engine, AudioLane::Embedded),
        "Old video PCM fully retires before the fixture substitutes a different real short asset")) return false;
    const auto shortPcm = std::filesystem::path{DESKTOP_SCENE_ENGINE_SEQUENTIAL_AUDIO_TEST_DATA_DIR}
        / "SequentialAudioSourceStereo48k.wav";
    if (!require(Tests::effect(engine, [&] {
        AudioOutputRecoveryEngineTestAccess::prepareEmbedded(engine, shortPcm); return true;
    }) && Tests::lane(engine, AudioLane::Embedded), "Real prepared short PCM replaces the video embedded lane")) return false;
    const auto source = Tests::lane(engine, AudioLane::Embedded)->source;
    const auto view = control.preparation.view(source);
    std::cerr << "Short-PCM fixture source=" << source.sourceToken
        << " path=" << shortPcm << " actual-duration=" << (view ? view->durationSeconds : -1)
        << " sample-count=" << (view ? view->sampleCount : 0) << '\n';
    if (!require(view && view->durationSeconds < 1,
        "The real embedded PCM ends before the valid one-second video target")) return false;
    if (!require(Tests::settle(engine), "Prior embedded replacement fully drains before the next admission")) return false;
    const auto before = control.clock;
    const auto confirmed = control.lastConfirmedPosition;
    const auto operation = engine.beginAudioEffect(Engine::AudioEffectScope::Structural);
    if (!require(operation.has_value(), "Rejected seek reserves a real structural operation")) return false;
    const bool admitted = engine.Engine::seek(1.0F);
    (void)engine.finishAudioEffect(*operation, admitted);
    if (!require(admitted && Tests::waitUntil(engine, [&] { return engine.audioEffect(*operation).terminal(); })
        && Tests::settle(engine) && engine.audioEffect(*operation).phase == Engine::AudioEffectPhase::Failed,
        "Valid video target is admitted by C and rejected by A before shorter-PCM seek commit")) return false;
    const auto applied = Tests::audioState(engine).clock;
    const auto survivingSource=Tests::lane(engine,AudioLane::Embedded);
    if (!require(control.clock.binding == before.binding && control.clock.timingVersion == before.timingVersion
        && control.clock.position == before.position && control.clock.time == before.time
        && applied.binding == before.binding && applied.timingVersion == before.timingVersion
        && applied.position == before.position && applied.time == before.time
        && control.lastConfirmedPosition == confirmed && control.plannedClock.timingVersion == before.timingVersion
        && control.plannedClock.position == before.position && survivingSource && survivingSource->source == source,
        "Precommit failure preserves actual C/A anchor, confirmed position, source identity and future admission authority")) return false;
    if (!require(engine.seek(0) && committedSeekAnchorIs(engine, 0),
        "A later valid seek succeeds after rejection instead of failing against a phantom future version")) return false;
    engine.togglePause();
    if (!require(!engine.isPaused(), "Resume remains admissible after rejected seek")) return false;
    engine.togglePause();
    return require(engine.isPaused() && Tests::audioState(engine).transport == DesktopSceneEngine::Audio::AudioTransport::Paused
        && control.clock.mode == DesktopSceneEngine::Audio::AudioClockMode::Paused,
        "A subsequent Pause commits through the same actual timing authority");
}

bool testBindingClockWaitsForActualAudioEffect(const std::filesystem::path& imagePath)
{
    using Engine=DesktopSceneEngine::Engine;
    const EntityId sceneId{std::string{"blocked-bind-scene"}};
    const auto project=makeMediaProject(imagePath,sceneId,EntityId{std::string{"blocked-bind-media"}},10,1.25F);
    Tests::AwaitAudioEngine engine;engine.initialize();
    if (!require(engine.processBackgroundMusicControl(makePlaylistBgmStartEvent())
        && Tests::lane(engine,AudioLane::PlaylistBgm),"A real prepared BGM source makes the native write boundary reachable"))return false;
    struct ReleaseWrite
    {
        Tests::AudioCaptureDevice& device;
        ~ReleaseWrite(){device.releaseWrite();}
    } release{engine.device};
    // Model an empty device buffer, so the render horizon admits a write.
    // A tiny writable credit instead represents padding above that horizon.
    engine.device.armBlockedWrite();engine.device.grant(engine.device.bufferFrames());
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{2};
    while(!engine.device.blockedWriteEntered.load(std::memory_order_acquire)&&std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    if(!require(engine.device.blockedWriteEntered.load()&&!engine.device.blockedWriteTimedOut.load(),
        "A is deterministically inside the bounded external native-write call before Bind admission"))return false;
    auto& control=AudioOutputRecoveryEngineTestAccess::control(engine);
    const auto oldConfirmed=control.clock;
    const auto operation=engine.beginAudioEffect(Engine::AudioEffectScope::Structural);
    if(!require(operation.has_value(),"Bind reserves capacity while A is in the external device call"))return false;
    const auto admittedAt=DesktopSceneEngine::Audio::audioMonotonicNow();
    const bool admitted=engine.Engine::bindProject(project,sceneId);
    (void)engine.finishAudioEffect(*operation,admitted);
    const auto first=engine.playbackClockSample();
    std::this_thread::sleep_for(std::chrono::milliseconds{200});
    const auto held=engine.playbackClockSample();
    if(!require(admitted&&engine.audioEffect(*operation).pending()
        && control.clock.binding==oldConfirmed.binding&&control.clock.timingVersion==oldConfirmed.timingVersion
        && control.clock.time==oldConfirmed.time&&control.clock.position==oldConfirmed.position
        && control.plannedClock.binding!=oldConfirmed.binding
        && first.position==0&&held.position==0&&first.rate==0&&held.rate==0
        && engine.playbackTime()==0&&engine.playbackSpeed()==0
        && held.asOfTimeNs>=first.asOfTimeNs&&!engine.device.blockedWriteTimedOut.load(),
        "Pending new Bind exposes no Running rate or elapsed position and does not overwrite the confirmed clock before A ACK"))return false;
    const auto releasedAt=DesktopSceneEngine::Audio::audioMonotonicNow();engine.device.releaseWrite();
    if(!require(Tests::waitUntil(engine,[&]{return engine.audioEffect(*operation).terminal();})&&Tests::settle(engine)
        && engine.audioEffect(*operation).phase==Engine::AudioEffectPhase::Applied,
        "Releasing the real A device boundary allows the queued Bind to apply"))return false;
    const auto actual=control.clock;
    const auto receipt=engine.audioEffect(*operation);
    return require(actual.binding==control.binding&&actual.binding!=oldConfirmed.binding
        && actual.position==0&&actual.rate==1.25&&actual.time>=releasedAt&&actual.time>admittedAt
        && actual.time==receipt.effectTimeNs&&engine.playbackTime()>=held.position
        && readMatchesNativeClock(engine,actual)&&!engine.device.blockedWriteTimedOut.load(),
        "New anchor starts at the true post-release A effect time, with no admission-elapsed progression or ACK-time rollback");
}

std::string aviWord(std::uint32_t value)
{
    std::string bytes;for(unsigned shift=0;shift<32;shift+=8)bytes.push_back(static_cast<char>((value>>shift)&0xff));return bytes;
}
std::string aviChunk(const char* tag,const std::string& payload)
{
    std::string bytes{tag,4};bytes+=aviWord(static_cast<std::uint32_t>(payload.size()));bytes+=payload;
    if(payload.size()%2)bytes.push_back('\0');return bytes;
}
struct OwnedSilentAvi
{
    std::filesystem::path directory=std::filesystem::temp_directory_path()
        /("DSE-CP1-SilentVideo-"+std::to_string(DesktopSceneEngine::Audio::audioMonotonicNow()));
    std::filesystem::path path=directory/"owned-silent.avi";
    bool owned=false;
    ~OwnedSilentAvi(){if(owned){std::error_code error;std::filesystem::remove(path,error);std::filesystem::remove(directory,error);}}
    bool write()
    {
        if(!std::filesystem::create_directory(directory))return false;owned=true;
        constexpr unsigned width=64,height=36,fps=10,frames=30,frameBytes=width*height*3;
        std::string main;for(const auto value:{100000U,frameBytes*fps,0U,0x10U,frames,0U,1U,frameBytes,width,height,0U,0U,0U,0U})main+=aviWord(value);
        std::string video{"vidsDIB ",8};for(const auto value:{0U,0U,0U,1U,fps,0U,frames,frameBytes,0xffffffffU,0U})video+=aviWord(value);
        video+=std::string(4,'\0');video.push_back(static_cast<char>(width));video.push_back('\0');video.push_back(static_cast<char>(height));video.push_back('\0');
        std::string format;for(const auto value:{40U,width,height,0x00180001U,0U,frameBytes,0U,0U,0U,0U})format+=aviWord(value);
        const auto header=aviChunk("LIST",std::string{"hdrl",4}+aviChunk("avih",main)
            +aviChunk("LIST",std::string{"strl",4}+aviChunk("strh",video)+aviChunk("strf",format)));
        std::string movie{"movi",4},index;
        for(unsigned frame=0;frame<frames;++frame)
        {
            std::string pixels(frameBytes,'\0');for(unsigned pixel=0;pixel<frameBytes;++pixel)pixels[pixel]=static_cast<char>((pixel+frame*7)%256);
            index+=std::string{"00db",4}+aviWord(0x10)+aviWord(static_cast<std::uint32_t>(movie.size()))+aviWord(frameBytes);
            movie+=aviChunk("00db",pixels);
        }
        const auto bytes=aviChunk("RIFF",std::string{"AVI ",4}+header+aviChunk("LIST",movie)+aviChunk("idx1",index));
        std::ofstream output{path,std::ios::binary};output.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));return output.good();
    }
};

bool testSilentVideoOptionalPreparationAndRate()
{
    using Engine=DesktopSceneEngine::Engine;
    OwnedSilentAvi media;if(!require(media.write(),"Own a real valid single-stream AVI fixture"))return false;
    {
        DesktopSceneEngine::Resources::VideoResource video;
        DesktopSceneEngine::Resources::AudioResource audio;
        if(!require(video.open(media.path)&&video.frameAt(0)&&!audio.open(media.path),
            "Owned AVI really decodes video and has no decodable audio stream"))return false;
    }
    const EntityId sceneId{std::string{"silent-video-scene"}};
    const auto project=makeMediaProject(media.path,sceneId,EntityId{std::string{"silent-video-media"}},0);
    Tests::AwaitAudioEngine engine;engine.initialize();
    const auto bind=engine.beginAudioEffect(Engine::AudioEffectScope::Structural);
    if(!require(bind.has_value(),"Silent video Bind admits its optional real audio preparation"))return false;
    const bool admitted=engine.Engine::bindProject(project,sceneId);(void)engine.finishAudioEffect(*bind,admitted);
    if(!require(admitted&&Tests::waitUntil(engine,[&]{return engine.audioEffect(*bind).terminal();})&&Tests::settle(engine)
        && engine.audioEffect(*bind).phase==Engine::AudioEffectPhase::Applied&&!Tests::lane(engine,AudioLane::Embedded),
        "Optional no-audio preparation leaves the valid visual Bind Applied, not Cancelled or Failed"))return false;
    auto& control=AudioOutputRecoveryEngineTestAccess::control(engine);
    if(!require(control.embeddedPath.empty(),"A current silent binding retains no decoder path to retry"))return false;
    const auto sourceCounter=control.nextSource;
    const auto rate=engine.beginAudioEffect(Engine::AudioEffectScope::Structural);
    if(!require(rate.has_value(),"Optional unused reservation was released for a later rate operation"))return false;
    engine.Engine::setPlaybackSpeed(1.25F);(void)engine.finishAudioEffect(*rate);
    if(!require(Tests::waitUntil(engine,[&]{return engine.audioEffect(*rate).terminal();})&&Tests::settle(engine)
        && engine.audioEffect(*rate).phase==Engine::AudioEffectPhase::Applied
        && engine.playbackSpeed()==1.25F&&engine.playbackClockSample().rate==1.25
        && control.nextSource==sourceCounter&&control.embeddedPath.empty()&&!Tests::lane(engine,AudioLane::Embedded),
        "Silent video's1.25x rate physically applies without another doomed P decode or an inherited old path"))return false;
    return true;
}

}

int main(int argc, char* argv[])
{
    if (argc == 2 && std::string_view{argv[1]} == "--generated")
        return testZeroDurationSeek() && testSilentVideoOptionalPreparationAndRate() ? 0 : 1;
    const std::filesystem::path resourceRoot{
        DESKTOP_SCENE_ENGINE_TEST_RESOURCES_DIR};
    if (resourceRoot.empty())
    {
        std::cerr << "External seek media unavailable; use --generated for the public cases.\n";
        return 77;
    }
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
            && testRejectedEmbeddedSeekDoesNotCreateTimingAuthority(videos.front())
            && testBindingClockWaitsForActualAudioEffect(images.front())
            && testSilentVideoOptionalPreparationAndRate()
        ? 0 : 1;
}
