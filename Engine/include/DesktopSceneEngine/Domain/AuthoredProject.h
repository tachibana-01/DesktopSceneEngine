#pragma once

#include <DesktopSceneEngine/Domain/AspectRatio.h>
#include <DesktopSceneEngine/Domain/Crop.h>
#include <DesktopSceneEngine/Domain/DisplayOwner.h>
#include <DesktopSceneEngine/Domain/Layout.h>
#include <DesktopSceneEngine/Domain/MediaPresentation.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace DesktopSceneEngine::Domain
{

namespace Detail
{

[[nodiscard]] constexpr bool isLowercaseHexadecimal(char value) noexcept
{
    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
}

[[nodiscard]] inline bool isCanonicalAuthoredUuid(std::string_view value) noexcept
{
    constexpr std::size_t CanonicalUuidLength = 36U;

    if (value.size() != CanonicalUuidLength)
    {
        return false;
    }

    for (std::size_t index = 0U; index < CanonicalUuidLength; ++index)
    {
        const bool isHyphenPosition = index == 8U || index == 13U || index == 18U
            || index == 23U;
        if (isHyphenPosition)
        {
            if (value[index] != '-')
            {
                return false;
            }

            continue;
        }

        if (!isLowercaseHexadecimal(value[index]))
        {
            return false;
        }
    }

    return true;
}

class AuthoredIdValue
{
public:

    AuthoredIdValue() = default;

    explicit AuthoredIdValue(std::string value)
        : value_(std::move(value))
    {
    }

    [[nodiscard]] const std::string& value() const noexcept
    {
        return value_;
    }

    [[nodiscard]] bool isValid() const noexcept
    {
        return isCanonicalAuthoredUuid(value_);
    }

    bool operator==(const AuthoredIdValue&) const = default;

private:

    std::string value_{};
};

}

class PlaylistId
{
public:

    PlaylistId() = default;

    explicit PlaylistId(std::string value)
        : value_(std::move(value))
    {
    }

    [[nodiscard]] const std::string& value() const noexcept
    {
        return value_.value();
    }

    [[nodiscard]] bool isValid() const noexcept
    {
        return value_.isValid();
    }

    bool operator==(const PlaylistId&) const = default;

private:

    Detail::AuthoredIdValue value_{};
};

class ItemId
{
public:

    ItemId() = default;

    explicit ItemId(std::string value)
        : value_(std::move(value))
    {
    }

    [[nodiscard]] const std::string& value() const noexcept
    {
        return value_.value();
    }

    [[nodiscard]] bool isValid() const noexcept
    {
        return value_.isValid();
    }

    bool operator==(const ItemId&) const = default;

private:

    Detail::AuthoredIdValue value_{};
};

class TrackId
{
public:

    TrackId() = default;

    explicit TrackId(std::string value)
        : value_(std::move(value))
    {
    }

    [[nodiscard]] const std::string& value() const noexcept
    {
        return value_.value();
    }

    [[nodiscard]] bool isValid() const noexcept
    {
        return value_.isValid();
    }

    bool operator==(const TrackId&) const = default;

private:

    Detail::AuthoredIdValue value_{};
};

enum class AuthoredMediaType
{
    Image,
    Video
};

enum class PlaybackSequenceMode
{
    ListLoop,
    Random,
    RepeatCurrent
};

enum class MusicPlaybackMode
{
    CurrentSingleLoop,
    ListLoop,
    Shuffle
};

// Logical Canvas data is authored. Physical display selection remains session state.
struct DesktopCanvas
{
    AspectRatio aspectRatio{};
};

struct AuthoredItemPresentation
{
    Crop crop{};
    Layout layout{};
    MediaPresentation mediaPresentation{};
};

// Authored aliases and source references are UTF-8 strings.
struct BackgroundMusicTrack
{
    TrackId id{};
    std::string source{};
    std::string displayAlias{};
    double volume = 1.0;
};

struct BackgroundMusicQueue
{
    // Authored configuration only. Runtime consumption is deferred to P5.
    bool enabled = true;
    std::vector<BackgroundMusicTrack> tracks{};
    MusicPlaybackMode playbackMode = MusicPlaybackMode::ListLoop;
    double volume = 1.0;
};

struct AuthoredItem
{
    ItemId id{};
    std::string displayAlias{};
    AuthoredMediaType mediaType = AuthoredMediaType::Image;
    std::string source{};
    double imageDurationSeconds = 10.0;
    double videoPlaybackRate = 1.0;
    AuthoredItemPresentation presentation{};
    double itemMediaVolume = 1.0;
    BackgroundMusicQueue musicQueue{};
    bool enabled = true;
};

struct AuthoredPlaylist
{
    PlaylistId id{};
    std::string displayAlias{};
    std::vector<AuthoredItem> items{};
    double masterVolume = 1.0;
    BackgroundMusicQueue globalMusicQueue{};
    PlaybackSequenceMode itemPlaybackMode = PlaybackSequenceMode::ListLoop;
    bool enabled = true;
};

struct AuthoredProject
{
    explicit AuthoredProject(DisplayOwner owner)
        : displayOwner(std::move(owner))
    {
    }

    DisplayOwner displayOwner;
    DesktopCanvas desktopCanvas{};
    PlaybackSequenceMode playlistPlaybackMode = PlaybackSequenceMode::RepeatCurrent;
    std::vector<AuthoredPlaylist> playlists{};
};

}
