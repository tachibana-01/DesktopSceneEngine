#include <DesktopSceneEngine/BackgroundMusicControl.h>
#include <DesktopSceneEngine/Engine.h>

#include "AudioExecutionEngineFixture.h"
#include "Music/BackgroundMusicRuntime.h"
#include "Resources/SequentialAudioSource.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <vector>


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
namespace Tests = DesktopSceneEngine::Tests;
using DesktopSceneEngine::Audio::AudioLane;
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
    // Assertions moved from F32 mixer memory to the real S16 output boundary.
    // Compare to the exact expected quantization; do not loosen gain tolerance.
    const auto quantized = right >= 1 ? 32767 : right <= -1 ? -32768
        : static_cast<int>(std::lround(right * (right < 0 ? 32768.0F : 32767.0F)));
    const auto expected = quantized < 0 ? static_cast<float>(quantized) / 32768.0F
        : static_cast<float>(quantized) / 32767.0F;
    return std::fabs(left - expected) <= Tolerance;
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

bool hasBgm(DesktopSceneEngine::Engine& engine)
{
    return Tests::lane(engine, AudioLane::PlaylistBgm).has_value()
        || Tests::lane(engine, AudioLane::ItemBgm).has_value();
}
bool mixFrames(Tests::AwaitAudioEngine& engine, std::span<float> output)
{
    return Tests::readOutput(engine, engine.device, output) && Tests::settle(engine);
}

bool verifyPublicControlAdmissionAndValidation()
{
    Tests::AwaitAudioEngine engine{};
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
    if (!require(hasBgm(engine),
            "A successfully started BGM session must expose Executor-owned input presence"))
    {
        return false;
    }
    const auto tokenBeforeInvalidEvents = runtime.playlistSession()->sourceToken();

    std::vector<float> firstFrame(2U, 0.0F);
    if (!require(mixFrames(engine, firstFrame)
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
    Tests::AwaitAudioEngine engine{};
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
    if (!require(mixFrames(engine, firstFrame)
            && approximatelyEqual(firstFrame[0U], -0.1125F)
            && approximatelyEqual(firstFrame[1U], 0.1125F),
        "A configuration update must update the existing Mixer gain without restart"))
    {
        return false;
    }

    Tests::AwaitAudioEngine itemConfigEngine{};
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
    if (!require(mixFrames(itemConfigEngine, itemFrame)
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

    // The old arbitrary fourth mixer input is not an Engine product lane.
    // Its executor-only removal invariant remains in Music/Audio unit tests.
    // The approved Engine PlaybackStopped contract retires all three owned lanes.
    if (!require(engine.processBackgroundMusicControl(PlaybackStopped{})
            && BackgroundMusicEngineTestAccess::runtime(engine).playbackState()
                == BackgroundMusicPlaybackState::Stopped,
        "Stop reaches policy and waits for actual owned-source retirement")) return false;
    return require(!hasBgm(engine) && !Tests::lane(engine, AudioLane::Embedded)
            && Tests::audioState(engine).activeVoices == 0,
        "PlaybackStopped removes every Engine-owned physical lane without a C mixer bypass");
}

bool verifyScopeChangeAndTerminalHarvest()
{
    Tests::AwaitAudioEngine engine{};
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

    Tests::AwaitAudioEngine terminalEngine{};
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
    if (!require(mixFrames(terminalEngine, exhaustFixture),
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

    Tests::AwaitAudioEngine orderingEngine{};
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
    if (!require(mixFrames(orderingEngine, exhaustBoth),
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
    Tests::AwaitAudioEngine engine{};
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

    Tests::AwaitAudioEngine failureEngine{};
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
        auto engine = std::make_unique<Tests::AwaitAudioEngine>();
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
    Tests::AwaitAudioEngine engine{};
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

bool verifyPublicNoPlayableHasNoCurrent()
{
    Tests::AwaitAudioEngine engine{};
    const auto missing = fixturePath().parent_path() / "c1a-owned-nonexistent-audio-file.wav";
    if (!require(!std::filesystem::exists(missing), "NPT public test requires a genuinely missing source")) return false;
    const QueueSnapshot missingQueue{{TrackSnapshot{TrackId{"a"}, missing, 1.0},
        TrackSnapshot{TrackId{"b"}, missing, 1.0}}, QueueMode::ListLoop, 1.0};
    if (!require(engine.processBackgroundMusicControl(PlaybackSessionStarted{scope(
            "npt-public", missingQueue, ItemId{"npt-item"}, missingQueue)}),
        "Public missing-source control admission completes boundedly")) return false;
    const auto& runtime = BackgroundMusicEngineTestAccess::runtime(engine);
    if (!require(runtime.playlistSession() && runtime.itemSession()
            && runtime.playlistSession()->phase() == MusicSessionPhase::NoPlayableTrack
            && runtime.itemSession()->phase() == MusicSessionPhase::NoPlayableTrack
            && !runtime.playlistSession()->currentTrackId() && !runtime.itemSession()->currentTrackId()
            && !engine.currentPlaylistBackgroundMusicTrackId() && !engine.currentItemBackgroundMusicTrackId()
            && !hasBgm(engine),
        "Public Engine and internal exhausted scopes both have no Current/source")) return false;
    // The bool remains control admission, not a new final Use admission result.
    if (!require(engine.processBackgroundMusicControl(PlaylistMusicTrackSwitchRequested{
                    PlaylistId{"npt-public"}, TrackId{"a"}})
            && engine.processBackgroundMusicControl(ItemMusicTrackSwitchRequested{
                    PlaylistId{"npt-public"}, ItemId{"npt-item"}, TrackId{"a"}})
            && !engine.currentPlaylistBackgroundMusicTrackId() && !engine.currentItemBackgroundMusicTrackId()
            && !hasBgm(engine)
            && runtime.playlistSession()->phase() == MusicSessionPhase::NoPlayableTrack
            && runtime.itemSession()->phase() == MusicSessionPhase::NoPlayableTrack,
        "Public manual switch is an admitted no-op for exhausted scopes, not retry")) return false;
    std::cout << "PASS C1A public Engine: real missing files, no Current, NPT switch no-op\n";
    return true;
}

}

// Compile the same behavioral checks against the old ID-only public API and the
// new read seam. Missing observation fails at runtime after real native setup;
// it is not a missing-header/build-error RED or a replacement runtime.
template<class EngineType>
bool verifyPublicBgmPhaseObservation()
{
    EngineType engine{};
    auto& runtime = DesktopSceneEngine::Core::BackgroundMusicEngineTestAccess::mutableRuntime(engine);
    using namespace DesktopSceneEngine::Music;
    const MusicQueueSnapshot config{{MusicTrackSnapshot{DesktopSceneEngine::Music::TrackId{"phase-a"}, fixturePath(), 1.0F}}};
    const auto starts = runtime.apply(DesktopSceneEngine::Music::PlaybackSessionStarted{
        ActiveMusicScopeSnapshot{DesktopSceneEngine::Music::PlaylistId{"phase-owner"}, std::nullopt, config, std::nullopt}});
    if (!require(starts.size() == 1U && std::get_if<StartSource>(&starts[0])
            && runtime.playlistSession()->phase() == MusicSessionPhase::PendingStart
            && engine.currentPlaylistBackgroundMusicTrackId()->value == "phase-a",
        "Phase RED setup: real Engine-owned runtime is PendingStart and already has public Current")) return false;
    std::cout << "C1 phase baseline: real PendingStart has Current=phase-a\n";
    if constexpr (requires(const EngineType& owner) { owner.backgroundMusicObservation(); })
    {
        const auto token = std::get<StartSource>(starts[0]).token;
        const auto pending = engine.backgroundMusicObservation();
        using Phase = typename std::remove_cvref_t<decltype(pending.playlist.phase)>::value_type;
        if (!require(pending.playlist.active && pending.playlist.exists
                && pending.playlist.phase == Phase::PendingStart && pending.playlist.currentTrackId->value == "phase-a"
                && pending.playlist.switchEligible && !pending.item.active && !pending.item.exists,
            "Public PendingStart must not be inferred as Playing from Current")) return false;
        (void)runtime.apply(MusicSourceStarted{token});
        if (!require(engine.backgroundMusicObservation().playlist.phase == Phase::Playing,
            "Public observation advances only after native start acknowledgement")) return false;
        (void)runtime.apply(MusicSourceFailed{token, MusicSourceFailureClass::TransientResourceFailure});
        const auto waiting = engine.backgroundMusicObservation();
        if (!require(waiting.playlist.phase == Phase::WaitingForExecutor
                && waiting.playlist.currentTrackId->value == "phase-a" && waiting.playlist.switchEligible,
            "Waiting retains logical Current and existing runtime switching capability")) return false;
        auto disabled = config; disabled.enabled = false;
        (void)runtime.apply(DesktopSceneEngine::Music::PlaylistMusicConfigChanged{
            DesktopSceneEngine::Music::PlaylistId{"phase-owner"}, disabled});
        const auto absent = engine.backgroundMusicObservation();
        if (!require(absent.playlist.active && !absent.playlist.exists && !absent.playlist.phase
                && !absent.playlist.currentTrackId && !absent.playlist.switchEligible,
            "Active owner without an enabled runtime session is not a fake phase/current")) return false;
        auto empty = config; empty.tracks.clear();
        (void)runtime.apply(DesktopSceneEngine::Music::PlaylistMusicConfigChanged{
            DesktopSceneEngine::Music::PlaylistId{"phase-owner"}, empty});
        if (!require(engine.backgroundMusicObservation().playlist.phase == Phase::SilentEmptyQueue
                && !engine.backgroundMusicObservation().playlist.switchEligible,
            "Empty existing session has its real SilentEmptyQueue phase")) return false;
        (void)runtime.apply(DesktopSceneEngine::Music::PlaybackStopped{});
        const auto retired = engine.backgroundMusicObservation();
        if (!require(!retired.playlist.active && !retired.playlist.exists && !retired.playlist.phase
                && !retired.activePlaylistId && !retired.item.currentTrackId,
            "Stopped owner exposes absence, not a recreated runtime")) return false;
        return require(pending.playlist.phase == Phase::PendingStart && pending.playlist.currentTrackId->value == "phase-a",
            "Observation is a detached value that survives later native transitions");
    }
    else return require(false, "RED phase fidelity: public ID-only API cannot expose PendingStart vs Playing");
}

template<class EngineType>
bool verifyPublicBgmFailedAttemptSurvivesSuccess()
{
    EngineType engine{};
    const auto missing = fixturePath().parent_path() / "c1-observation-asserted-missing.wav";
    DesktopSceneEngine::Resources::SequentialAudioSource probe{};
    if (!require(!std::filesystem::exists(missing) && !probe.open(missing)
            && probe.status() == DesktopSceneEngine::Resources::AudioSourceStatus::OpenFailure,
        "Failure RED setup has a genuine known native OpenFailure, not a guessed filesystem reason")) return false;
    QueueSnapshot config{{TrackSnapshot{TrackId{"failed-a"}, missing, 1.0},
        TrackSnapshot{TrackId{"ready-b"}, fixturePath(), 1.0},
        TrackSnapshot{TrackId{"ready-c"}, fixturePath(), 1.0}}, QueueMode::ListLoop, 1.0};
    if (!require(engine.processBackgroundMusicControl(PlaybackSessionStarted{scope("failure-owner", config)})
            && engine.currentPlaylistBackgroundMusicTrackId()->value == "ready-b"
            && BackgroundMusicEngineTestAccess::runtime(engine).playlistSession()->phase() == MusicSessionPhase::Playing,
        "One real native orchestration turn fails A then successfully starts B")) return false;
    std::cout << "C1 failure baseline: one real turn A OpenFailure -> B Playing\n";
    if constexpr (requires(const EngineType& owner) { owner.backgroundMusicObservation(); })
    {
        const auto ready = engine.backgroundMusicObservation();
        using Stage = std::remove_cvref_t<decltype(ready.playlist.lastFailedAttempt->stage)>;
        using Code = std::remove_cvref_t<decltype(ready.playlist.lastFailedAttempt->code)>;
        if (!require(ready.playlist.currentTrackId->value == "ready-b" && ready.playlist.lastFailedAttempt
                && ready.playlist.lastFailedAttempt->trackId.value == "failed-a"
                && ready.playlist.lastFailedAttempt->stage == Stage::SourceOpen
                && ready.playlist.lastFailedAttempt->code == Code::OpenFailure,
            "Successful automatic B must not erase failed A or fabricate more specific reason")) return false;
        const auto token = BackgroundMusicEngineTestAccess::runtime(engine).playlistSession()->sourceToken();
        config.queueVolume = 0.4;
        (void)engine.processBackgroundMusicControl(PlaylistMusicConfigChanged{PlaylistId{"failure-owner"}, config});
        (void)engine.processBackgroundMusicControl(PlaylistMusicTrackSwitchRequested{PlaylistId{"failure-owner"}, TrackId{"ready-b"}});
        (void)engine.processBackgroundMusicControl(PlaylistMusicTrackSwitchRequested{PlaylistId{"failure-owner"}, TrackId{"not-a-member"}});
        if (!require(engine.backgroundMusicObservation().playlist.lastFailedAttempt->trackId.value == "failed-a"
                && BackgroundMusicEngineTestAccess::runtime(engine).playlistSession()->sourceToken() == token,
            "Gain/read hydration/same-current/invalid switch must retain the bounded fact and current source")) return false;
        (void)engine.processBackgroundMusicControl(PlaybackPaused{});
        (void)engine.processBackgroundMusicControl(PlaylistMusicTrackSwitchRequested{PlaylistId{"failure-owner"}, TrackId{"ready-c"}});
        const auto switched = engine.backgroundMusicObservation();
        if (!require(switched.playlist.currentTrackId->value == "ready-c" && !switched.playlist.lastFailedAttempt,
            "An actual explicit fresh switch supersedes old failed attempt")) return false;
        return require(ready.playlist.lastFailedAttempt->trackId.value == "failed-a",
            "Previously read value is not an alias to the current runtime");
    }
    else return require(false, "RED failed-attempt fidelity: public observation loses A after B succeeds");
}

template<class EngineType>
bool verifyPublicBgmExhaustedObservation()
{
    EngineType engine{};
    const auto missing = fixturePath().parent_path() / "c1-observation-asserted-missing.wav";
    if (!require(!std::filesystem::exists(missing), "Exhaustion fixture path really is missing")) return false;
    const QueueSnapshot config{{TrackSnapshot{TrackId{"fail-a"}, missing, 1.0},
        TrackSnapshot{TrackId{"fail-b"}, missing, 1.0}}, QueueMode::ListLoop, 1.0};
    if (!require(engine.processBackgroundMusicControl(PlaybackSessionStarted{scope("npt-observed", config)})
            && !engine.currentPlaylistBackgroundMusicTrackId()
            && BackgroundMusicEngineTestAccess::runtime(engine).playlistSession()->phase() == MusicSessionPhase::NoPlayableTrack,
        "C1A normalized baseline remains true before new observation")) return false;
    if constexpr (requires(const EngineType& owner) { owner.backgroundMusicObservation(); })
    {
        const auto value = engine.backgroundMusicObservation();
        using Phase = typename std::remove_cvref_t<decltype(value.playlist.phase)>::value_type;
        using Code = std::remove_cvref_t<decltype(value.playlist.lastFailedAttempt->code)>;
        return require(value.playlist.active && value.playlist.exists && value.playlist.phase == Phase::NoPlayableTrack
                && !value.playlist.currentTrackId && !value.playlist.switchEligible && value.playlist.lastFailedAttempt
                && value.playlist.lastFailedAttempt->trackId.value == "fail-b"
                && value.playlist.lastFailedAttempt->code == Code::OpenFailure,
            "NPT read exposes final genuine failure independently of absent Current and false switch capability");
    }
    else return require(false, "RED NPT observation: C1A current absence alone cannot expose phase/capability/final failure");
}

int main(int argc, char** argv)
{
    if (argc == 2 && std::string{argv[1]} == "--bgm-phase-only") return verifyPublicBgmPhaseObservation<Tests::AwaitAudioEngine>() ? 0 : 1;
    if (argc == 2 && std::string{argv[1]} == "--bgm-failure-only") return verifyPublicBgmFailedAttemptSurvivesSuccess<Tests::AwaitAudioEngine>() ? 0 : 1;
    if (argc == 2 && std::string{argv[1]} == "--bgm-npt-only") return verifyPublicBgmExhaustedObservation<Tests::AwaitAudioEngine>() ? 0 : 1;
    if (argc == 2 && std::string{argv[1]} == "--npt-public-only") return verifyPublicNoPlayableHasNoCurrent() ? 0 : 1;
    const bool success = verifyPublicControlAdmissionAndValidation()
        && verifyConfigPauseResumeAndStop()
        && verifyScopeChangeAndTerminalHarvest()
        && verifyCurrentTrackQueriesAndManualSwitching()
        && verifyActiveBackgroundMusicDestruction()
        && verifyEnabledControlConversionAndRuntimeLifecycle()
        && verifyPublicNoPlayableHasNoCurrent()
        && verifyPublicBgmPhaseObservation<Tests::AwaitAudioEngine>()
        && verifyPublicBgmFailedAttemptSurvivesSuccess<Tests::AwaitAudioEngine>()
        && verifyPublicBgmExhaustedObservation<Tests::AwaitAudioEngine>();
    return success ? 0 : 1;
}
