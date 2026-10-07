#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace DesktopSceneEngine::BackgroundMusicControl
{

struct PlaylistId
{
    std::string value{};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return !value.empty();
    }

    friend bool operator==(const PlaylistId&, const PlaylistId&) = default;
};

struct ItemId
{
    std::string value{};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return !value.empty();
    }

    friend bool operator==(const ItemId&, const ItemId&) = default;
};

struct TrackId
{
    std::string value{};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return !value.empty();
    }

    friend bool operator==(const TrackId&, const TrackId&) = default;
};

enum class QueueMode
{
    CurrentSingleLoop,
    ListLoop,
    Shuffle
};

struct TrackSnapshot
{
    TrackId id{};
    std::filesystem::path sourcePath{};
    double trackVolume = 1.0;
};

struct QueueSnapshot
{
    std::vector<TrackSnapshot> tracks{};
    QueueMode mode = QueueMode::ListLoop;
    double queueVolume = 1.0;
    bool enabled = true;
};

struct ActiveScopeSnapshot
{
    PlaylistId playlistId{};
    QueueSnapshot playlistMusic{};
    std::optional<ItemId> itemId{};
    std::optional<QueueSnapshot> itemMusic{};
};

struct PlaybackSessionStarted
{
    ActiveScopeSnapshot scope{};
};

struct ActivePlaybackScopeChanged
{
    ActiveScopeSnapshot scope{};
};

struct PlaybackPaused
{
};

struct PlaybackResumed
{
};

struct PlaybackStopped
{
};

struct PlaylistMusicConfigChanged
{
    PlaylistId playlistId{};
    QueueSnapshot config{};
};

struct ItemMusicConfigChanged
{
    PlaylistId playlistId{};
    ItemId itemId{};
    QueueSnapshot config{};
};

struct PlaylistMusicTrackSwitchRequested
{
    PlaylistId playlistId{};
    TrackId trackId{};
};

struct ItemMusicTrackSwitchRequested
{
    PlaylistId playlistId{};
    ItemId itemId{};
    TrackId trackId{};
};

using Event = std::variant<
    PlaybackSessionStarted,
    ActivePlaybackScopeChanged,
    PlaybackPaused,
    PlaybackResumed,
    PlaybackStopped,
    PlaylistMusicConfigChanged,
    ItemMusicConfigChanged,
    PlaylistMusicTrackSwitchRequested,
    ItemMusicTrackSwitchRequested>;

}
