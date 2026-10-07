#include "BackgroundMusicControlAdapter.h"
#include "WorkspaceModel.h"

#include <QByteArray>

#include <filesystem>
#include <string>

namespace DesktopSceneEngine::Player
{

namespace
{

[[nodiscard]] std::string canonicalUuid(const QUuid& value)
{
    const QByteArray utf8 = value.toString(QUuid::WithoutBraces).toUtf8();
    return {utf8.constData(), static_cast<std::size_t>(utf8.size())};
}

[[nodiscard]] std::filesystem::path utf8Path(const std::string& value)
{
    std::u8string utf8{};
    utf8.reserve(value.size());
    for (const unsigned char byte : value)
    {
        utf8.push_back(static_cast<char8_t>(byte));
    }

    return std::filesystem::path{utf8};
}

}

std::optional<BackgroundMusicControl::PlaylistId>
makeBackgroundMusicPlaylistId(const Domain::PlaylistId& id)
{
    if (!id.isValid())
    {
        return std::nullopt;
    }

    return BackgroundMusicControl::PlaylistId{id.value()};
}

std::optional<BackgroundMusicControl::ItemId>
makeBackgroundMusicItemId(const Domain::ItemId& id)
{
    if (!id.isValid())
    {
        return std::nullopt;
    }

    return BackgroundMusicControl::ItemId{id.value()};
}

std::optional<BackgroundMusicControl::TrackId>
makeBackgroundMusicTrackId(const Domain::TrackId& id)
{
    if (!id.isValid())
    {
        return std::nullopt;
    }

    return BackgroundMusicControl::TrackId{id.value()};
}

std::optional<BackgroundMusicControl::TrackId>
makeBackgroundMusicTrackId(TrackId id)
{
    if (!id.isValid())
    {
        return std::nullopt;
    }

    return BackgroundMusicControl::TrackId{canonicalUuid(id.value)};
}

std::optional<BackgroundMusicControl::QueueMode>
makeBackgroundMusicQueueMode(Domain::MusicPlaybackMode mode) noexcept
{
    switch (mode)
    {
    case Domain::MusicPlaybackMode::CurrentSingleLoop:
        return BackgroundMusicControl::QueueMode::CurrentSingleLoop;
    case Domain::MusicPlaybackMode::ListLoop:
        return BackgroundMusicControl::QueueMode::ListLoop;
    case Domain::MusicPlaybackMode::Shuffle:
        return BackgroundMusicControl::QueueMode::Shuffle;
    }

    return std::nullopt;
}

std::optional<BackgroundMusicControl::QueueSnapshot>
makeBackgroundMusicQueueSnapshot(const Domain::BackgroundMusicQueue& queue)
{
    const auto mode = makeBackgroundMusicQueueMode(queue.playbackMode);
    if (!mode.has_value())
    {
        return std::nullopt;
    }

    BackgroundMusicControl::QueueSnapshot snapshot{};
    snapshot.mode = *mode;
    snapshot.queueVolume = queue.volume;
    snapshot.enabled = queue.enabled;
    snapshot.tracks.reserve(queue.tracks.size());
    for (const Domain::BackgroundMusicTrack& track : queue.tracks)
    {
        const auto trackId = makeBackgroundMusicTrackId(track.id);
        if (!trackId.has_value())
        {
            return std::nullopt;
        }

        snapshot.tracks.push_back(BackgroundMusicControl::TrackSnapshot{
            *trackId,
            utf8Path(track.source),
            track.volume});
    }

    return snapshot;
}

std::optional<BackgroundMusicControl::ActiveScopeSnapshot>
makeActiveBackgroundMusicScopeSnapshot(
    const Domain::AuthoredPlaylist& playlist,
    const Domain::AuthoredItem* activeItem
)
{
    const auto playlistId = makeBackgroundMusicPlaylistId(playlist.id);
    if (!playlistId.has_value())
    {
        return std::nullopt;
    }

    const auto playlistMusic = makeBackgroundMusicQueueSnapshot(playlist.globalMusicQueue);
    if (!playlistMusic.has_value())
    {
        return std::nullopt;
    }

    BackgroundMusicControl::ActiveScopeSnapshot snapshot{};
    snapshot.playlistId = *playlistId;
    snapshot.playlistMusic = *playlistMusic;

    if (activeItem == nullptr)
    {
        return snapshot;
    }

    const auto itemId = makeBackgroundMusicItemId(activeItem->id);
    if (!itemId.has_value())
    {
        return std::nullopt;
    }

    const auto itemMusic = makeBackgroundMusicQueueSnapshot(activeItem->musicQueue);
    if (!itemMusic.has_value())
    {
        return std::nullopt;
    }

    snapshot.itemId = *itemId;
    snapshot.itemMusic = *itemMusic;
    return snapshot;
}

}
