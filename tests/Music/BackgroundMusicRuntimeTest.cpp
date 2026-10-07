#include "Music/BackgroundMusicRuntime.h"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace
{

using namespace DesktopSceneEngine::Music;

bool require(bool condition, const char* message)
{
    if (condition)
    {
        return true;
    }

    std::cerr << "Test failed: " << message << std::endl;
    return false;
}

PlaylistId playlistId(const char* value)
{
    return PlaylistId{value};
}

ItemId itemId(const char* value)
{
    return ItemId{value};
}

TrackId trackId(const char* value)
{
    return TrackId{value};
}

MusicTrackSnapshot track(const char* id, const char* source, float volume = 1.0F)
{
    return MusicTrackSnapshot{trackId(id), std::filesystem::path{source}, volume};
}

MusicQueueSnapshot queue(
    std::initializer_list<MusicTrackSnapshot> tracks,
    float volume = 1.0F,
    MusicQueueMode mode = MusicQueueMode::ListLoop,
    bool enabled = true
)
{
    MusicQueueSnapshot snapshot{std::vector<MusicTrackSnapshot>{tracks}, mode, volume};
    snapshot.enabled = enabled;
    return snapshot;
}

ActiveMusicScopeSnapshot scope(
    const char* playlist,
    std::optional<const char*> item,
    MusicQueueSnapshot playlistMusic,
    std::optional<MusicQueueSnapshot> itemMusic,
    PlaybackActivity activity = PlaybackActivity::Playing
)
{
    return ActiveMusicScopeSnapshot{
        playlistId(playlist),
        item.has_value() ? std::optional<ItemId>{itemId(*item)} : std::nullopt,
        std::move(playlistMusic),
        std::move(itemMusic),
        activity};
}

const StartSource* startAction(const MusicRuntimeAction& action)
{
    return std::get_if<StartSource>(&action);
}

const StopSource* stopAction(const MusicRuntimeAction& action)
{
    return std::get_if<StopSource>(&action);
}

bool verifySnapshotValidation()
{
    const MusicTrackSnapshot first = track("track-a", "same-path.mp3", 0.0F);
    const MusicTrackSnapshot second = track("track-b", "same-path.mp3", 1.0F);
    const MusicQueueSnapshot validQueue = queue({first, second});
    const ActiveMusicScopeSnapshot validScope = scope(
        "playlist-a",
        "item-a",
        validQueue,
        queue({track("item-track", "item.mp3", 0.5F)}));

    MusicTrackSnapshot emptyPath = first;
    emptyPath.sourcePath.clear();
    MusicTrackSnapshot negativeVolume = first;
    negativeVolume.trackVolume = -0.1F;
    MusicTrackSnapshot oversizedVolume = first;
    oversizedVolume.trackVolume = 1.1F;
    MusicTrackSnapshot nanVolume = first;
    nanVolume.trackVolume = std::numeric_limits<float>::quiet_NaN();
    MusicQueueSnapshot duplicateIdQueue = queue({first, first});
    MusicQueueSnapshot invalidQueueVolume = validQueue;
    invalidQueueVolume.queueVolume = std::numeric_limits<float>::infinity();
    const ActiveMusicScopeSnapshot missingItemQueue = scope(
        "playlist-a",
        "item-a",
        validQueue,
        std::nullopt);
    const ActiveMusicScopeSnapshot unexpectedItemQueue = scope(
        "playlist-a",
        std::nullopt,
        validQueue,
        queue({}));

    return require(static_cast<bool>(playlistId("playlist-a")), "A non-empty PlaylistId must be valid")
        && require(static_cast<bool>(itemId("item-a")), "A non-empty ItemId must be valid")
        && require(static_cast<bool>(trackId("track-a")), "A non-empty TrackId must be valid")
        && require(!static_cast<bool>(PlaylistId{}), "An empty PlaylistId must be invalid")
        && require(!static_cast<bool>(ItemId{}), "An empty ItemId must be invalid")
        && require(!static_cast<bool>(TrackId{}), "An empty TrackId must be invalid")
        && require(isValid(first), "A complete track snapshot with zero volume must be valid")
        && require(isValid(second), "A complete track snapshot with unit volume must be valid")
        && require(!isValid(emptyPath), "An empty track path must be invalid")
        && require(!isValid(negativeVolume), "A negative track volume must be invalid")
        && require(!isValid(oversizedVolume), "A track volume above one must be invalid")
        && require(!isValid(nanVolume), "A non-finite track volume must be invalid")
        && require(isValid(validQueue), "Duplicate source paths with distinct TrackIds must be valid")
        && require(!isValid(duplicateIdQueue), "Duplicate TrackIds in one queue must be invalid")
        && require(!isValid(invalidQueueVolume), "A non-finite queue volume must be invalid")
        && require(isValid(validScope), "A complete active scope snapshot must be valid")
        && require(!isValid(missingItemQueue), "An ItemId without item music must be invalid")
        && require(!isValid(unexpectedItemQueue), "Item music without an ItemId must be invalid")
        && require(MusicQueueSnapshot{}.mode == MusicQueueMode::ListLoop,
            "The runtime queue default must remain ListLoop");
}

bool verifySessionStartAndPauseResume()
{
    BackgroundMusicRuntime runtime{};
    const ActiveMusicScopeSnapshot playingScope = scope(
        "playlist-a",
        "item-a",
        queue({track("playlist-track", "playlist.mp3", 0.8F)}, 0.5F),
        queue({track("item-track", "item.mp3", 0.25F)}, 0.4F));

    const std::vector<MusicRuntimeAction> startActions = runtime.apply(
        PlaybackSessionStarted{playingScope});
    if (!require(runtime.playbackState() == BackgroundMusicPlaybackState::Playing,
            "A playing session start must set Playing")
        || !require(runtime.activePlaylistId() == std::optional<PlaylistId>{playlistId("playlist-a")},
            "A valid session start must retain the PlaylistId")
        || !require(runtime.activeItemId() == std::optional<ItemId>{itemId("item-a")},
            "A valid session start must retain the ItemId")
        || !require(runtime.playlistSession() != nullptr && runtime.itemSession() != nullptr,
            "A valid session start must create both scoped sessions")
        || !require(startActions.size() == 2U,
            "Two non-empty scopes must emit two startup actions"))
    {
        return false;
    }

    if (!require(runtime.apply(PlaybackSessionStarted{playingScope}).empty(),
            "A duplicate session start while active must be a no-op")
        || !require(runtime.activePlaylistId() == std::optional<PlaylistId>{playlistId("playlist-a")}
                && runtime.activeItemId() == std::optional<ItemId>{itemId("item-a")},
            "A duplicate session start must preserve the active scope"))
    {
        return false;
    }

    const StartSource* const playlistStart = startAction(startActions[0]);
    const StartSource* const itemStart = startAction(startActions[1]);
    if (!require(playlistStart != nullptr && itemStart != nullptr,
            "Startup actions must be StartSource actions")
        || !require(playlistStart->scope == MusicScope::PlaylistGlobal
                && itemStart->scope == MusicScope::Item,
            "Startup actions must be ordered PlaylistGlobal then Item")
        || !require(playlistStart->trackId == trackId("playlist-track")
                && itemStart->trackId == trackId("item-track"),
            "Startup selection must use each queue's first TrackId")
        || !require(playlistStart->sourcePath == std::filesystem::path{"playlist.mp3"}
                && itemStart->sourcePath == std::filesystem::path{"item.mp3"},
            "StartSource actions must carry the selected source path")
        || !require(playlistStart->effectiveGain == 0.4F && itemStart->effectiveGain == 0.1F,
            "StartSource gain must be queue volume times track volume")
        || !require(static_cast<bool>(playlistStart->token)
                && static_cast<bool>(itemStart->token)
                && playlistStart->token != itemStart->token,
            "Playlist and Item startup tokens must be unique and nonzero")
        || !require(runtime.playlistSession()->phase() == MusicSessionPhase::PendingStart
                && runtime.itemSession()->phase() == MusicSessionPhase::PendingStart,
            "New non-empty sessions must wait for StartSource acknowledgement"))
    {
        return false;
    }

    const MusicSourceToken playlistToken = playlistStart->token;
    const MusicSourceToken itemToken = itemStart->token;
    if (!require(runtime.apply(PlaybackPaused{}).empty(), "Pause must not emit source actions")
        || !require(runtime.playbackState() == BackgroundMusicPlaybackState::Paused,
            "Pause must preserve sessions while changing top state")
        || !require(runtime.playlistSession()->sourceToken() == std::optional<MusicSourceToken>{playlistToken}
                && runtime.itemSession()->sourceToken() == std::optional<MusicSourceToken>{itemToken},
            "Pause must preserve active source tokens")
        || !require(runtime.apply(PlaybackPaused{}).empty(), "Repeated pause must be a no-op")
        || !require(runtime.apply(PlaybackResumed{}).empty(), "Resume must not recreate sources")
        || !require(runtime.playbackState() == BackgroundMusicPlaybackState::Playing,
            "Resume must restore Playing")
        || !require(runtime.apply(PlaybackResumed{}).empty(), "Repeated resume must be a no-op"))
    {
        return false;
    }

    const ActiveMusicScopeSnapshot pausedScope = scope(
        "playlist-paused",
        "item-paused",
        queue({track("playlist-paused-track", "playlist-paused.mp3")}),
        queue({track("item-paused-track", "item-paused.mp3")}),
        PlaybackActivity::Paused);
    BackgroundMusicRuntime pausedRuntime{};
    const std::vector<MusicRuntimeAction> pausedActions = pausedRuntime.apply(
        PlaybackSessionStarted{pausedScope});
    return require(pausedRuntime.playbackState() == BackgroundMusicPlaybackState::Paused,
            "A paused session start must retain Paused state")
        && require(pausedActions.size() == 2U
                && startAction(pausedActions[0]) != nullptr
                && startAction(pausedActions[1]) != nullptr
                && startAction(pausedActions[0])->activity == PlaybackActivity::Paused
                && startAction(pausedActions[1])->activity == PlaybackActivity::Paused,
            "Paused startup actions must carry Paused without a pause action");
}

bool verifyEmptyScopesAndInvalidStart()
{
    BackgroundMusicRuntime runtime{};
    const ActiveMusicScopeSnapshot emptyScope = scope(
        "playlist-empty",
        "item-empty",
        queue({}),
        queue({}));
    if (!require(runtime.apply(PlaybackSessionStarted{emptyScope}).empty(),
            "Empty queues must create silent sessions without source actions")
        || !require(runtime.playlistSession() != nullptr && runtime.itemSession() != nullptr,
            "Empty queues must still own active silent sessions")
        || !require(runtime.playlistSession()->phase() == MusicSessionPhase::SilentEmptyQueue
                && runtime.itemSession()->phase() == MusicSessionPhase::SilentEmptyQueue,
            "Empty queues must use SilentEmptyQueue"))
    {
        return false;
    }

    BackgroundMusicRuntime invalidRuntime{};
    const ActiveMusicScopeSnapshot invalidScope = scope(
        "",
        "item-a",
        queue({track("track-a", "a.mp3")}),
        queue({track("track-b", "b.mp3")}));
    return require(invalidRuntime.apply(PlaybackSessionStarted{invalidScope}).empty(),
            "Malformed initial scopes must not emit actions")
        && require(invalidRuntime.playbackState() == BackgroundMusicPlaybackState::Stopped
                && invalidRuntime.playlistSession() == nullptr
                && invalidRuntime.itemSession() == nullptr,
            "Malformed initial scopes must be atomically rejected");
}

bool verifyEnabledStartupCombinations()
{
    struct Combination
    {
        bool playlistEnabled = true;
        bool itemEnabled = true;
    };

    const std::vector<Combination> combinations{
        {true, true},
        {true, false},
        {false, true},
        {false, false}};

    for (const Combination combination : combinations)
    {
        BackgroundMusicRuntime runtime{};
        const auto actions = runtime.apply(PlaybackSessionStarted{scope(
            "playlist-enabled-matrix",
            "item-enabled-matrix",
            queue(
                {track("playlist-enabled-track", "playlist-enabled.mp3")},
                1.0F,
                MusicQueueMode::ListLoop,
                combination.playlistEnabled),
            queue(
                {track("item-enabled-track", "item-enabled.mp3")},
                1.0F,
                MusicQueueMode::ListLoop,
                combination.itemEnabled))});

        const std::size_t expectedStarts = static_cast<std::size_t>(combination.playlistEnabled)
            + static_cast<std::size_t>(combination.itemEnabled);
        if (!require(actions.size() == expectedStarts,
                "Startup must emit one StartSource per enabled non-empty scope")
            || !require((runtime.playlistSession() != nullptr) == combination.playlistEnabled,
                "Playlist Enabled must control Playlist MusicSession existence")
            || !require((runtime.itemSession() != nullptr) == combination.itemEnabled,
                "Item Enabled must control Item MusicSession existence")
            || !require(runtime.playbackState() == BackgroundMusicPlaybackState::Playing,
                "A valid all-disabled scope must still establish the main Playing state"))
        {
            return false;
        }

        std::size_t actionIndex = 0U;
        if (combination.playlistEnabled)
        {
            if (!require(startAction(actions[actionIndex]) != nullptr
                    && startAction(actions[actionIndex])->scope == MusicScope::PlaylistGlobal,
                "Enabled Playlist startup must precede enabled Item startup"))
            {
                return false;
            }
            ++actionIndex;
        }
        if (combination.itemEnabled
            && !require(startAction(actions[actionIndex]) != nullptr
                    && startAction(actions[actionIndex])->scope == MusicScope::Item,
                "Enabled Item startup must emit the Item StartSource"))
        {
            return false;
        }
    }

    BackgroundMusicRuntime disabledEmptyRuntime{};
    const auto disabledEmptyActions = disabledEmptyRuntime.apply(PlaybackSessionStarted{scope(
        "playlist-disabled-empty",
        "item-disabled-empty",
        queue({}, 1.0F, MusicQueueMode::ListLoop, false),
        queue({}, 1.0F, MusicQueueMode::ListLoop, false))});
    return require(disabledEmptyActions.empty()
            && disabledEmptyRuntime.playlistSession() == nullptr
            && disabledEmptyRuntime.itemSession() == nullptr,
        "Disabled empty queues must remain sessionless rather than silent enabled sessions");
}

bool verifyEnabledRuntimeReconciliation()
{
    BackgroundMusicRuntime runtime{};
    const MusicQueueSnapshot playlistEnabled = queue(
        {track("playlist-a", "playlist-a.mp3"), track("playlist-b", "playlist-b.mp3")},
        0.75F,
        MusicQueueMode::Shuffle);
    const MusicQueueSnapshot playlistDisabled = queue(
        {track("playlist-a", "playlist-a.mp3"), track("playlist-b", "playlist-b.mp3")},
        0.75F,
        MusicQueueMode::Shuffle,
        false);
    const MusicQueueSnapshot itemEnabled = queue({track("item-a", "item-a.mp3")});
    const MusicQueueSnapshot itemDisabled = queue(
        {track("item-a", "item-a.mp3")},
        1.0F,
        MusicQueueMode::ListLoop,
        false);

    const auto initialActions = runtime.apply(PlaybackSessionStarted{scope(
        "playlist-toggle",
        "item-toggle",
        playlistEnabled,
        itemEnabled)});
    if (!require(initialActions.size() == 2U,
            "Enabled reconciliation setup must start both scopes"))
    {
        return false;
    }

    const MusicSourceToken initialPlaylistToken = startAction(initialActions[0])->token;
    const MusicSourceToken initialItemToken = startAction(initialActions[1])->token;
    const auto playlistDisableActions = runtime.apply(PlaylistMusicConfigChanged{
        playlistId("playlist-toggle"), playlistDisabled});
    if (!require(playlistDisableActions.size() == 1U
            && stopAction(playlistDisableActions[0]) != nullptr
            && stopAction(playlistDisableActions[0])->token == initialPlaylistToken
            && runtime.playlistSession() == nullptr,
        "Disabling active Playlist BGM must stop and destroy only its session")
        || !require(runtime.itemSession() != nullptr
                && runtime.itemSession()->sourceToken()
                    == std::optional<MusicSourceToken>{initialItemToken},
            "Disabling Playlist BGM must preserve the active Item session")
        || !require(runtime.apply(PlaylistMusicTrackSwitchRequested{
                    playlistId("playlist-toggle"), trackId("playlist-b")}).empty()
                && runtime.playlistSession() == nullptr,
            "Manual Playlist switching must not resurrect a disabled scope"))
    {
        return false;
    }

    const auto playlistEnableActions = runtime.apply(PlaylistMusicConfigChanged{
        playlistId("playlist-toggle"), playlistEnabled});
    const StartSource* const playlistRestart = playlistEnableActions.size() == 1U
        ? startAction(playlistEnableActions[0])
        : nullptr;
    if (!require(playlistRestart != nullptr
            && playlistRestart->token != initialPlaylistToken
            && runtime.playlistSession() != nullptr
            && runtime.playlistSession()->config().mode == MusicQueueMode::Shuffle,
        "Re-enabling active Playlist BGM must create a fresh session from latest config")
        || !require(runtime.itemSession()->sourceToken()
                == std::optional<MusicSourceToken>{initialItemToken},
            "Re-enabling Playlist BGM must not restart Item BGM"))
    {
        return false;
    }
    const MusicSourceToken playingPlaylistToken = playlistRestart->token;

    (void)runtime.apply(PlaybackPaused{});
    const auto itemDisableActions = runtime.apply(ItemMusicConfigChanged{
        playlistId("playlist-toggle"), itemId("item-toggle"), itemDisabled});
    if (!require(itemDisableActions.size() == 1U
            && stopAction(itemDisableActions[0]) != nullptr
            && stopAction(itemDisableActions[0])->token == initialItemToken
            && runtime.itemSession() == nullptr,
        "Disabling active Item BGM while paused must destroy only its session")
        || !require(runtime.playlistSession()->sourceToken()
                == std::optional<MusicSourceToken>{playingPlaylistToken},
            "Disabling Item BGM must preserve Playlist BGM")
        || !require(runtime.apply(ItemMusicTrackSwitchRequested{
                    playlistId("playlist-toggle"),
                    itemId("item-toggle"),
                    trackId("item-a")}).empty()
                && runtime.itemSession() == nullptr,
            "Manual Item switching must not resurrect a disabled scope"))
    {
        return false;
    }

    const auto itemEnableActions = runtime.apply(ItemMusicConfigChanged{
        playlistId("playlist-toggle"), itemId("item-toggle"), itemEnabled});
    const StartSource* const pausedItemRestart = itemEnableActions.size() == 1U
        ? startAction(itemEnableActions[0])
        : nullptr;
    if (!require(pausedItemRestart != nullptr
            && pausedItemRestart->token != initialItemToken
            && pausedItemRestart->activity == PlaybackActivity::Paused,
        "Re-enabling active Item BGM while paused must fresh-start it paused"))
    {
        return false;
    }

    const auto pausedPlaylistDisable = runtime.apply(PlaylistMusicConfigChanged{
        playlistId("playlist-toggle"), playlistDisabled});
    const auto pausedPlaylistEnable = runtime.apply(PlaylistMusicConfigChanged{
        playlistId("playlist-toggle"), playlistEnabled});
    const StartSource* const pausedPlaylistRestart = pausedPlaylistEnable.size() == 1U
        ? startAction(pausedPlaylistEnable[0])
        : nullptr;
    if (!require(pausedPlaylistDisable.size() == 1U
            && pausedPlaylistRestart != nullptr
            && pausedPlaylistRestart->token != playingPlaylistToken
            && pausedPlaylistRestart->activity == PlaybackActivity::Paused,
        "Playlist disable/re-enable while paused must replace transient state without playing")
        || !require(runtime.apply(PlaybackResumed{}).empty()
                && runtime.playlistSession()->sourceToken()
                    == std::optional<MusicSourceToken>{pausedPlaylistRestart->token}
                && runtime.itemSession()->sourceToken()
                    == std::optional<MusicSourceToken>{pausedItemRestart->token},
            "Resume must preserve freshly enabled paused sessions without duplicate starts"))
    {
        return false;
    }

    (void)runtime.apply(PlaybackStopped{});
    if (!require(runtime.apply(PlaylistMusicConfigChanged{
                    playlistId("playlist-toggle"), playlistEnabled}).empty()
            && runtime.apply(ItemMusicConfigChanged{
                    playlistId("playlist-toggle"), itemId("item-toggle"), itemEnabled}).empty()
            && runtime.playlistSession() == nullptr
            && runtime.itemSession() == nullptr,
        "Enabled config events while stopped must not create sessions"))
    {
        return false;
    }

    const auto nextPlay = runtime.apply(PlaybackSessionStarted{scope(
        "playlist-toggle",
        "item-toggle",
        playlistDisabled,
        itemEnabled)});
    return require(nextPlay.size() == 1U
            && startAction(nextPlay[0]) != nullptr
            && startAction(nextPlay[0])->scope == MusicScope::Item
            && runtime.playlistSession() == nullptr
            && runtime.itemSession() != nullptr,
        "The next Play must consume latest Enabled snapshots independently");
}

bool verifyMixedEnabledScopeTransitions()
{
    const MusicQueueSnapshot playlistOn = queue({track("playlist-on", "playlist-on.mp3")});
    const MusicQueueSnapshot playlistOff = queue(
        {track("playlist-off", "playlist-off.mp3")},
        1.0F,
        MusicQueueMode::ListLoop,
        false);
    const MusicQueueSnapshot itemOn = queue({track("item-on", "item-on.mp3")});
    const MusicQueueSnapshot itemOff = queue(
        {track("item-off", "item-off.mp3")},
        1.0F,
        MusicQueueMode::ListLoop,
        false);

    BackgroundMusicRuntime runtime{};
    const auto initialActions = runtime.apply(PlaybackSessionStarted{scope(
        "playlist-a", "item-a", playlistOff, itemOn)});
    if (!require(initialActions.size() == 1U
            && startAction(initialActions[0]) != nullptr
            && startAction(initialActions[0])->scope == MusicScope::Item
            && runtime.playlistSession() == nullptr,
        "Playlist off / Item on must start Item without a Playlist session"))
    {
        return false;
    }

    const MusicSourceToken firstItemToken = startAction(initialActions[0])->token;
    const auto itemSwitch = runtime.apply(ActivePlaybackScopeChanged{scope(
        "playlist-a", "item-b", playlistOff, itemOn)});
    if (!require(itemSwitch.size() == 2U
            && stopAction(itemSwitch[0]) != nullptr
            && startAction(itemSwitch[1]) != nullptr
            && stopAction(itemSwitch[0])->token == firstItemToken
            && startAction(itemSwitch[1])->scope == MusicScope::Item
            && runtime.playlistSession() == nullptr,
        "Item switching must work when Playlist BGM is disabled")
        || !require(runtime.apply(ActivePlaybackScopeChanged{scope(
                    "playlist-a", "item-b", playlistOff, itemOff)}).empty(),
            "A same-Item visual loop must not reinterpret config or restart BGM"))
    {
        return false;
    }

    const MusicSourceToken secondItemToken = startAction(itemSwitch[1])->token;
    const auto disabledItemSwitch = runtime.apply(ActivePlaybackScopeChanged{scope(
        "playlist-a", "item-c", playlistOff, itemOff)});
    if (!require(disabledItemSwitch.size() == 1U
            && stopAction(disabledItemSwitch[0]) != nullptr
            && stopAction(disabledItemSwitch[0])->token == secondItemToken
            && runtime.playlistSession() == nullptr
            && runtime.itemSession() == nullptr,
        "Switching to a disabled Item must destroy the old Item session only"))
    {
        return false;
    }

    const auto itemReenabledByScope = runtime.apply(ActivePlaybackScopeChanged{scope(
        "playlist-a", "item-d", playlistOff, itemOn)});
    if (!require(itemReenabledByScope.size() == 1U
            && startAction(itemReenabledByScope[0]) != nullptr
            && startAction(itemReenabledByScope[0])->scope == MusicScope::Item,
        "A new enabled Item must start while Playlist BGM remains disabled"))
    {
        return false;
    }

    const MusicSourceToken fourthItemToken = startAction(itemReenabledByScope[0])->token;
    const auto playlistOnItemOff = runtime.apply(ActivePlaybackScopeChanged{scope(
        "playlist-b", "item-e", playlistOn, itemOff)});
    if (!require(playlistOnItemOff.size() == 2U
            && stopAction(playlistOnItemOff[0]) != nullptr
            && stopAction(playlistOnItemOff[0])->token == fourthItemToken
            && startAction(playlistOnItemOff[1]) != nullptr
            && startAction(playlistOnItemOff[1])->scope == MusicScope::PlaylistGlobal
            && runtime.playlistSession() != nullptr
            && runtime.itemSession() == nullptr,
        "Playlist switch must independently start Playlist on / Item off"))
    {
        return false;
    }

    const MusicSourceToken playlistToken = startAction(playlistOnItemOff[1])->token;
    const auto playlistOffItemOn = runtime.apply(ActivePlaybackScopeChanged{scope(
        "playlist-c", "item-f", playlistOff, itemOn)});
    return require(playlistOffItemOn.size() == 2U
            && stopAction(playlistOffItemOn[0]) != nullptr
            && stopAction(playlistOffItemOn[0])->token == playlistToken
            && startAction(playlistOffItemOn[1]) != nullptr
            && startAction(playlistOffItemOn[1])->scope == MusicScope::Item
            && runtime.playlistSession() == nullptr
            && runtime.itemSession() != nullptr,
        "Playlist switch must independently start Playlist off / Item on");
}

bool verifyStopAndStaleTokens()
{
    BackgroundMusicRuntime runtime{};
    const ActiveMusicScopeSnapshot initialScope = scope(
        "playlist-a",
        "item-a",
        queue({track("playlist-track", "playlist.mp3")}),
        queue({track("item-track", "item.mp3")}));
    const std::vector<MusicRuntimeAction> startActions = runtime.apply(
        PlaybackSessionStarted{initialScope});
    const MusicSourceToken playlistToken = startAction(startActions[0])->token;
    const MusicSourceToken itemToken = startAction(startActions[1])->token;
    const std::vector<MusicRuntimeAction> stopActions = runtime.apply(PlaybackStopped{});
    if (!require(stopActions.size() == 2U
            && stopAction(stopActions[0]) != nullptr
            && stopAction(stopActions[1]) != nullptr
            && stopAction(stopActions[0])->token == itemToken
            && stopAction(stopActions[1])->token == playlistToken,
        "Stop must order Item StopSource before Playlist StopSource")
        || !require(runtime.playbackState() == BackgroundMusicPlaybackState::Stopped
                && !runtime.activePlaylistId().has_value()
                && !runtime.activeItemId().has_value()
                && runtime.playlistSession() == nullptr
                && runtime.itemSession() == nullptr,
            "Stop must destroy sessions and clear scope identities")
        || !require(runtime.apply(PlaybackStopped{}).empty(), "Repeated stop must be a no-op")
        || !require(runtime.apply(MusicSourceStarted{itemToken}).empty()
                && runtime.apply(MusicSourceEnded{playlistToken}).empty()
                && runtime.apply(MusicSourceFailed{
                    itemToken,
                    MusicSourceFailureClass::UnplayableTrack}).empty(),
            "Facts for destroyed-session tokens must be ignored"))
    {
        return false;
    }

    const std::vector<MusicRuntimeAction> freshActions = runtime.apply(
        PlaybackSessionStarted{initialScope});
    return require(freshActions.size() == 2U
            && startAction(freshActions[0])->token != playlistToken
            && startAction(freshActions[1])->token != itemToken,
        "Stop followed by start must allocate fresh tokens without reuse");
}

bool verifyItemScopeLifecycle()
{
    BackgroundMusicRuntime runtime{};
    const ActiveMusicScopeSnapshot itemOne = scope(
        "playlist-a",
        "item-one",
        queue({track("playlist-track", "playlist.mp3")}),
        queue({track("item-one-track", "item-one.mp3")}));
    const std::vector<MusicRuntimeAction> initialActions = runtime.apply(
        PlaybackSessionStarted{itemOne});
    const MusicSourceToken playlistToken = startAction(initialActions[0])->token;
    const MusicSourceToken firstItemToken = startAction(initialActions[1])->token;
    const MusicQueueSnapshot playlistConfig = runtime.playlistSession()->config();

    const ActiveMusicScopeSnapshot itemTwo = scope(
        "playlist-a",
        "item-two",
        queue({track("different-playlist-track", "different.mp3")}),
        queue({track("item-two-track", "item-two.mp3")}));
    const std::vector<MusicRuntimeAction> itemChangeActions = runtime.apply(
        ActivePlaybackScopeChanged{itemTwo});
    if (!require(itemChangeActions.size() == 2U
            && stopAction(itemChangeActions[0]) != nullptr
            && startAction(itemChangeActions[1]) != nullptr
            && stopAction(itemChangeActions[0])->token == firstItemToken
            && startAction(itemChangeActions[1])->scope == MusicScope::Item,
        "Same-Playlist item replacement must stop old Item then start new Item")
        || !require(runtime.playlistSession()->sourceToken()
                == std::optional<MusicSourceToken>{playlistToken}
            && runtime.playlistSession()->config() == playlistConfig,
            "Same-Playlist item replacement must preserve Playlist session/token/config")
        || !require(startAction(itemChangeActions[1])->token != firstItemToken,
            "A replacement Item session must allocate a fresh token")
        || !require(runtime.activeItemId() == std::optional<ItemId>{itemId("item-two")},
            "Item replacement must adopt the new ItemId"))
    {
        return false;
    }

    const MusicSourceToken secondItemToken = startAction(itemChangeActions[1])->token;
    if (!require(runtime.apply(MusicSourceStarted{firstItemToken}).empty()
            && runtime.apply(MusicSourceEnded{firstItemToken}).empty()
            && runtime.apply(MusicSourceFailed{
                firstItemToken,
                MusicSourceFailureClass::TransientResourceFailure}).empty(),
        "Stale Item facts must not alter the replacement Item session"))
    {
        return false;
    }

    const ActiveMusicScopeSnapshot noItem = scope(
        "playlist-a",
        std::nullopt,
        queue({track("ignored-playlist-track", "ignored.mp3")}),
        std::nullopt);
    const std::vector<MusicRuntimeAction> removeActions = runtime.apply(
        ActivePlaybackScopeChanged{noItem});
    if (!require(removeActions.size() == 1U
            && stopAction(removeActions[0]) != nullptr
            && stopAction(removeActions[0])->token == secondItemToken
            && runtime.itemSession() == nullptr
            && !runtime.activeItemId().has_value(),
        "Removing an Item scope must stop only its Item source")
        || !require(runtime.playlistSession()->sourceToken()
                == std::optional<MusicSourceToken>{playlistToken},
            "Removing an Item scope must not restart PlaylistGlobal"))
    {
        return false;
    }

    const ActiveMusicScopeSnapshot itemThree = scope(
        "playlist-a",
        "item-three",
        queue({track("ignored-again", "ignored-again.mp3")}),
        queue({track("item-three-track", "item-three.mp3")}));
    const std::vector<MusicRuntimeAction> addActions = runtime.apply(
        ActivePlaybackScopeChanged{itemThree});
    return require(addActions.size() == 1U
            && startAction(addActions[0]) != nullptr
            && startAction(addActions[0])->scope == MusicScope::Item
            && runtime.playlistSession()->sourceToken()
                == std::optional<MusicSourceToken>{playlistToken},
        "Adding an Item scope must start only the new Item session");
}

bool verifyPlaylistLifecycleAndDeferredEvents()
{
    BackgroundMusicRuntime runtime{};
    const ActiveMusicScopeSnapshot scopeOne = scope(
        "playlist-one",
        "item-one",
        queue({track("playlist-one-track", "playlist-one.mp3")}),
        queue({track("item-one-track", "item-one.mp3")}));
    const std::vector<MusicRuntimeAction> initialActions = runtime.apply(
        PlaybackSessionStarted{scopeOne});
    const MusicSourceToken oldPlaylistToken = startAction(initialActions[0])->token;
    const MusicSourceToken oldItemToken = startAction(initialActions[1])->token;

    const ActiveMusicScopeSnapshot malformedScope = scope(
        "",
        "item-malformed",
        queue({track("malformed-playlist-track", "malformed-playlist.mp3")}),
        queue({track("malformed-item-track", "malformed-item.mp3")}));
    if (!require(runtime.apply(ActivePlaybackScopeChanged{malformedScope}).empty()
            && runtime.activePlaylistId() == std::optional<PlaylistId>{playlistId("playlist-one")}
            && runtime.activeItemId() == std::optional<ItemId>{itemId("item-one")}
            && runtime.playlistSession()->sourceToken()
                == std::optional<MusicSourceToken>{oldPlaylistToken}
            && runtime.itemSession()->sourceToken() == std::optional<MusicSourceToken>{oldItemToken},
        "A malformed active scope change must be atomically rejected"))
    {
        return false;
    }

    const ActiveMusicScopeSnapshot scopeTwo = scope(
        "playlist-two",
        "item-two",
        queue({track("playlist-two-track", "playlist-two.mp3")}),
        queue({track("item-two-track", "item-two.mp3")}));
    const std::vector<MusicRuntimeAction> playlistChangeActions = runtime.apply(
        ActivePlaybackScopeChanged{scopeTwo});
    if (!require(playlistChangeActions.size() == 4U
            && stopAction(playlistChangeActions[0]) != nullptr
            && stopAction(playlistChangeActions[1]) != nullptr
            && startAction(playlistChangeActions[2]) != nullptr
            && startAction(playlistChangeActions[3]) != nullptr
            && stopAction(playlistChangeActions[0])->token == oldItemToken
            && stopAction(playlistChangeActions[1])->token == oldPlaylistToken
            && startAction(playlistChangeActions[2])->scope == MusicScope::PlaylistGlobal
            && startAction(playlistChangeActions[3])->scope == MusicScope::Item,
        "Playlist replacement must stop Item/Playlist then start Playlist/Item")
        || !require(startAction(playlistChangeActions[2])->token != oldPlaylistToken
                && startAction(playlistChangeActions[3])->token != oldItemToken,
            "Playlist replacement must allocate fresh globally unique tokens")
        || !require(runtime.apply(MusicSourceEnded{oldPlaylistToken}).empty()
                && runtime.apply(MusicSourceFailed{
                    oldItemToken,
                    MusicSourceFailureClass::UnplayableTrack}).empty(),
            "Old Playlist and Item facts must be stale after Playlist replacement"))
    {
        return false;
    }

    const MusicSourceToken activePlaylistToken = startAction(playlistChangeActions[2])->token;
    if (!require(runtime.apply(MusicSourceStarted{activePlaylistToken}).empty()
            && runtime.playlistSession()->phase() == MusicSessionPhase::Playing
            && runtime.apply(MusicSourceStarted{activePlaylistToken}).empty()
            && runtime.playlistSession()->phase() == MusicSessionPhase::Playing,
        "Only a current PendingStart token may acknowledge source startup once"))
    {
        return false;
    }

    const MusicQueueSnapshot retainedConfig = runtime.playlistSession()->config();
    const ActiveMusicScopeSnapshot sameIdentityChangedConfig = scope(
        "playlist-two",
        "item-two",
        queue({track("replacement-would-be-live-edit", "replacement.mp3")}),
        queue({track("item-replacement", "item-replacement.mp3")}));
    if (!require(runtime.apply(ActivePlaybackScopeChanged{sameIdentityChangedConfig}).empty()
            && runtime.playlistSession()->config() == retainedConfig
            && runtime.playlistSession()->sourceToken()
                == std::optional<MusicSourceToken>{activePlaylistToken},
        "Same-identity scope events must defer config reconciliation without restart")
        || !require(runtime.apply(PlaylistMusicConfigChanged{
                    playlistId("inactive-playlist"),
                    queue({track("ignored", "ignored.mp3")})}).empty()
            && runtime.apply(ItemMusicConfigChanged{
                    playlistId("playlist-two"),
                    itemId("inactive-item"),
                    queue({track("ignored-item", "ignored-item.mp3")})}).empty()
            && runtime.playlistSession()->config() == retainedConfig,
            "Mismatched music configuration events must not mutate an active session"))
    {
        return false;
    }

    return require(runtime.apply(MusicExecutionAvailable{}).empty(),
        "MusicExecutionAvailable must not restart a Playing session");
}

bool verifyItemABAFreshnessAndPausedReplacement()
{
    BackgroundMusicRuntime runtime{};
    const ActiveMusicScopeSnapshot itemOne = scope(
        "playlist-a",
        "item-one",
        queue({track("playlist-track", "playlist.mp3")}),
        queue({track("item-one-track", "item-one.mp3")}));
    const std::vector<MusicRuntimeAction> firstActions = runtime.apply(
        PlaybackSessionStarted{itemOne});
    const MusicSourceToken playlistToken = startAction(firstActions[0])->token;
    const MusicSourceToken itemOneFirstToken = startAction(firstActions[1])->token;

    const ActiveMusicScopeSnapshot itemTwo = scope(
        "playlist-a",
        "item-two",
        queue({track("ignored", "ignored.mp3")}),
        queue({track("item-two-track", "item-two.mp3")}));
    (void)runtime.apply(ActivePlaybackScopeChanged{itemTwo});
    const std::vector<MusicRuntimeAction> returnActions = runtime.apply(
        ActivePlaybackScopeChanged{itemOne});
    if (!require(returnActions.size() == 2U
            && startAction(returnActions[1]) != nullptr
            && startAction(returnActions[1])->trackId == trackId("item-one-track")
            && startAction(returnActions[1])->token != itemOneFirstToken
            && runtime.playlistSession()->sourceToken()
                == std::optional<MusicSourceToken>{playlistToken},
        "Item A to B to A must restart Item A fresh while preserving PlaylistGlobal"))
    {
        return false;
    }

    (void)runtime.apply(PlaybackPaused{});
    const ActiveMusicScopeSnapshot pausedItemTwo = scope(
        "playlist-a",
        "item-two",
        queue({track("ignored-paused", "ignored-paused.mp3")}),
        queue({track("item-two-paused-track", "item-two-paused.mp3")}));
    const std::vector<MusicRuntimeAction> pausedActions = runtime.apply(
        ActivePlaybackScopeChanged{pausedItemTwo});
    if (!require(runtime.playbackState() == BackgroundMusicPlaybackState::Paused
            && pausedActions.size() == 2U
            && startAction(pausedActions[1]) != nullptr
            && startAction(pausedActions[1])->activity == PlaybackActivity::Paused,
        "Item replacement while paused must preserve activity and start the new Item paused"))
    {
        return false;
    }

    const ActiveMusicScopeSnapshot pausedPlaylistTwo = scope(
        "playlist-b",
        "item-b",
        queue({track("playlist-b-track", "playlist-b.mp3")}),
        queue({track("item-b-track", "item-b.mp3")}));
    const std::vector<MusicRuntimeAction> pausedPlaylistActions = runtime.apply(
        ActivePlaybackScopeChanged{pausedPlaylistTwo});
    return require(pausedPlaylistActions.size() == 4U
            && startAction(pausedPlaylistActions[2]) != nullptr
            && startAction(pausedPlaylistActions[3]) != nullptr
            && startAction(pausedPlaylistActions[2])->activity == PlaybackActivity::Paused
            && startAction(pausedPlaylistActions[3])->activity == PlaybackActivity::Paused,
        "Playlist replacement while paused must start both fresh scopes paused");
}

bool verifyListLoopNaturalEndProgression()
{
    BackgroundMusicRuntime runtime{};
    const ActiveMusicScopeSnapshot initialScope = scope(
        "playlist-list-loop",
        std::nullopt,
        queue({
            track("track-a", "a.mp3"),
            track("track-b", "b.mp3")}),
        std::nullopt);

    const std::vector<MusicRuntimeAction> initialActions = runtime.apply(
        PlaybackSessionStarted{initialScope});
    if (!require(initialActions.size() == 1U && startAction(initialActions[0]) != nullptr,
            "ListLoop startup must start its first configured track"))
    {
        return false;
    }

    const MusicSourceToken firstToken = startAction(initialActions[0])->token;
    (void)runtime.apply(MusicSourceStarted{firstToken});
    const std::vector<MusicRuntimeAction> endActions = runtime.apply(MusicSourceEnded{firstToken});
    return require(endActions.size() == 2U
            && stopAction(endActions[0]) != nullptr
            && startAction(endActions[1]) != nullptr
            && stopAction(endActions[0])->token == firstToken
            && startAction(endActions[1])->trackId == trackId("track-b")
            && startAction(endActions[1])->token != firstToken,
        "ListLoop EOF must replace the ended source with the next track and a fresh token");
}

bool verifyManualTrackSwitching()
{
    BackgroundMusicRuntime runtime{};
    const std::vector<MusicRuntimeAction> initialActions = runtime.apply(PlaybackSessionStarted{
        scope(
            "playlist-manual-switch",
            "item-manual-switch",
            queue({
                track("playlist-a", "playlist-a.mp3"),
                track("playlist-b", "playlist-b.mp3", 0.75F),
                track("playlist-c", "playlist-c.mp3")},
                0.5F),
            queue({
                track("item-a", "item-a.mp3"),
                track("item-b", "item-b.mp3", 0.25F)},
                0.8F))});
    if (!require(initialActions.size() == 2U
            && startAction(initialActions[0]) != nullptr
            && startAction(initialActions[1]) != nullptr,
        "Manual-switch setup must start both active music scopes"))
    {
        return false;
    }

    const MusicSourceToken playlistInitialToken = startAction(initialActions[0])->token;
    const MusicSourceToken itemInitialToken = startAction(initialActions[1])->token;
    (void)runtime.apply(MusicSourceStarted{playlistInitialToken});
    (void)runtime.apply(MusicSourceStarted{itemInitialToken});

    const std::vector<MusicRuntimeAction> playlistSwitchActions = runtime.apply(
        PlaylistMusicTrackSwitchRequested{playlistId("playlist-manual-switch"), trackId("playlist-b")});
    const StartSource* const playlistSwitchStart = playlistSwitchActions.size() == 2U
        ? startAction(playlistSwitchActions[1])
        : nullptr;
    if (!require(playlistSwitchActions.size() == 2U
            && stopAction(playlistSwitchActions[0]) != nullptr
            && playlistSwitchStart != nullptr
            && stopAction(playlistSwitchActions[0])->token == playlistInitialToken
            && playlistSwitchStart->trackId == trackId("playlist-b")
            && playlistSwitchStart->token != playlistInitialToken
            && playlistSwitchStart->effectiveGain == 0.375F
            && playlistSwitchStart->activity == PlaybackActivity::Playing
            && runtime.playlistSession()->currentTrackId() == std::optional<TrackId>{trackId("playlist-b")}
            && runtime.itemSession()->currentTrackId() == std::optional<TrackId>{trackId("item-a")}
            && runtime.itemSession()->sourceToken()
                == std::optional<MusicSourceToken>{itemInitialToken},
        "A Playlist switch must replace only the requested scope with a fresh active source"))
    {
        return false;
    }

    const MusicSourceToken playlistBToken = playlistSwitchStart->token;
    (void)runtime.apply(MusicSourceStarted{playlistBToken});
    if (!require(runtime.apply(PlaylistMusicTrackSwitchRequested{
                    playlistId("playlist-manual-switch"), trackId("playlist-b")}).empty()
            && runtime.playlistSession()->sourceToken()
                == std::optional<MusicSourceToken>{playlistBToken}
            && runtime.apply(PlaylistMusicTrackSwitchRequested{
                    playlistId("inactive-playlist"), trackId("playlist-c")}).empty()
            && runtime.playlistSession()->currentTrackId()
                == std::optional<TrackId>{trackId("playlist-b")},
        "Same-track and inactive Playlist switch requests must be idempotent no-ops"))
    {
        return false;
    }

    const std::vector<MusicRuntimeAction> itemSwitchActions = runtime.apply(
        ItemMusicTrackSwitchRequested{
            playlistId("playlist-manual-switch"),
            itemId("item-manual-switch"),
            trackId("item-b")});
    const StartSource* const itemSwitchStart = itemSwitchActions.size() == 2U
        ? startAction(itemSwitchActions[1])
        : nullptr;
    if (!require(itemSwitchActions.size() == 2U
            && stopAction(itemSwitchActions[0]) != nullptr
            && itemSwitchStart != nullptr
            && stopAction(itemSwitchActions[0])->token == itemInitialToken
            && itemSwitchStart->trackId == trackId("item-b")
            && itemSwitchStart->token != itemInitialToken
            && itemSwitchStart->effectiveGain == 0.2F
            && runtime.itemSession()->currentTrackId() == std::optional<TrackId>{trackId("item-b")}
            && runtime.playlistSession()->sourceToken()
                == std::optional<MusicSourceToken>{playlistBToken}
            && runtime.apply(ItemMusicTrackSwitchRequested{
                    playlistId("playlist-manual-switch"),
                    itemId("inactive-item"),
                    trackId("item-a")}).empty(),
        "An Item switch must isolate Item state and ignore inactive Item requests"))
    {
        return false;
    }

    const MusicSourceToken itemBToken = itemSwitchStart->token;
    (void)runtime.apply(MusicSourceStarted{itemBToken});
    const std::vector<MusicRuntimeAction> naturalEndActions = runtime.apply(MusicSourceEnded{playlistBToken});
    const StartSource* const naturalEndStart = naturalEndActions.size() == 2U
        ? startAction(naturalEndActions[1])
        : nullptr;
    if (!require(naturalEndStart != nullptr
            && naturalEndStart->trackId == trackId("playlist-c")
            && runtime.playlistSession()->currentTrackId()
                == std::optional<TrackId>{trackId("playlist-c")},
        "ListLoop must continue from a manually selected track using normal queue order"))
    {
        return false;
    }

    const MusicSourceToken playlistCToken = naturalEndStart->token;
    (void)runtime.apply(MusicSourceStarted{playlistCToken});
    const std::vector<MusicRuntimeAction> invalidTargetActions = runtime.apply(
        PlaylistMusicTrackSwitchRequested{playlistId("playlist-manual-switch"), trackId("missing")});
    if (!require(invalidTargetActions.empty()
            && runtime.playlistSession()->sourceToken()
                == std::optional<MusicSourceToken>{playlistCToken}
            && runtime.playlistSession()->currentTrackId()
                == std::optional<TrackId>{trackId("playlist-c")},
        "An unknown TrackId must not stop or replace the current source"))
    {
        return false;
    }

    const std::vector<MusicRuntimeAction> failedTargetActions = runtime.apply(
        PlaylistMusicTrackSwitchRequested{playlistId("playlist-manual-switch"), trackId("playlist-a")});
    const StartSource* const failedTargetStart = failedTargetActions.size() == 2U
        ? startAction(failedTargetActions[1])
        : nullptr;
    if (!require(failedTargetStart != nullptr, "A distinct valid TrackId must issue a replacement start"))
    {
        return false;
    }

    const std::vector<MusicRuntimeAction> retryAfterFailure = runtime.apply(MusicSourceFailed{
        failedTargetStart->token,
        MusicSourceFailureClass::UnplayableTrack});
    if (!require(retryAfterFailure.size() == 2U
            && startAction(retryAfterFailure[1]) != nullptr
            && startAction(retryAfterFailure[1])->trackId == trackId("playlist-b")
            && runtime.playlistSession()->currentTrackId()
                == std::optional<TrackId>{trackId("playlist-b")},
        "A manually selected track failure must follow the existing normal failure progression")
        || !require(runtime.apply(PlaybackPaused{}).empty(), "Pause setup must not recreate sources"))
    {
        return false;
    }

    const std::vector<MusicRuntimeAction> pausedSwitchActions = runtime.apply(
        PlaylistMusicTrackSwitchRequested{playlistId("playlist-manual-switch"), trackId("playlist-c")});
    return require(pausedSwitchActions.size() == 2U
            && startAction(pausedSwitchActions[1]) != nullptr
            && startAction(pausedSwitchActions[1])->activity == PlaybackActivity::Paused,
        "A manual switch while paused must create the replacement source paused");
}

bool verifyTerminalRoutingStalenessAndPausedReplacement()
{
    BackgroundMusicRuntime runtime{};
    const ActiveMusicScopeSnapshot initialScope = scope(
        "playlist-terminal-routing",
        "item-terminal-routing",
        queue({track("playlist-a", "playlist-a.mp3"), track("playlist-b", "playlist-b.mp3")}),
        queue({track("item-a", "item-a.mp3"), track("item-b", "item-b.mp3")}));
    const std::vector<MusicRuntimeAction> initialActions = runtime.apply(
        PlaybackSessionStarted{initialScope});
    const MusicSourceToken playlistFirstToken = startAction(initialActions[0])->token;
    const MusicSourceToken itemFirstToken = startAction(initialActions[1])->token;
    (void)runtime.apply(MusicSourceStarted{playlistFirstToken});
    (void)runtime.apply(MusicSourceStarted{itemFirstToken});

    const std::vector<MusicRuntimeAction> playlistEndActions = runtime.apply(
        MusicSourceEnded{playlistFirstToken});
    if (!require(playlistEndActions.size() == 2U
            && stopAction(playlistEndActions[0]) != nullptr
            && startAction(playlistEndActions[1]) != nullptr
            && stopAction(playlistEndActions[0])->token == playlistFirstToken
            && startAction(playlistEndActions[1])->scope == MusicScope::PlaylistGlobal
            && startAction(playlistEndActions[1])->trackId == trackId("playlist-b")
            && runtime.itemSession()->sourceToken()
                == std::optional<MusicSourceToken>{itemFirstToken},
        "A Playlist terminal fact must replace only the Playlist session"))
    {
        return false;
    }

    const MusicSourceToken playlistSecondToken = startAction(playlistEndActions[1])->token;
    if (!require(runtime.apply(MusicSourceEnded{playlistFirstToken}).empty()
            && runtime.apply(MusicSourceStarted{playlistFirstToken}).empty()
            && runtime.playlistSession()->sourceToken()
                == std::optional<MusicSourceToken>{playlistSecondToken},
        "Terminal facts for a replaced source token must be stale no-ops"))
    {
        return false;
    }

    (void)runtime.apply(PlaybackPaused{});
    const std::vector<MusicRuntimeAction> itemEndActions = runtime.apply(
        MusicSourceEnded{itemFirstToken});
    if (!require(itemEndActions.size() == 2U
            && stopAction(itemEndActions[0]) != nullptr
            && startAction(itemEndActions[1]) != nullptr
            && startAction(itemEndActions[1])->scope == MusicScope::Item
            && startAction(itemEndActions[1])->trackId == trackId("item-b")
            && startAction(itemEndActions[1])->activity == PlaybackActivity::Paused
            && runtime.playlistSession()->sourceToken()
                == std::optional<MusicSourceToken>{playlistSecondToken},
        "A paused Item replacement must preserve Paused activity and Playlist independence"))
    {
        return false;
    }

    const std::vector<MusicRuntimeAction> failureActions = runtime.apply(MusicSourceFailed{
        playlistSecondToken,
        MusicSourceFailureClass::UnplayableTrack});
    if (!require(failureActions.size() == 2U
            && stopAction(failureActions[0]) != nullptr
            && startAction(failureActions[1]) != nullptr
            && stopAction(failureActions[0])->token == playlistSecondToken
            && startAction(failureActions[1])->trackId == trackId("playlist-a")
            && startAction(failureActions[1])->activity == PlaybackActivity::Paused,
        "An UnplayableTrack fact while PendingStart must replace the source with the next candidate"))
    {
        return false;
    }

    const MusicSourceToken playlistThirdToken = startAction(failureActions[1])->token;
    const std::vector<MusicRuntimeAction> transientActions = runtime.apply(MusicSourceFailed{
        playlistThirdToken,
        MusicSourceFailureClass::TransientResourceFailure});
    return require(transientActions.size() == 1U
            && stopAction(transientActions[0]) != nullptr
            && stopAction(transientActions[0])->token == playlistThirdToken
            && runtime.playlistSession()->phase() == MusicSessionPhase::WaitingForExecutor
            && runtime.playlistSession()->currentTrackId()
                == std::optional<TrackId>{trackId("playlist-a")}
            && !runtime.playlistSession()->sourceToken().has_value(),
        "Transient failure must wait without traversing the queue")
        && require(runtime.apply(MusicSourceFailed{
                    playlistThirdToken,
                    MusicSourceFailureClass::TransientResourceFailure}).empty(),
            "A transient terminal fact must become stale after token invalidation");
}

bool verifyExecutionAvailableRecoversWaitingSession()
{
    BackgroundMusicRuntime runtime{};
    const std::vector<MusicRuntimeAction> initialActions = runtime.apply(PlaybackSessionStarted{
        scope(
            "playlist-recovery",
            std::nullopt,
            queue({track("track-a", "a.mp3", 0.5F)}, 0.8F),
            std::nullopt)});
    if (!require(initialActions.size() == 1U && startAction(initialActions.front()) != nullptr,
            "Recovery test setup must create one Playlist start action"))
    {
        return false;
    }

    const MusicSourceToken firstToken = startAction(initialActions.front())->token;
    (void)runtime.apply(MusicSourceFailed{
        firstToken,
        MusicSourceFailureClass::TransientResourceFailure});

    const std::vector<MusicRuntimeAction> recoveryActions = runtime.apply(MusicExecutionAvailable{});
    const StartSource* const recoveryStart = recoveryActions.empty()
        ? nullptr
        : startAction(recoveryActions.front());
    return require(runtime.playlistSession() != nullptr
            && recoveryActions.size() == 1U
            && recoveryStart != nullptr
            && recoveryStart->trackId == trackId("track-a")
            && recoveryStart->token != firstToken
            && recoveryStart->sourcePath == std::filesystem::path{"a.mp3"}
            && recoveryStart->effectiveGain == 0.4F
            && runtime.playlistSession()->phase() == MusicSessionPhase::PendingStart,
        "ExecutionAvailable must retry the waiting TrackId with a fresh token and latest StartSource data");
}

bool verifyExecutionAvailableStateMatrixAndOrdering()
{
    BackgroundMusicRuntime stoppedRuntime{};
    if (!require(stoppedRuntime.apply(MusicExecutionAvailable{}).empty(),
            "ExecutionAvailable must be a no-op while stopped"))
    {
        return false;
    }

    BackgroundMusicRuntime pendingRuntime{};
    const std::vector<MusicRuntimeAction> pendingStartActions = pendingRuntime.apply(PlaybackSessionStarted{
        scope("playlist-pending", std::nullopt, queue({track("a", "a.mp3")}), std::nullopt)});
    if (!require(!pendingStartActions.empty()
            && pendingRuntime.apply(MusicExecutionAvailable{}).empty(),
        "ExecutionAvailable must not duplicate a PendingStart source"))
    {
        return false;
    }

    const MusicSourceToken pendingToken = startAction(pendingStartActions.front())->token;
    (void)pendingRuntime.apply(MusicSourceStarted{pendingToken});
    if (!require(pendingRuntime.apply(MusicExecutionAvailable{}).empty(),
            "ExecutionAvailable must not restart a Playing source"))
    {
        return false;
    }

    BackgroundMusicRuntime silentRuntime{};
    (void)silentRuntime.apply(PlaybackSessionStarted{
        scope("playlist-silent", std::nullopt, queue({}), std::nullopt)});
    if (!require(silentRuntime.apply(MusicExecutionAvailable{}).empty(),
            "ExecutionAvailable must not start a SilentEmptyQueue"))
    {
        return false;
    }

    BackgroundMusicRuntime noPlayableRuntime{};
    const std::vector<MusicRuntimeAction> noPlayableStartActions = noPlayableRuntime.apply(
        PlaybackSessionStarted{
            scope("playlist-no-playable", std::nullopt, queue({track("a", "a.mp3")}), std::nullopt)});
    (void)noPlayableRuntime.apply(MusicSourceFailed{
        startAction(noPlayableStartActions.front())->token,
        MusicSourceFailureClass::UnplayableTrack});
    if (!require(noPlayableRuntime.playlistSession() != nullptr
            && noPlayableRuntime.playlistSession()->phase() == MusicSessionPhase::NoPlayableTrack
            && noPlayableRuntime.apply(MusicExecutionAvailable{}).empty(),
        "ExecutionAvailable must not revive NoPlayableTrack"))
    {
        return false;
    }

    BackgroundMusicRuntime pausedRuntime{};
    const std::vector<MusicRuntimeAction> pausedStartActions = pausedRuntime.apply(PlaybackSessionStarted{
        scope(
            "playlist-paused-recovery",
            std::nullopt,
            queue({track("a", "a.mp3")}),
            std::nullopt,
            PlaybackActivity::Paused)});
    const MusicSourceToken pausedToken = startAction(pausedStartActions.front())->token;
    (void)pausedRuntime.apply(MusicSourceFailed{
        pausedToken,
        MusicSourceFailureClass::TransientResourceFailure});
    const std::vector<MusicRuntimeAction> pausedRecovery = pausedRuntime.apply(MusicExecutionAvailable{});
    if (!require(pausedRecovery.size() == 1U
            && startAction(pausedRecovery.front()) != nullptr
            && startAction(pausedRecovery.front())->activity == PlaybackActivity::Paused,
        "Paused WaitingForExecutor recovery must emit a paused StartSource"))
    {
        return false;
    }

    BackgroundMusicRuntime orderedRuntime{};
    const std::vector<MusicRuntimeAction> orderedStartActions = orderedRuntime.apply(PlaybackSessionStarted{
        scope(
            "playlist-two-waiting",
            "item-two-waiting",
            queue({track("playlist-a", "playlist-a.mp3")}),
            queue({track("item-a", "item-a.mp3")}))});
    (void)orderedRuntime.apply(MusicSourceFailed{
        startAction(orderedStartActions[0])->token,
        MusicSourceFailureClass::TransientResourceFailure});
    (void)orderedRuntime.apply(MusicSourceFailed{
        startAction(orderedStartActions[1])->token,
        MusicSourceFailureClass::TransientResourceFailure});
    const std::vector<MusicRuntimeAction> orderedRecovery = orderedRuntime.apply(MusicExecutionAvailable{});
    return require(orderedRecovery.size() == 2U
            && startAction(orderedRecovery[0]) != nullptr
            && startAction(orderedRecovery[1]) != nullptr
            && startAction(orderedRecovery[0])->scope == MusicScope::PlaylistGlobal
            && startAction(orderedRecovery[1])->scope == MusicScope::Item,
        "ExecutionAvailable must recover PlaylistGlobal before Item");
}

}

int main()
{
    const bool success = verifySnapshotValidation()
        && verifySessionStartAndPauseResume()
        && verifyEmptyScopesAndInvalidStart()
        && verifyEnabledStartupCombinations()
        && verifyEnabledRuntimeReconciliation()
        && verifyMixedEnabledScopeTransitions()
        && verifyStopAndStaleTokens()
        && verifyItemScopeLifecycle()
        && verifyPlaylistLifecycleAndDeferredEvents()
        && verifyItemABAFreshnessAndPausedReplacement()
        && verifyListLoopNaturalEndProgression()
        && verifyManualTrackSwitching()
        && verifyTerminalRoutingStalenessAndPausedReplacement()
        && verifyExecutionAvailableRecoversWaitingSession()
        && verifyExecutionAvailableStateMatrixAndOrdering();
    return success ? 0 : 1;
}
