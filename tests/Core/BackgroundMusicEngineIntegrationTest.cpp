#include <DesktopSceneEngine/BackgroundMusicControl.h>
#include <DesktopSceneEngine/Engine.h>

#include "Audio/AudioMixer.h"
#include "Audio/AudioMixerInput.h"
#include "Music/BackgroundMusicExecutor.h"
#include "Music/BackgroundMusicRuntime.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace DesktopSceneEngine::Core
{

class BackgroundMusicEngineTestAccess final
{
public:
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
};

}

namespace
{

using DesktopSceneEngine::BackgroundMusicControl::ActiveScopeSnapshot;
using DesktopSceneEngine::BackgroundMusicControl::Event;
using DesktopSceneEngine::BackgroundMusicControl::ItemId;
using DesktopSceneEngine::BackgroundMusicControl::ItemMusicConfigChanged;
using DesktopSceneEngine::BackgroundMusicControl::ItemMusicTrackSwitchRequested;
using DesktopSceneEngine::BackgroundMusicControl::PlaybackPaused;
using DesktopSceneEngine::BackgroundMusicControl::PlaybackResumed;
using DesktopSceneEngine::BackgroundMusicControl::PlaybackSessionStarted;
using DesktopSceneEngine::BackgroundMusicControl::PlaybackStopped;
using DesktopSceneEngine::BackgroundMusicControl::PlaylistId;
using DesktopSceneEngine::BackgroundMusicControl::PlaylistMusicConfigChanged;
using DesktopSceneEngine::BackgroundMusicControl::PlaylistMusicTrackSwitchRequested;
using DesktopSceneEngine::BackgroundMusicControl::QueueMode;
using DesktopSceneEngine::BackgroundMusicControl::QueueSnapshot;
using DesktopSceneEngine::BackgroundMusicControl::TrackId;
using DesktopSceneEngine::BackgroundMusicControl::TrackSnapshot;
using DesktopSceneEngine::Audio::AudioMixer;
using DesktopSceneEngine::Audio::AudioMixerInput;
using DesktopSceneEngine::Audio::AudioMixerInputReadResult;
using DesktopSceneEngine::Audio::AudioMixerInputReadStatus;
using DesktopSceneEngine::Audio::AudioMixStatus;
using DesktopSceneEngine::Core::BackgroundMusicEngineTestAccess;
using DesktopSceneEngine::Music::BackgroundMusicPlaybackState;
using DesktopSceneEngine::Music::MusicSessionPhase;

constexpr std::size_t StereoChannels = 2U;
constexpr float Tolerance = 0.00001F;

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

std::filesystem::path fixturePath()
{
    return std::filesystem::path{DESKTOP_SCENE_ENGINE_SEQUENTIAL_AUDIO_TEST_DATA_DIR}
        / "SequentialAudioSourceStereo48k.wav";
}

QueueSnapshot queue(
    std::string trackId = "track",
    double queueVolume = 1.0,
    double trackVolume = 1.0,
    std::filesystem::path path = fixturePath(),
    bool enabled = true
)
{
    QueueSnapshot snapshot{
        {TrackSnapshot{TrackId{std::move(trackId)}, std::move(path), trackVolume}},
        QueueMode::ListLoop,
        queueVolume};
    snapshot.enabled = enabled;
    return snapshot;
}

ActiveScopeSnapshot scope(
    std::string playlist = "playlist",
    QueueSnapshot playlistQueue = queue(),
    std::optional<ItemId> itemId = std::nullopt,
    std::optional<QueueSnapshot> itemQueue = std::nullopt
)
{
    return ActiveScopeSnapshot{
        PlaylistId{std::move(playlist)},
        std::move(playlistQueue),
        std::move(itemId),
        std::move(itemQueue)};
}

bool mixFrames(AudioMixer& mixer, std::span<float> output)
{
    const auto result = mixer.mixFrames(output);
    return result.status == AudioMixStatus::Success
        && result.framesProduced == output.size() / StereoChannels;
}

class ConstantInput final : public AudioMixerInput
{
public:
    explicit ConstantInput(float sample) noexcept
        : sample_(sample)
    {
    }

    [[nodiscard]] AudioMixerInputReadResult readFrames(
        std::span<float> destination
    ) noexcept override
    {
        if (destination.size() % StereoChannels != 0U)
        {
            return {0U, AudioMixerInputReadStatus::Failure};
        }

        std::fill(destination.begin(), destination.end(), sample_);
        return {destination.size() / StereoChannels, AudioMixerInputReadStatus::Data};
    }

private:
    float sample_ = 0.0F;
};

bool verifyPublicControlAdmissionAndValidation()
{
    DesktopSceneEngine::Engine engine{};
    const Event start = PlaybackSessionStarted{scope("playlist", queue("track", 0.5, 0.5))};
    if (!require(engine.processBackgroundMusicControl(start),
            "A valid public playback-session control event must be admitted by Engine"))
    {
        return false;
    }

    const auto& runtime = BackgroundMusicEngineTestAccess::runtime(engine);
    if (!require(runtime.playbackState() == BackgroundMusicPlaybackState::Playing
            && runtime.playlistSession() != nullptr
            && runtime.playlistSession()->phase() == MusicSessionPhase::Playing,
        "A valid public session start must reach the real Runtime, Orchestrator, and Executor"))
    {
        return false;
    }
    if (!require(BackgroundMusicEngineTestAccess::executor(engine).hasActiveSources(),
            "A successfully started BGM session must expose Executor-owned input presence"))
    {
        return false;
    }
    const auto tokenBeforeInvalidEvents = runtime.playlistSession()->sourceToken();

    std::vector<float> firstFrame(2U, 0.0F);
    if (!require(mixFrames(BackgroundMusicEngineTestAccess::mixer(engine), firstFrame)
            && approximatelyEqual(firstFrame[0U], -0.1875F)
            && approximatelyEqual(firstFrame[1U], 0.1875F),
        "The real Executor registration must reach the shared Mixer at queue times track gain"))
    {
        return false;
    }

    const Event emptyPlaylist = PlaybackSessionStarted{scope("", queue())};
    const Event duplicateTracks = PlaybackSessionStarted{scope(
        "playlist",
        QueueSnapshot{
            {TrackSnapshot{TrackId{"duplicate"}, fixturePath(), 1.0},
             TrackSnapshot{TrackId{"duplicate"}, fixturePath(), 1.0}},
            QueueMode::ListLoop,
            1.0})};
    const Event invalidQueueVolume = PlaylistMusicConfigChanged{
        PlaylistId{"playlist"}, queue("track", 1.1, 1.0)};
    const Event nonFiniteQueueVolume = PlaylistMusicConfigChanged{
        PlaylistId{"playlist"}, queue("track", std::numeric_limits<double>::quiet_NaN(), 1.0)};
    const Event invalidTrackVolume = PlaylistMusicConfigChanged{
        PlaylistId{"playlist"}, queue("track", 1.0, -0.1)};
    const Event nonFiniteTrackVolume = PlaylistMusicConfigChanged{
        PlaylistId{"playlist"}, queue("track", 1.0, std::numeric_limits<double>::infinity())};
    const Event emptyPath = PlaylistMusicConfigChanged{
        PlaylistId{"playlist"}, queue("track", 1.0, 1.0, {})};
    const Event malformedItemPairing = PlaybackSessionStarted{scope(
        "playlist",
        queue(),
        ItemId{"item"},
        std::nullopt)};

    for (const Event* invalidEvent : {
             &emptyPlaylist,
             &duplicateTracks,
             &invalidQueueVolume,
             &nonFiniteQueueVolume,
             &invalidTrackVolume,
             &nonFiniteTrackVolume,
             &emptyPath,
             &malformedItemPairing})
    {
        if (!require(!engine.processBackgroundMusicControl(*invalidEvent),
                "An invalid public control DTO must be rejected"))
        {
            return false;
        }

        const auto& currentRuntime = BackgroundMusicEngineTestAccess::runtime(engine);
        if (!require(currentRuntime.playlistSession() != nullptr
                && currentRuntime.playlistSession()->sourceToken() == tokenBeforeInvalidEvents,
            "Rejected public control DTOs must not partially mutate Runtime state"))
        {
            return false;
        }
    }

    return true;
}

bool verifyConfigPauseResumeAndStop()
{
    DesktopSceneEngine::Engine engine{};
    if (!require(engine.processBackgroundMusicControl(
                    PlaybackSessionStarted{scope("playlist", queue("track", 1.0, 1.0))}),
            "Session start must succeed before control lifecycle validation"))
    {
        return false;
    }

    const auto* const session = BackgroundMusicEngineTestAccess::runtime(engine).playlistSession();
    if (!require(session != nullptr, "Session start must create an active playlist session"))
    {
        return false;
    }
    const auto token = session->sourceToken();
    if (!require(engine.processBackgroundMusicControl(
                    PlaylistMusicConfigChanged{PlaylistId{"playlist"}, queue("track", 0.2, 0.75)}),
            "A valid active playlist configuration update must be admitted")
        || !require(BackgroundMusicEngineTestAccess::runtime(engine)
                    .playlistSession()->sourceToken() == token,
            "A gain-only configuration update must not replace the source token"))
    {
        return false;
    }

    std::vector<float> firstFrame(2U, 0.0F);
    if (!require(mixFrames(BackgroundMusicEngineTestAccess::mixer(engine), firstFrame)
            && approximatelyEqual(firstFrame[0U], -0.1125F)
            && approximatelyEqual(firstFrame[1U], 0.1125F),
        "A configuration update must update the existing Mixer gain without restart"))
    {
        return false;
    }

    DesktopSceneEngine::Engine itemConfigEngine{};
    const QueueSnapshot emptyPlaylistQueue{{}, QueueMode::ListLoop, 1.0};
    if (!require(itemConfigEngine.processBackgroundMusicControl(
                    PlaybackSessionStarted{scope(
                        "item-config-playlist",
                        emptyPlaylistQueue,
                        ItemId{"item"},
                        queue("item-track"))}),
            "Item configuration setup must start an Item-only session"))
    {
        return false;
    }

    const auto* const itemSession = BackgroundMusicEngineTestAccess::runtime(itemConfigEngine)
        .itemSession();
    if (!require(itemSession != nullptr, "Item configuration setup must create an Item session"))
    {
        return false;
    }
    const auto itemToken = itemSession->sourceToken();
    if (!require(itemConfigEngine.processBackgroundMusicControl(ItemMusicConfigChanged{
                    PlaylistId{"item-config-playlist"},
                    ItemId{"item"},
                    queue("item-track", 0.2, 0.75)}),
            "A valid active Item configuration update must be admitted")
        || !require(BackgroundMusicEngineTestAccess::runtime(itemConfigEngine)
                    .itemSession()->sourceToken() == itemToken,
            "An Item gain-only configuration update must not replace the source token"))
    {
        return false;
    }

    std::vector<float> itemFrame(2U, 0.0F);
    if (!require(mixFrames(BackgroundMusicEngineTestAccess::mixer(itemConfigEngine), itemFrame)
            && approximatelyEqual(itemFrame[0U], -0.1125F)
            && approximatelyEqual(itemFrame[1U], 0.1125F),
        "An Item configuration update must update the existing Mixer gain without restart"))
    {
        return false;
    }

    if (!require(engine.processBackgroundMusicControl(PlaybackPaused{})
            && BackgroundMusicEngineTestAccess::runtime(engine).playbackState()
                == BackgroundMusicPlaybackState::Paused
            && BackgroundMusicEngineTestAccess::runtime(engine)
                .playlistSession()->sourceToken() == token,
        "Pause must preserve the existing BGM source token"))
    {
        return false;
    }

    if (!require(engine.processBackgroundMusicControl(PlaybackResumed{})
            && BackgroundMusicEngineTestAccess::runtime(engine).playbackState()
                == BackgroundMusicPlaybackState::Playing
            && BackgroundMusicEngineTestAccess::runtime(engine)
                .playlistSession()->sourceToken() == token,
        "Resume must preserve the existing BGM source token"))
    {
        return false;
    }

    AudioMixer& mixer = BackgroundMusicEngineTestAccess::mixer(engine);
    if (!require(static_cast<bool>(mixer.addInput(std::make_unique<ConstantInput>(0.25F))),
            "Test setup must install a non-BGM Mixer input"))
    {
        return false;
    }

    if (!require(engine.processBackgroundMusicControl(PlaybackStopped{})
            && BackgroundMusicEngineTestAccess::runtime(engine).playbackState()
                == BackgroundMusicPlaybackState::Stopped,
        "Stop must reach the Background Music Runtime"))
    {
        return false;
    }

    std::vector<float> remainingInput(2U, 0.0F);
    return require(!BackgroundMusicEngineTestAccess::executor(engine).hasActiveSources(),
               "Stopping BGM must remove Executor-owned input presence")
        && require(mixFrames(mixer, remainingInput)
            && approximatelyEqual(remainingInput[0U], 0.25F)
            && approximatelyEqual(remainingInput[1U], 0.25F),
        "Stopping BGM must remove only Executor-owned Mixer inputs");
}

bool verifyScopeChangeAndTerminalHarvest()
{
    DesktopSceneEngine::Engine engine{};
    if (!require(engine.processBackgroundMusicControl(PlaybackSessionStarted{scope(
                    "playlist",
                    queue("playlist-track"),
                    ItemId{"item-a"},
                    queue("item-a-track"))}),
            "Playlist and Item scopes must both start through Engine"))
    {
        return false;
    }

    const auto* const initialItemSession = BackgroundMusicEngineTestAccess::runtime(engine).itemSession();
    if (!require(initialItemSession != nullptr, "Scope change setup must create an Item session"))
    {
        return false;
    }
    const auto oldItemToken = initialItemSession->sourceToken();
    if (!require(engine.processBackgroundMusicControl(
                    DesktopSceneEngine::BackgroundMusicControl::ActivePlaybackScopeChanged{scope(
                        "playlist",
                        queue("playlist-track"),
                        ItemId{"item-b"},
                        queue("item-b-track"))}),
            "An atomic active scope change must be admitted")
        || !require(BackgroundMusicEngineTestAccess::runtime(engine).activeItemId()
                    == DesktopSceneEngine::Music::ItemId{"item-b"}
                && BackgroundMusicEngineTestAccess::runtime(engine)
                    .itemSession()->sourceToken() != oldItemToken,
            "An item scope change must replace only the Item BGM session"))
    {
        return false;
    }

    DesktopSceneEngine::Engine terminalEngine{};
    if (!require(terminalEngine.processBackgroundMusicControl(
                    PlaybackSessionStarted{scope("terminal", queue("loop-track"))}),
            "Terminal harvesting setup must start a real source"))
    {
        return false;
    }

    const auto* const initialTerminalSession = BackgroundMusicEngineTestAccess::runtime(terminalEngine)
        .playlistSession();
    if (!require(initialTerminalSession != nullptr,
            "Terminal harvesting setup must create a Playlist session"))
    {
        return false;
    }
    const auto oldToken = initialTerminalSession->sourceToken();
    std::vector<float> exhaustFixture(1200U * StereoChannels, 0.0F);
    if (!require(mixFrames(BackgroundMusicEngineTestAccess::mixer(terminalEngine), exhaustFixture),
            "Test setup must drive the real BGM source to its terminal Mixer state"))
    {
        return false;
    }

    terminalEngine.update();
    const auto newToken = BackgroundMusicEngineTestAccess::runtime(terminalEngine)
        .playlistSession()->sourceToken();
    if (!require(newToken.has_value() && newToken != oldToken,
            "Engine update must harvest terminal facts through Orchestrator and register the successor"))
    {
        return false;
    }

    DesktopSceneEngine::Engine orderingEngine{};
    if (!require(orderingEngine.processBackgroundMusicControl(PlaybackSessionStarted{scope(
                    "ordering",
                    queue("playlist-order"),
                    ItemId{"item-order"},
                    queue("item-order"))}),
            "Terminal ordering setup must start Playlist and Item sources"))
    {
        return false;
    }

    std::vector<float> exhaustBoth(1200U * StereoChannels, 0.0F);
    if (!require(mixFrames(BackgroundMusicEngineTestAccess::mixer(orderingEngine), exhaustBoth),
            "Terminal ordering setup must make both Mixer inputs terminal"))
    {
        return false;
    }

    orderingEngine.update();
    const auto playlistReplacement = BackgroundMusicEngineTestAccess::runtime(orderingEngine)
        .playlistSession()->sourceToken();
    const auto itemReplacement = BackgroundMusicEngineTestAccess::runtime(orderingEngine)
        .itemSession()->sourceToken();
    return require(playlistReplacement.has_value()
            && itemReplacement.has_value()
            && playlistReplacement->value < itemReplacement->value,
        "Engine must preserve Executor terminal fact order: Playlist before Item");
}

bool verifyCurrentTrackQueriesAndManualSwitching()
{
    DesktopSceneEngine::Engine engine{};
    if (!require(!engine.currentPlaylistBackgroundMusicTrackId().has_value()
            && !engine.currentItemBackgroundMusicTrackId().has_value(),
        "Current-track queries must be empty before a playback session starts"))
    {
        return false;
    }

    const QueueSnapshot playlistQueue{
        {TrackSnapshot{TrackId{"playlist-a"}, fixturePath(), 1.0},
         TrackSnapshot{TrackId{"playlist-b"}, fixturePath(), 0.75},
         TrackSnapshot{TrackId{"playlist-c"}, fixturePath(), 1.0}},
        QueueMode::ListLoop,
        0.5};
    const QueueSnapshot itemQueue{
        {TrackSnapshot{TrackId{"item-a"}, fixturePath(), 1.0},
         TrackSnapshot{TrackId{"item-b"}, fixturePath(), 0.25}},
        QueueMode::CurrentSingleLoop,
        0.8};
    if (!require(engine.processBackgroundMusicControl(PlaybackSessionStarted{scope(
                    "manual-switch-playlist",
                    playlistQueue,
                    ItemId{"manual-switch-item"},
                    itemQueue)}),
        "Manual-switch setup must start through the public Engine boundary"))
    {
        return false;
    }

    const auto& initialRuntime = BackgroundMusicEngineTestAccess::runtime(engine);
    const auto initialPlaylistToken = initialRuntime.playlistSession()->sourceToken();
    const auto initialItemToken = initialRuntime.itemSession()->sourceToken();
    if (!require(engine.currentPlaylistBackgroundMusicTrackId()
                == std::optional<TrackId>{TrackId{"playlist-a"}}
            && engine.currentItemBackgroundMusicTrackId()
                == std::optional<TrackId>{TrackId{"item-a"}}
            && initialPlaylistToken.has_value()
            && initialItemToken.has_value(),
        "Engine queries must expose Runtime-selected TrackIds after start"))
    {
        return false;
    }

    if (!require(engine.processBackgroundMusicControl(PlaylistMusicTrackSwitchRequested{
                    PlaylistId{"manual-switch-playlist"}, TrackId{"playlist-b"}}),
        "A valid active Playlist manual-switch event must be admitted")
        || !require(engine.currentPlaylistBackgroundMusicTrackId()
                    == std::optional<TrackId>{TrackId{"playlist-b"}}
                && engine.currentItemBackgroundMusicTrackId()
                    == std::optional<TrackId>{TrackId{"item-a"}}
                && BackgroundMusicEngineTestAccess::runtime(engine).playlistSession()->sourceToken()
                    != initialPlaylistToken
                && BackgroundMusicEngineTestAccess::runtime(engine).itemSession()->sourceToken()
                    == initialItemToken,
            "A Playlist switch must replace only Playlist runtime state"))
    {
        return false;
    }

    const auto playlistBToken = BackgroundMusicEngineTestAccess::runtime(engine)
        .playlistSession()->sourceToken();
    if (!require(engine.processBackgroundMusicControl(PlaylistMusicTrackSwitchRequested{
                    PlaylistId{"manual-switch-playlist"}, TrackId{"playlist-b"}})
            && BackgroundMusicEngineTestAccess::runtime(engine).playlistSession()->sourceToken()
                == playlistBToken
            && engine.processBackgroundMusicControl(PlaylistMusicTrackSwitchRequested{
                    PlaylistId{"inactive-playlist"}, TrackId{"playlist-c"}})
            && engine.currentPlaylistBackgroundMusicTrackId()
                == std::optional<TrackId>{TrackId{"playlist-b"}},
        "Same-track and inactive Playlist requests must not churn the active source"))
    {
        return false;
    }

    if (!require(engine.processBackgroundMusicControl(ItemMusicTrackSwitchRequested{
                    PlaylistId{"manual-switch-playlist"},
                    ItemId{"manual-switch-item"},
                    TrackId{"item-b"}}),
        "A valid active Item manual-switch event must be admitted")
        || !require(engine.currentPlaylistBackgroundMusicTrackId()
                    == std::optional<TrackId>{TrackId{"playlist-b"}}
                && engine.currentItemBackgroundMusicTrackId()
                    == std::optional<TrackId>{TrackId{"item-b"}}
                && BackgroundMusicEngineTestAccess::runtime(engine).playlistSession()->sourceToken()
                    == playlistBToken
                && BackgroundMusicEngineTestAccess::runtime(engine).itemSession()->sourceToken()
                    != initialItemToken,
            "An Item switch must preserve the active Playlist source"))
    {
        return false;
    }

    if (!require(engine.processBackgroundMusicControl(PlaybackPaused{}),
            "Pause must be admitted before paused manual-switch validation")
        || !require(engine.processBackgroundMusicControl(PlaylistMusicTrackSwitchRequested{
                    PlaylistId{"manual-switch-playlist"}, TrackId{"playlist-c"}})
            && BackgroundMusicEngineTestAccess::runtime(engine).playbackState()
                == BackgroundMusicPlaybackState::Paused
            && engine.currentPlaylistBackgroundMusicTrackId()
                == std::optional<TrackId>{TrackId{"playlist-c"}},
        "A paused manual switch must retain paused Runtime state and update the selected TrackId"))
    {
        return false;
    }

    const Event malformedSwitch = PlaylistMusicTrackSwitchRequested{PlaylistId{}, TrackId{"playlist-a"}};
    if (!require(!engine.processBackgroundMusicControl(malformedSwitch)
            && engine.currentPlaylistBackgroundMusicTrackId()
                == std::optional<TrackId>{TrackId{"playlist-c"}},
        "Malformed public switch DTOs must be rejected without mutating current selection"))
    {
        return false;
    }

    if (!require(engine.processBackgroundMusicControl(PlaybackStopped{})
            && !engine.currentPlaylistBackgroundMusicTrackId().has_value()
            && !engine.currentItemBackgroundMusicTrackId().has_value(),
        "Stopping playback must clear both current-track queries"))
    {
        return false;
    }

    DesktopSceneEngine::Engine failureEngine{};
    const QueueSnapshot failureQueue{
        {TrackSnapshot{TrackId{"valid-a"}, fixturePath(), 1.0},
         TrackSnapshot{TrackId{"invalid-b"}, std::filesystem::path{"does-not-exist.mp3"}, 1.0},
         TrackSnapshot{TrackId{"valid-c"}, fixturePath(), 1.0}},
        QueueMode::ListLoop,
        1.0};
    if (!require(failureEngine.processBackgroundMusicControl(
                    PlaybackSessionStarted{scope("failure-playlist", failureQueue)}),
        "Failure-progression setup must start from a valid first TrackId")
        || !require(failureEngine.processBackgroundMusicControl(PlaylistMusicTrackSwitchRequested{
                    PlaylistId{"failure-playlist"}, TrackId{"invalid-b"}}),
            "A selected but unreadable TrackId must still be admitted for Executor processing"))
    {
        return false;
    }

    return require(failureEngine.currentPlaylistBackgroundMusicTrackId()
            == std::optional<TrackId>{TrackId{"valid-c"}}
        && BackgroundMusicEngineTestAccess::runtime(failureEngine).playlistSession()->phase()
            == MusicSessionPhase::Playing,
        "Manual target failure must use the normal Runtime successor path without leaving stale selection");
}

bool verifyActiveBackgroundMusicDestruction()
{
    {
        auto engine = std::make_unique<DesktopSceneEngine::Engine>();
        if (!require(engine->processBackgroundMusicControl(
                        PlaybackSessionStarted{scope("lifetime", queue("lifetime-track"))}),
                "Lifetime setup must register an active Background Music source"))
        {
            return false;
        }
    }

    return true;
}

bool verifyEnabledControlConversionAndRuntimeLifecycle()
{
    DesktopSceneEngine::Engine engine{};
    const QueueSnapshot playlistDisabled = queue(
        "playlist-disabled",
        1.0,
        1.0,
        fixturePath(),
        false);
    const QueueSnapshot itemEnabled = queue("item-enabled");
    if (!require(engine.processBackgroundMusicControl(PlaybackSessionStarted{scope(
                    "enabled-public-boundary",
                    playlistDisabled,
                    ItemId{"enabled-item"},
                    itemEnabled)}),
            "Engine must admit an independently disabled Playlist scope")
        || !require(BackgroundMusicEngineTestAccess::runtime(engine).playlistSession() == nullptr
                && BackgroundMusicEngineTestAccess::runtime(engine).itemSession() != nullptr,
            "Public QueueSnapshot Enabled must survive Engine conversion into Runtime"))
    {
        return false;
    }

    const auto itemToken = BackgroundMusicEngineTestAccess::runtime(engine)
        .itemSession()->sourceToken();
    if (!require(engine.processBackgroundMusicControl(PlaylistMusicConfigChanged{
                    PlaylistId{"enabled-public-boundary"},
                    queue("playlist-enabled")})
            && BackgroundMusicEngineTestAccess::runtime(engine).playlistSession() != nullptr
            && BackgroundMusicEngineTestAccess::runtime(engine).itemSession()->sourceToken()
                == itemToken,
        "Public Playlist enable must fresh-start only Playlist Runtime state"))
    {
        return false;
    }

    return require(engine.processBackgroundMusicControl(ItemMusicConfigChanged{
                PlaylistId{"enabled-public-boundary"},
                ItemId{"enabled-item"},
                queue("item-disabled", 1.0, 1.0, fixturePath(), false)})
            && BackgroundMusicEngineTestAccess::runtime(engine).playlistSession() != nullptr
            && BackgroundMusicEngineTestAccess::runtime(engine).itemSession() == nullptr,
        "Public Item disable must destroy only Item Runtime state");
}

}

int main()
{
    const bool success = verifyPublicControlAdmissionAndValidation()
        && verifyConfigPauseResumeAndStop()
        && verifyScopeChangeAndTerminalHarvest()
        && verifyCurrentTrackQueriesAndManualSwitching()
        && verifyActiveBackgroundMusicDestruction()
        && verifyEnabledControlConversionAndRuntimeLifecycle();
    return success ? 0 : 1;
}
