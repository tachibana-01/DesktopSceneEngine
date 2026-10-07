#include "BackgroundMusicRuntime.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace DesktopSceneEngine::Music
{

namespace
{

[[nodiscard]] bool isUnitInterval(float value) noexcept
{
    return std::isfinite(value) && value >= 0.0F && value <= 1.0F;
}

}

bool isValid(const PlaylistId& id) noexcept
{
    return static_cast<bool>(id);
}

bool isValid(const ItemId& id) noexcept
{
    return static_cast<bool>(id);
}

bool isValid(const TrackId& id) noexcept
{
    return static_cast<bool>(id);
}

bool isValid(MusicQueueMode mode) noexcept
{
    return mode == MusicQueueMode::CurrentSingleLoop
        || mode == MusicQueueMode::ListLoop
        || mode == MusicQueueMode::Shuffle;
}

bool isValid(PlaybackActivity activity) noexcept
{
    return activity == PlaybackActivity::Playing || activity == PlaybackActivity::Paused;
}

bool isValid(const MusicTrackSnapshot& snapshot) noexcept
{
    return isValid(snapshot.id)
        && !snapshot.sourcePath.empty()
        && isUnitInterval(snapshot.trackVolume);
}

bool isValid(const MusicQueueSnapshot& snapshot) noexcept
{
    if (!isValid(snapshot.mode) || !isUnitInterval(snapshot.queueVolume))
    {
        return false;
    }

    for (std::size_t index = 0U; index < snapshot.tracks.size(); ++index)
    {
        if (!isValid(snapshot.tracks[index]))
        {
            return false;
        }

        const auto duplicate = std::find_if(
            snapshot.tracks.cbegin(),
            snapshot.tracks.cbegin() + static_cast<std::ptrdiff_t>(index),
            [&snapshot, index](const MusicTrackSnapshot& candidate)
            {
                return candidate.id == snapshot.tracks[index].id;
            });
        if (duplicate != snapshot.tracks.cbegin() + static_cast<std::ptrdiff_t>(index))
        {
            return false;
        }
    }

    return true;
}

bool isValid(const ActiveMusicScopeSnapshot& snapshot) noexcept
{
    if (!isValid(snapshot.playlistId)
        || !isValid(snapshot.playlistMusic)
        || !isValid(snapshot.activity)
        || snapshot.itemId.has_value() != snapshot.itemMusic.has_value())
    {
        return false;
    }

    return !snapshot.itemId.has_value()
        || (isValid(*snapshot.itemId) && isValid(*snapshot.itemMusic));
}

BackgroundMusicRuntime::BackgroundMusicRuntime(
    ShufflePermutationProvider shufflePermutationProvider
)
    : shufflePermutationProvider_(std::move(shufflePermutationProvider))
{
}

std::vector<MusicRuntimeAction> BackgroundMusicRuntime::apply(const MusicRuntimeEvent& event)
{
    return std::visit(
        [this](const auto& typedEvent)
        {
            return applyEvent(typedEvent);
        },
        event);
}

BackgroundMusicPlaybackState BackgroundMusicRuntime::playbackState() const noexcept
{
    return playbackState_;
}

const std::optional<PlaylistId>& BackgroundMusicRuntime::activePlaylistId() const noexcept
{
    return activePlaylistId_;
}

const std::optional<ItemId>& BackgroundMusicRuntime::activeItemId() const noexcept
{
    return activeItemId_;
}

const MusicSession* BackgroundMusicRuntime::playlistSession() const noexcept
{
    return playlistSession_ ? &*playlistSession_ : nullptr;
}

const MusicSession* BackgroundMusicRuntime::itemSession() const noexcept
{
    return itemSession_ ? &*itemSession_ : nullptr;
}

std::vector<MusicRuntimeAction> BackgroundMusicRuntime::applyEvent(
    const PlaybackSessionStarted& event
)
{
    if (playbackState_ != BackgroundMusicPlaybackState::Stopped || !isValid(event.scope))
    {
        return {};
    }

    if (!canAllocateSourceTokens(startupCount(event.scope)))
    {
        return {};
    }

    std::optional<MusicSession> newPlaylistSession{};
    if (event.scope.playlistMusic.enabled)
    {
        newPlaylistSession.emplace(
            MusicScope::PlaylistGlobal,
            event.scope.playlistMusic,
            shufflePermutationProvider_);
    }
    std::optional<MusicSession> newItemSession{};
    if (event.scope.itemId.has_value() && event.scope.itemMusic->enabled)
    {
        newItemSession.emplace(MusicScope::Item, *event.scope.itemMusic, shufflePermutationProvider_);
    }

    std::vector<MusicRuntimeAction> actions{};
    actions.reserve(startupCount(event.scope));
    playbackState_ = event.scope.activity == PlaybackActivity::Playing
        ? BackgroundMusicPlaybackState::Playing
        : BackgroundMusicPlaybackState::Paused;
    activePlaylistId_ = event.scope.playlistId;
    activeItemId_ = event.scope.itemId;
    playlistSession_ = std::move(newPlaylistSession);
    itemSession_ = std::move(newItemSession);

    const PlaybackActivity activity = activityFor(playbackState_);
    if (playlistSession_)
    {
        startSession(*playlistSession_, activity, actions);
    }
    if (itemSession_)
    {
        startSession(*itemSession_, activity, actions);
    }

    return actions;
}

std::vector<MusicRuntimeAction> BackgroundMusicRuntime::applyEvent(
    const ActivePlaybackScopeChanged& event
)
{
    if (playbackState_ == BackgroundMusicPlaybackState::Stopped || !isValid(event.scope))
    {
        return {};
    }

    if (!activePlaylistId_.has_value())
    {
        return {};
    }

    if (event.scope.playlistId == *activePlaylistId_ && event.scope.itemId == activeItemId_)
    {
        return {};
    }

    const bool playlistChanged = event.scope.playlistId != *activePlaylistId_;
    const std::size_t requiredTokens = playlistChanged
        ? startupCount(event.scope)
        : (event.scope.itemMusic.has_value()
                && event.scope.itemMusic->enabled
                && !event.scope.itemMusic->tracks.empty()
            ? 1U
            : 0U);
    if (!canAllocateSourceTokens(requiredTokens))
    {
        return {};
    }

    const PlaybackActivity activity = activityFor(playbackState_);
    std::vector<MusicRuntimeAction> actions{};
    actions.reserve((playlistChanged ? 2U : 1U) + requiredTokens);

    if (playlistChanged)
    {
        std::optional<MusicSession> newPlaylistSession{};
        if (event.scope.playlistMusic.enabled)
        {
            newPlaylistSession.emplace(
                MusicScope::PlaylistGlobal,
                event.scope.playlistMusic,
                shufflePermutationProvider_);
        }
        std::optional<MusicSession> newItemSession{};
        if (event.scope.itemId.has_value() && event.scope.itemMusic->enabled)
        {
            newItemSession.emplace(MusicScope::Item, *event.scope.itemMusic, shufflePermutationProvider_);
        }

        if (itemSession_)
        {
            stopSession(*itemSession_, actions);
        }
        if (playlistSession_)
        {
            stopSession(*playlistSession_, actions);
        }

        itemSession_.reset();
        playlistSession_.reset();
        activePlaylistId_ = event.scope.playlistId;
        activeItemId_ = event.scope.itemId;
        playlistSession_ = std::move(newPlaylistSession);
        itemSession_ = std::move(newItemSession);

        if (playlistSession_)
        {
            startSession(*playlistSession_, activity, actions);
        }
        if (itemSession_)
        {
            startSession(*itemSession_, activity, actions);
        }

        return actions;
    }

    std::optional<MusicSession> newItemSession{};
    if (event.scope.itemId.has_value() && event.scope.itemMusic->enabled)
    {
        newItemSession.emplace(MusicScope::Item, *event.scope.itemMusic, shufflePermutationProvider_);
    }

    if (itemSession_)
    {
        stopSession(*itemSession_, actions);
    }
    itemSession_.reset();
    activeItemId_ = event.scope.itemId;
    itemSession_ = std::move(newItemSession);
    if (itemSession_)
    {
        startSession(*itemSession_, activity, actions);
    }

    return actions;
}

std::vector<MusicRuntimeAction> BackgroundMusicRuntime::applyEvent(const PlaybackPaused&)
{
    if (playbackState_ == BackgroundMusicPlaybackState::Playing)
    {
        playbackState_ = BackgroundMusicPlaybackState::Paused;
    }

    return {};
}

std::vector<MusicRuntimeAction> BackgroundMusicRuntime::applyEvent(const PlaybackResumed&)
{
    if (playbackState_ == BackgroundMusicPlaybackState::Paused)
    {
        playbackState_ = BackgroundMusicPlaybackState::Playing;
    }

    return {};
}

std::vector<MusicRuntimeAction> BackgroundMusicRuntime::applyEvent(const PlaybackStopped&)
{
    if (playbackState_ == BackgroundMusicPlaybackState::Stopped)
    {
        return {};
    }

    std::vector<MusicRuntimeAction> actions{};
    actions.reserve(2U);
    if (itemSession_)
    {
        stopSession(*itemSession_, actions);
    }
    if (playlistSession_)
    {
        stopSession(*playlistSession_, actions);
    }

    itemSession_.reset();
    playlistSession_.reset();
    activeItemId_.reset();
    activePlaylistId_.reset();
    playbackState_ = BackgroundMusicPlaybackState::Stopped;
    return actions;
}

std::vector<MusicRuntimeAction> BackgroundMusicRuntime::applyEvent(
    const PlaylistMusicConfigChanged& event
)
{
    if (!isValid(event.playlistId) || !isValid(event.config)
        || playbackState_ == BackgroundMusicPlaybackState::Stopped
        || !activePlaylistId_.has_value() || event.playlistId != *activePlaylistId_)
    {
        return {};
    }

    if (!event.config.enabled)
    {
        std::vector<MusicRuntimeAction> actions{};
        if (playlistSession_)
        {
            actions.reserve(1U);
            stopSession(*playlistSession_, actions);
            playlistSession_.reset();
        }
        return actions;
    }

    if (!playlistSession_)
    {
        const std::size_t requiredTokens = event.config.tracks.empty() ? 0U : 1U;
        if (!canAllocateSourceTokens(requiredTokens))
        {
            return {};
        }

        MusicSession newSession{
            MusicScope::PlaylistGlobal,
            event.config,
            shufflePermutationProvider_};
        std::vector<MusicRuntimeAction> actions{};
        actions.reserve(requiredTokens);
        startSession(newSession, activityFor(playbackState_), actions);
        playlistSession_ = std::move(newSession);
        return actions;
    }

    return playlistSession_->reconcileConfig(
        event.config,
        activityFor(playbackState_),
        [this]() -> std::optional<MusicSourceToken>
        {
            if (!canAllocateSourceTokens(1U))
            {
                return std::nullopt;
            }

            return allocateSourceToken();
        });
}

std::vector<MusicRuntimeAction> BackgroundMusicRuntime::applyEvent(
    const ItemMusicConfigChanged& event
)
{
    if (!isValid(event.playlistId) || !isValid(event.itemId) || !isValid(event.config)
        || playbackState_ == BackgroundMusicPlaybackState::Stopped
        || !activePlaylistId_.has_value() || !activeItemId_.has_value()
        || event.playlistId != *activePlaylistId_ || event.itemId != *activeItemId_)
    {
        return {};
    }

    if (!event.config.enabled)
    {
        std::vector<MusicRuntimeAction> actions{};
        if (itemSession_)
        {
            actions.reserve(1U);
            stopSession(*itemSession_, actions);
            itemSession_.reset();
        }
        return actions;
    }

    if (!itemSession_)
    {
        const std::size_t requiredTokens = event.config.tracks.empty() ? 0U : 1U;
        if (!canAllocateSourceTokens(requiredTokens))
        {
            return {};
        }

        MusicSession newSession{
            MusicScope::Item,
            event.config,
            shufflePermutationProvider_};
        std::vector<MusicRuntimeAction> actions{};
        actions.reserve(requiredTokens);
        startSession(newSession, activityFor(playbackState_), actions);
        itemSession_ = std::move(newSession);
        return actions;
    }

    return itemSession_->reconcileConfig(
        event.config,
        activityFor(playbackState_),
        [this]() -> std::optional<MusicSourceToken>
        {
            if (!canAllocateSourceTokens(1U))
            {
                return std::nullopt;
            }

            return allocateSourceToken();
        });
}

std::vector<MusicRuntimeAction> BackgroundMusicRuntime::applyEvent(
    const PlaylistMusicTrackSwitchRequested& event
)
{
    if (!isValid(event.playlistId) || !isValid(event.trackId)
        || playbackState_ == BackgroundMusicPlaybackState::Stopped
        || !activePlaylistId_.has_value() || event.playlistId != *activePlaylistId_
        || !playlistSession_)
    {
        return {};
    }

    return playlistSession_->switchToTrack(
        event.trackId,
        activityFor(playbackState_),
        [this]() -> std::optional<MusicSourceToken>
        {
            if (!canAllocateSourceTokens(1U))
            {
                return std::nullopt;
            }

            return allocateSourceToken();
        });
}

std::vector<MusicRuntimeAction> BackgroundMusicRuntime::applyEvent(
    const ItemMusicTrackSwitchRequested& event
)
{
    if (!isValid(event.playlistId) || !isValid(event.itemId) || !isValid(event.trackId)
        || playbackState_ == BackgroundMusicPlaybackState::Stopped
        || !activePlaylistId_.has_value() || !activeItemId_.has_value()
        || event.playlistId != *activePlaylistId_ || event.itemId != *activeItemId_
        || !itemSession_)
    {
        return {};
    }

    return itemSession_->switchToTrack(
        event.trackId,
        activityFor(playbackState_),
        [this]() -> std::optional<MusicSourceToken>
        {
            if (!canAllocateSourceTokens(1U))
            {
                return std::nullopt;
            }

            return allocateSourceToken();
        });
}

std::vector<MusicRuntimeAction> BackgroundMusicRuntime::applyEvent(
    const MusicSourceStarted& event
)
{
    if (MusicSession* const session = sessionForToken(event.token))
    {
        if (const std::optional<SetSourceGain> deferredGain = session->acknowledgeStartedWithDeferredGain(
                event.token);
            deferredGain.has_value())
        {
            return std::vector<MusicRuntimeAction>{*deferredGain};
        }
    }

    return {};
}

std::vector<MusicRuntimeAction> BackgroundMusicRuntime::applyEvent(
    const MusicSourceEnded& event
)
{
    MusicSession* const session = sessionForToken(event.token);
    if (session == nullptr || !canAllocateSourceTokens(1U))
    {
        return {};
    }

    const std::optional<TrackId> successor = session->selectNextTrackAfterEnd();
    if (!successor.has_value())
    {
        return {};
    }

    const MusicSourceToken replacementToken = allocateSourceToken();
    if (!replacementToken)
    {
        return {};
    }

    std::vector<MusicRuntimeAction> actions{};
    actions.reserve(2U);
    actions.emplace_back(StopSource{event.token});
    appendStartAction(
        *session,
        *successor,
        replacementToken,
        activityFor(playbackState_),
        actions);
    return actions;
}

std::vector<MusicRuntimeAction> BackgroundMusicRuntime::applyEvent(
    const MusicSourceFailed& event
)
{
    MusicSession* const session = sessionForToken(event.token);
    if (session == nullptr)
    {
        return {};
    }

    if (event.failure == MusicSourceFailureClass::TransientResourceFailure)
    {
        session->enterWaitingForExecutor();
        return std::vector<MusicRuntimeAction>{StopSource{event.token}};
    }

    if (!canAllocateSourceTokens(1U))
    {
        return {};
    }

    const std::optional<TrackId> successor = session->selectNextTrackAfterUnplayableFailure();
    std::vector<MusicRuntimeAction> actions{};
    actions.reserve(successor.has_value() ? 2U : 1U);
    actions.emplace_back(StopSource{event.token});
    if (!successor.has_value())
    {
        return actions;
    }

    const MusicSourceToken replacementToken = allocateSourceToken();
    if (!replacementToken)
    {
        return {};
    }

    appendStartAction(
        *session,
        *successor,
        replacementToken,
        activityFor(playbackState_),
        actions);
    return actions;
}

std::vector<MusicRuntimeAction> BackgroundMusicRuntime::applyEvent(
    const MusicExecutionAvailable&
)
{
    if (playbackState_ == BackgroundMusicPlaybackState::Stopped)
    {
        return {};
    }

    const PlaybackActivity activity = activityFor(playbackState_);
    std::vector<MusicRuntimeAction> actions{};
    actions.reserve(2U);

    const auto retryWaitingSession = [this, activity, &actions](std::optional<MusicSession>& session)
    {
        if (!session || session->phase() != MusicSessionPhase::WaitingForExecutor
            || !canAllocateSourceTokens(1U))
        {
            return;
        }

        const std::vector<MusicRuntimeAction> recoveryActions = session->retryExecutionAvailable(
            activity,
            [this]() -> std::optional<MusicSourceToken>
            {
                if (!canAllocateSourceTokens(1U))
                {
                    return std::nullopt;
                }

                return allocateSourceToken();
            });
        actions.insert(
            actions.end(),
            recoveryActions.cbegin(),
            recoveryActions.cend());
    };

    retryWaitingSession(playlistSession_);
    retryWaitingSession(itemSession_);
    return actions;
}

bool BackgroundMusicRuntime::canAllocateSourceTokens(std::size_t count) const noexcept
{
    const std::uint64_t maximum = (std::numeric_limits<std::uint64_t>::max)();
    return nextSourceTokenValue_ != 0U
        && static_cast<std::uintmax_t>(count)
            <= static_cast<std::uintmax_t>(maximum - nextSourceTokenValue_);
}

MusicSourceToken BackgroundMusicRuntime::allocateSourceToken() noexcept
{
    const std::uint64_t maximum = (std::numeric_limits<std::uint64_t>::max)();
    if (nextSourceTokenValue_ == 0U || nextSourceTokenValue_ == maximum)
    {
        return {};
    }

    const MusicSourceToken token{nextSourceTokenValue_};
    ++nextSourceTokenValue_;
    return token;
}

std::size_t BackgroundMusicRuntime::startupCount(
    const ActiveMusicScopeSnapshot& snapshot
) noexcept
{
    const bool playlistStarts = snapshot.playlistMusic.enabled
        && !snapshot.playlistMusic.tracks.empty();
    const bool itemStarts = snapshot.itemMusic.has_value()
        && snapshot.itemMusic->enabled
        && !snapshot.itemMusic->tracks.empty();
    return static_cast<std::size_t>(playlistStarts) + static_cast<std::size_t>(itemStarts);
}

PlaybackActivity BackgroundMusicRuntime::activityFor(
    BackgroundMusicPlaybackState state
) noexcept
{
    return state == BackgroundMusicPlaybackState::Paused
        ? PlaybackActivity::Paused
        : PlaybackActivity::Playing;
}

void BackgroundMusicRuntime::startSession(
    MusicSession& session,
    PlaybackActivity activity,
    std::vector<MusicRuntimeAction>& actions
)
{
    if (session.config().tracks.empty())
    {
        return;
    }

    const std::optional<TrackId> selectedTrack = session.selectInitialTrack();
    if (!selectedTrack.has_value())
    {
        return;
    }

    const MusicSourceToken token = allocateSourceToken();
    if (!token)
    {
        return;
    }

    appendStartAction(session, *selectedTrack, token, activity, actions);
}

void BackgroundMusicRuntime::appendStartAction(
    MusicSession& session,
    TrackId trackId,
    MusicSourceToken token,
    PlaybackActivity activity,
    std::vector<MusicRuntimeAction>& actions
)
{
    const MusicTrackSnapshot* const selectedTrack = session.trackById(trackId);
    if (selectedTrack == nullptr)
    {
        return;
    }

    session.beginStart(std::move(trackId), token);
    actions.emplace_back(StartSource{
        session.scope(),
        selectedTrack->id,
        token,
        selectedTrack->sourcePath,
        session.config().queueVolume * selectedTrack->trackVolume,
        activity});
}

void BackgroundMusicRuntime::stopSession(
    const MusicSession& session,
    std::vector<MusicRuntimeAction>& actions
)
{
    if (session.sourceToken().has_value())
    {
        actions.emplace_back(StopSource{*session.sourceToken()});
    }
}

MusicSession* BackgroundMusicRuntime::sessionForToken(MusicSourceToken token) noexcept
{
    if (!token)
    {
        return nullptr;
    }

    if (playlistSession_ && playlistSession_->ownsToken(token))
    {
        return &*playlistSession_;
    }

    if (itemSession_ && itemSession_->ownsToken(token))
    {
        return &*itemSession_;
    }

    return nullptr;
}

}
