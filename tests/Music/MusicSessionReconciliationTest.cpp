#include "Music/BackgroundMusicRuntime.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <optional>
#include <span>
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
    MusicQueueMode mode = MusicQueueMode::ListLoop,
    float queueVolume = 1.0F
)
{
    return MusicQueueSnapshot{std::vector<MusicTrackSnapshot>{tracks}, mode, queueVolume};
}

ActiveMusicScopeSnapshot scope(MusicQueueSnapshot config, PlaybackActivity activity)
{
    return ActiveMusicScopeSnapshot{
        playlistId("playlist"),
        std::nullopt,
        std::move(config),
        std::nullopt,
        activity};
}

ActiveMusicScopeSnapshot scopeWithItem(
    MusicQueueSnapshot playlistConfig,
    MusicQueueSnapshot itemConfig,
    PlaybackActivity activity = PlaybackActivity::Playing
)
{
    return ActiveMusicScopeSnapshot{
        playlistId("playlist"),
        itemId("item"),
        std::move(playlistConfig),
        std::move(itemConfig),
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

const SetSourceGain* gainAction(const MusicRuntimeAction& action)
{
    return std::get_if<SetSourceGain>(&action);
}

const StartSource* startActionForScope(
    const std::vector<MusicRuntimeAction>& actions,
    MusicScope scope
)
{
    const auto found = std::find_if(
        actions.cbegin(),
        actions.cend(),
        [scope](const MusicRuntimeAction& action)
        {
            const StartSource* const start = startAction(action);
            return start != nullptr && start->scope == scope;
        });
    return found == actions.cend() ? nullptr : startAction(*found);
}

bool startAndAcknowledge(
    BackgroundMusicRuntime& runtime,
    MusicQueueSnapshot config,
    MusicSourceToken& token,
    PlaybackActivity activity = PlaybackActivity::Playing
)
{
    const std::vector<MusicRuntimeAction> actions = runtime.apply(
        PlaybackSessionStarted{scope(std::move(config), activity)});
    if (actions.size() != 1U || startAction(actions.front()) == nullptr)
    {
        return false;
    }

    token = startAction(actions.front())->token;
    (void)runtime.apply(MusicSourceStarted{token});
    return true;
}

bool verifyRetainedCurrentConfigReplacementIsLive()
{
    BackgroundMusicRuntime runtime{};
    MusicSourceToken token{};
    if (!require(startAndAcknowledge(runtime, queue({track("a", "a.mp3"), track("b", "b.mp3")}), token),
            "Test setup must start and acknowledge Track A"))
    {
        return false;
    }

    const MusicQueueSnapshot replacement = queue({
        track("a", "a.mp3"), track("b", "b.mp3"), track("c", "c.mp3")});
    const std::vector<MusicRuntimeAction> actions = runtime.apply(PlaylistMusicConfigChanged{
        playlistId("playlist"), replacement});
    const MusicSession* const session = runtime.playlistSession();
    return require(actions.empty(), "Adding a non-current track must not restart Track A")
        && require(session != nullptr && session->sourceToken() == std::optional<MusicSourceToken>{token},
            "A retained current TrackId must keep its source token")
        && require(session != nullptr && session->config() == replacement,
            "A retained current TrackId must still receive the new queue snapshot");
}

bool verifySilentQueueStartsWhenMadeNonempty()
{
    BackgroundMusicRuntime runtime{};
    const std::vector<MusicRuntimeAction> initialActions = runtime.apply(
        PlaybackSessionStarted{scope(queue({}), PlaybackActivity::Playing)});
    if (!require(initialActions.empty(), "An empty session must begin silently"))
    {
        return false;
    }

    const std::vector<MusicRuntimeAction> actions = runtime.apply(PlaylistMusicConfigChanged{
        playlistId("playlist"), queue({track("a", "a.mp3")})});
    return require(actions.size() == 1U && startAction(actions.front()) != nullptr
            && startAction(actions.front())->trackId == trackId("a"),
        "A nonempty active queue must start its initial TrackId immediately")
        && require(runtime.playlistSession() != nullptr
                && runtime.playlistSession()->phase() == MusicSessionPhase::PendingStart,
            "An empty-to-nonempty reconciliation must enter PendingStart");
}

bool verifyRemovingCurrentStopsThenStartsSuccessor()
{
    BackgroundMusicRuntime runtime{};
    MusicSourceToken token{};
    if (!require(startAndAcknowledge(
            runtime,
            queue({track("a", "a.mp3"), track("b", "b.mp3"), track("c", "c.mp3")}),
            token),
        "Test setup must start and acknowledge Track A"))
    {
        return false;
    }

    const std::vector<MusicRuntimeAction> actions = runtime.apply(PlaylistMusicConfigChanged{
        playlistId("playlist"), queue({track("b", "b.mp3"), track("c", "c.mp3")})});
    return require(actions.size() == 2U && stopAction(actions[0]) != nullptr
            && startAction(actions[1]) != nullptr && stopAction(actions[0])->token == token
            && startAction(actions[1])->trackId == trackId("b")
            && startAction(actions[1])->token != token,
        "Removing the current TrackId must stop it before starting its surviving successor");
}

bool verifyNoPlayableTrackRevivesForNewTrack()
{
    BackgroundMusicRuntime runtime{};
    MusicSourceToken firstToken{};
    if (!require(startAndAcknowledge(
            runtime,
            queue({track("a", "a.mp3"), track("b", "b.mp3")}),
            firstToken),
        "Test setup must start and acknowledge Track A"))
    {
        return false;
    }

    const std::vector<MusicRuntimeAction> firstFailure = runtime.apply(MusicSourceFailed{
        firstToken, MusicSourceFailureClass::UnplayableTrack});
    if (!require(firstFailure.size() == 2U && startAction(firstFailure[1]) != nullptr,
            "Track A failure must advance to Track B"))
    {
        return false;
    }

    const MusicSourceToken secondToken = startAction(firstFailure[1])->token;
    const std::vector<MusicRuntimeAction> secondFailure = runtime.apply(MusicSourceFailed{
        secondToken, MusicSourceFailureClass::UnplayableTrack});
    if (!require(secondFailure.size() == 1U && stopAction(secondFailure[0]) != nullptr
            && runtime.playlistSession() != nullptr
            && runtime.playlistSession()->phase() == MusicSessionPhase::NoPlayableTrack,
        "All attempted tracks must enter NoPlayableTrack"))
    {
        return false;
    }

    const std::vector<MusicRuntimeAction> revival = runtime.apply(PlaylistMusicConfigChanged{
        playlistId("playlist"),
        queue({track("a", "a.mp3"), track("b", "b.mp3"), track("c", "c.mp3")})});
    return require(revival.size() == 1U && startAction(revival.front()) != nullptr
            && startAction(revival.front())->trackId == trackId("c"),
        "A new unattempted TrackId must revive NoPlayableTrack without retrying A or B first");
}

bool verifyRetainedListEditsAndGainUpdates()
{
    BackgroundMusicRuntime runtime{};
    MusicSourceToken token{};
    if (!require(startAndAcknowledge(
            runtime,
            queue({track("a", "a.mp3", 0.8F), track("b", "b.mp3"), track("c", "c.mp3")},
                MusicQueueMode::ListLoop,
                0.5F),
            token),
        "Test setup must start and acknowledge Track A"))
    {
        return false;
    }

    const MusicQueueSnapshot reordered = queue({
        track("b", "b.mp3"), track("a", "a.mp3", 0.8F), track("c", "c.mp3")},
        MusicQueueMode::ListLoop,
        0.5F);
    if (!require(runtime.apply(PlaylistMusicConfigChanged{playlistId("playlist"), reordered}).empty(),
            "Reordering around a retained current TrackId must not restart it"))
    {
        return false;
    }

    const std::vector<MusicRuntimeAction> endActions = runtime.apply(MusicSourceEnded{token});
    if (!require(endActions.size() == 2U && startAction(endActions[1]) != nullptr
            && startAction(endActions[1])->trackId == trackId("c"),
        "ListLoop must use the retained current TrackId position in the new order"))
    {
        return false;
    }

    const MusicSourceToken currentToken = startAction(endActions[1])->token;
    (void)runtime.apply(MusicSourceStarted{currentToken});
    const MusicQueueSnapshot gainChanged = queue({
        track("b", "b.mp3"), track("a", "a.mp3", 0.8F), track("c", "c.mp3", 0.5F)},
        MusicQueueMode::ListLoop,
        0.4F);
    const std::vector<MusicRuntimeAction> gainActions = runtime.apply(PlaylistMusicConfigChanged{
        playlistId("playlist"), gainChanged});
    if (!require(gainActions.size() == 1U && gainAction(gainActions.front()) != nullptr
            && gainAction(gainActions.front())->token == currentToken
            && gainAction(gainActions.front())->gain == 0.2F,
        "A retained current TrackId must receive one final effective-gain update"))
    {
        return false;
    }

    return require(runtime.apply(PlaylistMusicConfigChanged{playlistId("playlist"), gainChanged}).empty(),
        "An unchanged effective gain must not emit a redundant action");
}

bool verifyRetainedNoncurrentEditsAndIndividualGainUpdates()
{
    BackgroundMusicRuntime runtime{};
    MusicSourceToken token{};
    if (!require(startAndAcknowledge(
            runtime,
            queue({track("a", "a.mp3", 0.5F), track("b", "b.mp3", 0.5F), track("c", "c.mp3")},
                MusicQueueMode::ListLoop,
                0.5F),
            token),
        "Test setup must start retained Track A"))
    {
        return false;
    }

    const MusicQueueSnapshot noncurrentEdit = queue({
        track("a", "a.mp3", 0.5F), track("b", "b.mp3", 0.9F)},
        MusicQueueMode::ListLoop,
        0.5F);
    if (!require(runtime.apply(PlaylistMusicConfigChanged{
                    playlistId("playlist"), noncurrentEdit}).empty()
            && runtime.playlistSession() != nullptr
            && runtime.playlistSession()->sourceToken() == std::optional<MusicSourceToken>{token},
        "Removing or changing a non-current track must not restart or retune the retained source"))
    {
        return false;
    }

    const MusicQueueSnapshot queueOnlyGain = queue({
        track("a", "a.mp3", 0.5F), track("b", "b.mp3", 0.9F)},
        MusicQueueMode::ListLoop,
        0.8F);
    const std::vector<MusicRuntimeAction> queueGainActions = runtime.apply(
        PlaylistMusicConfigChanged{playlistId("playlist"), queueOnlyGain});
    if (!require(queueGainActions.size() == 1U && gainAction(queueGainActions.front()) != nullptr
            && gainAction(queueGainActions.front())->token == token
            && gainAction(queueGainActions.front())->gain == 0.4F,
        "A queue-volume-only edit must update the retained Playing source once"))
    {
        return false;
    }

    const MusicQueueSnapshot currentGain = queue({
        track("a", "a.mp3", 0.75F), track("b", "b.mp3", 0.9F)},
        MusicQueueMode::ListLoop,
        0.8F);
    const std::vector<MusicRuntimeAction> trackGainActions = runtime.apply(
        PlaylistMusicConfigChanged{playlistId("playlist"), currentGain});
    return require(trackGainActions.size() == 1U && gainAction(trackGainActions.front()) != nullptr
            && gainAction(trackGainActions.front())->gain == 0.6F,
        "A current-track-volume-only edit must update the retained Playing source once");
}

bool verifyPendingStartGainAndPausedReplacement()
{
    BackgroundMusicRuntime runtime{};
    const std::vector<MusicRuntimeAction> startActions = runtime.apply(PlaybackSessionStarted{
        scope(queue({track("a", "a.mp3", 0.5F)}, MusicQueueMode::ListLoop, 0.5F),
            PlaybackActivity::Paused)});
    if (!require(startActions.size() == 1U && startAction(startActions.front()) != nullptr,
            "A paused session must still issue its initial StartSource"))
    {
        return false;
    }

    const MusicSourceToken pendingToken = startAction(startActions.front())->token;
    const MusicQueueSnapshot gainChanged = queue(
        {track("a", "a.mp3", 0.8F)}, MusicQueueMode::ListLoop, 0.5F);
    if (!require(runtime.apply(PlaylistMusicConfigChanged{playlistId("playlist"), gainChanged}).empty(),
            "A PendingStart gain change must not issue SetSourceGain before Started"))
    {
        return false;
    }

    const std::vector<MusicRuntimeAction> acknowledgement = runtime.apply(MusicSourceStarted{pendingToken});
    if (!require(acknowledgement.size() == 1U && gainAction(acknowledgement.front()) != nullptr
            && gainAction(acknowledgement.front())->token == pendingToken
            && gainAction(acknowledgement.front())->gain == 0.4F,
        "Matching Started must emit the final deferred gain exactly once"))
    {
        return false;
    }

    const std::vector<MusicRuntimeAction> replacement = runtime.apply(PlaylistMusicConfigChanged{
        playlistId("playlist"), queue({track("b", "b.mp3")})});
    return require(replacement.size() == 2U && stopAction(replacement[0]) != nullptr
            && startAction(replacement[1]) != nullptr
            && startAction(replacement[1])->activity == PlaybackActivity::Paused,
        "A paused current-track replacement must preserve Paused activity");
}

bool verifyNoPlayableAndWaitingDoNotRetryOnBenignEdits()
{
    BackgroundMusicRuntime runtime{};
    MusicSourceToken firstToken{};
    if (!require(startAndAcknowledge(
            runtime,
            queue({track("a", "a.mp3"), track("b", "b.mp3")}),
            firstToken),
        "Test setup must start Track A"))
    {
        return false;
    }

    const std::vector<MusicRuntimeAction> firstFailure = runtime.apply(MusicSourceFailed{
        firstToken, MusicSourceFailureClass::UnplayableTrack});
    const MusicSourceToken secondToken = startAction(firstFailure[1])->token;
    (void)runtime.apply(MusicSourceFailed{secondToken, MusicSourceFailureClass::UnplayableTrack});
    if (!require(runtime.playlistSession() != nullptr
            && runtime.playlistSession()->phase() == MusicSessionPhase::NoPlayableTrack,
        "Test setup must enter NoPlayableTrack"))
    {
        return false;
    }

    if (!require(runtime.apply(PlaylistMusicConfigChanged{
                    playlistId("playlist"),
                    queue({track("b", "b.mp3"), track("a", "a.mp3")})}).empty()
            && runtime.apply(PlaylistMusicConfigChanged{
                    playlistId("playlist"),
                    queue({track("b", "b.mp3"), track("a", "a.mp3")},
                        MusicQueueMode::Shuffle)}).empty(),
        "Reorder-only and mode-only edits must not revive NoPlayableTrack"))
    {
        return false;
    }

    BackgroundMusicRuntime waitingRuntime{};
    MusicSourceToken waitingToken{};
    if (!require(startAndAcknowledge(waitingRuntime, queue({track("a", "a.mp3")}), waitingToken),
            "Test setup must start the waiting candidate"))
    {
        return false;
    }

    (void)waitingRuntime.apply(MusicSourceFailed{
        waitingToken, MusicSourceFailureClass::TransientResourceFailure});
    if (!require(waitingRuntime.playlistSession() != nullptr
            && waitingRuntime.playlistSession()->phase() == MusicSessionPhase::WaitingForExecutor
            && waitingRuntime.apply(PlaylistMusicConfigChanged{
                    playlistId("playlist"), queue({track("a", "a.mp3"), track("b", "b.mp3")})}).empty(),
        "Adding an unrelated track must not abandon WaitingForExecutor"))
    {
        return false;
    }

    const std::vector<MusicRuntimeAction> replacement = waitingRuntime.apply(PlaylistMusicConfigChanged{
        playlistId("playlist"), queue({track("b", "b.mp3")})});
    return require(replacement.size() == 1U && startAction(replacement.front()) != nullptr
            && startAction(replacement.front())->trackId == trackId("b"),
        "Removing the waiting candidate must select a replacement immediately");
}

bool verifyNoPlayableShuffleRevivalAndWaitingNoncurrentRemoval()
{
    const ShufflePermutationProvider identityPermutation = [](std::span<const TrackId> ids)
    {
        return std::vector<TrackId>{ids.begin(), ids.end()};
    };

    BackgroundMusicRuntime noPlayableRuntime{identityPermutation};
    MusicSourceToken firstToken{};
    if (!require(startAndAcknowledge(
            noPlayableRuntime,
            queue({track("a", "a.mp3"), track("b", "b.mp3")}, MusicQueueMode::Shuffle),
            firstToken),
        "Test setup must start Shuffle Track A"))
    {
        return false;
    }

    const std::vector<MusicRuntimeAction> firstFailure = noPlayableRuntime.apply(MusicSourceFailed{
        firstToken, MusicSourceFailureClass::UnplayableTrack});
    const MusicSourceToken secondToken = startAction(firstFailure[1])->token;
    (void)noPlayableRuntime.apply(MusicSourceFailed{
        secondToken, MusicSourceFailureClass::UnplayableTrack});
    const std::vector<MusicRuntimeAction> revival = noPlayableRuntime.apply(PlaylistMusicConfigChanged{
        playlistId("playlist"),
        queue({track("a", "a.mp3"), track("b", "b.mp3"), track("c", "c.mp3")},
            MusicQueueMode::Shuffle)});
    if (!require(revival.size() == 1U && startAction(revival.front()) != nullptr
            && startAction(revival.front())->trackId == trackId("c"),
        "NoPlayable Shuffle must revive only for its new unattempted TrackId"))
    {
        return false;
    }

    BackgroundMusicRuntime waitingRuntime{};
    MusicSourceToken waitingToken{};
    if (!require(startAndAcknowledge(
            waitingRuntime,
            queue({track("a", "a.mp3"), track("b", "b.mp3"), track("c", "c.mp3")}),
            waitingToken),
        "Test setup must start a WaitingForExecutor candidate"))
    {
        return false;
    }

    (void)waitingRuntime.apply(MusicSourceFailed{
        waitingToken, MusicSourceFailureClass::TransientResourceFailure});
    const MusicQueueSnapshot retainedWaiting = queue({
        track("c", "c.mp3"), track("a", "a.mp3"), track("b", "b.mp3", 0.5F)},
        MusicQueueMode::CurrentSingleLoop,
        0.5F);
    if (!require(waitingRuntime.apply(PlaylistMusicConfigChanged{
                    playlistId("playlist"), retainedWaiting}).empty()
            && waitingRuntime.playlistSession() != nullptr
            && waitingRuntime.playlistSession()->phase() == MusicSessionPhase::WaitingForExecutor
            && waitingRuntime.playlistSession()->currentTrackId() == std::optional<TrackId>{trackId("a")}
            && waitingRuntime.playlistSession()->config() == retainedWaiting,
        "WaitingForExecutor must accept non-current removal/reorder/mode/gain changes without retry"))
    {
        return false;
    }

    MusicQueueSnapshot malformed = retainedWaiting;
    malformed.tracks.push_back(track("a", "duplicate.mp3"));
    return require(waitingRuntime.apply(PlaylistMusicConfigChanged{
                       playlistId("playlist"), malformed}).empty()
            && waitingRuntime.playlistSession()->config() == retainedWaiting,
        "Malformed WaitingForExecutor edits must be rejected atomically");
}

bool verifyShuffleBagReconciliation()
{
    const ShufflePermutationProvider identityPermutation = [](std::span<const TrackId> ids)
    {
        return std::vector<TrackId>{ids.begin(), ids.end()};
    };
    BackgroundMusicRuntime runtime{identityPermutation};
    MusicSourceToken token{};
    if (!require(startAndAcknowledge(
            runtime,
            queue({track("a", "a.mp3"), track("b", "b.mp3"), track("c", "c.mp3")},
                MusicQueueMode::Shuffle),
            token),
        "Test setup must start Shuffle Track A"))
    {
        return false;
    }

    const MusicQueueSnapshot addedAndReordered = queue({
        track("c", "c.mp3"), track("b", "b.mp3"), track("a", "a.mp3"), track("d", "d.mp3")},
        MusicQueueMode::Shuffle);
    if (!require(runtime.apply(PlaylistMusicConfigChanged{
                    playlistId("playlist"), addedAndReordered}).empty(),
        "Shuffle add/reorder must retain the current source"))
    {
        return false;
    }

    const std::vector<MusicRuntimeAction> firstEnd = runtime.apply(MusicSourceEnded{token});
    if (!require(firstEnd.size() == 2U && startAction(firstEnd[1]) != nullptr
            && startAction(firstEnd[1])->trackId == trackId("b"),
        "Shuffle reorder must preserve the old unplayed remainder before appended new IDs"))
    {
        return false;
    }

    const MusicSourceToken secondToken = startAction(firstEnd[1])->token;
    (void)runtime.apply(MusicSourceStarted{secondToken});
    const std::vector<MusicRuntimeAction> removedCurrent = runtime.apply(PlaylistMusicConfigChanged{
        playlistId("playlist"), queue({track("c", "c.mp3"), track("d", "d.mp3")},
            MusicQueueMode::Shuffle)});
    return require(removedCurrent.size() == 2U && stopAction(removedCurrent[0]) != nullptr
            && startAction(removedCurrent[1]) != nullptr
            && startAction(removedCurrent[1])->trackId == trackId("c"),
        "Removing the current Shuffle TrackId must choose the reconciled bag remainder");
}

bool verifyAtomicValidationAndTokenAllocationFailure()
{
    BackgroundMusicRuntime runtime{};
    MusicSourceToken token{};
    const MusicQueueSnapshot original = queue({track("a", "a.mp3"), track("b", "b.mp3")});
    if (!require(startAndAcknowledge(runtime, original, token), "Test setup must start Track A"))
    {
        return false;
    }

    MusicQueueSnapshot duplicateId = original;
    duplicateId.tracks.push_back(track("a", "different.mp3"));
    MusicQueueSnapshot changedPath = original;
    changedPath.tracks.front().sourcePath = "new-a.mp3";
    if (!require(runtime.apply(PlaylistMusicConfigChanged{playlistId("playlist"), duplicateId}).empty()
            && runtime.apply(PlaylistMusicConfigChanged{playlistId("playlist"), changedPath}).empty()
            && runtime.playlistSession() != nullptr
            && runtime.playlistSession()->config() == original
            && runtime.playlistSession()->sourceToken() == std::optional<MusicSourceToken>{token},
        "Malformed or unsupported source-path snapshots must be atomically rejected"))
    {
        return false;
    }

    MusicSession session{MusicScope::PlaylistGlobal, original};
    session.beginStart(trackId("a"), MusicSourceToken{9U});
    const std::vector<MusicRuntimeAction> allocationFailure = session.reconcileConfig(
        queue({track("b", "b.mp3")}),
        PlaybackActivity::Playing,
        []() -> std::optional<MusicSourceToken>
        {
            return std::nullopt;
        });
    return require(allocationFailure.empty()
            && session.currentTrackId() == std::optional<TrackId>{trackId("a")}
            && session.sourceToken() == std::optional<MusicSourceToken>{MusicSourceToken{9U}}
            && session.config() == original,
        "Token allocation failure must leave the original session state untouched");
}

bool verifyRetainedCurrentNeverAllocatesAndInvalidPendingIsAtomic()
{
    const MusicQueueSnapshot original = queue({track("a", "a.mp3"), track("b", "b.mp3")});
    MusicSession playing{MusicScope::PlaylistGlobal, original};
    playing.beginStart(trackId("a"), MusicSourceToken{9U});
    (void)playing.acknowledgeStarted(MusicSourceToken{9U});

    std::size_t allocationCalls = 0U;
    const std::vector<MusicRuntimeAction> retainedActions = playing.reconcileConfig(
        queue({track("b", "b.mp3"), track("a", "a.mp3"), track("c", "c.mp3")}),
        PlaybackActivity::Playing,
        [&allocationCalls]() -> std::optional<MusicSourceToken>
        {
            ++allocationCalls;
            return MusicSourceToken{10U};
        });
    if (!require(retainedActions.empty() && allocationCalls == 0U
            && playing.currentTrackId() == std::optional<TrackId>{trackId("a")}
            && playing.sourceToken() == std::optional<MusicSourceToken>{MusicSourceToken{9U}},
        "A retained current source must not invoke token allocation while updating its snapshot"))
    {
        return false;
    }

    MusicSession pending{MusicScope::PlaylistGlobal, original};
    pending.beginStart(trackId("a"), MusicSourceToken{11U});
    MusicQueueSnapshot malformed = original;
    malformed.tracks.push_back(track("a", "duplicate.mp3"));
    const std::vector<MusicRuntimeAction> malformedActions = pending.reconcileConfig(
        malformed,
        PlaybackActivity::Playing,
        []() -> std::optional<MusicSourceToken>
        {
            return MusicSourceToken{12U};
        });
    return require(malformedActions.empty() && pending.config() == original
            && pending.phase() == MusicSessionPhase::PendingStart
            && pending.currentTrackId() == std::optional<TrackId>{trackId("a")}
            && pending.sourceToken() == std::optional<MusicSourceToken>{MusicSourceToken{11U}},
        "A malformed PendingStart snapshot must be rejected without a partial state change");
}

bool verifyCurrentRemovalAndEmptyTransitions()
{
    BackgroundMusicRuntime listRuntime{};
    MusicSourceToken firstToken{};
    if (!require(startAndAcknowledge(
            listRuntime,
            queue({track("a", "a.mp3"), track("b", "b.mp3"), track("c", "c.mp3")}),
            firstToken),
        "Test setup must start ListLoop Track A"))
    {
        return false;
    }

    const MusicSourceToken secondToken = startAction(
        listRuntime.apply(MusicSourceEnded{firstToken})[1])->token;
    (void)listRuntime.apply(MusicSourceStarted{secondToken});
    const MusicSourceToken thirdToken = startAction(
        listRuntime.apply(MusicSourceEnded{secondToken})[1])->token;
    (void)listRuntime.apply(MusicSourceStarted{thirdToken});
    const std::vector<MusicRuntimeAction> listRemoval = listRuntime.apply(PlaylistMusicConfigChanged{
        playlistId("playlist"), queue({track("a", "a.mp3"), track("b", "b.mp3")})});
    if (!require(listRemoval.size() == 2U && stopAction(listRemoval[0]) != nullptr
            && startAction(listRemoval[1]) != nullptr && stopAction(listRemoval[0])->token == thirdToken
            && startAction(listRemoval[1])->trackId == trackId("a"),
        "Removing the last ListLoop track must wrap through old-order survivors"))
    {
        return false;
    }

    BackgroundMusicRuntime singleRuntime{};
    MusicSourceToken singleToken{};
    if (!require(startAndAcknowledge(
            singleRuntime,
            queue({track("a", "a.mp3"), track("b", "b.mp3")}, MusicQueueMode::CurrentSingleLoop),
            singleToken),
        "Test setup must start CurrentSingleLoop Track A"))
    {
        return false;
    }

    const std::vector<MusicRuntimeAction> singleRemoval = singleRuntime.apply(PlaylistMusicConfigChanged{
        playlistId("playlist"), queue({track("b", "b.mp3")}, MusicQueueMode::CurrentSingleLoop)});
    if (!require(singleRemoval.size() == 2U && startAction(singleRemoval[1]) != nullptr
            && startAction(singleRemoval[1])->trackId == trackId("b"),
        "Removing a CurrentSingleLoop target must choose its old-order successor"))
    {
        return false;
    }

    BackgroundMusicRuntime pendingRuntime{};
    const std::vector<MusicRuntimeAction> pendingStart = pendingRuntime.apply(PlaybackSessionStarted{
        scope(queue({track("a", "a.mp3"), track("b", "b.mp3")}), PlaybackActivity::Playing)});
    const MusicSourceToken pendingToken = startAction(pendingStart.front())->token;
    const std::vector<MusicRuntimeAction> pendingRemoval = pendingRuntime.apply(PlaylistMusicConfigChanged{
        playlistId("playlist"), queue({track("b", "b.mp3")})});
    if (!require(pendingRemoval.size() == 2U && stopAction(pendingRemoval[0]) != nullptr
            && startAction(pendingRemoval[1]) != nullptr
            && pendingRuntime.apply(MusicSourceStarted{pendingToken}).empty()
            && pendingRuntime.apply(MusicSourceEnded{pendingToken}).empty()
            && pendingRuntime.apply(MusicSourceFailed{
                pendingToken, MusicSourceFailureClass::UnplayableTrack}).empty(),
        "Removing a PendingStart track must make every old-token fact stale before replacement"))
    {
        return false;
    }

    BackgroundMusicRuntime emptyRuntime{};
    MusicSourceToken onlyToken{};
    if (!require(startAndAcknowledge(emptyRuntime, queue({track("only", "only.mp3")}), onlyToken),
            "Test setup must start the only track"))
    {
        return false;
    }

    const std::vector<MusicRuntimeAction> clearActions = emptyRuntime.apply(PlaylistMusicConfigChanged{
        playlistId("playlist"), queue({})});
    if (!require(clearActions.size() == 1U && stopAction(clearActions.front()) != nullptr
            && stopAction(clearActions.front())->token == onlyToken
            && emptyRuntime.playlistSession()->phase() == MusicSessionPhase::SilentEmptyQueue,
        "Clearing a nonempty queue must stop its source and become silent"))
    {
        return false;
    }

    BackgroundMusicRuntime silentPausedRuntime{};
    (void)silentPausedRuntime.apply(PlaybackSessionStarted{
        scope(queue({}), PlaybackActivity::Paused)});
    const std::vector<MusicRuntimeAction> pausedStart = silentPausedRuntime.apply(
        PlaylistMusicConfigChanged{playlistId("playlist"), queue({track("a", "a.mp3")})});
    return require(pausedStart.size() == 1U && startAction(pausedStart.front()) != nullptr
            && startAction(pausedStart.front())->activity == PlaybackActivity::Paused,
        "An empty paused queue becoming nonempty must start the candidate paused");
}

bool verifyWaitingAndNoPlayableBenignTransitions()
{
    BackgroundMusicRuntime noPlayableRuntime{};
    MusicSourceToken firstToken{};
    if (!require(startAndAcknowledge(
            noPlayableRuntime,
            queue({track("a", "a.mp3"), track("b", "b.mp3")}),
            firstToken),
        "Test setup must start NoPlayable candidate A"))
    {
        return false;
    }

    const MusicSourceToken secondToken = startAction(noPlayableRuntime.apply(MusicSourceFailed{
        firstToken, MusicSourceFailureClass::UnplayableTrack})[1])->token;
    (void)noPlayableRuntime.apply(MusicSourceFailed{
        secondToken, MusicSourceFailureClass::UnplayableTrack});
    const MusicQueueSnapshot volumeOnly = queue(
        {track("a", "a.mp3"), track("b", "b.mp3")}, MusicQueueMode::ListLoop, 0.5F);
    if (!require(noPlayableRuntime.apply(PlaylistMusicConfigChanged{
                    playlistId("playlist"), volumeOnly}).empty()
            && noPlayableRuntime.apply(PlaylistMusicConfigChanged{
                    playlistId("playlist"), queue({track("a", "a.mp3")})}).empty()
            && noPlayableRuntime.playlistSession()->phase() == MusicSessionPhase::NoPlayableTrack,
        "Volume-only and failed-track removal edits must not revive NoPlayableTrack"))
    {
        return false;
    }

    const std::vector<MusicRuntimeAction> noPlayableClear = noPlayableRuntime.apply(
        PlaylistMusicConfigChanged{playlistId("playlist"), queue({})});
    if (!require(noPlayableClear.empty()
            && noPlayableRuntime.playlistSession()->phase() == MusicSessionPhase::SilentEmptyQueue,
        "Clearing NoPlayableTrack must become SilentEmptyQueue without a stop token"))
    {
        return false;
    }

    BackgroundMusicRuntime waitingRuntime{};
    MusicSourceToken waitingToken{};
    if (!require(startAndAcknowledge(
            waitingRuntime,
            queue({track("a", "a.mp3"), track("b", "b.mp3")}),
            waitingToken),
        "Test setup must start WaitingForExecutor Track A"))
    {
        return false;
    }

    (void)waitingRuntime.apply(MusicSourceFailed{
        waitingToken, MusicSourceFailureClass::TransientResourceFailure});
    const MusicQueueSnapshot waitingChanged = queue(
        {track("b", "b.mp3"), track("a", "a.mp3")}, MusicQueueMode::Shuffle, 0.5F);
    if (!require(waitingRuntime.apply(PlaylistMusicConfigChanged{
                    playlistId("playlist"), waitingChanged}).empty()
            && waitingRuntime.playlistSession()->phase() == MusicSessionPhase::WaitingForExecutor,
        "WaitingForExecutor must retain its selected TrackId through reorder/mode/volume edits"))
    {
        return false;
    }

    const std::vector<MusicRuntimeAction> waitingClear = waitingRuntime.apply(
        PlaylistMusicConfigChanged{playlistId("playlist"), queue({})});
    return require(waitingClear.empty()
            && waitingRuntime.playlistSession()->phase() == MusicSessionPhase::SilentEmptyQueue,
        "Clearing WaitingForExecutor must not emit a stop action for its absent token");
}

bool verifyShuffleModeAndFailureTraversalReconciliation()
{
    const ShufflePermutationProvider identityPermutation = [](std::span<const TrackId> ids)
    {
        return std::vector<TrackId>{ids.begin(), ids.end()};
    };

    BackgroundMusicRuntime modeRuntime{identityPermutation};
    MusicSourceToken modeToken{};
    if (!require(startAndAcknowledge(
            modeRuntime,
            queue({track("a", "a.mp3"), track("b", "b.mp3"), track("c", "c.mp3")}),
            modeToken),
        "Test setup must start ListLoop Track A"))
    {
        return false;
    }

    if (!require(modeRuntime.apply(PlaylistMusicConfigChanged{
                    playlistId("playlist"),
                    queue({track("a", "a.mp3"), track("b", "b.mp3"), track("c", "c.mp3")},
                        MusicQueueMode::Shuffle)}).empty(),
        "Entering Shuffle must retain the active source"))
    {
        return false;
    }

    const MusicSourceToken shuffleToken = startAction(modeRuntime.apply(MusicSourceEnded{modeToken})[1])->token;
    if (!require(startAction(modeRuntime.apply(MusicSourceEnded{shuffleToken})[1])->trackId == trackId("c"),
            "An entered Shuffle bag must provide each remaining TrackId once"))
    {
        return false;
    }

    BackgroundMusicRuntime failureRuntime{identityPermutation};
    MusicSourceToken failureToken{};
    if (!require(startAndAcknowledge(
            failureRuntime,
            queue({track("a", "shared.mp3"), track("b", "shared.mp3")}, MusicQueueMode::Shuffle),
            failureToken),
        "Test setup must start TrackId A despite duplicate source paths"))
    {
        return false;
    }

    const MusicSourceToken pendingToken = startAction(failureRuntime.apply(MusicSourceFailed{
        failureToken, MusicSourceFailureClass::UnplayableTrack})[1])->token;
    if (!require(failureRuntime.apply(PlaylistMusicConfigChanged{
                    playlistId("playlist"),
                    queue({track("a", "shared.mp3"), track("b", "shared.mp3"), track("c", "c.mp3")},
                        MusicQueueMode::Shuffle)}).empty(),
        "Adding a Shuffle TrackId during PendingStart must not restart the candidate"))
    {
        return false;
    }

    const std::vector<MusicRuntimeAction> nextFailure = failureRuntime.apply(MusicSourceFailed{
        pendingToken, MusicSourceFailureClass::UnplayableTrack});
    return require(nextFailure.size() == 2U && startAction(nextFailure[1]) != nullptr
            && startAction(nextFailure[1])->trackId == trackId("c"),
        "A newly added Shuffle TrackId must provide one bounded failure-cycle opportunity");
}

bool verifyActiveScopeRouting()
{
    BackgroundMusicRuntime runtime{};
    const MusicQueueSnapshot initialPlaylist = queue({track("playlist-a", "playlist-a.mp3")});
    const MusicQueueSnapshot initialItem = queue({track("item-a", "item-a.mp3")});
    const std::vector<MusicRuntimeAction> startup = runtime.apply(PlaybackSessionStarted{
        scopeWithItem(initialPlaylist, initialItem)});
    const StartSource* const playlistStart = startActionForScope(startup, MusicScope::PlaylistGlobal);
    const StartSource* const itemStart = startActionForScope(startup, MusicScope::Item);
    if (!require(playlistStart != nullptr && itemStart != nullptr,
            "Test setup must start both active Playlist and Item sessions"))
    {
        return false;
    }

    (void)runtime.apply(MusicSourceStarted{playlistStart->token});
    (void)runtime.apply(MusicSourceStarted{itemStart->token});
    const MusicQueueSnapshot changedItem = queue({
        track("item-a", "item-a.mp3"), track("item-b", "item-b.mp3")});
    if (!require(runtime.apply(ItemMusicConfigChanged{
                    playlistId("playlist"), itemId("item"), changedItem}).empty()
            && runtime.itemSession() != nullptr && runtime.itemSession()->config() == changedItem
            && runtime.playlistSession() != nullptr && runtime.playlistSession()->config() == initialPlaylist,
        "An active Item configuration event must reconcile only the Item session"))
    {
        return false;
    }

    const MusicQueueSnapshot changedPlaylist = queue({
        track("playlist-a", "playlist-a.mp3"), track("playlist-b", "playlist-b.mp3")});
    if (!require(runtime.apply(PlaylistMusicConfigChanged{
                    playlistId("playlist"), changedPlaylist}).empty()
            && runtime.playlistSession() != nullptr && runtime.playlistSession()->config() == changedPlaylist
            && runtime.itemSession() != nullptr && runtime.itemSession()->config() == changedItem,
        "An active Playlist configuration event must reconcile only the Playlist session"))
    {
        return false;
    }

    return require(runtime.apply(ItemMusicConfigChanged{
                       playlistId("playlist"), itemId("inactive-item"), queue({track("ignored", "ignored.mp3")})}).empty()
            && runtime.itemSession() != nullptr && runtime.itemSession()->config() == changedItem,
        "A mismatched Item event must leave the active Item session unchanged");
}

bool verifyShuffleProviderInputAndLeaveShuffle()
{
    std::vector<std::vector<TrackId>> providerInputs{};
    const ShufflePermutationProvider recordingPermutation = [&providerInputs](std::span<const TrackId> ids)
    {
        providerInputs.emplace_back(ids.begin(), ids.end());
        return std::vector<TrackId>{ids.begin(), ids.end()};
    };

    BackgroundMusicRuntime runtime{recordingPermutation};
    MusicSourceToken token{};
    if (!require(startAndAcknowledge(
            runtime,
            queue({track("a", "a.mp3"), track("b", "b.mp3"), track("c", "c.mp3")},
                MusicQueueMode::Shuffle),
            token),
        "Test setup must start Shuffle Track A"))
    {
        return false;
    }

    providerInputs.clear();
    const MusicQueueSnapshot added = queue({
        track("c", "c.mp3"), track("b", "b.mp3"), track("a", "a.mp3"),
        track("d", "d.mp3"), track("e", "e.mp3")}, MusicQueueMode::Shuffle);
    if (!require(runtime.apply(PlaylistMusicConfigChanged{playlistId("playlist"), added}).empty()
            && providerInputs.size() == 1U
            && providerInputs.front() == std::vector<TrackId>{trackId("d"), trackId("e")},
        "Shuffle live-add must invoke the permutation provider only for new TrackIds in new-config order"))
    {
        return false;
    }

    const MusicQueueSnapshot reordered = queue({
        track("e", "e.mp3"), track("d", "d.mp3"), track("a", "a.mp3"),
        track("b", "b.mp3"), track("c", "c.mp3")}, MusicQueueMode::Shuffle);
    if (!require(runtime.apply(PlaylistMusicConfigChanged{playlistId("playlist"), reordered}).empty()
            && providerInputs.size() == 1U,
        "A Shuffle reorder must preserve the runtime bag without a new permutation"))
    {
        return false;
    }

    const MusicQueueSnapshot listMode = queue({
        track("a", "a.mp3"), track("c", "c.mp3"), track("b", "b.mp3"),
        track("d", "d.mp3"), track("e", "e.mp3")}, MusicQueueMode::ListLoop);
    if (!require(runtime.apply(PlaylistMusicConfigChanged{playlistId("playlist"), listMode}).empty(),
        "Leaving Shuffle with a retained current source must not restart it"))
    {
        return false;
    }

    const std::vector<MusicRuntimeAction> endActions = runtime.apply(MusicSourceEnded{token});
    return require(endActions.size() == 2U && startAction(endActions[1]) != nullptr
            && startAction(endActions[1])->trackId == trackId("c"),
        "Leaving Shuffle must discard the old bag so EOF follows the new ListLoop order");
}

bool verifyShuffleRemovalAndExhaustedBagReplacement()
{
    const ShufflePermutationProvider identityPermutation = [](std::span<const TrackId> ids)
    {
        return std::vector<TrackId>{ids.begin(), ids.end()};
    };

    BackgroundMusicRuntime removalRuntime{identityPermutation};
    MusicSourceToken firstToken{};
    if (!require(startAndAcknowledge(
            removalRuntime,
            queue({track("a", "a.mp3"), track("b", "b.mp3"), track("c", "c.mp3")},
                MusicQueueMode::Shuffle),
            firstToken),
        "Test setup must start Shuffle Track A for unplayed removal"))
    {
        return false;
    }

    if (!require(removalRuntime.apply(PlaylistMusicConfigChanged{
                    playlistId("playlist"),
                    queue({track("a", "a.mp3"), track("c", "c.mp3")}, MusicQueueMode::Shuffle)}).empty(),
        "Removing an unplayed Shuffle TrackId must not restart the current source"))
    {
        return false;
    }

    const std::vector<MusicRuntimeAction> firstEnd = removalRuntime.apply(MusicSourceEnded{firstToken});
    if (!require(firstEnd.size() == 2U && startAction(firstEnd[1]) != nullptr
            && startAction(firstEnd[1])->trackId == trackId("c"),
        "A removed unplayed Shuffle TrackId must not remain in the future bag"))
    {
        return false;
    }

    BackgroundMusicRuntime exhaustedRuntime{identityPermutation};
    MusicSourceToken tokenA{};
    if (!require(startAndAcknowledge(
            exhaustedRuntime,
            queue({track("a", "a.mp3"), track("b", "b.mp3")}, MusicQueueMode::Shuffle),
            tokenA),
        "Test setup must start Shuffle Track A for exhausted-bag replacement"))
    {
        return false;
    }

    const std::vector<MusicRuntimeAction> advance = exhaustedRuntime.apply(MusicSourceEnded{tokenA});
    if (!require(advance.size() == 2U && startAction(advance[1]) != nullptr,
        "Test setup must consume the only remaining Shuffle candidate"))
    {
        return false;
    }

    const MusicSourceToken tokenB = startAction(advance[1])->token;
    (void)exhaustedRuntime.apply(MusicSourceStarted{tokenB});
    const std::vector<MusicRuntimeAction> replacement = exhaustedRuntime.apply(PlaylistMusicConfigChanged{
        playlistId("playlist"), queue({track("a", "a.mp3")}, MusicQueueMode::Shuffle)});
    return require(replacement.size() == 2U && stopAction(replacement[0]) != nullptr
            && startAction(replacement[1]) != nullptr
            && startAction(replacement[1])->trackId == trackId("a"),
        "Removing a current Shuffle TrackId with an exhausted bag must create a fresh bag from survivors");
}

}

int main()
{
    const bool success = verifyRetainedCurrentConfigReplacementIsLive()
        && verifySilentQueueStartsWhenMadeNonempty()
        && verifyRemovingCurrentStopsThenStartsSuccessor()
        && verifyNoPlayableTrackRevivesForNewTrack()
        && verifyRetainedListEditsAndGainUpdates()
        && verifyRetainedNoncurrentEditsAndIndividualGainUpdates()
        && verifyPendingStartGainAndPausedReplacement()
        && verifyNoPlayableAndWaitingDoNotRetryOnBenignEdits()
        && verifyNoPlayableShuffleRevivalAndWaitingNoncurrentRemoval()
        && verifyShuffleBagReconciliation()
        && verifyAtomicValidationAndTokenAllocationFailure()
        && verifyRetainedCurrentNeverAllocatesAndInvalidPendingIsAtomic()
        && verifyCurrentRemovalAndEmptyTransitions()
        && verifyWaitingAndNoPlayableBenignTransitions()
        && verifyShuffleModeAndFailureTraversalReconciliation()
        && verifyActiveScopeRouting()
        && verifyShuffleProviderInputAndLeaveShuffle()
        && verifyShuffleRemovalAndExhaustedBagReplacement();
    return success ? 0 : 1;
}
