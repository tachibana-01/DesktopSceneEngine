#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace DesktopSceneEngine::Music
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

enum class MusicScope
{
    PlaylistGlobal,
    Item
};

enum class PlaybackActivity
{
    Playing,
    Paused
};

enum class BackgroundMusicPlaybackState
{
    Stopped,
    Playing,
    Paused
};

enum class MusicQueueMode
{
    CurrentSingleLoop,
    ListLoop,
    Shuffle
};

struct MusicTrackSnapshot
{
    TrackId id{};
    std::filesystem::path sourcePath{};
    float trackVolume = 1.0F;

    friend bool operator==(const MusicTrackSnapshot&, const MusicTrackSnapshot&) = default;
};

struct MusicQueueSnapshot
{
    std::vector<MusicTrackSnapshot> tracks{};
    MusicQueueMode mode = MusicQueueMode::ListLoop;
    float queueVolume = 1.0F;
    bool enabled = true;

    friend bool operator==(const MusicQueueSnapshot&, const MusicQueueSnapshot&) = default;
};

struct ActiveMusicScopeSnapshot
{
    PlaylistId playlistId{};
    std::optional<ItemId> itemId{};
    MusicQueueSnapshot playlistMusic{};
    std::optional<MusicQueueSnapshot> itemMusic{};
    PlaybackActivity activity = PlaybackActivity::Playing;

    friend bool operator==(const ActiveMusicScopeSnapshot&, const ActiveMusicScopeSnapshot&) = default;
};

struct MusicSourceToken
{
    std::uint64_t value = 0U;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return value != 0U;
    }

    friend bool operator==(const MusicSourceToken&, const MusicSourceToken&) = default;
};

enum class MusicSourceFailureClass
{
    UnplayableTrack,
    TransientResourceFailure
};

struct PlaybackSessionStarted
{
    ActiveMusicScopeSnapshot scope{};
};

struct ActivePlaybackScopeChanged
{
    ActiveMusicScopeSnapshot scope{};
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
    MusicQueueSnapshot config{};
};

struct ItemMusicConfigChanged
{
    PlaylistId playlistId{};
    ItemId itemId{};
    MusicQueueSnapshot config{};
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

struct MusicSourceStarted
{
    MusicSourceToken token{};
};

struct MusicSourceEnded
{
    MusicSourceToken token{};
};

struct MusicSourceFailed
{
    MusicSourceToken token{};
    MusicSourceFailureClass failure = MusicSourceFailureClass::UnplayableTrack;
};

struct MusicExecutionAvailable
{
};

using MusicRuntimeEvent = std::variant<
    PlaybackSessionStarted,
    ActivePlaybackScopeChanged,
    PlaybackPaused,
    PlaybackResumed,
    PlaybackStopped,
    PlaylistMusicConfigChanged,
    ItemMusicConfigChanged,
    PlaylistMusicTrackSwitchRequested,
    ItemMusicTrackSwitchRequested,
    MusicSourceStarted,
    MusicSourceEnded,
    MusicSourceFailed,
    MusicExecutionAvailable>;

struct StartSource
{
    MusicScope scope = MusicScope::PlaylistGlobal;
    TrackId trackId{};
    MusicSourceToken token{};
    std::filesystem::path sourcePath{};
    float effectiveGain = 1.0F;
    PlaybackActivity activity = PlaybackActivity::Playing;
};

struct StopSource
{
    MusicSourceToken token{};
};

struct SetSourceGain
{
    MusicSourceToken token{};
    float gain = 1.0F;
};

using MusicRuntimeAction = std::variant<
    StartSource,
    StopSource,
    SetSourceGain>;

[[nodiscard]] bool isValid(const PlaylistId& id) noexcept;
[[nodiscard]] bool isValid(const ItemId& id) noexcept;
[[nodiscard]] bool isValid(const TrackId& id) noexcept;
[[nodiscard]] bool isValid(MusicQueueMode mode) noexcept;
[[nodiscard]] bool isValid(PlaybackActivity activity) noexcept;
[[nodiscard]] bool isValid(const MusicTrackSnapshot& snapshot) noexcept;
[[nodiscard]] bool isValid(const MusicQueueSnapshot& snapshot) noexcept;
[[nodiscard]] bool isValid(const ActiveMusicScopeSnapshot& snapshot) noexcept;

}
