#include "BackgroundMusicControlAdapter.h"

#include <filesystem>
#include <iostream>
#include <string>

namespace
{

using DesktopSceneEngine::BackgroundMusicControl::QueueMode;
using DesktopSceneEngine::Domain::AuthoredItem;
using DesktopSceneEngine::Domain::AuthoredPlaylist;
using DesktopSceneEngine::Domain::BackgroundMusicQueue;
using DesktopSceneEngine::Domain::BackgroundMusicTrack;
using DesktopSceneEngine::Domain::ItemId;
using DesktopSceneEngine::Domain::MusicPlaybackMode;
using DesktopSceneEngine::Domain::PlaylistId;
using DesktopSceneEngine::Domain::TrackId;

bool require(bool condition, const char* message)
{
    if (condition)
    {
        return true;
    }

    std::cerr << "Test failed: " << message << std::endl;
    return false;
}

std::filesystem::path utf8Path(const std::string& value)
{
    std::u8string utf8{};
    utf8.reserve(value.size());
    for (const unsigned char byte : value)
    {
        utf8.push_back(static_cast<char8_t>(byte));
    }

    return std::filesystem::path{utf8};
}

bool verifyIdentifiersAndQueueSnapshot()
{
    const PlaylistId playlistId{"20000000-0000-0000-0000-000000000001"};
    const TrackId firstTrackId{"20000000-0000-0000-0000-000000000002"};
    const TrackId secondTrackId{"20000000-0000-0000-0000-000000000003"};
    const std::string unicodePath{
        "C:/\xE5\xAA\x92\xE4\xBD\x93/\xE8\x83\x8C\xE6\x99\xAF.mp3"};

    BackgroundMusicQueue queue{};
    queue.enabled = false;
    queue.playbackMode = MusicPlaybackMode::Shuffle;
    queue.volume = 0.35;
    queue.tracks.push_back(BackgroundMusicTrack{
        firstTrackId,
        unicodePath,
        "First alias",
        0.4});
    queue.tracks.push_back(BackgroundMusicTrack{
        secondTrackId,
        unicodePath,
        "Second alias",
        0.8});

    const auto playlistIdentifier = DesktopSceneEngine::Player::makeBackgroundMusicPlaylistId(
        playlistId);
    const auto firstTrackIdentifier = DesktopSceneEngine::Player::makeBackgroundMusicTrackId(
        firstTrackId);
    if (!require(playlistIdentifier.has_value()
            && playlistIdentifier->value == playlistId.value()
            && firstTrackIdentifier.has_value()
            && firstTrackIdentifier->value == firstTrackId.value(),
            "Authored Playlist and Track IDs must transfer directly without Qt conversion"))
    {
        return false;
    }

    const auto snapshot = DesktopSceneEngine::Player::makeBackgroundMusicQueueSnapshot(queue);
    if (!require(snapshot.has_value()
            && !snapshot->enabled
            && snapshot->mode == QueueMode::Shuffle
            && snapshot->queueVolume == 0.35
            && snapshot->tracks.size() == 2U
            && snapshot->tracks[0].id.value == firstTrackId.value()
            && snapshot->tracks[1].id.value == secondTrackId.value()
            && snapshot->tracks[0].id != snapshot->tracks[1].id
            && snapshot->tracks[0].sourcePath == utf8Path(unicodePath)
            && snapshot->tracks[1].sourcePath == utf8Path(unicodePath)
            && snapshot->tracks[0].trackVolume == 0.4
            && snapshot->tracks[1].trackVolume == 0.8,
            "Authored queue snapshots must preserve Enabled, order, Unicode paths, duplicate-path TrackIds, and volumes"))
    {
        return false;
    }

    return require(
        DesktopSceneEngine::Player::makeBackgroundMusicQueueMode(
            MusicPlaybackMode::CurrentSingleLoop) == QueueMode::CurrentSingleLoop
            && DesktopSceneEngine::Player::makeBackgroundMusicQueueMode(
                MusicPlaybackMode::ListLoop) == QueueMode::ListLoop
            && DesktopSceneEngine::Player::makeBackgroundMusicQueueMode(
                MusicPlaybackMode::Shuffle) == QueueMode::Shuffle
            && !DesktopSceneEngine::Player::makeBackgroundMusicQueueMode(
                static_cast<MusicPlaybackMode>(999)),
        "Authored queue mode conversion must be exhaustive without a silent fallback");
}

bool verifyActiveScopeUsesAuthoredValues()
{
    AuthoredPlaylist playlist{};
    playlist.id = PlaylistId{"30000000-0000-0000-0000-000000000001"};
    playlist.globalMusicQueue.enabled = false;
    playlist.globalMusicQueue.volume = 0.45;

    AuthoredItem activeItem{};
    activeItem.id = ItemId{"30000000-0000-0000-0000-000000000002"};
    activeItem.musicQueue.enabled = true;
    activeItem.musicQueue.volume = 0.6;
    activeItem.musicQueue.tracks.push_back(BackgroundMusicTrack{
        TrackId{"30000000-0000-0000-0000-000000000003"},
        "C:/Music/active.mp3",
        "Active track",
        0.7});

    const auto scope = DesktopSceneEngine::Player::makeActiveBackgroundMusicScopeSnapshot(
        playlist,
        &activeItem);
    if (!require(scope.has_value()
            && scope->playlistId.value == playlist.id.value()
            && !scope->playlistMusic.enabled
            && scope->playlistMusic.queueVolume == 0.45
            && scope->itemId.has_value()
            && scope->itemId->value == activeItem.id.value()
            && scope->itemMusic.has_value()
            && scope->itemMusic->enabled
            && scope->itemMusic->tracks.size() == 1U
            && scope->itemMusic->queueVolume == 0.6,
            "Active scope must map canonical authored Playlist and active Item values directly"))
    {
        return false;
    }

    const auto playlistOnlyScope =
        DesktopSceneEngine::Player::makeActiveBackgroundMusicScopeSnapshot(playlist, nullptr);
    return require(playlistOnlyScope.has_value()
            && !playlistOnlyScope->itemId.has_value()
            && !playlistOnlyScope->itemMusic.has_value(),
        "A missing active Item must preserve the Playlist-only scope behavior");
}

}

int main()
{
    return verifyIdentifiersAndQueueSnapshot() && verifyActiveScopeUsesAuthoredValues()
        ? 0
        : 1;
}
