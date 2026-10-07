#include "MusicSession.h"

#include <algorithm>
#include <utility>

namespace DesktopSceneEngine::Music
{

namespace
{

[[nodiscard]] std::vector<TrackId> configuredTrackIds(const MusicQueueSnapshot& config)
{
    std::vector<TrackId> ids{};
    ids.reserve(config.tracks.size());
    for (const MusicTrackSnapshot& track : config.tracks)
    {
        ids.push_back(track.id);
    }

    return ids;
}

[[nodiscard]] bool isPermutationOf(
    const std::vector<TrackId>& candidate,
    const std::vector<TrackId>& configured
)
{
    if (candidate.size() != configured.size())
    {
        return false;
    }

    return std::all_of(
        configured.cbegin(),
        configured.cend(),
        [&candidate](const TrackId& id)
        {
            return std::count(candidate.cbegin(), candidate.cend(), id) == 1;
        });
}

}

MusicSession::MusicSession(
    MusicScope scope,
    MusicQueueSnapshot config,
    ShufflePermutationProvider shufflePermutationProvider
)
    : scope_(scope)
    , config_(std::move(config))
    , shufflePermutationProvider_(std::move(shufflePermutationProvider))
{
}

MusicScope MusicSession::scope() const noexcept
{
    return scope_;
}

const MusicQueueSnapshot& MusicSession::config() const noexcept
{
    return config_;
}

MusicSessionPhase MusicSession::phase() const noexcept
{
    return phase_;
}

const std::optional<TrackId>& MusicSession::currentTrackId() const noexcept
{
    return currentTrackId_;
}

const std::optional<MusicSourceToken>& MusicSession::sourceToken() const noexcept
{
    return sourceToken_;
}

std::optional<TrackId> MusicSession::selectInitialTrack()
{
    if (config_.tracks.empty())
    {
        return {};
    }

    resetFailureTraversal();
    const TrackId& initialTrack = config_.tracks.front().id;
    if (config_.mode == MusicQueueMode::Shuffle)
    {
        createShuffleBag(initialTrack);
        removeFromShuffleBag(initialTrack);
    }

    return initialTrack;
}

std::optional<TrackId> MusicSession::selectNextTrackAfterEnd()
{
    if (!currentTrackId_.has_value())
    {
        return {};
    }

    resetFailureTraversal();
    switch (config_.mode)
    {
    case MusicQueueMode::CurrentSingleLoop:
        return currentTrackId_;

    case MusicQueueMode::ListLoop:
        return selectNextListTrack();

    case MusicQueueMode::Shuffle:
        return selectNextShuffleTrack();
    }

    return {};
}

std::optional<TrackId> MusicSession::selectNextTrackAfterUnplayableFailure()
{
    if (!currentTrackId_.has_value())
    {
        return {};
    }

    markAttempted(*currentTrackId_);
    std::optional<TrackId> successor{};
    switch (config_.mode)
    {
    case MusicQueueMode::CurrentSingleLoop:
        successor = selectFirstUnattemptedTrack();
        break;

    case MusicQueueMode::ListLoop:
        successor = selectNextListTrack();
        break;

    case MusicQueueMode::Shuffle:
        successor = selectNextShuffleTrack();
        break;
    }

    if (!successor.has_value())
    {
        enterNoPlayableTrack();
    }

    return successor;
}

void MusicSession::beginStart(TrackId trackId, MusicSourceToken token)
{
    currentTrackId_ = std::move(trackId);
    sourceToken_ = token;
    phase_ = MusicSessionPhase::PendingStart;
    sourceGain_ = effectiveGainForCurrentTrack();
    deferredPendingGain_.reset();
}

bool MusicSession::acknowledgeStarted(MusicSourceToken token) noexcept
{
    return completeStartAcknowledgement(token);
}

std::optional<SetSourceGain> MusicSession::acknowledgeStartedWithDeferredGain(
    MusicSourceToken token
) noexcept
{
    if (!completeStartAcknowledgement(token) || !deferredPendingGain_.has_value())
    {
        return {};
    }

    const float gain = *deferredPendingGain_;
    deferredPendingGain_.reset();
    if (gain == sourceGain_)
    {
        return {};
    }

    sourceGain_ = gain;
    return SetSourceGain{token, gain};
}

bool MusicSession::completeStartAcknowledgement(MusicSourceToken token) noexcept
{
    if (!ownsToken(token) || phase_ != MusicSessionPhase::PendingStart)
    {
        return false;
    }

    phase_ = MusicSessionPhase::Playing;
    lastSuccessfullyPlayedTrack_ = currentTrackId_;
    resetFailureTraversal();
    return true;
}

bool MusicSession::ownsToken(MusicSourceToken token) const noexcept
{
    return static_cast<bool>(token) && sourceToken_ == token;
}

std::vector<MusicRuntimeAction> MusicSession::reconcileConfig(
    const MusicQueueSnapshot& newConfig,
    PlaybackActivity activity,
    const SourceTokenAllocator& allocateFreshToken
)
{
    if (!isValid(newConfig) || !isValid(activity) || !hasSupportedSourcePaths(newConfig))
    {
        return {};
    }

    MusicSession candidate{*this};
    const MusicQueueSnapshot oldConfig = candidate.config_;
    const std::optional<TrackId> oldCurrentTrackId = candidate.currentTrackId_;
    const std::optional<MusicSourceToken> oldToken = candidate.sourceToken_;
    candidate.config_ = newConfig;
    candidate.reconcileAttemptedTracks();

    std::vector<MusicRuntimeAction> actions{};
    if (candidate.config_.tracks.empty())
    {
        if (oldToken.has_value())
        {
            actions.emplace_back(StopSource{*oldToken});
        }

        candidate.clearForSilentQueue();
        *this = std::move(candidate);
        return actions;
    }

    const bool currentTrackRetainedForShuffle = candidate.currentTrackId_.has_value()
        && candidate.trackById(*candidate.currentTrackId_) != nullptr;
    candidate.reconcileShuffleBag(
        oldConfig,
        currentTrackRetainedForShuffle ? candidate.currentTrackId_ : std::nullopt);

    if (candidate.phase_ == MusicSessionPhase::SilentEmptyQueue)
    {
        const std::optional<TrackId> initialTrack = candidate.selectInitialTrack();
        if (!initialTrack.has_value())
        {
            return {};
        }

        if (!allocateFreshToken)
        {
            return {};
        }

        const std::optional<MusicSourceToken> token = allocateFreshToken();
        if (!token.has_value() || !*token)
        {
            return {};
        }

        candidate.beginStart(*initialTrack, *token);
        actions.emplace_back(candidate.startAction(*initialTrack, *token, activity));
        *this = std::move(candidate);
        return actions;
    }

    const bool currentTrackRetained = candidate.currentTrackId_.has_value()
        && candidate.trackById(*candidate.currentTrackId_) != nullptr;
    if (currentTrackRetained)
    {
        if (candidate.phase_ == MusicSessionPhase::NoPlayableTrack
            && candidate.hasUnattemptedTrack())
        {
            const std::optional<TrackId> revivalTrack = candidate.selectReconciliationCandidate();
            if (!revivalTrack.has_value())
            {
                return {};
            }

            if (!allocateFreshToken)
            {
                return {};
            }

            const std::optional<MusicSourceToken> token = allocateFreshToken();
            if (!token.has_value() || !*token)
            {
                return {};
            }

            candidate.beginStart(*revivalTrack, *token);
            actions.emplace_back(candidate.startAction(*revivalTrack, *token, activity));
        }

        if (candidate.phase_ == MusicSessionPhase::Playing && candidate.sourceToken_.has_value())
        {
            const float desiredGain = candidate.effectiveGainForCurrentTrack();
            if (desiredGain != candidate.sourceGain_)
            {
                candidate.sourceGain_ = desiredGain;
                actions.emplace_back(SetSourceGain{*candidate.sourceToken_, desiredGain});
            }
        }
        else if (candidate.phase_ == MusicSessionPhase::PendingStart)
        {
            const float desiredGain = candidate.effectiveGainForCurrentTrack();
            if (desiredGain == candidate.sourceGain_)
            {
                candidate.deferredPendingGain_.reset();
            }
            else
            {
                candidate.deferredPendingGain_ = desiredGain;
            }
        }

        *this = std::move(candidate);
        return actions;
    }

    if (candidate.phase_ == MusicSessionPhase::NoPlayableTrack
        && !candidate.hasUnattemptedTrack())
    {
        candidate.clearForNoPlayableTrack();
        *this = std::move(candidate);
        return {};
    }

    std::optional<TrackId> successor{};
    if (oldCurrentTrackId.has_value()
        && candidate.config_.mode != MusicQueueMode::Shuffle)
    {
        successor = candidate.selectSuccessorAfterRemovedCurrent(oldConfig, *oldCurrentTrackId);
    }
    else
    {
        successor = candidate.selectReconciliationCandidate();
    }

    if (!successor.has_value())
    {
        if (oldToken.has_value())
        {
            actions.emplace_back(StopSource{*oldToken});
        }

        candidate.clearForNoPlayableTrack();
        *this = std::move(candidate);
        return actions;
    }

    if (!allocateFreshToken)
    {
        return {};
    }

    const std::optional<MusicSourceToken> replacementToken = allocateFreshToken();
    if (!replacementToken.has_value() || !*replacementToken)
    {
        return {};
    }

    candidate.beginStart(*successor, *replacementToken);
    if (oldToken.has_value())
    {
        actions.emplace_back(StopSource{*oldToken});
    }
    actions.emplace_back(candidate.startAction(*successor, *replacementToken, activity));
    *this = std::move(candidate);
    return actions;
}

std::vector<MusicRuntimeAction> MusicSession::switchToTrack(
    const TrackId& trackId,
    PlaybackActivity activity,
    const SourceTokenAllocator& allocateFreshToken
)
{
    if (!isValid(trackId) || !isValid(activity) || !currentTrackId_.has_value()
        || trackById(trackId) == nullptr || trackId == *currentTrackId_
        || !allocateFreshToken)
    {
        return {};
    }

    const std::optional<MusicSourceToken> replacementToken = allocateFreshToken();
    if (!replacementToken.has_value() || !*replacementToken)
    {
        return {};
    }

    MusicSession candidate{*this};
    const std::optional<MusicSourceToken> oldToken = candidate.sourceToken_;
    if (candidate.config_.mode == MusicQueueMode::Shuffle)
    {
        candidate.removeFromShuffleBag(trackId);
    }

    candidate.beginStart(trackId, *replacementToken);
    std::vector<MusicRuntimeAction> actions{};
    actions.reserve(oldToken.has_value() ? 2U : 1U);
    if (oldToken.has_value())
    {
        actions.emplace_back(StopSource{*oldToken});
    }
    actions.emplace_back(candidate.startAction(trackId, *replacementToken, activity));
    *this = std::move(candidate);
    return actions;
}

std::vector<MusicRuntimeAction> MusicSession::retryExecutionAvailable(
    PlaybackActivity activity,
    const SourceTokenAllocator& allocateFreshToken
)
{
    if (phase_ != MusicSessionPhase::WaitingForExecutor
        || !currentTrackId_.has_value()
        || !isValid(activity)
        || !allocateFreshToken)
    {
        return {};
    }

    MusicSession candidate{*this};
    if (candidate.trackById(*candidate.currentTrackId_) == nullptr)
    {
        return {};
    }

    const std::optional<MusicSourceToken> token = allocateFreshToken();
    if (!token.has_value() || !*token)
    {
        return {};
    }

    candidate.beginStart(*candidate.currentTrackId_, *token);
    std::vector<MusicRuntimeAction> actions{};
    actions.reserve(1U);
    actions.emplace_back(candidate.startAction(*candidate.currentTrackId_, *token, activity));
    *this = std::move(candidate);
    return actions;
}

void MusicSession::enterWaitingForExecutor() noexcept
{
    sourceToken_.reset();
    phase_ = MusicSessionPhase::WaitingForExecutor;
}

const MusicTrackSnapshot* MusicSession::trackById(const TrackId& id) const noexcept
{
    const auto found = std::find_if(
        config_.tracks.cbegin(),
        config_.tracks.cend(),
        [&id](const MusicTrackSnapshot& track)
        {
            return track.id == id;
        });
    return found == config_.tracks.cend() ? nullptr : &*found;
}

std::optional<TrackId> MusicSession::selectSuccessorAfterRemovedCurrent(
    const MusicQueueSnapshot& oldConfig,
    const TrackId& removedCurrent
) const
{
    const auto current = std::find_if(
        oldConfig.tracks.cbegin(),
        oldConfig.tracks.cend(),
        [&removedCurrent](const MusicTrackSnapshot& track)
        {
            return track.id == removedCurrent;
        });
    if (current == oldConfig.tracks.cend() || oldConfig.tracks.empty())
    {
        return selectFirstUnattemptedTrack();
    }

    const std::size_t currentIndex = static_cast<std::size_t>(
        std::distance(oldConfig.tracks.cbegin(), current));
    for (std::size_t offset = 1U; offset <= oldConfig.tracks.size(); ++offset)
    {
        const TrackId& candidate = oldConfig.tracks[
            (currentIndex + offset) % oldConfig.tracks.size()].id;
        if (candidate != removedCurrent && trackById(candidate) != nullptr && !wasAttempted(candidate))
        {
            return candidate;
        }
    }

    return selectFirstUnattemptedTrack();
}

std::optional<TrackId> MusicSession::selectReconciliationCandidate()
{
    switch (config_.mode)
    {
    case MusicQueueMode::CurrentSingleLoop:
        return selectFirstUnattemptedTrack();

    case MusicQueueMode::ListLoop:
        if (currentTrackId_.has_value() && trackById(*currentTrackId_) != nullptr)
        {
            return selectNextListTrack();
        }

        return selectFirstUnattemptedTrack();

    case MusicQueueMode::Shuffle:
        if (!currentTrackId_.has_value())
        {
            createShuffleBag(TrackId{});
        }

        return selectNextShuffleTrack();
    }

    return {};
}

bool MusicSession::hasUnattemptedTrack() const noexcept
{
    return std::any_of(
        config_.tracks.cbegin(),
        config_.tracks.cend(),
        [this](const MusicTrackSnapshot& track)
        {
            return !wasAttempted(track.id);
        });
}

bool MusicSession::hasSupportedSourcePaths(const MusicQueueSnapshot& newConfig) const
{
    return std::all_of(
        config_.tracks.cbegin(),
        config_.tracks.cend(),
        [&newConfig](const MusicTrackSnapshot& oldTrack)
        {
            const auto newTrack = std::find_if(
                newConfig.tracks.cbegin(),
                newConfig.tracks.cend(),
                [&oldTrack](const MusicTrackSnapshot& candidate)
                {
                    return candidate.id == oldTrack.id;
                });
            return newTrack == newConfig.tracks.cend() || newTrack->sourcePath == oldTrack.sourcePath;
        });
}

void MusicSession::reconcileAttemptedTracks()
{
    attemptedTrackIds_.erase(
        std::remove_if(
            attemptedTrackIds_.begin(),
            attemptedTrackIds_.end(),
            [this](const TrackId& id)
            {
                return trackById(id) == nullptr;
            }),
        attemptedTrackIds_.end());

    if (lastSuccessfullyPlayedTrack_.has_value()
        && trackById(*lastSuccessfullyPlayedTrack_) == nullptr)
    {
        lastSuccessfullyPlayedTrack_.reset();
    }
}

void MusicSession::reconcileShuffleBag(
    const MusicQueueSnapshot& oldConfig,
    const std::optional<TrackId>& retainedCurrentTrackId
)
{
    if (config_.mode != MusicQueueMode::Shuffle)
    {
        shuffleBag_.clear();
        return;
    }

    if (oldConfig.mode != MusicQueueMode::Shuffle)
    {
        const TrackId previousTrack = retainedCurrentTrackId.value_or(TrackId{});
        createShuffleBag(previousTrack);
        if (retainedCurrentTrackId.has_value())
        {
            removeFromShuffleBag(*retainedCurrentTrackId);
        }

        return;
    }

    std::vector<TrackId> reconciledBag{};
    reconciledBag.reserve(shuffleBag_.size() + config_.tracks.size());
    for (const TrackId& id : shuffleBag_)
    {
        if (trackById(id) != nullptr
            && (!retainedCurrentTrackId.has_value() || id != *retainedCurrentTrackId)
            && std::find(reconciledBag.cbegin(), reconciledBag.cend(), id) == reconciledBag.cend())
        {
            reconciledBag.push_back(id);
        }
    }

    std::vector<TrackId> addedTrackIds{};
    for (const MusicTrackSnapshot& newTrack : config_.tracks)
    {
        const auto oldTrack = std::find_if(
            oldConfig.tracks.cbegin(),
            oldConfig.tracks.cend(),
            [&newTrack](const MusicTrackSnapshot& candidate)
            {
                return candidate.id == newTrack.id;
            });
        if (oldTrack == oldConfig.tracks.cend()
            && (!retainedCurrentTrackId.has_value() || newTrack.id != *retainedCurrentTrackId))
        {
            addedTrackIds.push_back(newTrack.id);
        }
    }

    std::vector<TrackId> addedPermutation = addedTrackIds;
    if (!addedTrackIds.empty() && shufflePermutationProvider_)
    {
        addedPermutation = shufflePermutationProvider_(addedTrackIds);
    }
    if (!isPermutationOf(addedPermutation, addedTrackIds))
    {
        addedPermutation = std::move(addedTrackIds);
    }

    for (const TrackId& id : addedPermutation)
    {
        if (std::find(reconciledBag.cbegin(), reconciledBag.cend(), id) == reconciledBag.cend())
        {
            reconciledBag.push_back(id);
        }
    }

    shuffleBag_ = std::move(reconciledBag);
}

float MusicSession::effectiveGainForCurrentTrack() const noexcept
{
    if (!currentTrackId_.has_value())
    {
        return 1.0F;
    }

    const MusicTrackSnapshot* const track = trackById(*currentTrackId_);
    return track == nullptr ? 1.0F : config_.queueVolume * track->trackVolume;
}

void MusicSession::clearForSilentQueue() noexcept
{
    phase_ = MusicSessionPhase::SilentEmptyQueue;
    currentTrackId_.reset();
    sourceToken_.reset();
    attemptedTrackIds_.clear();
    shuffleBag_.clear();
    lastSuccessfullyPlayedTrack_.reset();
    sourceGain_ = 1.0F;
    deferredPendingGain_.reset();
}

void MusicSession::clearForNoPlayableTrack() noexcept
{
    phase_ = MusicSessionPhase::NoPlayableTrack;
    currentTrackId_.reset();
    sourceToken_.reset();
    shuffleBag_.clear();
    sourceGain_ = 1.0F;
    deferredPendingGain_.reset();
}

StartSource MusicSession::startAction(
    const TrackId& trackId,
    MusicSourceToken token,
    PlaybackActivity activity
) const
{
    const MusicTrackSnapshot* const track = trackById(trackId);
    return StartSource{
        scope_,
        trackId,
        token,
        track == nullptr ? std::filesystem::path{} : track->sourcePath,
        track == nullptr ? 1.0F : config_.queueVolume * track->trackVolume,
        activity};
}

std::optional<TrackId> MusicSession::selectFirstUnattemptedTrack() const
{
    const auto found = std::find_if(
        config_.tracks.cbegin(),
        config_.tracks.cend(),
        [this](const MusicTrackSnapshot& track)
        {
            return !wasAttempted(track.id);
        });
    return found == config_.tracks.cend() ? std::optional<TrackId>{} : found->id;
}

std::optional<TrackId> MusicSession::selectNextListTrack() const
{
    const MusicTrackSnapshot* const currentTrack = trackById(*currentTrackId_);
    if (currentTrack == nullptr || config_.tracks.empty())
    {
        return {};
    }

    const std::size_t currentIndex = static_cast<std::size_t>(currentTrack - config_.tracks.data());
    for (std::size_t offset = 1U; offset <= config_.tracks.size(); ++offset)
    {
        const MusicTrackSnapshot& candidate = config_.tracks[
            (currentIndex + offset) % config_.tracks.size()];
        if (!wasAttempted(candidate.id))
        {
            return candidate.id;
        }
    }

    return {};
}

std::optional<TrackId> MusicSession::selectNextShuffleTrack()
{
    while (true)
    {
        const auto candidate = std::find_if(
            shuffleBag_.cbegin(),
            shuffleBag_.cend(),
            [this](const TrackId& id)
            {
                return !wasAttempted(id);
            });
        if (candidate != shuffleBag_.cend())
        {
            TrackId selected = *candidate;
            shuffleBag_.erase(candidate);
            return selected;
        }

        if (attemptedTrackIds_.size() >= config_.tracks.size() || !currentTrackId_.has_value())
        {
            return {};
        }

        const TrackId& previousTrack = phase_ == MusicSessionPhase::Playing
                && lastSuccessfullyPlayedTrack_.has_value()
            ? *lastSuccessfullyPlayedTrack_
            : *currentTrackId_;
        createShuffleBag(previousTrack);
    }
}

void MusicSession::createShuffleBag(const TrackId& previousTrack)
{
    const std::vector<TrackId> configured = configuredTrackIds(config_);
    std::vector<TrackId> permutation = shufflePermutationProvider_
        ? shufflePermutationProvider_(configured)
        : configured;
    if (!isPermutationOf(permutation, configured))
    {
        permutation = configured;
    }

    if (permutation.size() > 1U && permutation.front() == previousTrack)
    {
        const auto different = std::find_if(
            permutation.begin() + 1,
            permutation.end(),
            [&previousTrack](const TrackId& id)
            {
                return id != previousTrack;
            });
        if (different != permutation.end())
        {
            std::iter_swap(permutation.begin(), different);
        }
    }

    shuffleBag_ = std::move(permutation);
}

void MusicSession::removeFromShuffleBag(const TrackId& id)
{
    const auto found = std::find(shuffleBag_.cbegin(), shuffleBag_.cend(), id);
    if (found != shuffleBag_.cend())
    {
        shuffleBag_.erase(found);
    }
}

bool MusicSession::wasAttempted(const TrackId& id) const noexcept
{
    return std::find(attemptedTrackIds_.cbegin(), attemptedTrackIds_.cend(), id)
        != attemptedTrackIds_.cend();
}

void MusicSession::markAttempted(TrackId id)
{
    if (!wasAttempted(id))
    {
        attemptedTrackIds_.push_back(std::move(id));
    }
}

void MusicSession::resetFailureTraversal() noexcept
{
    attemptedTrackIds_.clear();
}

void MusicSession::enterNoPlayableTrack() noexcept
{
    sourceToken_.reset();
    phase_ = MusicSessionPhase::NoPlayableTrack;
}

}
