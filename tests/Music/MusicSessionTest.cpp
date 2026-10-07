#include "Music/MusicSession.h"

#include <filesystem>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <utility>
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

TrackId trackId(const char* value)
{
    return TrackId{value};
}

MusicTrackSnapshot track(const char* id, const char* source)
{
    return MusicTrackSnapshot{trackId(id), std::filesystem::path{source}, 1.0F};
}

MusicQueueSnapshot queue(
    std::initializer_list<MusicTrackSnapshot> tracks,
    MusicQueueMode mode
)
{
    return MusicQueueSnapshot{std::vector<MusicTrackSnapshot>{tracks}, mode, 1.0F};
}

bool beginAndAcknowledge(MusicSession& session, const TrackId& id, std::uint64_t tokenValue)
{
    const MusicSourceToken token{tokenValue};
    session.beginStart(id, token);
    return session.acknowledgeStarted(token);
}

bool verifyEmptyQueue()
{
    MusicSession session{MusicScope::PlaylistGlobal, queue({}, MusicQueueMode::ListLoop)};
    return require(!session.selectInitialTrack().has_value(), "An empty queue must not select a track")
        && require(session.phase() == MusicSessionPhase::SilentEmptyQueue,
            "An empty queue must remain SilentEmptyQueue")
        && require(!session.sourceToken().has_value(), "An empty queue must not own a token");
}

bool verifyListLoopSequenceAndBoundedFailures()
{
    const MusicQueueSnapshot config = queue({
        track("a", "a.mp3"),
        track("b", "b.mp3"),
        track("c", "c.mp3")}, MusicQueueMode::ListLoop);
    MusicSession session{MusicScope::PlaylistGlobal, config};
    const std::optional<TrackId> initial = session.selectInitialTrack();
    if (!require(initial == std::optional<TrackId>{trackId("a")},
            "ListLoop must start with the first configured TrackId")
        || !require(beginAndAcknowledge(session, *initial, 1U), "Initial ListLoop source must start"))
    {
        return false;
    }

    const std::optional<TrackId> second = session.selectNextTrackAfterEnd();
    if (!second.has_value() || !require(beginAndAcknowledge(session, *second, 2U),
            "The second ListLoop source must start"))
    {
        return false;
    }

    const std::optional<TrackId> third = session.selectNextTrackAfterEnd();
    if (!third.has_value() || !require(beginAndAcknowledge(session, *third, 3U),
            "The third ListLoop source must start"))
    {
        return false;
    }

    const std::optional<TrackId> wrapped = session.selectNextTrackAfterEnd();
    if (!require(second == std::optional<TrackId>{trackId("b")}
            && third == std::optional<TrackId>{trackId("c")}
            && wrapped == std::optional<TrackId>{trackId("a")},
        "ListLoop must progress A to B to C to A"))
    {
        return false;
    }

    MusicSession failures{MusicScope::PlaylistGlobal, config};
    const std::optional<TrackId> firstFailure = failures.selectInitialTrack();
    failures.beginStart(*firstFailure, MusicSourceToken{10U});
    const std::optional<TrackId> secondFailure = failures.selectNextTrackAfterUnplayableFailure();
    failures.beginStart(*secondFailure, MusicSourceToken{11U});
    const std::optional<TrackId> thirdFailure = failures.selectNextTrackAfterUnplayableFailure();
    failures.beginStart(*thirdFailure, MusicSourceToken{12U});
    const std::optional<TrackId> exhausted = failures.selectNextTrackAfterUnplayableFailure();
    if (!require(secondFailure == std::optional<TrackId>{trackId("b")}
            && thirdFailure == std::optional<TrackId>{trackId("c")}
            && !exhausted.has_value(),
        "ListLoop failures must traverse each TrackId once in configured order")
        || !require(failures.phase() == MusicSessionPhase::NoPlayableTrack
                && !failures.sourceToken().has_value(),
            "All-invalid ListLoop must become tokenless NoPlayableTrack"))
    {
        return false;
    }

    MusicSession wrappedFailure{MusicScope::PlaylistGlobal, config};
    const std::optional<TrackId> wrappedInitial = wrappedFailure.selectInitialTrack();
    if (!require(beginAndAcknowledge(wrappedFailure, *wrappedInitial, 20U),
        "ListLoop wrap test must start A"))
    {
        return false;
    }

    const std::optional<TrackId> wrappedSecond = wrappedFailure.selectNextTrackAfterEnd();
    if (!require(beginAndAcknowledge(wrappedFailure, *wrappedSecond, 21U),
        "ListLoop wrap test must start B"))
    {
        return false;
    }

    const std::optional<TrackId> wrappedThird = wrappedFailure.selectNextTrackAfterEnd();
    if (!require(beginAndAcknowledge(wrappedFailure, *wrappedThird, 22U),
        "ListLoop wrap test must start C"))
    {
        return false;
    }

    const std::optional<TrackId> wrappedFirstCandidate = wrappedFailure.selectNextTrackAfterEnd();
    wrappedFailure.beginStart(*wrappedFirstCandidate, MusicSourceToken{23U});
    const std::optional<TrackId> wrappedSuccessor = wrappedFailure.selectNextTrackAfterUnplayableFailure();
    return require(wrappedFirstCandidate == std::optional<TrackId>{trackId("a")}
            && wrappedSuccessor == std::optional<TrackId>{trackId("b")},
        "ListLoop failure must wrap from C through A to B without retrying C");
}

bool verifyCurrentSingleLoopFailureFallbackAndReset()
{
    const MusicQueueSnapshot config = queue({
        track("a", "a.mp3"),
        track("b", "b.mp3"),
        track("c", "c.mp3")}, MusicQueueMode::CurrentSingleLoop);
    MusicSession session{MusicScope::PlaylistGlobal, config};
    const std::optional<TrackId> initial = session.selectInitialTrack();
    if (!require(initial == std::optional<TrackId>{trackId("a")}
            && beginAndAcknowledge(session, *initial, 1U),
        "CurrentSingleLoop must begin with A"))
    {
        return false;
    }

    const std::optional<TrackId> restartA = session.selectNextTrackAfterEnd();
    session.beginStart(*restartA, MusicSourceToken{2U});
    const std::optional<TrackId> fallbackB = session.selectNextTrackAfterUnplayableFailure();
    session.beginStart(*fallbackB, MusicSourceToken{3U});
    if (!require(restartA == std::optional<TrackId>{trackId("a")}
            && fallbackB == std::optional<TrackId>{trackId("b")}
            && beginAndAcknowledge(session, *fallbackB, 3U),
        "A failing CurrentSingleLoop restart must fall back in configured order"))
    {
        return false;
    }

    const std::optional<TrackId> restartB = session.selectNextTrackAfterEnd();
    session.beginStart(*restartB, MusicSourceToken{4U});
    const std::optional<TrackId> retryA = session.selectNextTrackAfterUnplayableFailure();
    return require(restartB == std::optional<TrackId>{trackId("b")},
            "After successful fallback, CurrentSingleLoop must loop the successful TrackId")
        && require(retryA == std::optional<TrackId>{trackId("a")},
            "A successful start must reset failure traversal for later cycles");
}

bool verifyCurrentSingleLoopAllInvalid()
{
    MusicSession session{
        MusicScope::Item,
        queue({track("a", "a.mp3"), track("b", "b.mp3")}, MusicQueueMode::CurrentSingleLoop)};
    const std::optional<TrackId> initial = session.selectInitialTrack();
    session.beginStart(*initial, MusicSourceToken{1U});
    const std::optional<TrackId> second = session.selectNextTrackAfterUnplayableFailure();
    session.beginStart(*second, MusicSourceToken{2U});
    const std::optional<TrackId> exhausted = session.selectNextTrackAfterUnplayableFailure();
    return require(second == std::optional<TrackId>{trackId("b")} && !exhausted.has_value(),
            "CurrentSingleLoop failure fallback must not retry a failing current track forever")
        && require(session.phase() == MusicSessionPhase::NoPlayableTrack,
            "All-invalid CurrentSingleLoop must enter NoPlayableTrack");
}

ShufflePermutationProvider scriptedProvider(std::vector<std::vector<TrackId>> permutations)
{
    return [permutations = std::move(permutations), callIndex = std::size_t{0U}](
        std::span<const TrackId> candidates
    ) mutable
    {
        if (callIndex < permutations.size())
        {
            return permutations[callIndex++];
        }

        return std::vector<TrackId>{candidates.begin(), candidates.end()};
    };
}

bool verifyShuffleBagAndBoundary()
{
    const MusicQueueSnapshot config = queue({
        track("a", "shared.mp3"),
        track("b", "shared.mp3"),
        track("c", "c.mp3"),
        track("d", "d.mp3")}, MusicQueueMode::Shuffle);
    MusicSession session{
        MusicScope::PlaylistGlobal,
        config,
        scriptedProvider({
            {trackId("c"), trackId("a"), trackId("d"), trackId("b")},
            {trackId("b"), trackId("c"), trackId("a"), trackId("d")}})};

    const std::optional<TrackId> first = session.selectInitialTrack();
    session.beginStart(*first, MusicSourceToken{1U});
    (void)session.acknowledgeStarted(MusicSourceToken{1U});
    const std::optional<TrackId> second = session.selectNextTrackAfterEnd();
    session.beginStart(*second, MusicSourceToken{2U});
    (void)session.acknowledgeStarted(MusicSourceToken{2U});
    const std::optional<TrackId> third = session.selectNextTrackAfterEnd();
    session.beginStart(*third, MusicSourceToken{3U});
    (void)session.acknowledgeStarted(MusicSourceToken{3U});
    const std::optional<TrackId> fourth = session.selectNextTrackAfterEnd();
    session.beginStart(*fourth, MusicSourceToken{4U});
    (void)session.acknowledgeStarted(MusicSourceToken{4U});
    const std::optional<TrackId> nextRound = session.selectNextTrackAfterEnd();

    return require(first == std::optional<TrackId>{trackId("a")}
            && second == std::optional<TrackId>{trackId("c")}
            && third == std::optional<TrackId>{trackId("d")}
            && fourth == std::optional<TrackId>{trackId("b")},
        "Shuffle must consume each TrackId exactly once from the initial bag")
        && require(nextRound == std::optional<TrackId>{trackId("c")},
            "Shuffle must correct a new bag that would immediately repeat its prior TrackId");
}

bool verifyShuffleFailuresAndSingleTrack()
{
    const MusicQueueSnapshot config = queue({
        track("a", "a.mp3"),
        track("b", "b.mp3"),
        track("c", "c.mp3")}, MusicQueueMode::Shuffle);
    MusicSession failures{
        MusicScope::PlaylistGlobal,
        config,
        scriptedProvider({{{trackId("c"), trackId("a"), trackId("b")}}})};
    const std::optional<TrackId> first = failures.selectInitialTrack();
    failures.beginStart(*first, MusicSourceToken{1U});
    const std::optional<TrackId> second = failures.selectNextTrackAfterUnplayableFailure();
    failures.beginStart(*second, MusicSourceToken{2U});
    const std::optional<TrackId> third = failures.selectNextTrackAfterUnplayableFailure();
    failures.beginStart(*third, MusicSourceToken{3U});
    const std::optional<TrackId> exhausted = failures.selectNextTrackAfterUnplayableFailure();
    if (!require(first == std::optional<TrackId>{trackId("a")}
            && second == std::optional<TrackId>{trackId("c")}
            && third == std::optional<TrackId>{trackId("b")}
            && !exhausted.has_value(),
        "Shuffle failures must consume each configured TrackId once before silence")
        || !require(failures.phase() == MusicSessionPhase::NoPlayableTrack,
            "All-invalid Shuffle must not generate endless fresh bags"))
    {
        return false;
    }

    MusicSession single{
        MusicScope::PlaylistGlobal,
        queue({track("only", "only.mp3")}, MusicQueueMode::Shuffle),
        scriptedProvider({{{trackId("only")}}, {{trackId("only")}}})};
    const std::optional<TrackId> only = single.selectInitialTrack();
    single.beginStart(*only, MusicSourceToken{20U});
    (void)single.acknowledgeStarted(MusicSourceToken{20U});
    const std::optional<TrackId> looped = single.selectNextTrackAfterEnd();
    return require(looped == std::optional<TrackId>{trackId("only")},
        "Single-track Shuffle may repeat across the bag boundary");
}

bool verifyTransientFailureState()
{
    MusicSession session{
        MusicScope::Item,
        queue({track("a", "a.mp3"), track("b", "b.mp3")}, MusicQueueMode::ListLoop)};
    const std::optional<TrackId> initial = session.selectInitialTrack();
    session.beginStart(*initial, MusicSourceToken{1U});
    session.enterWaitingForExecutor();
    return require(session.phase() == MusicSessionPhase::WaitingForExecutor,
            "Transient failure must enter WaitingForExecutor")
        && require(session.currentTrackId() == std::optional<TrackId>{trackId("a")},
            "WaitingForExecutor must retain the selected TrackId")
        && require(!session.sourceToken().has_value(),
            "WaitingForExecutor must invalidate the failed source token");
}

bool verifyExecutionAvailableRecovery()
{
    const MusicQueueSnapshot config = queue({
        track("a", "a.mp3"),
        track("b", "b.mp3"),
        track("c", "c.mp3")}, MusicQueueMode::Shuffle);
    MusicSession session{
        MusicScope::PlaylistGlobal,
        config,
        scriptedProvider({{{trackId("c"), trackId("a"), trackId("b")}}})};
    const std::optional<TrackId> initial = session.selectInitialTrack();
    session.beginStart(*initial, MusicSourceToken{1U});
    session.enterWaitingForExecutor();

    const std::vector<MusicRuntimeAction> recovery = session.retryExecutionAvailable(
        PlaybackActivity::Paused,
        []() -> std::optional<MusicSourceToken>
        {
            return MusicSourceToken{2U};
        });
    const StartSource* const start = recovery.empty()
        ? nullptr
        : std::get_if<StartSource>(&recovery.front());
    if (!require(start != nullptr
            && start->trackId == trackId("a")
            && start->token == MusicSourceToken{2U}
            && start->sourcePath == std::filesystem::path{"a.mp3"}
            && start->activity == PlaybackActivity::Paused
            && session.phase() == MusicSessionPhase::PendingStart
            && session.currentTrackId() == std::optional<TrackId>{trackId("a")}
            && session.sourceToken() == std::optional<MusicSourceToken>{MusicSourceToken{2U}},
            "Execution recovery must restart the same waiting TrackId with the supplied fresh token"))
    {
        return false;
    }

    if (!require(session.retryExecutionAvailable(
                    PlaybackActivity::Paused,
                    []() -> std::optional<MusicSourceToken>
                    {
                        return MusicSourceToken{3U};
                    }).empty(),
            "PendingStart recovery must not allocate or start a duplicate source"))
    {
        return false;
    }

    (void)session.acknowledgeStarted(MusicSourceToken{2U});
    const std::optional<TrackId> shuffleSuccessor = session.selectNextTrackAfterUnplayableFailure();
    if (!require(shuffleSuccessor == std::optional<TrackId>{trackId("c")},
            "Recovery must not consume a second Shuffle candidate"))
    {
        return false;
    }

    MusicSession allocationFailure{MusicScope::Item, queue({track("a", "a.mp3")}, MusicQueueMode::ListLoop)};
    const std::optional<TrackId> allocationInitial = allocationFailure.selectInitialTrack();
    allocationFailure.beginStart(*allocationInitial, MusicSourceToken{11U});
    allocationFailure.enterWaitingForExecutor();
    return require(allocationFailure.retryExecutionAvailable(
                       PlaybackActivity::Playing,
                       []() -> std::optional<MusicSourceToken>
                       {
                           return std::nullopt;
                       }).empty(),
        "A recovery allocation failure must emit no action")
        && require(allocationFailure.phase() == MusicSessionPhase::WaitingForExecutor
                && allocationFailure.currentTrackId() == std::optional<TrackId>{trackId("a")}
                && !allocationFailure.sourceToken().has_value(),
            "A recovery allocation failure must preserve WaitingForExecutor state");
}

bool verifyManualTrackSwitchSemantics()
{
    const MusicQueueSnapshot listConfig = queue({
        track("a", "shared.mp3"),
        track("b", "shared.mp3"),
        track("c", "c.mp3")}, MusicQueueMode::ListLoop);
    MusicSession list{MusicScope::PlaylistGlobal, listConfig};
    const std::optional<TrackId> initial = list.selectInitialTrack();
    if (!require(initial == std::optional<TrackId>{trackId("a")},
            "Manual-switch setup must begin ListLoop at A")
        || !require(beginAndAcknowledge(list, *initial, 1U),
            "Manual-switch setup must start A"))
    {
        return false;
    }

    std::size_t tokenAllocations = 0U;
    const std::vector<MusicRuntimeAction> switchToC = list.switchToTrack(
        trackId("c"),
        PlaybackActivity::Playing,
        [&tokenAllocations]() -> std::optional<MusicSourceToken>
        {
            ++tokenAllocations;
            return MusicSourceToken{2U};
        });
    const StopSource* const stopA = switchToC.empty()
        ? nullptr : std::get_if<StopSource>(&switchToC.front());
    const StartSource* const startC = switchToC.size() < 2U
        ? nullptr : std::get_if<StartSource>(&switchToC[1U]);
    if (!require(stopA != nullptr && stopA->token == MusicSourceToken{1U}
            && startC != nullptr && startC->trackId == trackId("c")
            && startC->token == MusicSourceToken{2U}
            && startC->activity == PlaybackActivity::Playing
            && list.currentTrackId() == std::optional<TrackId>{trackId("c")}
            && list.sourceToken() == std::optional<MusicSourceToken>{MusicSourceToken{2U}},
            "A different TrackId must stop the old source and begin the requested track fresh")
        || !require(list.acknowledgeStarted(MusicSourceToken{2U}),
            "The manually selected ListLoop source must acknowledge normally"))
    {
        return false;
    }

    const std::vector<MusicRuntimeAction> sameTrack = list.switchToTrack(
        trackId("c"),
        PlaybackActivity::Playing,
        [&tokenAllocations]() -> std::optional<MusicSourceToken>
        {
            ++tokenAllocations;
            return MusicSourceToken{3U};
        });
    if (!require(sameTrack.empty() && tokenAllocations == 1U
            && list.sourceToken() == std::optional<MusicSourceToken>{MusicSourceToken{2U}},
            "Selecting the current TrackId must not allocate or replace a source")
        || !require(list.selectNextTrackAfterEnd() == std::optional<TrackId>{trackId("a")},
            "ListLoop must continue from a manually selected C to A at EOF"))
    {
        return false;
    }

    MusicSession single{
        MusicScope::Item,
        queue({track("a", "a.mp3"), track("b", "b.mp3")}, MusicQueueMode::CurrentSingleLoop)};
    const std::optional<TrackId> singleInitial = single.selectInitialTrack();
    if (!require(beginAndAcknowledge(single, *singleInitial, 10U),
        "CurrentSingleLoop setup must start A"))
    {
        return false;
    }
    const std::vector<MusicRuntimeAction> switchToB = single.switchToTrack(
        trackId("b"),
        PlaybackActivity::Playing,
        []() -> std::optional<MusicSourceToken>
        {
            return MusicSourceToken{11U};
        });
    if (!require(switchToB.size() == 2U
            && single.acknowledgeStarted(MusicSourceToken{11U})
            && single.selectNextTrackAfterEnd() == std::optional<TrackId>{trackId("b")},
            "CurrentSingleLoop must loop the manually selected TrackId"))
    {
        return false;
    }

    int shufflePermutationCalls = 0;
    MusicSession shuffle{
        MusicScope::PlaylistGlobal,
        queue({
            track("a", "a.mp3"),
            track("b", "b.mp3"),
            track("c", "c.mp3"),
            track("d", "d.mp3")}, MusicQueueMode::Shuffle),
        [&shufflePermutationCalls](std::span<const TrackId>)
        {
            ++shufflePermutationCalls;
            return std::vector<TrackId>{trackId("c"), trackId("a"), trackId("d"), trackId("b")};
        }};
    const std::optional<TrackId> shuffleInitial = shuffle.selectInitialTrack();
    if (!require(beginAndAcknowledge(shuffle, *shuffleInitial, 20U),
        "Shuffle setup must start A"))
    {
        return false;
    }
    const std::vector<MusicRuntimeAction> shuffleToC = shuffle.switchToTrack(
        trackId("c"),
        PlaybackActivity::Playing,
        []() -> std::optional<MusicSourceToken>
        {
            return MusicSourceToken{21U};
        });
    if (!require(shuffleToC.size() == 2U
            && shuffle.acknowledgeStarted(MusicSourceToken{21U})
            && shuffle.selectNextTrackAfterEnd() == std::optional<TrackId>{trackId("d")}
            && shufflePermutationCalls == 1,
            "Manual Shuffle selection must remove a target still in the bag without rebuilding it"))
    {
        return false;
    }

    shuffle.beginStart(trackId("d"), MusicSourceToken{22U});
    if (!require(shuffle.acknowledgeStarted(MusicSourceToken{22U}),
        "Shuffle continuation setup must acknowledge D"))
    {
        return false;
    }
    const std::vector<MusicRuntimeAction> shuffleToA = shuffle.switchToTrack(
        trackId("a"),
        PlaybackActivity::Playing,
        []() -> std::optional<MusicSourceToken>
        {
            return MusicSourceToken{23U};
        });
    if (!require(shuffleToA.size() == 2U
            && shuffle.acknowledgeStarted(MusicSourceToken{23U})
            && shuffle.selectNextTrackAfterEnd() == std::optional<TrackId>{trackId("b")}
            && shufflePermutationCalls == 1,
            "Selecting an already consumed Shuffle TrackId must leave the remaining bag unchanged"))
    {
        return false;
    }

    const MusicQueueSnapshot pausedConfig{
        {MusicTrackSnapshot{trackId("a"), "a.mp3", 0.20F},
         MusicTrackSnapshot{trackId("b"), "b.mp3", 0.75F}},
        MusicQueueMode::ListLoop,
        0.50F};
    MusicSession paused{MusicScope::Item, pausedConfig};
    const std::optional<TrackId> pausedInitial = paused.selectInitialTrack();
    if (!require(beginAndAcknowledge(paused, *pausedInitial, 30U),
        "Paused switch setup must start A"))
    {
        return false;
    }
    const std::vector<MusicRuntimeAction> pausedSwitch = paused.switchToTrack(
        trackId("b"),
        PlaybackActivity::Paused,
        []() -> std::optional<MusicSourceToken>
        {
            return MusicSourceToken{31U};
        });
    const StartSource* const pausedStart = pausedSwitch.size() < 2U
        ? nullptr : std::get_if<StartSource>(&pausedSwitch[1U]);
    if (!require(pausedStart != nullptr && pausedStart->activity == PlaybackActivity::Paused
            && pausedStart->effectiveGain == 0.375F
            && paused.currentTrackId() == std::optional<TrackId>{trackId("b")},
            "A paused switch must preserve activity and use the target TrackId gain"))
    {
        return false;
    }

    const MusicSourceToken tokenBeforeRejectedSwitch = paused.sourceToken().value_or(MusicSourceToken{});
    if (!require(paused.switchToTrack(
                     trackId("a"),
                     PlaybackActivity::Paused,
                     []() -> std::optional<MusicSourceToken>
                     {
                         return std::nullopt;
                     }).empty()
            && paused.currentTrackId() == std::optional<TrackId>{trackId("b")}
            && paused.sourceToken()
                == std::optional<MusicSourceToken>{tokenBeforeRejectedSwitch},
        "Replacement-token allocation failure must leave manual-switch state unchanged"))
    {
        return false;
    }

    return require(paused.switchToTrack(
                       trackId("missing"),
                       PlaybackActivity::Paused,
                       []() -> std::optional<MusicSourceToken>
                       {
                           return MusicSourceToken{32U};
                       }).empty()
            && paused.currentTrackId() == std::optional<TrackId>{trackId("b")}
            && paused.sourceToken()
                == std::optional<MusicSourceToken>{tokenBeforeRejectedSwitch},
        "An invalid TrackId must leave current identity and source ownership unchanged");
}

}

int main()
{
    const bool success = verifyEmptyQueue()
        && verifyListLoopSequenceAndBoundedFailures()
        && verifyCurrentSingleLoopFailureFallbackAndReset()
        && verifyCurrentSingleLoopAllInvalid()
        && verifyShuffleBagAndBoundary()
        && verifyShuffleFailuresAndSingleTrack()
        && verifyTransientFailureState()
        && verifyExecutionAvailableRecovery()
        && verifyManualTrackSwitchSemantics();
    return success ? 0 : 1;
}
