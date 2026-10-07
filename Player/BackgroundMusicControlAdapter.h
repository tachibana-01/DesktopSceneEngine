#pragma once

#include <DesktopSceneEngine/BackgroundMusicControl.h>
#include <DesktopSceneEngine/Domain/AuthoredProject.h>

#include <optional>

namespace DesktopSceneEngine::Player
{

struct TrackId;

[[nodiscard]] std::optional<BackgroundMusicControl::PlaylistId>
makeBackgroundMusicPlaylistId(const Domain::PlaylistId& id);

[[nodiscard]] std::optional<BackgroundMusicControl::ItemId>
makeBackgroundMusicItemId(const Domain::ItemId& id);

[[nodiscard]] std::optional<BackgroundMusicControl::TrackId>
makeBackgroundMusicTrackId(const Domain::TrackId& id);

// Temporary P4 UI bridge while track QVariant roles still carry Player TrackId.
[[nodiscard]] std::optional<BackgroundMusicControl::TrackId>
makeBackgroundMusicTrackId(TrackId id);

[[nodiscard]] std::optional<BackgroundMusicControl::QueueMode>
makeBackgroundMusicQueueMode(Domain::MusicPlaybackMode mode) noexcept;

[[nodiscard]] std::optional<BackgroundMusicControl::QueueSnapshot>
makeBackgroundMusicQueueSnapshot(const Domain::BackgroundMusicQueue& queue);

[[nodiscard]] std::optional<BackgroundMusicControl::ActiveScopeSnapshot>
makeActiveBackgroundMusicScopeSnapshot(
    const Domain::AuthoredPlaylist& playlist,
    const Domain::AuthoredItem* activeItem
);

}
