#include "ActivePlaybackProjection.h"

#include <DesktopSceneEngine/Domain/EntityId.h>
#include <DesktopSceneEngine/Domain/Library.h>
#include <DesktopSceneEngine/Domain/Media.h>
#include <DesktopSceneEngine/Domain/Playback.h>
#include <DesktopSceneEngine/Domain/Scene.h>

#include <QString>
#include <QUuid>

#include <cmath>
#include <limits>
#include <string>

namespace DesktopSceneEngine::Player
{

namespace
{

PlaylistId makePlayerPlaylistId(const Domain::PlaylistId& id)
{
    return PlaylistId{QUuid::fromString(QString::fromLatin1(id.value()))};
}

ItemId makePlayerItemId(const Domain::ItemId& id)
{
    return ItemId{QUuid::fromString(QString::fromLatin1(id.value()))};
}

bool isWhitespaceOnlySource(const std::string& source)
{
    return QString::fromUtf8(source.data(), static_cast<qsizetype>(source.size()))
        .trimmed()
        .isEmpty();
}

bool isFiniteFloat(double value) noexcept
{
    return std::isfinite(value)
        && value >= -static_cast<double>((std::numeric_limits<float>::max)())
        && value <= static_cast<double>((std::numeric_limits<float>::max)());
}

}

std::optional<std::size_t> ActivePlaybackProjection::sceneIndexForItem(
    ItemId itemId
) const noexcept
{
    for (std::size_t index = 0; index < static_cast<std::size_t>(sceneToItem.size()); ++index)
    {
        if (sceneToItem[static_cast<qsizetype>(index)] == itemId)
        {
            return index;
        }
    }

    return std::nullopt;
}

std::optional<ItemId> ActivePlaybackProjection::itemForSceneIndex(
    std::size_t sceneIndex
) const noexcept
{
    if (sceneIndex >= static_cast<std::size_t>(sceneToItem.size()))
    {
        return std::nullopt;
    }

    return sceneToItem[static_cast<qsizetype>(sceneIndex)];
}

std::optional<ActivePlaybackProjection> makeActivePlaybackProjection(
    const Domain::AuthoredPlaylist& playlist
)
{
    if (!playlist.id.isValid() || playlist.items.empty())
    {
        return std::nullopt;
    }

    ActivePlaybackProjection projection{};
    projection.sourcePlaylistId = makePlayerPlaylistId(playlist.id);
    projection.project.library.media.reserve(playlist.items.size());
    projection.project.scenes.reserve(playlist.items.size());
    projection.sceneToItem.reserve(static_cast<qsizetype>(playlist.items.size()));

    for (const Domain::AuthoredItem& item : playlist.items)
    {
        if (!item.id.isValid() || isWhitespaceOnlySource(item.source))
        {
            return std::nullopt;
        }

        float duration = 0.0F;
        float speed = 1.0F;
        if (item.mediaType == Domain::AuthoredMediaType::Image)
        {
            if (!isFiniteFloat(item.imageDurationSeconds)
                || item.imageDurationSeconds <= 0.0)
            {
                return std::nullopt;
            }

            duration = static_cast<float>(item.imageDurationSeconds);
        }
        else if (item.mediaType == Domain::AuthoredMediaType::Video)
        {
            if (!isFiniteFloat(item.videoPlaybackRate)
                || item.videoPlaybackRate < 0.5 || item.videoPlaybackRate > 2.0)
            {
                return std::nullopt;
            }

            speed = static_cast<float>(item.videoPlaybackRate);
        }
        else
        {
            return std::nullopt;
        }

        const std::string& suffix = item.id.value();
        const Domain::EntityId mediaId{std::string{"media-"} + suffix};
        const Domain::EntityId sceneId{std::string{"scene-"} + suffix};
        projection.project.library.media.push_back(Domain::Media{
            mediaId,
            item.displayAlias,
            item.source});
        projection.project.scenes.push_back(Domain::Scene{
            sceneId,
            mediaId,
            item.presentation.crop,
            item.presentation.layout,
            Domain::Playback{true, speed, 1.0F},
            duration,
            item.presentation.mediaPresentation});
        projection.sceneToItem.append(makePlayerItemId(item.id));
    }

    return projection;
}

std::optional<PreparedActivePlaybackProjection>
prepareActivePlaybackProjection(
    const Domain::AuthoredPlaylist& playlist,
    const Domain::ItemId& targetItemId
)
{
    if (!targetItemId.isValid())
    {
        return std::nullopt;
    }

    auto projection = makeActivePlaybackProjection(playlist);
    if (!projection.has_value())
    {
        return std::nullopt;
    }

    const auto targetSceneIndex = projection->sceneIndexForItem(
        makePlayerItemId(targetItemId));
    if (!targetSceneIndex.has_value())
    {
        return std::nullopt;
    }

    return PreparedActivePlaybackProjection{
        std::move(*projection),
        *targetSceneIndex};
}

std::optional<std::size_t> successorIndexAfterRemovingActiveItem(
    std::size_t previousActiveIndex,
    std::size_t remainingItemCount
) noexcept
{
    if (remainingItemCount == 0U)
    {
        return std::nullopt;
    }

    return previousActiveIndex < remainingItemCount
        ? std::optional<std::size_t>{previousActiveIndex}
        : std::optional<std::size_t>{0U};
}

}
