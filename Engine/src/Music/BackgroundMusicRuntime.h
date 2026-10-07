#pragma once

#include "MusicSession.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace DesktopSceneEngine::Music
{

class BackgroundMusicRuntime final
{
public:
    explicit BackgroundMusicRuntime(
        ShufflePermutationProvider shufflePermutationProvider = {}
    );

    [[nodiscard]] std::vector<MusicRuntimeAction> apply(const MusicRuntimeEvent& event);

    [[nodiscard]] BackgroundMusicPlaybackState playbackState() const noexcept;

    [[nodiscard]] const std::optional<PlaylistId>& activePlaylistId() const noexcept;

    [[nodiscard]] const std::optional<ItemId>& activeItemId() const noexcept;

    [[nodiscard]] const MusicSession* playlistSession() const noexcept;

    [[nodiscard]] const MusicSession* itemSession() const noexcept;

private:
    [[nodiscard]] std::vector<MusicRuntimeAction> applyEvent(const PlaybackSessionStarted& event);

    [[nodiscard]] std::vector<MusicRuntimeAction> applyEvent(const ActivePlaybackScopeChanged& event);

    [[nodiscard]] std::vector<MusicRuntimeAction> applyEvent(const PlaybackPaused& event);

    [[nodiscard]] std::vector<MusicRuntimeAction> applyEvent(const PlaybackResumed& event);

    [[nodiscard]] std::vector<MusicRuntimeAction> applyEvent(const PlaybackStopped& event);

    [[nodiscard]] std::vector<MusicRuntimeAction> applyEvent(const PlaylistMusicConfigChanged& event);

    [[nodiscard]] std::vector<MusicRuntimeAction> applyEvent(const ItemMusicConfigChanged& event);

    [[nodiscard]] std::vector<MusicRuntimeAction> applyEvent(
        const PlaylistMusicTrackSwitchRequested& event
    );

    [[nodiscard]] std::vector<MusicRuntimeAction> applyEvent(
        const ItemMusicTrackSwitchRequested& event
    );

    [[nodiscard]] std::vector<MusicRuntimeAction> applyEvent(const MusicSourceStarted& event);

    [[nodiscard]] std::vector<MusicRuntimeAction> applyEvent(const MusicSourceEnded& event);

    [[nodiscard]] std::vector<MusicRuntimeAction> applyEvent(const MusicSourceFailed& event);

    [[nodiscard]] std::vector<MusicRuntimeAction> applyEvent(const MusicExecutionAvailable& event);

    [[nodiscard]] bool canAllocateSourceTokens(std::size_t count) const noexcept;

    [[nodiscard]] MusicSourceToken allocateSourceToken() noexcept;

    [[nodiscard]] static std::size_t startupCount(const ActiveMusicScopeSnapshot& snapshot) noexcept;

    [[nodiscard]] static PlaybackActivity activityFor(
        BackgroundMusicPlaybackState state
    ) noexcept;

    void startSession(
        MusicSession& session,
        PlaybackActivity activity,
        std::vector<MusicRuntimeAction>& actions
    );

    static void appendStartAction(
        MusicSession& session,
        TrackId trackId,
        MusicSourceToken token,
        PlaybackActivity activity,
        std::vector<MusicRuntimeAction>& actions
    );

    static void stopSession(
        const MusicSession& session,
        std::vector<MusicRuntimeAction>& actions
    );

    [[nodiscard]] MusicSession* sessionForToken(MusicSourceToken token) noexcept;

    BackgroundMusicPlaybackState playbackState_ = BackgroundMusicPlaybackState::Stopped;
    std::optional<PlaylistId> activePlaylistId_{};
    std::optional<ItemId> activeItemId_{};
    std::optional<MusicSession> playlistSession_{};
    std::optional<MusicSession> itemSession_{};
    ShufflePermutationProvider shufflePermutationProvider_{};
    std::uint64_t nextSourceTokenValue_ = 1U;
};

}
