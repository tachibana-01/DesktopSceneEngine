#include "Music/BackgroundMusicOrchestrator.h"

#include <algorithm>
#include <deque>
#include <filesystem>
#include <functional>
#include <iostream>
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
    MusicQueueMode mode = MusicQueueMode::ListLoop
)
{
    return MusicQueueSnapshot{std::vector<MusicTrackSnapshot>{tracks}, mode, volume};
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

const SetSourceGain* gainAction(const MusicRuntimeAction& action)
{
    return std::get_if<SetSourceGain>(&action);
}

enum class StartOutcome
{
    Started,
    Unplayable,
    Transient,
    Deferred
};

class ScriptedExecutor final
{
public:
    explicit ScriptedExecutor(std::initializer_list<StartOutcome> outcomes)
        : outcomes_(outcomes)
    {
    }

    [[nodiscard]] std::optional<MusicRuntimeEvent> execute(const MusicRuntimeAction& action)
    {
        actions_.push_back(action);
        if (const StartSource* const start = startAction(action))
        {
            if (onStart_)
            {
                onStart_(*start);
            }

            const StartOutcome outcome = outcomes_.empty()
                ? StartOutcome::Started
                : outcomes_.front();
            if (!outcomes_.empty())
            {
                outcomes_.pop_front();
            }

            switch (outcome)
            {
            case StartOutcome::Started:
                return MusicSourceStarted{start->token};

            case StartOutcome::Unplayable:
                return MusicSourceFailed{start->token, MusicSourceFailureClass::UnplayableTrack};

            case StartOutcome::Transient:
                return MusicSourceFailed{start->token, MusicSourceFailureClass::TransientResourceFailure};

            case StartOutcome::Deferred:
                return std::nullopt;
            }
        }

        return std::nullopt;
    }

    [[nodiscard]] const std::vector<MusicRuntimeAction>& actions() const noexcept
    {
        return actions_;
    }

    [[nodiscard]] std::size_t startCount() const noexcept
    {
        return static_cast<std::size_t>(std::count_if(
            actions_.cbegin(),
            actions_.cend(),
            [](const MusicRuntimeAction& action)
            {
                return startAction(action) != nullptr;
            }));
    }

    std::function<void(const StartSource&)> onStart_{};

private:
    std::deque<StartOutcome> outcomes_{};
    std::vector<MusicRuntimeAction> actions_{};
};

bool verifySimpleSuccess()
{
    BackgroundMusicRuntime runtime{};
    ScriptedExecutor executor{StartOutcome::Started};
    BackgroundMusicOrchestrator orchestrator{
        runtime,
        [&executor](const MusicRuntimeAction& action)
        {
            return executor.execute(action);
        }};

    const MusicProcessResult result = orchestrator.processMusicEvent(PlaybackSessionStarted{
        scope("playlist-success", std::nullopt, queue({track("a", "a.mp3")}), std::nullopt)});
    return require(result == MusicProcessResult::Completed,
        "A synchronous successful start must fully drain")
        && require(executor.startCount() == 1U,
            "A successful session start must execute one StartSource")
        && require(runtime.playlistSession() != nullptr
                && runtime.playlistSession()->phase() == MusicSessionPhase::Playing,
            "A Started fact must make the session Playing in the same outer drain");
}

bool verifyUnplayableCascadeAndAllInvalid()
{
    BackgroundMusicRuntime runtime{};
    ScriptedExecutor executor{
        StartOutcome::Unplayable,
        StartOutcome::Unplayable,
        StartOutcome::Started};
    BackgroundMusicOrchestrator orchestrator{
        runtime,
        [&executor](const MusicRuntimeAction& action)
        {
            return executor.execute(action);
        }};

    const MusicProcessResult result = orchestrator.processMusicEvent(PlaybackSessionStarted{
        scope(
            "playlist-cascade",
            std::nullopt,
            queue({track("a", "a.mp3"), track("b", "b.mp3"), track("c", "c.mp3")}),
            std::nullopt)});
    const std::vector<MusicRuntimeAction>& actions = executor.actions();
    if (!require(result == MusicProcessResult::Completed
            && executor.startCount() == 3U
            && actions.size() == 5U
            && startAction(actions[0]) != nullptr
            && stopAction(actions[1]) != nullptr
            && startAction(actions[2]) != nullptr
            && stopAction(actions[3]) != nullptr
            && startAction(actions[4]) != nullptr
            && runtime.playlistSession() != nullptr
            && runtime.playlistSession()->currentTrackId() == std::optional<TrackId>{trackId("c")}
            && runtime.playlistSession()->phase() == MusicSessionPhase::Playing,
        "Unplayable facts must iteratively advance A to B to C without recursion"))
    {
        return false;
    }

    BackgroundMusicRuntime allInvalidRuntime{};
    ScriptedExecutor allInvalidExecutor{
        StartOutcome::Unplayable,
        StartOutcome::Unplayable,
        StartOutcome::Unplayable};
    BackgroundMusicOrchestrator allInvalidOrchestrator{
        allInvalidRuntime,
        [&allInvalidExecutor](const MusicRuntimeAction& action)
        {
            return allInvalidExecutor.execute(action);
        }};
    const MusicProcessResult allInvalidResult = allInvalidOrchestrator.processMusicEvent(
        PlaybackSessionStarted{
            scope(
                "playlist-all-invalid",
                std::nullopt,
                queue({track("a", "a.mp3"), track("b", "b.mp3"), track("c", "c.mp3")}),
                std::nullopt)});
    return require(allInvalidResult == MusicProcessResult::Completed
            && allInvalidExecutor.startCount() == 3U
            && allInvalidRuntime.playlistSession() != nullptr
            && allInvalidRuntime.playlistSession()->phase() == MusicSessionPhase::NoPlayableTrack,
        "All invalid candidates must finish at NoPlayableTrack without a fourth StartSource");
}

bool verifyTransientRecoveryAndPausedRecovery()
{
    BackgroundMusicRuntime runtime{};
    ScriptedExecutor executor{
        StartOutcome::Transient,
        StartOutcome::Transient,
        StartOutcome::Started};
    BackgroundMusicOrchestrator orchestrator{
        runtime,
        [&executor](const MusicRuntimeAction& action)
        {
            return executor.execute(action);
        }};

    const ActiveMusicScopeSnapshot initialScope = scope(
        "playlist-transient",
        std::nullopt,
        queue({track("a", "a.mp3"), track("b", "b.mp3")}),
        std::nullopt,
        PlaybackActivity::Paused);
    if (!require(orchestrator.processMusicEvent(PlaybackSessionStarted{initialScope})
                == MusicProcessResult::Completed
            && executor.startCount() == 1U
            && runtime.playlistSession() != nullptr
            && runtime.playlistSession()->phase() == MusicSessionPhase::WaitingForExecutor,
        "A transient start failure must wait without starting a successor"))
    {
        return false;
    }

    const MusicProcessResult firstRecovery = orchestrator.processMusicEvent(MusicExecutionAvailable{});
    if (!require(firstRecovery == MusicProcessResult::Completed
            && executor.startCount() == 2U
            && runtime.playlistSession()->phase() == MusicSessionPhase::WaitingForExecutor,
        "A recovery transient failure must return to Waiting without automatic token3"))
    {
        return false;
    }

    const MusicProcessResult secondRecovery = orchestrator.processMusicEvent(MusicExecutionAvailable{});
    const std::vector<MusicRuntimeAction>& actions = executor.actions();
    return require(secondRecovery == MusicProcessResult::Completed
            && executor.startCount() == 3U
            && startAction(actions.back()) != nullptr
            && startAction(actions.back())->trackId == trackId("a")
            && startAction(actions.back())->activity == PlaybackActivity::Paused
            && runtime.playlistSession()->phase() == MusicSessionPhase::Playing,
        "Only a second explicit availability event may retry the same paused TrackId again");
}

bool verifyTwoWaitingSessionsAndBatchFactOrdering()
{
    BackgroundMusicRuntime runtime{};
    ScriptedExecutor executor{
        StartOutcome::Transient,
        StartOutcome::Transient,
        StartOutcome::Unplayable,
        StartOutcome::Started};
    BackgroundMusicOrchestrator orchestrator{
        runtime,
        [&executor](const MusicRuntimeAction& action)
        {
            return executor.execute(action);
        }};

    if (!require(orchestrator.processMusicEvent(PlaybackSessionStarted{
                    scope(
                        "playlist-two-waiting",
                        "item-two-waiting",
                        queue({track("playlist-a", "playlist-a.mp3")}),
                        queue({track("item-a", "item-a.mp3")}))}) == MusicProcessResult::Completed
            && runtime.playlistSession() != nullptr
            && runtime.itemSession() != nullptr
            && runtime.playlistSession()->phase() == MusicSessionPhase::WaitingForExecutor
            && runtime.itemSession()->phase() == MusicSessionPhase::WaitingForExecutor,
        "Test setup must put both sessions into WaitingForExecutor"))
    {
        return false;
    }

    bool itemStartObservedPlaylistPending = false;
    executor.onStart_ = [&runtime, &itemStartObservedPlaylistPending](const StartSource& start)
    {
        if (start.scope == MusicScope::Item)
        {
            itemStartObservedPlaylistPending = runtime.playlistSession() != nullptr
                && runtime.playlistSession()->phase() == MusicSessionPhase::PendingStart;
        }
    };
    const MusicProcessResult recovery = orchestrator.processMusicEvent(MusicExecutionAvailable{});
    const std::vector<MusicRuntimeAction>& actions = executor.actions();
    return require(recovery == MusicProcessResult::Completed
            && executor.startCount() == 4U
            && actions.size() == 7U
            && startAction(actions[4]) != nullptr
            && startAction(actions[5]) != nullptr
            && startAction(actions[4])->scope == MusicScope::PlaylistGlobal
            && startAction(actions[5])->scope == MusicScope::Item
            && itemStartObservedPlaylistPending
            && runtime.playlistSession()->phase() == MusicSessionPhase::NoPlayableTrack
            && runtime.itemSession()->phase() == MusicSessionPhase::Playing,
        "Recovery must execute Playlist then Item before applying their immediate facts");
}

bool verifyDeferredGainAndStaleFacts()
{
    BackgroundMusicRuntime runtime{};
    ScriptedExecutor executor{StartOutcome::Deferred};
    BackgroundMusicOrchestrator orchestrator{
        runtime,
        [&executor](const MusicRuntimeAction& action)
        {
            return executor.execute(action);
        }};

    if (!require(orchestrator.processMusicEvent(PlaybackSessionStarted{
                    scope(
                        "playlist-deferred-gain",
                        std::nullopt,
                        queue({track("a", "a.mp3", 0.5F)}, 1.0F),
                        std::nullopt)}) == MusicProcessResult::Completed
            && runtime.playlistSession() != nullptr
            && runtime.playlistSession()->phase() == MusicSessionPhase::PendingStart,
        "A deferred start fact must leave the session PendingStart"))
    {
        return false;
    }

    const MusicSourceToken pendingToken = *runtime.playlistSession()->sourceToken();
    if (!require(orchestrator.processMusicEvent(PlaylistMusicConfigChanged{
                    playlistId("playlist-deferred-gain"),
                    queue({track("a", "a.mp3", 0.25F)}, 0.8F)})
                == MusicProcessResult::Completed
            && executor.actions().size() == 1U,
        "A pending gain update must remain deferred until Started"))
    {
        return false;
    }

    if (!require(orchestrator.processMusicEvent(MusicSourceStarted{pendingToken})
                == MusicProcessResult::Completed
            && executor.actions().size() == 2U
            && gainAction(executor.actions().back()) != nullptr
            && gainAction(executor.actions().back())->token == pendingToken
            && gainAction(executor.actions().back())->gain == 0.2F,
        "Started must execute the deferred gain action before its outer drain returns"))
    {
        return false;
    }

    BackgroundMusicRuntime staleRuntime{};
    ScriptedExecutor staleExecutor{StartOutcome::Unplayable, StartOutcome::Deferred};
    BackgroundMusicOrchestrator staleOrchestrator{
        staleRuntime,
        [&staleExecutor](const MusicRuntimeAction& action)
        {
            return staleExecutor.execute(action);
        }};
    (void)staleOrchestrator.processMusicEvent(PlaybackSessionStarted{
        scope(
            "playlist-stale",
            std::nullopt,
            queue({track("a", "a.mp3"), track("b", "b.mp3")}),
            std::nullopt)});
    const MusicSourceToken staleToken = startAction(staleExecutor.actions().front())->token;
    const std::size_t actionsBeforeStaleFacts = staleExecutor.actions().size();
    (void)staleOrchestrator.processMusicEvent(MusicSourceStarted{staleToken});
    (void)staleOrchestrator.processMusicEvent(MusicSourceEnded{staleToken});
    (void)staleOrchestrator.processMusicEvent(MusicSourceFailed{
        staleToken,
        MusicSourceFailureClass::UnplayableTrack});
    return require(staleExecutor.actions().size() == actionsBeforeStaleFacts
            && staleRuntime.playlistSession() != nullptr
            && staleRuntime.playlistSession()->currentTrackId() == std::optional<TrackId>{trackId("b")},
        "Late facts for a replaced token must be stale no-ops through the orchestrator");
}

bool verifyReentrancyAndSafetyLimit()
{
    BackgroundMusicRuntime runtime{};
    ScriptedExecutor executor{StartOutcome::Started};
    BackgroundMusicOrchestrator orchestrator{
        runtime,
        [&executor](const MusicRuntimeAction& action)
        {
            return executor.execute(action);
        }};

    MusicProcessResult nestedResult = MusicProcessResult::Completed;
    executor.onStart_ = [&orchestrator, &nestedResult](const StartSource&)
    {
        nestedResult = orchestrator.processMusicEvent(PlaybackPaused{});
    };
    if (!require(orchestrator.processMusicEvent(PlaybackSessionStarted{
                    scope("playlist-reentrant", std::nullopt, queue({track("a", "a.mp3")}), std::nullopt)})
                == MusicProcessResult::Completed
            && nestedResult == MusicProcessResult::Enqueued
            && runtime.playbackState() == BackgroundMusicPlaybackState::Paused
            && runtime.playlistSession() != nullptr
            && runtime.playlistSession()->phase() == MusicSessionPhase::Playing,
        "Nested events must enqueue without recursively applying Runtime"))
    {
        return false;
    }

    BackgroundMusicRuntime cappedRuntime{};
    ScriptedExecutor cappedExecutor{StartOutcome::Started};
    BackgroundMusicOrchestrator cappedOrchestrator{
        cappedRuntime,
        [&cappedExecutor](const MusicRuntimeAction& action)
        {
            return cappedExecutor.execute(action);
        }};
    MusicProcessResult callbackResult = MusicProcessResult::Completed;
    cappedExecutor.onStart_ = [&cappedOrchestrator, &callbackResult](const StartSource&)
    {
        for (std::size_t index = 0U; index < 820U; ++index)
        {
            callbackResult = cappedOrchestrator.processMusicEvent(PlaybackPaused{});
        }
    };
    const MusicProcessResult cappedResult = cappedOrchestrator.processMusicEvent(PlaybackSessionStarted{
        scope("playlist-capped", std::nullopt, queue({track("a", "a.mp3")}), std::nullopt)});
    if (!require(cappedResult == MusicProcessResult::SafetyLimitExceeded
            && callbackResult == MusicProcessResult::Enqueued
            && cappedExecutor.startCount() == 1U,
        "The safety budget must stop before admitting the next queued event"))
    {
        return false;
    }

    return require(cappedOrchestrator.processMusicEvent(PlaybackResumed{})
                == MusicProcessResult::Completed
            && cappedRuntime.playlistSession() != nullptr
            && cappedRuntime.playlistSession()->phase() == MusicSessionPhase::Playing,
        "A later explicit event must resume draining the FIFO after a safety limit");
}

}

int main()
{
    const bool success = verifySimpleSuccess()
        && verifyUnplayableCascadeAndAllInvalid()
        && verifyTransientRecoveryAndPausedRecovery()
        && verifyTwoWaitingSessionsAndBatchFactOrdering()
        && verifyDeferredGainAndStaleFacts()
        && verifyReentrancyAndSafetyLimit();
    return success ? 0 : 1;
}
