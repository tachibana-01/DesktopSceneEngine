#include <DesktopSceneEngine/BackgroundMusicControl.h>
#include <DesktopSceneEngine/Domain/Playback.h>
#include <DesktopSceneEngine/Domain/Project.h>
#include <DesktopSceneEngine/Engine.h>

#include "Audio/AudioMixer.h"
#include "Core/AudioOutputRecoveryTestHooks.h"
#include "Core/SharedAudioOutputRecoveryPolicy.h"
#include "Core/SharedAudioOutputPolicy.h"
#include "Music/BackgroundMusicExecutor.h"
#include "Music/BackgroundMusicRuntime.h"
#include "Resources/AudioResource.h"
#include "Runtime/Playback/PlaybackRuntime.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace DesktopSceneEngine::Core
{

class AudioOutputRecoveryEngineTestAccess final
{
public:
    static void preparePlayingEngine(Engine& engine, const Domain::Project& project)
    {
        engine.initialized_ = true;
        engine.projectBound_ = true;
        engine.boundProject_ = &project;
        engine.playbackRuntime_ = std::make_unique<Runtime::PlaybackRuntime>();

        Domain::Playback playback{};
        playback.speed = 1.0F;
        playback.loop = true;
        engine.playbackRuntime_->configure(playback, 600.0F);
    }

    static void installHooks(Engine& engine, AudioOutputRecoveryTestHooks hooks)
    {
        engine.audioOutputRecoveryTestHooks_ = std::make_unique<AudioOutputRecoveryTestHooks>(
            std::move(hooks));
    }

    [[nodiscard]] static Audio::AudioMixer& mixer(Engine& engine) noexcept
    {
        return *engine.audioMixer_;
    }

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

    [[nodiscard]] static bool configureEmbeddedAudio(
        Engine& engine,
        const std::filesystem::path& path
    )
    {
        auto resource = std::make_unique<Resources::AudioResource>();
        if (!resource->open(path) || !resource->setTempo(1.0F))
        {
            return false;
        }

        engine.audioResource_ = std::move(resource);
        return engine.configureEmbeddedAudioInputAt(
            engine.playbackRuntime_->currentTime(),
            engine.playbackRuntime_->configuration().speed);
    }

    [[nodiscard]] static std::uint64_t embeddedInputHandle(const Engine& engine) noexcept
    {
        return engine.embeddedAudioInputHandleValue_;
    }

    [[nodiscard]] static SharedAudioOutputState sharedOutputState(
        const Engine& engine
    ) noexcept
    {
        return static_cast<SharedAudioOutputState>(engine.sharedAudioOutputState_);
    }

    [[nodiscard]] static AudioOutputOperationalState operationalState(
        const Engine& engine
    ) noexcept
    {
        return engine.audioOutputOperationalState_;
    }
};

}

namespace
{

using DesktopSceneEngine::BackgroundMusicControl::ActiveScopeSnapshot;
using DesktopSceneEngine::BackgroundMusicControl::ActivePlaybackScopeChanged;
using DesktopSceneEngine::BackgroundMusicControl::Event;
using DesktopSceneEngine::BackgroundMusicControl::ItemId;
using DesktopSceneEngine::BackgroundMusicControl::ItemMusicConfigChanged;
using DesktopSceneEngine::BackgroundMusicControl::PlaybackPaused;
using DesktopSceneEngine::BackgroundMusicControl::PlaybackResumed;
using DesktopSceneEngine::BackgroundMusicControl::PlaybackSessionStarted;
using DesktopSceneEngine::BackgroundMusicControl::PlaybackStopped;
using DesktopSceneEngine::BackgroundMusicControl::PlaylistId;
using DesktopSceneEngine::BackgroundMusicControl::PlaylistMusicConfigChanged;
using DesktopSceneEngine::BackgroundMusicControl::QueueMode;
using DesktopSceneEngine::BackgroundMusicControl::QueueSnapshot;
using DesktopSceneEngine::BackgroundMusicControl::TrackId;
using DesktopSceneEngine::BackgroundMusicControl::TrackSnapshot;
using DesktopSceneEngine::Audio::AudioMixStatus;
using DesktopSceneEngine::Audio::MixedOutputBridgeResult;
using DesktopSceneEngine::Audio::MixedOutputBridgeStatus;
using DesktopSceneEngine::Core::AudioOutputRecoveryEngineTestAccess;
using DesktopSceneEngine::Core::AudioOutputRecoveryTestHooks;
using DesktopSceneEngine::Core::AudioOutputOperationalState;
using DesktopSceneEngine::Core::SharedAudioOutputState;
using DesktopSceneEngine::Music::MusicSession;
using DesktopSceneEngine::Music::MusicSessionPhase;

constexpr std::size_t StereoChannels = 2U;
constexpr std::size_t PumpFrames = 16U;
constexpr float Tolerance = 0.0001F;

bool require(bool condition, const char* message)
{
    if (condition)
    {
        return true;
    }

    std::cerr << "Test failed: " << message << std::endl;
    return false;
}

bool approximatelyEqual(float left, float right)
{
    return std::fabs(left - right) <= Tolerance;
}

bool approximatelyEqual(
    const std::vector<float>& left,
    const std::vector<float>& right
)
{
    if (left.size() != right.size())
    {
        return false;
    }

    for (std::size_t index = 0U; index < left.size(); ++index)
    {
        if (!approximatelyEqual(left[index], right[index]))
        {
            return false;
        }
    }

    return true;
}

std::filesystem::path fixturePath()
{
    return std::filesystem::path{DESKTOP_SCENE_ENGINE_SEQUENTIAL_AUDIO_TEST_DATA_DIR}
        / "SequentialAudioSourceStereo48k.wav";
}

QueueSnapshot queue(
    std::vector<TrackSnapshot> tracks,
    double queueVolume = 1.0,
    QueueMode mode = QueueMode::ListLoop
)
{
    return QueueSnapshot{std::move(tracks), mode, queueVolume};
}

TrackSnapshot track(
    std::string id,
    std::filesystem::path source = fixturePath(),
    double volume = 1.0
)
{
    return TrackSnapshot{TrackId{std::move(id)}, std::move(source), volume};
}

ActiveScopeSnapshot scope(
    std::string playlistId,
    QueueSnapshot playlistQueue,
    std::optional<ItemId> itemId = std::nullopt,
    std::optional<QueueSnapshot> itemQueue = std::nullopt
)
{
    return ActiveScopeSnapshot{
        PlaylistId{std::move(playlistId)},
        std::move(playlistQueue),
        std::move(itemId),
        std::move(itemQueue)};
}

const MusicSession* playlistSession(const DesktopSceneEngine::Engine& engine) noexcept
{
    return AudioOutputRecoveryEngineTestAccess::runtime(engine).playlistSession();
}

const MusicSession* itemSession(const DesktopSceneEngine::Engine& engine) noexcept
{
    return AudioOutputRecoveryEngineTestAccess::runtime(engine).itemSession();
}

std::optional<DesktopSceneEngine::Music::MusicSourceToken> playlistToken(
    const DesktopSceneEngine::Engine& engine
) noexcept
{
    const MusicSession* const session = playlistSession(engine);
    return session == nullptr ? std::nullopt : session->sourceToken();
}

std::optional<DesktopSceneEngine::Music::MusicSourceToken> itemToken(
    const DesktopSceneEngine::Engine& engine
) noexcept
{
    const MusicSession* const session = itemSession(engine);
    return session == nullptr ? std::nullopt : session->sourceToken();
}

bool mixFrames(DesktopSceneEngine::Engine& engine, std::vector<float>& output)
{
    const auto result = AudioOutputRecoveryEngineTestAccess::mixer(engine).mixFrames(output);
    return result.status == AudioMixStatus::Success
        && result.framesProduced == output.size() / StereoChannels;
}

struct ScriptedOutput
{
    DesktopSceneEngine::Engine* engine = nullptr;
    bool mixOnPump = true;
    std::vector<MixedOutputBridgeStatus> queuedPumpStatuses{};
    std::size_t initializeCalls = 0U;
    std::size_t shutdownCalls = 0U;
    std::size_t beginCalls = 0U;
    std::size_t pauseCalls = 0U;
    std::size_t resumeCalls = 0U;
    std::size_t pumpCalls = 0U;
    std::size_t mixedBlocks = 0U;
    std::uint64_t endpointGeneration = 0U;

    [[nodiscard]] AudioOutputRecoveryTestHooks hooks()
    {
        AudioOutputRecoveryTestHooks result{};
        result.initialize = [this]()
        {
            ++initializeCalls;
            return true;
        };
        result.shutdown = [this]()
        {
            ++shutdownCalls;
        };
        result.beginBlockMode = [this](const auto)
        {
            ++beginCalls;
            return DesktopSceneEngine::Audio::AudioBlockStatus::Success;
        };
        result.pause = [this]()
        {
            ++pauseCalls;
            return DesktopSceneEngine::Audio::AudioBlockStatus::Success;
        };
        result.resume = [this]()
        {
            ++resumeCalls;
            return DesktopSceneEngine::Audio::AudioBlockStatus::Success;
        };
        result.pump = [this]()
        {
            ++pumpCalls;
            const MixedOutputBridgeStatus status = queuedPumpStatuses.empty()
                ? MixedOutputBridgeStatus::WroteFrames
                : queuedPumpStatuses.front();
            if (!queuedPumpStatuses.empty())
            {
                queuedPumpStatuses.erase(queuedPumpStatuses.begin());
            }
            if (status != MixedOutputBridgeStatus::WroteFrames || !mixOnPump || engine == nullptr)
            {
                return MixedOutputBridgeResult{0U, status};
            }

            std::vector<float> output(PumpFrames * StereoChannels, 0.0F);
            const auto mixed = AudioOutputRecoveryEngineTestAccess::mixer(*engine).mixFrames(output);
            if (mixed.status != AudioMixStatus::Success
                || mixed.framesProduced != PumpFrames)
            {
                return MixedOutputBridgeResult{
                    0U,
                    MixedOutputBridgeStatus::MixerContractFailureSilenced};
            }

            ++mixedBlocks;
            return MixedOutputBridgeResult{PumpFrames, MixedOutputBridgeStatus::WroteFrames};
        };
        result.defaultAudioEndpointGeneration = [this]()
        {
            return endpointGeneration;
        };
        return result;
    }
};

void prepareEngine(
    DesktopSceneEngine::Engine& engine,
    const DesktopSceneEngine::Domain::Project& project,
    ScriptedOutput& output
)
{
    AudioOutputRecoveryEngineTestAccess::preparePlayingEngine(engine, project);
    output.engine = &engine;
    AudioOutputRecoveryEngineTestAccess::installHooks(engine, output.hooks());
}

bool pausePlaybackAndMusic(DesktopSceneEngine::Engine& engine)
{
    engine.togglePause();
    return engine.isPaused()
        && engine.processBackgroundMusicControl(PlaybackPaused{});
}

bool resumePlaybackAndMusic(DesktopSceneEngine::Engine& engine)
{
    engine.togglePause();
    return !engine.isPaused()
        && engine.processBackgroundMusicControl(PlaybackResumed{});
}

class ScopedCorruptFixture final
{
public:
    ScopedCorruptFixture()
        : path_(std::filesystem::temp_directory_path()
            / "DesktopSceneEngine-A4-9a-CorruptAudioFixture.wav")
    {
        std::ofstream output(path_, std::ios::binary | std::ios::trunc);
        const char invalidBytes[] = {'n', 'o', 't', '-', 'a', '-', 'w', 'a', 'v'};
        output.write(invalidBytes, static_cast<std::streamsize>(sizeof(invalidBytes)));
    }

    ~ScopedCorruptFixture()
    {
        std::error_code error;
        std::filesystem::remove(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

private:
    std::filesystem::path path_{};
};

bool verifyItemLifecycleAndSameItemLoop()
{
    DesktopSceneEngine::Domain::Project project{};
    ScriptedOutput output{};
    DesktopSceneEngine::Engine engine{};
    prepareEngine(engine, project, output);

    const QueueSnapshot itemQueue = queue({track("item-a-track")}, 1.0);
    if (!require(engine.processBackgroundMusicControl(PlaybackSessionStarted{scope(
                    "playlist",
                    queue({}),
                    ItemId{"item-a"},
                    itemQueue)}),
            "An Item-only background-music session must start through the real Executor"))
    {
        return false;
    }

    const auto originalToken = itemToken(engine);
    if (!require(originalToken.has_value()
            && AudioOutputRecoveryEngineTestAccess::executor(engine).hasActiveSources()
            && output.mixedBlocks > 0U,
        "Item-only start must register and pull a real source through the shared Mixer"))
    {
        return false;
    }

    const std::size_t mixedBeforePause = output.mixedBlocks;
    if (!require(pausePlaybackAndMusic(engine)
            && itemToken(engine) == originalToken,
        "Pause must preserve the Item source identity"))
    {
        return false;
    }

    engine.update(0.25F);
    if (!require(output.mixedBlocks == mixedBeforePause,
            "Paused Engine updates must not pull the Item BGM source"))
    {
        return false;
    }

    if (!require(resumePlaybackAndMusic(engine)
            && itemToken(engine) == originalToken,
        "Resume must preserve the paused Item source identity"))
    {
        return false;
    }
    engine.update(0.25F);
    if (!require(output.mixedBlocks > mixedBeforePause,
            "Resumed Engine updates must continue pulling the same Item source"))
    {
        return false;
    }

    if (!require(engine.processBackgroundMusicControl(ActivePlaybackScopeChanged{scope(
                    "playlist",
                    queue({}),
                    ItemId{"item-a"},
                    itemQueue)})
            && itemToken(engine) == originalToken,
        "A same-Item visual loop must not restart Item BGM"))
    {
        return false;
    }

    if (!require(engine.processBackgroundMusicControl(PlaybackStopped{})
            && !AudioOutputRecoveryEngineTestAccess::executor(engine).hasActiveSources(),
        "Stop must remove the Item BGM registration"))
    {
        return false;
    }

    engine.update(2.0F);
    if (!require(!AudioOutputRecoveryEngineTestAccess::executor(engine).hasActiveSources(),
            "Stopped Item BGM must not resurrect on later updates"))
    {
        return false;
    }

    if (!require(engine.processBackgroundMusicControl(PlaybackSessionStarted{scope(
                    "playlist",
                    queue({}),
                    ItemId{"item-a"},
                    itemQueue)}),
            "Replay must start a fresh Item session"))
    {
        return false;
    }

    const auto replayToken = itemToken(engine);
    return require(replayToken.has_value() && replayToken != originalToken,
        "Replay must allocate a fresh non-reused Item BGM token");
}

bool verifyPlaylistItemCompositionAndScopeReplacement()
{
    const QueueSnapshot playlistQueue = queue({track("playlist-track", fixturePath(), 0.5)}, 0.5);
    const QueueSnapshot itemQueue = queue({track("item-a-track", fixturePath(), 0.5)}, 0.4);

    DesktopSceneEngine::Domain::Project project{};
    ScriptedOutput output{};
    output.mixOnPump = false;
    DesktopSceneEngine::Engine combined{};
    prepareEngine(combined, project, output);

    if (!require(combined.processBackgroundMusicControl(PlaybackSessionStarted{scope(
                    "playlist",
                    playlistQueue,
                    ItemId{"item-a"},
                    itemQueue)}),
            "Playlist and Item BGM must start together through the real Executor"))
    {
        return false;
    }

    const auto originalPlaylistToken = playlistToken(combined);
    const auto originalItemToken = itemToken(combined);
    if (!require(originalPlaylistToken.has_value()
            && originalItemToken.has_value()
            && originalPlaylistToken != originalItemToken,
        "Simultaneous Playlist and Item sessions must own distinct source tokens"))
    {
        return false;
    }

    DesktopSceneEngine::Domain::Project playlistProject{};
    ScriptedOutput playlistOutput{};
    playlistOutput.mixOnPump = false;
    DesktopSceneEngine::Engine playlistOnly{};
    prepareEngine(playlistOnly, playlistProject, playlistOutput);
    if (!require(playlistOnly.processBackgroundMusicControl(PlaybackSessionStarted{scope(
                    "playlist",
                    playlistQueue)}),
            "Playlist-only comparison setup must start"))
    {
        return false;
    }

    DesktopSceneEngine::Domain::Project itemProject{};
    ScriptedOutput itemOutput{};
    itemOutput.mixOnPump = false;
    DesktopSceneEngine::Engine itemOnly{};
    prepareEngine(itemOnly, itemProject, itemOutput);
    if (!require(itemOnly.processBackgroundMusicControl(PlaybackSessionStarted{scope(
                    "playlist",
                    queue({}),
                    ItemId{"item-a"},
                    itemQueue)}),
            "Item-only comparison setup must start"))
    {
        return false;
    }

    std::vector<float> combinedSamples(StereoChannels, 0.0F);
    std::vector<float> playlistSamples(StereoChannels, 0.0F);
    std::vector<float> itemSamples(StereoChannels, 0.0F);
    if (!require(mixFrames(combined, combinedSamples)
            && mixFrames(playlistOnly, playlistSamples)
            && mixFrames(itemOnly, itemSamples),
        "Composition setup must mix real Playlist and Item source samples"))
    {
        return false;
    }

    const std::vector<float> expected{
        playlistSamples[0U] + itemSamples[0U],
        playlistSamples[1U] + itemSamples[1U]};
    if (!require(approximatelyEqual(combinedSamples, expected),
            "Playlist and Item queue/track gains must accumulate independently without ducking"))
    {
        return false;
    }

    const QueueSnapshot itemBQueue = queue({track("item-b-track", fixturePath(), 0.8)}, 0.25);
    if (!require(combined.processBackgroundMusicControl(ActivePlaybackScopeChanged{scope(
                    "playlist",
                    playlistQueue,
                    ItemId{"item-b"},
                    itemBQueue)}),
            "Item A to B scope replacement must be admitted")
        || !require(playlistToken(combined) == originalPlaylistToken,
            "Changing Items must preserve the Playlist BGM token")
        || !require(itemToken(combined).has_value() && itemToken(combined) != originalItemToken,
            "Changing Items must replace only the Item BGM token"))
    {
        return false;
    }

    const auto itemBToken = itemToken(combined);
    if (!require(combined.processBackgroundMusicControl(ActivePlaybackScopeChanged{scope(
                    "playlist",
                    playlistQueue,
                    ItemId{"item-a"},
                    itemQueue)}),
            "Item B to A scope replacement must be admitted")
        || !require(playlistToken(combined) == originalPlaylistToken,
            "Playlist BGM must remain continuous across Item A to B to A")
        || !require(itemToken(combined).has_value()
            && itemToken(combined) != originalItemToken
            && itemToken(combined) != itemBToken,
            "The second Item A session must be fresh with no stale Item token"))
    {
        return false;
    }

    const auto gainToken = playlistToken(combined);
    const QueueSnapshot zeroPlaylistGain = queue({track("playlist-track", fixturePath(), 0.5)}, 0.0);
    if (!require(combined.processBackgroundMusicControl(
                    PlaylistMusicConfigChanged{PlaylistId{"playlist"}, zeroPlaylistGain})
            && playlistToken(combined) == gainToken,
        "Changing Playlist gain to zero must keep the source registered"))
    {
        return false;
    }

    std::vector<float> mutedPlaylistContribution(StereoChannels, 0.0F);
    if (!require(mixFrames(combined, mutedPlaylistContribution),
            "Zero-gain source must still be pulled by the real Mixer"))
    {
        return false;
    }

    if (!require(combined.processBackgroundMusicControl(PlaylistMusicConfigChanged{
                    PlaylistId{"playlist"}, playlistQueue})
            && playlistToken(combined) == gainToken,
        "Restoring Playlist gain must not restart its source"))
    {
        return false;
    }

    std::vector<float> restoredPlaylistContribution(StereoChannels, 0.0F);
    return require(mixFrames(combined, restoredPlaylistContribution)
            && (std::fabs(restoredPlaylistContribution[0U]) > Tolerance
                || std::fabs(restoredPlaylistContribution[1U]) > Tolerance),
        "Zero-to-positive gain must restore audible contribution without source replacement");
}

bool verifyEmbeddedPlaylistItemCompositionAndLifecycle()
{
    const QueueSnapshot playlistQueue = queue({track("playlist-track", fixturePath(), 0.25)}, 0.5);
    const QueueSnapshot itemQueue = queue({track("item-track", fixturePath(), 0.5)}, 0.25);

    DesktopSceneEngine::Domain::Project embeddedProject{};
    ScriptedOutput embeddedOutput{};
    embeddedOutput.mixOnPump = false;
    DesktopSceneEngine::Engine embeddedOnly{};
    prepareEngine(embeddedOnly, embeddedProject, embeddedOutput);
    if (!require(AudioOutputRecoveryEngineTestAccess::configureEmbeddedAudio(
                    embeddedOnly,
                    fixturePath()),
            "Committed WAV fixture must configure a real embedded-audio input"))
    {
        return false;
    }

    DesktopSceneEngine::Domain::Project bgmProject{};
    ScriptedOutput bgmOutput{};
    bgmOutput.mixOnPump = false;
    DesktopSceneEngine::Engine bgmOnly{};
    prepareEngine(bgmOnly, bgmProject, bgmOutput);
    if (!require(bgmOnly.processBackgroundMusicControl(PlaybackSessionStarted{scope(
                    "playlist",
                    playlistQueue,
                    ItemId{"item"},
                    itemQueue)}),
            "Playlist and Item BGM comparison setup must start"))
    {
        return false;
    }

    DesktopSceneEngine::Domain::Project project{};
    ScriptedOutput output{};
    output.mixOnPump = false;
    DesktopSceneEngine::Engine allThree{};
    prepareEngine(allThree, project, output);
    if (!require(AudioOutputRecoveryEngineTestAccess::configureEmbeddedAudio(allThree, fixturePath())
            && allThree.processBackgroundMusicControl(PlaybackSessionStarted{scope(
                "playlist",
                playlistQueue,
                ItemId{"item"},
                itemQueue)}),
        "Embedded, Playlist, and Item sources must all configure through Engine"))
    {
        return false;
    }

    const auto playlistTokenBefore = playlistToken(allThree);
    const auto itemTokenBefore = itemToken(allThree);
    const std::uint64_t embeddedHandleBefore =
        AudioOutputRecoveryEngineTestAccess::embeddedInputHandle(allThree);
    if (!require(embeddedHandleBefore != 0U
            && playlistTokenBefore.has_value()
            && itemTokenBefore.has_value()
            && playlistTokenBefore != itemTokenBefore
            && AudioOutputRecoveryEngineTestAccess::executor(allThree).hasActiveSources(),
        "All three producers must have distinct embedded/BGM ownership state"))
    {
        return false;
    }

    std::vector<float> embeddedSamples(StereoChannels, 0.0F);
    std::vector<float> bgmSamples(StereoChannels, 0.0F);
    std::vector<float> allThreeSamples(StereoChannels, 0.0F);
    if (!require(mixFrames(embeddedOnly, embeddedSamples)
            && mixFrames(bgmOnly, bgmSamples)
            && mixFrames(allThree, allThreeSamples),
        "All three producer configurations must mix valid frames"))
    {
        return false;
    }

    const std::vector<float> expected{
        embeddedSamples[0U] + bgmSamples[0U],
        embeddedSamples[1U] + bgmSamples[1U]};
    if (!require(approximatelyEqual(allThreeSamples, expected),
            "Embedded, Playlist, and Item sources must coexist in one shared Mixer path"))
    {
        return false;
    }

    // The committed 48 kHz WAV is deliberately too short for tempo output;
    // exercise the Engine rollback path and ensure it cannot replace BGM.
    allThree.setPlaybackSpeed(2.0F);
    const std::uint64_t embeddedHandleAfterSpeed =
        AudioOutputRecoveryEngineTestAccess::embeddedInputHandle(allThree);
    if (!require(approximatelyEqual(allThree.playbackSpeed(), 1.0F)
            && embeddedHandleAfterSpeed != 0U
            && embeddedHandleAfterSpeed != embeddedHandleBefore
            && playlistToken(allThree) == playlistTokenBefore
            && itemToken(allThree) == itemTokenBefore,
        "Unsupported embedded speed rollback must preserve both BGM sources"))
    {
        return false;
    }

    output.mixOnPump = true;
    const std::size_t mixedBeforePause = output.mixedBlocks;
    if (!require(pausePlaybackAndMusic(allThree),
            "Three-producer setup must pause through the shared playback path"))
    {
        return false;
    }
    allThree.update(0.25F);
    if (!require(output.mixedBlocks == mixedBeforePause,
            "Paused embedded, Playlist, and Item sources must not be pulled"))
    {
        return false;
    }
    if (!require(resumePlaybackAndMusic(allThree),
            "Three-producer setup must resume through the shared playback path"))
    {
        return false;
    }
    allThree.update(0.25F);
    if (!require(output.mixedBlocks > mixedBeforePause
            && playlistToken(allThree) == playlistTokenBefore
            && itemToken(allThree) == itemTokenBefore,
        "Resuming all three producers must retain BGM identity and source continuity"))
    {
        return false;
    }

    if (!require(allThree.processBackgroundMusicControl(PlaybackStopped{})
            && !AudioOutputRecoveryEngineTestAccess::executor(allThree).hasActiveSources()
            && AudioOutputRecoveryEngineTestAccess::embeddedInputHandle(allThree) == 0U,
        "Stopping a three-producer session must clear embedded and both BGM registrations"))
    {
        return false;
    }

    allThree.update(2.0F);
    return require(!AudioOutputRecoveryEngineTestAccess::executor(allThree).hasActiveSources(),
        "Stopped three-producer state must not schedule delayed BGM resurrection");
}

bool verifyRealExecutorInvalidPathsAndLiveEdits()
{
    const std::filesystem::path missingPath = fixturePath().parent_path() / "does-not-exist.wav";
    DesktopSceneEngine::Domain::Project project{};
    ScriptedOutput output{};
    output.mixOnPump = false;
    DesktopSceneEngine::Engine engine{};
    prepareEngine(engine, project, output);

    const QueueSnapshot validAfterMissing = queue({
        track("missing", missingPath),
        track("valid", fixturePath())});
    if (!require(engine.processBackgroundMusicControl(PlaybackSessionStarted{scope(
                    "playlist",
                    validAfterMissing)}),
            "A real missing-path failure followed by a valid track must be handled"))
    {
        return false;
    }

    const MusicSession* const validSession = playlistSession(engine);
    if (!require(validSession != nullptr
            && validSession->phase() == MusicSessionPhase::Playing
            && validSession->currentTrackId().has_value()
            && validSession->currentTrackId()->value == "valid"
            && validSession->sourceToken().has_value(),
        "A missing track must be skipped boundedly and select the valid real source"))
    {
        return false;
    }

    const auto validToken = validSession->sourceToken();
    const QueueSnapshot replacementQueue = queue({track("replacement", fixturePath())});
    if (!require(engine.processBackgroundMusicControl(
                    PlaylistMusicConfigChanged{PlaylistId{"playlist"}, replacementQueue})
            && playlistSession(engine) != nullptr
            && playlistSession(engine)->currentTrackId().has_value()
            && playlistSession(engine)->currentTrackId()->value == "replacement"
            && playlistToken(engine).has_value()
            && playlistToken(engine) != validToken,
        "Removing the active real source must stop it and start the configured successor"))
    {
        return false;
    }

    const auto replacementToken = playlistToken(engine);
    const QueueSnapshot itemQueue = queue({track("item-track", fixturePath())});
    if (!require(engine.processBackgroundMusicControl(ActivePlaybackScopeChanged{scope(
                    "playlist",
                    replacementQueue,
                    ItemId{"item"},
                    itemQueue)}),
            "Live-remove setup must add a separate active Item BGM scope"))
    {
        return false;
    }
    const auto itemTokenBeforeFinalRemove = itemToken(engine);
    const std::size_t initializeBeforeFinalRemove = output.initializeCalls;
    const std::size_t beginBeforeFinalRemove = output.beginCalls;
    if (!require(engine.processBackgroundMusicControl(
                    PlaylistMusicConfigChanged{PlaylistId{"playlist"}, queue({})})
            && AudioOutputRecoveryEngineTestAccess::executor(engine).hasActiveSources()
            && playlistSession(engine) != nullptr
            && playlistSession(engine)->phase() == MusicSessionPhase::SilentEmptyQueue
            && itemToken(engine) == itemTokenBeforeFinalRemove
            && AudioOutputRecoveryEngineTestAccess::sharedOutputState(engine)
                == SharedAudioOutputState::Running
            && output.initializeCalls == initializeBeforeFinalRemove
            && output.beginCalls == beginBeforeFinalRemove,
        "Removing the final Playlist BGM track must preserve active Item output without churn"))
    {
        return false;
    }

    if (!require(engine.processBackgroundMusicControl(ItemMusicConfigChanged{
                    PlaylistId{"playlist"},
                    ItemId{"item"},
                    queue({})})
            && !AudioOutputRecoveryEngineTestAccess::executor(engine).hasActiveSources()
            && AudioOutputRecoveryEngineTestAccess::sharedOutputState(engine)
                == SharedAudioOutputState::Stopped,
        "Removing the final Item BGM track must leave no active shared-audio registration"))
    {
        return false;
    }

    const QueueSnapshot revivedQueue = queue({track("revived", fixturePath())});
    if (!require(engine.processBackgroundMusicControl(
                    PlaylistMusicConfigChanged{PlaylistId{"playlist"}, revivedQueue})
            && playlistToken(engine).has_value()
            && playlistToken(engine) != replacementToken,
        "Adding a valid track to an active empty queue must revive it with a fresh token"))
    {
        return false;
    }

    DesktopSceneEngine::Domain::Project invalidProject{};
    ScriptedOutput invalidOutput{};
    invalidOutput.mixOnPump = false;
    DesktopSceneEngine::Engine invalidEngine{};
    prepareEngine(invalidEngine, invalidProject, invalidOutput);
    const QueueSnapshot allMissing = queue({
        track("missing-a", missingPath),
        track("missing-b", missingPath)});
    if (!require(invalidEngine.processBackgroundMusicControl(PlaybackSessionStarted{scope(
                    "invalid-playlist",
                    allMissing)}),
            "All-missing queue admission must complete boundedly"))
    {
        return false;
    }
    invalidEngine.update(1.0F);
    if (!require(!AudioOutputRecoveryEngineTestAccess::executor(invalidEngine).hasActiveSources()
            && playlistSession(invalidEngine) != nullptr
            && playlistSession(invalidEngine)->phase() == MusicSessionPhase::NoPlayableTrack
            && invalidOutput.initializeCalls == 0U,
        "All missing tracks must reach NoPlayableTrack without output restart or action looping"))
    {
        return false;
    }

    ScopedCorruptFixture corruptFixture{};
    DesktopSceneEngine::Domain::Project corruptProject{};
    ScriptedOutput corruptOutput{};
    corruptOutput.mixOnPump = false;
    DesktopSceneEngine::Engine corruptEngine{};
    prepareEngine(corruptEngine, corruptProject, corruptOutput);
    if (!require(corruptEngine.processBackgroundMusicControl(PlaybackSessionStarted{scope(
                    "corrupt-playlist",
                    queue({
                        track("corrupt", corruptFixture.path()),
                        track("valid", fixturePath())}))}),
            "A corrupt real file followed by a valid source must be handled"))
    {
        return false;
    }

    return require(playlistSession(corruptEngine) != nullptr
            && playlistSession(corruptEngine)->phase() == MusicSessionPhase::Playing
            && playlistSession(corruptEngine)->currentTrackId().has_value()
            && playlistSession(corruptEngine)->currentTrackId()->value == "valid"
            && AudioOutputRecoveryEngineTestAccess::executor(corruptEngine).hasActiveSources(),
        "A corrupt source must be classified as unplayable and fall back to the valid track");
}

bool verifyPausedLiveEditAndOutputRecoveryWithTwoScopes()
{
    const QueueSnapshot playlistQueue = queue({track("playlist-track")});
    const QueueSnapshot itemQueue = queue({track("item-track")});
    DesktopSceneEngine::Domain::Project project{};
    ScriptedOutput output{};
    DesktopSceneEngine::Engine engine{};
    prepareEngine(engine, project, output);

    if (!require(engine.processBackgroundMusicControl(PlaybackSessionStarted{scope(
                    "playlist",
                    playlistQueue,
                    ItemId{"item"},
                    itemQueue)}),
            "Two-scope recovery setup must start both real BGM sources"))
    {
        return false;
    }

    const auto playlistTokenBefore = playlistToken(engine);
    const auto itemTokenBefore = itemToken(engine);
    if (!require(playlistTokenBefore.has_value() && itemTokenBefore.has_value(),
            "Two-scope recovery setup must expose both tokens"))
    {
        return false;
    }

    if (!require(pausePlaybackAndMusic(engine),
            "Two-scope paused-edit setup must pause"))
    {
        return false;
    }
    const std::size_t mixedBeforeEdit = output.mixedBlocks;
    const QueueSnapshot pausedReplacement = queue({track("item-replacement", fixturePath(), 0.75)}, 0.5);
    if (!require(engine.processBackgroundMusicControl(ItemMusicConfigChanged{
                    PlaylistId{"playlist"},
                    ItemId{"item"},
                    pausedReplacement})
            && itemToken(engine).has_value()
            && itemToken(engine) != itemTokenBefore
            && playlistToken(engine) == playlistTokenBefore,
        "Paused Item live edit must replace only Item BGM semantic state"))
    {
        return false;
    }
    engine.update(0.25F);
    if (!require(output.mixedBlocks == mixedBeforeEdit,
            "Paused live edits must not consume BGM before Resume"))
    {
        return false;
    }

    if (!require(resumePlaybackAndMusic(engine),
            "Two-scope paused-edit setup must resume"))
    {
        return false;
    }
    engine.update(0.25F);
    if (!require(output.mixedBlocks > mixedBeforeEdit,
            "Resumed live-edited BGM must resume source consumption"))
    {
        return false;
    }

    const auto playlistTokenAfterEdit = playlistToken(engine);
    const auto itemTokenAfterEdit = itemToken(engine);
    ++output.endpointGeneration;
    engine.update(0.0F);
    if (!require(playlistToken(engine) == playlistTokenAfterEdit
            && itemToken(engine) == itemTokenAfterEdit
            && output.shutdownCalls == 1U
            && output.initializeCalls == 2U,
        "Endpoint reroute must preserve both BGM tokens in one recovery transaction"))
    {
        return false;
    }

    output.queuedPumpStatuses.push_back(MixedOutputBridgeStatus::DeviceFailure);
    engine.update(0.0F);
    engine.update(0.5F);
    return require(playlistToken(engine) == playlistTokenAfterEdit
            && itemToken(engine) == itemTokenAfterEdit
            && AudioOutputRecoveryEngineTestAccess::executor(engine).hasActiveSources()
            && output.initializeCalls == 3U,
        "DeviceUnavailable recovery must preserve both BGM source identities");
}

bool verifyDeterministicLifecycleSoak()
{
    constexpr std::size_t CycleCount = 300U;
    const QueueSnapshot playlistQueue = queue({
        track("playlist-a"),
        track("playlist-b")});
    const QueueSnapshot itemAQueue = queue({
        track("item-a-1"),
        track("item-a-2")});
    const QueueSnapshot itemBQueue = queue({
        track("item-b-1"),
        track("item-b-2")});

    DesktopSceneEngine::Domain::Project project{};
    ScriptedOutput output{};
    DesktopSceneEngine::Engine engine{};
    prepareEngine(engine, project, output);
    if (!require(engine.processBackgroundMusicControl(PlaybackSessionStarted{scope(
                    "soak-playlist",
                    playlistQueue,
                    ItemId{"item-a"},
                    itemAQueue)}),
            "Soak setup must start both BGM scopes"))
    {
        return false;
    }

    std::optional<DesktopSceneEngine::Music::MusicSourceToken> lastPlaylistToken{};
    std::optional<DesktopSceneEngine::Music::MusicSourceToken> lastItemToken{};
    std::set<std::uint64_t> retiredTokens{};
    const auto observeTokens = [&]()
    {
        const auto observe = [&](const auto token, auto& previous)
        {
            if (!token.has_value() || token->value == 0U)
            {
                return false;
            }
            if (token != previous)
            {
                if (previous.has_value() && !retiredTokens.insert(previous->value).second)
                {
                    return false;
                }
                previous = token;
            }
            return true;
        };
        return observe(playlistToken(engine), lastPlaylistToken)
            && observe(itemToken(engine), lastItemToken)
            && playlistToken(engine) != itemToken(engine);
    };

    if (!require(observeTokens(), "Soak tokens must start non-zero and distinct"))
    {
        return false;
    }

    for (std::size_t cycle = 0U; cycle < CycleCount; ++cycle)
    {
        const QueueSnapshot& activeItemQueue = cycle % 2U == 0U ? itemAQueue : itemBQueue;
        const ItemId activeItemId{cycle % 2U == 0U ? "item-a" : "item-b"};
        bool operationSucceeded = true;

        switch (cycle % 10U)
        {
        case 0U:
            operationSucceeded = engine.processBackgroundMusicControl(
                ActivePlaybackScopeChanged{scope(
                    "soak-playlist",
                    playlistQueue,
                    activeItemId,
                    activeItemQueue)});
            break;

        case 1U:
            operationSucceeded = engine.processBackgroundMusicControl(
                PlaylistMusicConfigChanged{
                    PlaylistId{"soak-playlist"},
                    queue(playlistQueue.tracks, cycle % 4U == 1U ? 0.0 : 0.75)});
            break;

        case 2U:
            operationSucceeded = engine.processBackgroundMusicControl(
                ItemMusicConfigChanged{
                    PlaylistId{"soak-playlist"},
                    activeItemId,
                    queue(activeItemQueue.tracks, cycle % 4U == 2U ? 0.0 : 0.6)});
            break;

        case 3U:
            operationSucceeded = pausePlaybackAndMusic(engine);
            break;

        case 4U:
            operationSucceeded = resumePlaybackAndMusic(engine);
            break;

        case 5U:
            ++output.endpointGeneration;
            break;

        case 6U:
            output.queuedPumpStatuses.push_back(MixedOutputBridgeStatus::DeviceFailure);
            break;

        case 7U:
            operationSucceeded = engine.processBackgroundMusicControl(PlaybackStopped{})
                && engine.processBackgroundMusicControl(PlaybackSessionStarted{scope(
                    "soak-playlist",
                    playlistQueue,
                    activeItemId,
                    activeItemQueue)});
            break;

        case 8U:
            operationSucceeded = engine.processBackgroundMusicControl(
                PlaylistMusicConfigChanged{
                    PlaylistId{"soak-playlist"},
                    queue(playlistQueue.tracks, 1.0, QueueMode::Shuffle)});
            break;

        case 9U:
            operationSucceeded = engine.processBackgroundMusicControl(
                ActivePlaybackScopeChanged{scope(
                    "soak-playlist",
                    playlistQueue,
                    activeItemId,
                    activeItemQueue)});
            break;
        }

        if (!require(operationSucceeded,
                "Every deterministic soak control event must complete without action safety exhaustion"))
        {
            return false;
        }

        engine.update(cycle % 10U == 6U ? 0.5F : 0.05F);
        if (cycle % 10U == 6U)
        {
            // A DeviceFailure enters Unavailable during the first update;
            // recovery starts on the next update and then consumes its
            // deterministic 500 ms cooldown.
            engine.update(0.5F);
        }
        if (!require(observeTokens()
                && AudioOutputRecoveryEngineTestAccess::executor(engine).hasActiveSources()
                && AudioOutputRecoveryEngineTestAccess::operationalState(engine)
                    == AudioOutputOperationalState::Healthy,
            "Soak must retain two live BGM scopes without entering an output contract fault"))
        {
            return false;
        }
    }

    const std::size_t initializationBound = CycleCount;
    if (!require(output.initializeCalls < initializationBound
            && output.beginCalls <= output.initializeCalls
            && output.pumpCalls < CycleCount * 4U,
        "Soak recovery/reroute work must remain bounded without output transition explosion"))
    {
        return false;
    }

    if (!require(engine.processBackgroundMusicControl(PlaybackStopped{}),
            "Soak cleanup stop must complete"))
    {
        return false;
    }
    engine.update(2.0F);
    return require(!AudioOutputRecoveryEngineTestAccess::executor(engine).hasActiveSources(),
        "Soak cleanup must leave zero BGM registrations with no delayed replay");
}

}

int main()
{
    const bool success = verifyItemLifecycleAndSameItemLoop()
        && verifyPlaylistItemCompositionAndScopeReplacement()
        && verifyEmbeddedPlaylistItemCompositionAndLifecycle()
        && verifyRealExecutorInvalidPathsAndLiveEdits()
        && verifyPausedLiveEditAndOutputRecoveryWithTwoScopes()
        && verifyDeterministicLifecycleSoak();
    return success ? 0 : 1;
}
