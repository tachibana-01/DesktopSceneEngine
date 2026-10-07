#pragma once

#include "MusicRuntimeTypes.h"

#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace DesktopSceneEngine::Music
{

enum class MusicSessionPhase
{
    SilentEmptyQueue,
    PendingStart,
    Playing,
    NoPlayableTrack,
    WaitingForExecutor
};

using ShufflePermutationProvider = std::function<std::vector<TrackId>(
    std::span<const TrackId> candidates)>;

using SourceTokenAllocator = std::function<std::optional<MusicSourceToken>()>;

class MusicSession final
{
public:
    MusicSession(
        MusicScope scope,
        MusicQueueSnapshot config,
        ShufflePermutationProvider shufflePermutationProvider = {}
    );

    [[nodiscard]] MusicScope scope() const noexcept;

    [[nodiscard]] const MusicQueueSnapshot& config() const noexcept;

    [[nodiscard]] MusicSessionPhase phase() const noexcept;

    [[nodiscard]] const std::optional<TrackId>& currentTrackId() const noexcept;

    [[nodiscard]] const std::optional<MusicSourceToken>& sourceToken() const noexcept;

    [[nodiscard]] std::optional<TrackId> selectInitialTrack();

    [[nodiscard]] std::optional<TrackId> selectNextTrackAfterEnd();

    [[nodiscard]] std::optional<TrackId> selectNextTrackAfterUnplayableFailure();

    void beginStart(TrackId trackId, MusicSourceToken token);

    [[nodiscard]] bool acknowledgeStarted(MusicSourceToken token) noexcept;

    [[nodiscard]] std::optional<SetSourceGain> acknowledgeStartedWithDeferredGain(
        MusicSourceToken token
    ) noexcept;

    [[nodiscard]] bool ownsToken(MusicSourceToken token) const noexcept;

    [[nodiscard]] std::vector<MusicRuntimeAction> reconcileConfig(
        const MusicQueueSnapshot& newConfig,
        PlaybackActivity activity,
        const SourceTokenAllocator& allocateFreshToken
    );

    [[nodiscard]] std::vector<MusicRuntimeAction> switchToTrack(
        const TrackId& trackId,
        PlaybackActivity activity,
        const SourceTokenAllocator& allocateFreshToken
    );

    [[nodiscard]] std::vector<MusicRuntimeAction> retryExecutionAvailable(
        PlaybackActivity activity,
        const SourceTokenAllocator& allocateFreshToken
    );

    void enterWaitingForExecutor() noexcept;

    [[nodiscard]] const MusicTrackSnapshot* trackById(const TrackId& id) const noexcept;

private:
    [[nodiscard]] std::optional<TrackId> selectFirstUnattemptedTrack() const;

    [[nodiscard]] std::optional<TrackId> selectNextListTrack() const;

    [[nodiscard]] std::optional<TrackId> selectNextShuffleTrack();

    [[nodiscard]] std::optional<TrackId> selectSuccessorAfterRemovedCurrent(
        const MusicQueueSnapshot& oldConfig,
        const TrackId& removedCurrent
    ) const;

    [[nodiscard]] std::optional<TrackId> selectReconciliationCandidate();

    [[nodiscard]] bool hasUnattemptedTrack() const noexcept;

    [[nodiscard]] bool hasSupportedSourcePaths(const MusicQueueSnapshot& newConfig) const;

    void reconcileAttemptedTracks();

    void reconcileShuffleBag(
        const MusicQueueSnapshot& oldConfig,
        const std::optional<TrackId>& retainedCurrentTrackId
    );

    [[nodiscard]] float effectiveGainForCurrentTrack() const noexcept;

    [[nodiscard]] bool completeStartAcknowledgement(MusicSourceToken token) noexcept;

    void clearForSilentQueue() noexcept;

    void clearForNoPlayableTrack() noexcept;

    [[nodiscard]] StartSource startAction(
        const TrackId& trackId,
        MusicSourceToken token,
        PlaybackActivity activity
    ) const;

    void createShuffleBag(const TrackId& previousTrack);

    void removeFromShuffleBag(const TrackId& id);

    [[nodiscard]] bool wasAttempted(const TrackId& id) const noexcept;

    void markAttempted(TrackId id);

    void resetFailureTraversal() noexcept;

    void enterNoPlayableTrack() noexcept;

    MusicScope scope_ = MusicScope::PlaylistGlobal;
    MusicQueueSnapshot config_{};
    ShufflePermutationProvider shufflePermutationProvider_{};
    MusicSessionPhase phase_ = MusicSessionPhase::SilentEmptyQueue;
    std::optional<TrackId> currentTrackId_{};
    std::optional<MusicSourceToken> sourceToken_{};
    std::vector<TrackId> attemptedTrackIds_{};
    std::vector<TrackId> shuffleBag_{};
    std::optional<TrackId> lastSuccessfullyPlayedTrack_{};
    float sourceGain_ = 1.0F;
    std::optional<float> deferredPendingGain_{};
};

}
