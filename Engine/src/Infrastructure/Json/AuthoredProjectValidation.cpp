#include "AuthoredProjectValidation.h"

#include <cmath>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>

namespace DesktopSceneEngine::Infrastructure::Json
{
namespace
{

[[noreturn]] void failValidation(
    const std::string& path,
    const char* message)
{
    throw std::runtime_error{
        "Invalid AuthoredProject at " + path + ": " + message};
}

bool isContinuationByte(unsigned char value) noexcept
{
    return value >= 0x80U && value <= 0xBFU;
}

bool isValidUtf8(std::string_view value) noexcept
{
    const auto* bytes = reinterpret_cast<const unsigned char*>(value.data());
    std::size_t index = 0U;

    while (index < value.size())
    {
        const unsigned char first = bytes[index];
        if (first <= 0x7FU)
        {
            ++index;
            continue;
        }

        if (first >= 0xC2U && first <= 0xDFU)
        {
            if (index + 1U >= value.size()
                || !isContinuationByte(bytes[index + 1U]))
            {
                return false;
            }

            index += 2U;
            continue;
        }

        if (first >= 0xE0U && first <= 0xEFU)
        {
            if (index + 2U >= value.size()
                || !isContinuationByte(bytes[index + 1U])
                || !isContinuationByte(bytes[index + 2U]))
            {
                return false;
            }

            if ((first == 0xE0U && bytes[index + 1U] < 0xA0U)
                || (first == 0xEDU && bytes[index + 1U] > 0x9FU))
            {
                return false;
            }

            index += 3U;
            continue;
        }

        if (first >= 0xF0U && first <= 0xF4U)
        {
            if (index + 3U >= value.size()
                || !isContinuationByte(bytes[index + 1U])
                || !isContinuationByte(bytes[index + 2U])
                || !isContinuationByte(bytes[index + 3U]))
            {
                return false;
            }

            if ((first == 0xF0U && bytes[index + 1U] < 0x90U)
                || (first == 0xF4U && bytes[index + 1U] > 0x8FU))
            {
                return false;
            }

            index += 4U;
            continue;
        }

        return false;
    }

    return true;
}

void validateUtf8(const std::string& value, const std::string& path)
{
    if (!isValidUtf8(value))
    {
        failValidation(path, "must contain valid UTF-8.");
    }
}

void validateAspectRatio(
    const Domain::AspectRatio& aspectRatio,
    const std::string& path)
{
    if (!aspectRatio.isValid())
    {
        failValidation(path, "numerator and denominator must be positive.");
    }
}

void validatePlaybackSequenceMode(
    Domain::PlaybackSequenceMode mode,
    const std::string& path)
{
    switch (mode)
    {
    case Domain::PlaybackSequenceMode::ListLoop:
    case Domain::PlaybackSequenceMode::Random:
    case Domain::PlaybackSequenceMode::RepeatCurrent:
        return;
    }

    failValidation(path, "contains an unknown playback sequence mode.");
}

void validateMusicPlaybackMode(
    Domain::MusicPlaybackMode mode,
    const std::string& path)
{
    switch (mode)
    {
    case Domain::MusicPlaybackMode::CurrentSingleLoop:
    case Domain::MusicPlaybackMode::ListLoop:
    case Domain::MusicPlaybackMode::Shuffle:
        return;
    }

    failValidation(path, "contains an unknown music playback mode.");
}

void validateMediaType(
    Domain::AuthoredMediaType mediaType,
    const std::string& path)
{
    switch (mediaType)
    {
    case Domain::AuthoredMediaType::Image:
    case Domain::AuthoredMediaType::Video:
        return;
    }

    failValidation(path, "contains an unknown media type.");
}

void validateMediaPresentationMode(
    Domain::MediaPresentationMode mode,
    const std::string& path)
{
    switch (mode)
    {
    case Domain::MediaPresentationMode::Native:
    case Domain::MediaPresentationMode::AutoFill:
    case Domain::MediaPresentationMode::Custom:
        return;
    }

    failValidation(path, "contains an unknown presentation mode.");
}

void validateVolume(double volume, const std::string& path)
{
    if (!std::isfinite(volume) || volume < 0.0 || volume > 1.0)
    {
        failValidation(path, "must be finite and within [0.0, 1.0].");
    }
}

void validateCrop(const Domain::Crop& crop, const std::string& path)
{
    const bool finite = std::isfinite(crop.x)
        && std::isfinite(crop.y)
        && std::isfinite(crop.width)
        && std::isfinite(crop.height);
    const double right = static_cast<double>(crop.x)
        + static_cast<double>(crop.width);
    const double bottom = static_cast<double>(crop.y)
        + static_cast<double>(crop.height);

    if (!finite
        || crop.x < 0.0F
        || crop.y < 0.0F
        || crop.width <= 0.0F
        || crop.height <= 0.0F
        || right > 1.0
        || bottom > 1.0)
    {
        failValidation(
            path,
            "must be a finite, positive normalized region within [0.0, 1.0].");
    }
}

void validateLayout(const Domain::Layout& layout, const std::string& path)
{
    if (!std::isfinite(layout.positionX)
        || !std::isfinite(layout.positionY)
        || !std::isfinite(layout.scaleX)
        || !std::isfinite(layout.scaleY)
        || layout.scaleX <= 0.0F
        || layout.scaleY <= 0.0F)
    {
        failValidation(
            path,
            "positions must be finite and scales must be finite and positive.");
    }
}

void validatePresentation(
    const Domain::AuthoredItemPresentation& presentation,
    const std::string& path)
{
    validateCrop(presentation.crop, path + ".crop");
    validateLayout(presentation.layout, path + ".layout");
    validateMediaPresentationMode(
        presentation.mediaPresentation.mode,
        path + ".mediaPresentation.mode");
    validateAspectRatio(
        presentation.mediaPresentation.customAspect,
        path + ".mediaPresentation.customAspect");
}

void validateTrack(
    const Domain::BackgroundMusicTrack& track,
    const std::string& path,
    std::unordered_set<std::string>& trackIds)
{
    if (!track.id.isValid())
    {
        failValidation(path + ".id", "must be a canonical lowercase UUID.");
    }

    if (!trackIds.insert(track.id.value()).second)
    {
        failValidation(path + ".id", "duplicates another TrackId in the project.");
    }

    validateUtf8(track.source, path + ".source");
    validateUtf8(track.displayAlias, path + ".displayAlias");
    validateVolume(track.volume, path + ".volume");
}

void validateQueue(
    const Domain::BackgroundMusicQueue& queue,
    const std::string& path,
    std::unordered_set<std::string>& trackIds)
{
    validateMusicPlaybackMode(queue.playbackMode, path + ".mode");
    validateVolume(queue.volume, path + ".volume");

    for (std::size_t index = 0U; index < queue.tracks.size(); ++index)
    {
        validateTrack(
            queue.tracks[index],
            path + ".tracks[" + std::to_string(index) + "]",
            trackIds);
    }
}

}

void validateAuthoredProject(const Domain::AuthoredProject& project)
{
    validateAspectRatio(
        project.desktopCanvas.aspectRatio,
        "desktopCanvas.aspectRatio");
    validatePlaybackSequenceMode(
        project.playlistPlaybackMode,
        "playlistPlaybackMode");

    std::unordered_set<std::string> playlistIds{};
    std::unordered_set<std::string> itemIds{};
    std::unordered_set<std::string> trackIds{};

    for (std::size_t playlistIndex = 0U;
         playlistIndex < project.playlists.size();
         ++playlistIndex)
    {
        const Domain::AuthoredPlaylist& playlist =
            project.playlists[playlistIndex];
        const std::string playlistPath =
            "playlists[" + std::to_string(playlistIndex) + "]";

        if (!playlist.id.isValid())
        {
            failValidation(
                playlistPath + ".id",
                "must be a canonical lowercase UUID.");
        }

        if (!playlistIds.insert(playlist.id.value()).second)
        {
            failValidation(
                playlistPath + ".id",
                "duplicates another PlaylistId in the project.");
        }

        validateUtf8(playlist.displayAlias, playlistPath + ".displayAlias");
        validateVolume(playlist.masterVolume, playlistPath + ".masterVolume");
        validatePlaybackSequenceMode(
            playlist.itemPlaybackMode,
            playlistPath + ".itemPlaybackMode");
        validateQueue(
            playlist.globalMusicQueue,
            playlistPath + ".backgroundMusic",
            trackIds);

        for (std::size_t itemIndex = 0U;
             itemIndex < playlist.items.size();
             ++itemIndex)
        {
            const Domain::AuthoredItem& item = playlist.items[itemIndex];
            const std::string itemPath = playlistPath
                + ".items[" + std::to_string(itemIndex) + "]";

            if (!item.id.isValid())
            {
                failValidation(
                    itemPath + ".id",
                    "must be a canonical lowercase UUID.");
            }

            if (!itemIds.insert(item.id.value()).second)
            {
                failValidation(
                    itemPath + ".id",
                    "duplicates another ItemId in the project.");
            }

            validateUtf8(item.displayAlias, itemPath + ".displayAlias");
            validateMediaType(item.mediaType, itemPath + ".mediaType");
            validateUtf8(item.source, itemPath + ".source");

            if (!std::isfinite(item.imageDurationSeconds)
                || item.imageDurationSeconds <= 0.0)
            {
                failValidation(
                    itemPath + ".imageDurationSeconds",
                    "must be finite and positive.");
            }

            if (!std::isfinite(item.videoPlaybackRate)
                || item.videoPlaybackRate < 0.5
                || item.videoPlaybackRate > 2.0)
            {
                failValidation(
                    itemPath + ".videoPlaybackRate",
                    "must be finite and within [0.5, 2.0].");
            }

            validatePresentation(item.presentation, itemPath + ".presentation");
            validateVolume(item.itemMediaVolume, itemPath + ".itemMediaVolume");
            validateQueue(
                item.musicQueue,
                itemPath + ".backgroundMusic",
                trackIds);
        }
    }
}

}
