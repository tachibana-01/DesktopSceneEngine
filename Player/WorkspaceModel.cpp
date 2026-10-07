#include "WorkspaceModel.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

#include <QByteArray>
#include <QFileInfo>

namespace DesktopSceneEngine::Player
{

namespace
{

constexpr double minimumPlaybackRate = 0.5;
constexpr double maximumPlaybackRate = 2.0;
constexpr double playbackRateStep = 0.1;

[[nodiscard]] bool hasMeaningfulAlias(const QString& alias)
{
    return !alias.trimmed().isEmpty();
}

[[nodiscard]] bool isSupportedPlaybackRate(double playbackRate)
{
    if (!std::isfinite(playbackRate) || playbackRate < minimumPlaybackRate
        || playbackRate > maximumPlaybackRate)
    {
        return false;
    }

    const double stepCount = (playbackRate - minimumPlaybackRate) / playbackRateStep;
    return std::abs(stepCount - std::round(stepCount)) < 0.000001;
}

[[nodiscard]] bool isValidItemPresentation(
    const WorkspaceItemPresentation& presentation)
{
    const Domain::Crop& crop = presentation.crop;
    const Domain::Layout& layout = presentation.layout;
    const bool validCrop = std::isfinite(crop.x) && std::isfinite(crop.y)
        && std::isfinite(crop.width) && std::isfinite(crop.height)
        && crop.x >= 0.0F && crop.y >= 0.0F
        && crop.width > 0.0F && crop.height > 0.0F
        && crop.x + crop.width <= 1.0F && crop.y + crop.height <= 1.0F;
    const bool validLayout = std::isfinite(layout.positionX)
        && std::isfinite(layout.positionY)
        && std::isfinite(layout.scaleX) && std::isfinite(layout.scaleY)
        && layout.scaleX > 0.0F && layout.scaleY > 0.0F;
    const bool validMode = presentation.mediaPresentation.mode
            == Domain::MediaPresentationMode::Native
        || presentation.mediaPresentation.mode == Domain::MediaPresentationMode::AutoFill
        || presentation.mediaPresentation.mode == Domain::MediaPresentationMode::Custom;
    return validCrop && validLayout && validMode
        && presentation.mediaPresentation.customAspect.isValid();
}

[[nodiscard]] bool sameItemPresentation(
    const WorkspaceItemPresentation& left,
    const WorkspaceItemPresentation& right) noexcept
{
    return left.crop.x == right.crop.x
        && left.crop.y == right.crop.y
        && left.crop.width == right.crop.width
        && left.crop.height == right.crop.height
        && left.layout.positionX == right.layout.positionX
        && left.layout.positionY == right.layout.positionY
        && left.layout.scaleX == right.layout.scaleX
        && left.layout.scaleY == right.layout.scaleY
        && left.mediaPresentation.mode == right.mediaPresentation.mode
        && left.mediaPresentation.customAspect
            == right.mediaPresentation.customAspect;
}

[[nodiscard]] QString aliasFromSource(const QString& source)
{
    const QFileInfo fileInfo{source};
    const QString stem = fileInfo.completeBaseName();
    return stem.isEmpty() ? fileInfo.fileName() : stem;
}

[[nodiscard]] bool isValidMusicPlaybackMode(MusicPlaybackMode playbackMode)
{
    return playbackMode == MusicPlaybackMode::CurrentSingleLoop
        || playbackMode == MusicPlaybackMode::ListLoop
        || playbackMode == MusicPlaybackMode::Shuffle;
}

[[nodiscard]] std::string toUtf8(const QString& value)
{
    const QByteArray bytes = value.toUtf8();
    return std::string{bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

[[nodiscard]] QString fromUtf8(const std::string& value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

[[nodiscard]] std::string canonicalUuidText(const QUuid& value)
{
    return toUtf8(value.toString(QUuid::WithoutBraces).toLower());
}

[[nodiscard]] Domain::PlaylistId toAuthoredId(PlaylistId id)
{
    return id.isValid()
        ? Domain::PlaylistId{canonicalUuidText(id.value)}
        : Domain::PlaylistId{};
}

[[nodiscard]] Domain::ItemId toAuthoredId(ItemId id)
{
    return id.isValid()
        ? Domain::ItemId{canonicalUuidText(id.value)}
        : Domain::ItemId{};
}

[[nodiscard]] Domain::TrackId toAuthoredId(TrackId id)
{
    return id.isValid()
        ? Domain::TrackId{canonicalUuidText(id.value)}
        : Domain::TrackId{};
}

[[nodiscard]] PlaylistId toPlayerId(const Domain::PlaylistId& id)
{
    return id.isValid()
        ? PlaylistId{QUuid{QString::fromLatin1(id.value())}}
        : PlaylistId{};
}

[[nodiscard]] ItemId toPlayerId(const Domain::ItemId& id)
{
    return id.isValid()
        ? ItemId{QUuid{QString::fromLatin1(id.value())}}
        : ItemId{};
}

[[nodiscard]] TrackId toPlayerId(const Domain::TrackId& id)
{
    return id.isValid()
        ? TrackId{QUuid{QString::fromLatin1(id.value())}}
        : TrackId{};
}

[[nodiscard]] Domain::AuthoredMediaType toAuthoredMediaType(WorkspaceMediaType type)
{
    return type == WorkspaceMediaType::Video
        ? Domain::AuthoredMediaType::Video
        : Domain::AuthoredMediaType::Image;
}

[[nodiscard]] WorkspaceMediaType toWorkspaceMediaType(Domain::AuthoredMediaType type)
{
    return type == Domain::AuthoredMediaType::Video
        ? WorkspaceMediaType::Video
        : WorkspaceMediaType::Image;
}

[[nodiscard]] Domain::PlaybackSequenceMode toAuthoredPlaybackMode(
    PlaybackSequenceMode mode)
{
    switch (mode)
    {
    case PlaybackSequenceMode::Random:
        return Domain::PlaybackSequenceMode::Random;
    case PlaybackSequenceMode::RepeatCurrent:
        return Domain::PlaybackSequenceMode::RepeatCurrent;
    case PlaybackSequenceMode::ListLoop:
    default:
        return Domain::PlaybackSequenceMode::ListLoop;
    }
}

[[nodiscard]] PlaybackSequenceMode toWorkspacePlaybackMode(
    Domain::PlaybackSequenceMode mode)
{
    switch (mode)
    {
    case Domain::PlaybackSequenceMode::Random:
        return PlaybackSequenceMode::Random;
    case Domain::PlaybackSequenceMode::RepeatCurrent:
        return PlaybackSequenceMode::RepeatCurrent;
    case Domain::PlaybackSequenceMode::ListLoop:
    default:
        return PlaybackSequenceMode::ListLoop;
    }
}

[[nodiscard]] Domain::MusicPlaybackMode toAuthoredMusicPlaybackMode(
    MusicPlaybackMode mode)
{
    switch (mode)
    {
    case MusicPlaybackMode::CurrentSingleLoop:
        return Domain::MusicPlaybackMode::CurrentSingleLoop;
    case MusicPlaybackMode::Shuffle:
        return Domain::MusicPlaybackMode::Shuffle;
    case MusicPlaybackMode::ListLoop:
    default:
        return Domain::MusicPlaybackMode::ListLoop;
    }
}

[[nodiscard]] MusicPlaybackMode toWorkspaceMusicPlaybackMode(
    Domain::MusicPlaybackMode mode)
{
    switch (mode)
    {
    case Domain::MusicPlaybackMode::CurrentSingleLoop:
        return MusicPlaybackMode::CurrentSingleLoop;
    case Domain::MusicPlaybackMode::Shuffle:
        return MusicPlaybackMode::Shuffle;
    case Domain::MusicPlaybackMode::ListLoop:
    default:
        return MusicPlaybackMode::ListLoop;
    }
}

[[nodiscard]] MusicQueueConfig makeCompatibilityQueue(
    const Domain::BackgroundMusicQueue& authoredQueue)
{
    MusicQueueConfig queue{};
    queue.enabled = authoredQueue.enabled;
    queue.playbackMode = toWorkspaceMusicPlaybackMode(authoredQueue.playbackMode);
    queue.volume = authoredQueue.volume;
    queue.tracks.reserve(static_cast<qsizetype>(authoredQueue.tracks.size()));
    for (const Domain::BackgroundMusicTrack& authoredTrack : authoredQueue.tracks)
    {
        queue.tracks.append(MusicTrack{
            toPlayerId(authoredTrack.id),
            fromUtf8(authoredTrack.source),
            fromUtf8(authoredTrack.displayAlias),
            authoredTrack.volume});
    }

    return queue;
}

[[nodiscard]] std::optional<TrackId> appendMusicTrack(
    Domain::BackgroundMusicQueue& queue,
    const QString& sourcePath,
    const QString& displayAlias)
{
    if (!isSupportedBackgroundMusicSource(sourcePath) || sourcePath.trimmed().isEmpty())
    {
        return std::nullopt;
    }

    const QString resolvedAlias = displayAlias.isEmpty()
        ? aliasFromSource(sourcePath)
        : displayAlias;
    if (!hasMeaningfulAlias(resolvedAlias))
    {
        return std::nullopt;
    }

    const TrackId id{QUuid::createUuid()};
    queue.tracks.push_back(Domain::BackgroundMusicTrack{
        toAuthoredId(id), toUtf8(sourcePath), toUtf8(resolvedAlias), 1.0});
    return id;
}

enum class MutationResult
{
    Failed,
    Unchanged,
    Changed
};

[[nodiscard]] MutationResult setMusicTrackVolume(
    Domain::BackgroundMusicQueue& queue,
    TrackId trackId,
    double volume)
{
    if (!std::isfinite(volume))
    {
        return MutationResult::Failed;
    }

    const Domain::TrackId authoredTrackId = toAuthoredId(trackId);
    const auto iterator = std::find_if(
        queue.tracks.begin(),
        queue.tracks.end(),
        [&authoredTrackId](const Domain::BackgroundMusicTrack& candidate)
        {
            return candidate.id == authoredTrackId;
        });
    if (iterator == queue.tracks.end())
    {
        return MutationResult::Failed;
    }

    const double normalizedVolume = std::clamp(volume, 0.0, 1.0);
    if (iterator->volume == normalizedVolume)
    {
        return MutationResult::Unchanged;
    }

    iterator->volume = normalizedVolume;
    return MutationResult::Changed;
}

[[nodiscard]] bool removeMusicTrack(
    Domain::BackgroundMusicQueue& queue,
    TrackId trackId)
{
    const Domain::TrackId authoredTrackId = toAuthoredId(trackId);
    const auto iterator = std::find_if(
        queue.tracks.cbegin(),
        queue.tracks.cend(),
        [&authoredTrackId](const Domain::BackgroundMusicTrack& candidate)
        {
            return candidate.id == authoredTrackId;
        });
    if (iterator == queue.tracks.cend())
    {
        return false;
    }

    queue.tracks.erase(iterator);
    return true;
}

[[nodiscard]] bool moveMusicTrack(
    Domain::BackgroundMusicQueue& queue,
    TrackId trackId,
    int direction)
{
    if (direction != -1 && direction != 1)
    {
        return false;
    }

    const Domain::TrackId authoredTrackId = toAuthoredId(trackId);
    const auto iterator = std::find_if(
        queue.tracks.begin(),
        queue.tracks.end(),
        [&authoredTrackId](const Domain::BackgroundMusicTrack& candidate)
        {
            return candidate.id == authoredTrackId;
        });
    if (iterator == queue.tracks.end())
    {
        return false;
    }

    const auto currentIndex = std::distance(queue.tracks.begin(), iterator);
    const auto targetIndex = currentIndex + direction;
    if (targetIndex < 0
        || targetIndex >= static_cast<decltype(targetIndex)>(queue.tracks.size()))
    {
        return false;
    }

    std::iter_swap(iterator, queue.tracks.begin() + targetIndex);
    return true;
}

[[nodiscard]] QString spreadsheetSuffix(std::size_t ordinal)
{
    QString suffix{};

    for (;;)
    {
        const std::size_t remainder = ordinal % 26U;
        suffix.prepend(QChar{static_cast<ushort>('A' + remainder)});
        if (ordinal < 26U)
        {
            break;
        }

        ordinal = ordinal / 26U - 1U;
    }

    return suffix;
}

[[nodiscard]] std::optional<std::size_t> defaultPlaylistOrdinal(
    const std::string& alias) noexcept
{
    constexpr std::string_view Prefix{"Playlist "};
    if (!alias.starts_with(Prefix) || alias.size() == Prefix.size())
    {
        return std::nullopt;
    }

    std::size_t oneBasedOrdinal = 0U;
    for (std::size_t index = Prefix.size(); index < alias.size(); ++index)
    {
        const char value = alias[index];
        if (value < 'A' || value > 'Z')
        {
            return std::nullopt;
        }

        const std::size_t digit = static_cast<std::size_t>(value - 'A') + 1U;
        if (oneBasedOrdinal
            > (std::numeric_limits<std::size_t>::max() - digit) / 26U)
        {
            return std::nullopt;
        }

        oneBasedOrdinal = oneBasedOrdinal * 26U + digit;
    }

    return oneBasedOrdinal - 1U;
}

}

bool PlaylistId::isValid() const noexcept
{
    return !value.isNull();
}

bool ItemId::isValid() const noexcept
{
    return !value.isNull();
}

bool MediaLayerId::isValid() const noexcept
{
    return !value.isNull();
}

bool TrackId::isValid() const noexcept
{
    return !value.isNull();
}

std::optional<MediaLayerType> mediaLayerTypeForSource(const QString& source)
{
    const QString extension = QFileInfo{source}.suffix().toLower();
    if (extension == QStringLiteral("mp4") || extension == QStringLiteral("mkv")
        || extension == QStringLiteral("mov") || extension == QStringLiteral("avi")
        || extension == QStringLiteral("webm") || extension == QStringLiteral("m4v")
        || extension == QStringLiteral("video"))
    {
        return MediaLayerType::Video;
    }

    if (extension == QStringLiteral("png") || extension == QStringLiteral("jpg")
        || extension == QStringLiteral("jpeg") || extension == QStringLiteral("bmp")
        || extension == QStringLiteral("webp") || extension == QStringLiteral("gif"))
    {
        return MediaLayerType::Image;
    }

    return std::nullopt;
}

bool isSupportedBackgroundMusicSource(const QString& source)
{
    const QString extension = QFileInfo{source}.suffix().toLower();
    return extension == QStringLiteral("mp3") || extension == QStringLiteral("aac")
        || extension == QStringLiteral("m4a") || extension == QStringLiteral("flac")
        || extension == QStringLiteral("wav") || extension == QStringLiteral("ogg")
        || extension == QStringLiteral("opus") || extension == QStringLiteral("wma")
        || extension == QStringLiteral("alac") || extension == QStringLiteral("aiff")
        || extension == QStringLiteral("aif") || extension == QStringLiteral("ape");
}

WorkspaceModel::WorkspaceModel(Domain::DisplayOwner owner)
    : authoredProject_(std::move(owner))
{
    const PlaylistId initialPlaylistId = addPlaylist();
    selectedPlaylistId_ = initialPlaylistId;
    activePlaylistId_ = initialPlaylistId;
}

void WorkspaceModel::setAuthoredChangedCallback(AuthoredChangedCallback callback)
{
    authoredChangedCallback_ = std::move(callback);
}

void WorkspaceModel::replaceAuthoredProject(Domain::AuthoredProject project)
{
    authoredProject_ = std::move(project);
    selectedPlaylistId_ = PlaylistId{};
    selectedItemId_.reset();
    activePlaylistId_ = PlaylistId{};
    activeItemId_.reset();
    mediaLayerAttachments_.clear();
    recalculateNextDefaultPlaylistOrdinal();
    rebuildCompatibilityView();
    bumpAuthoredRevision();
}

void WorkspaceModel::resetAuthoredProject()
{
    replaceAuthoredProject(Domain::AuthoredProject{authoredProject_.displayOwner});
}

std::uint64_t WorkspaceModel::authoredRevision() const noexcept
{
    return authoredRevision_;
}

const Domain::AuthoredProject& WorkspaceModel::authoredProject() const noexcept
{
    return authoredProject_;
}

const QVector<WorkspacePlaylist>& WorkspaceModel::playlists() const noexcept
{
    return compatibilityPlaylists_;
}

const WorkspacePlaylist* WorkspaceModel::playlist(PlaylistId id) const noexcept
{
    const auto iterator = std::find_if(
        compatibilityPlaylists_.cbegin(),
        compatibilityPlaylists_.cend(),
        [id](const WorkspacePlaylist& candidate)
        {
            return candidate.id == id;
        });

    return iterator == compatibilityPlaylists_.cend() ? nullptr : &*iterator;
}

const Domain::AuthoredPlaylist* WorkspaceModel::authoredPlaylist(
    PlaylistId id) const noexcept
{
    const Domain::PlaylistId authoredId = toAuthoredId(id);
    const auto iterator = std::find_if(
        authoredProject_.playlists.cbegin(),
        authoredProject_.playlists.cend(),
        [&authoredId](const Domain::AuthoredPlaylist& candidate)
        {
            return candidate.id == authoredId;
        });

    return iterator == authoredProject_.playlists.cend() ? nullptr : &*iterator;
}

Domain::AuthoredPlaylist* WorkspaceModel::authoredPlaylistMutable(PlaylistId id) noexcept
{
    const Domain::PlaylistId authoredId = toAuthoredId(id);
    const auto iterator = std::find_if(
        authoredProject_.playlists.begin(),
        authoredProject_.playlists.end(),
        [&authoredId](const Domain::AuthoredPlaylist& candidate)
        {
            return candidate.id == authoredId;
        });

    return iterator == authoredProject_.playlists.end() ? nullptr : &*iterator;
}

const WorkspaceItem* WorkspaceModel::item(ItemId id) const noexcept
{
    const auto location = itemLocation(id);
    if (!location.has_value())
    {
        return nullptr;
    }

    return &compatibilityPlaylists_
        .at(static_cast<qsizetype>(location->playlistIndex))
        .items.at(static_cast<qsizetype>(location->itemIndex));
}

const Domain::AuthoredItem* WorkspaceModel::authoredItem(ItemId id) const noexcept
{
    const auto location = itemLocation(id);
    if (!location.has_value())
    {
        return nullptr;
    }

    return &authoredProject_.playlists[location->playlistIndex].items[location->itemIndex];
}

Domain::AuthoredItem* WorkspaceModel::authoredItemMutable(ItemId id) noexcept
{
    const auto location = itemLocation(id);
    if (!location.has_value())
    {
        return nullptr;
    }

    return &authoredProject_.playlists[location->playlistIndex].items[location->itemIndex];
}

const WorkspaceMediaLayer* WorkspaceModel::mediaLayer(MediaLayerId id) const noexcept
{
    const auto iterator = std::find_if(
        mediaLayerAttachments_.cbegin(),
        mediaLayerAttachments_.cend(),
        [id](const MediaLayerAttachment& candidate)
        {
            return candidate.layer.id == id;
        });
    return iterator == mediaLayerAttachments_.cend() ? nullptr : &iterator->layer;
}

const MusicQueueConfig* WorkspaceModel::playlistMusicQueue(PlaylistId id) const noexcept
{
    const WorkspacePlaylist* entry = playlist(id);
    return entry == nullptr ? nullptr : &entry->globalMusicQueue;
}

const MusicQueueConfig* WorkspaceModel::itemMusicQueue(ItemId id) const noexcept
{
    const WorkspaceItem* entry = item(id);
    return entry == nullptr ? nullptr : &entry->musicQueue;
}

const DesktopCanvas& WorkspaceModel::desktopCanvas() const noexcept
{
    return authoredProject_.desktopCanvas;
}

PlaybackSequenceMode WorkspaceModel::playlistPlaybackMode() const noexcept
{
    return toWorkspacePlaybackMode(authoredProject_.playlistPlaybackMode);
}

std::optional<PlaybackSequenceMode> WorkspaceModel::playlistItemPlaybackMode(
    PlaylistId id
) const noexcept
{
    const Domain::AuthoredPlaylist* const entry = authoredPlaylist(id);
    return entry == nullptr
        ? std::nullopt
        : std::optional{toWorkspacePlaybackMode(entry->itemPlaybackMode)};
}

bool WorkspaceModel::playlistEnabled(PlaylistId id) const noexcept
{
    const Domain::AuthoredPlaylist* const entry = authoredPlaylist(id);
    return entry != nullptr && entry->enabled;
}

bool WorkspaceModel::itemEnabled(ItemId id) const noexcept
{
    const Domain::AuthoredItem* const entry = authoredItem(id);
    return entry != nullptr && entry->enabled;
}

bool WorkspaceModel::isItemPlayable(PlaylistId playlistId, ItemId itemId) const noexcept
{
    const Domain::AuthoredPlaylist* const playlistEntry = authoredPlaylist(playlistId);
    if (playlistEntry == nullptr || !playlistEntry->enabled)
    {
        return false;
    }

    const Domain::ItemId authoredId = toAuthoredId(itemId);
    const auto iterator = std::find_if(
        playlistEntry->items.cbegin(),
        playlistEntry->items.cend(),
        [&authoredId](const Domain::AuthoredItem& candidate)
        {
            return candidate.id == authoredId;
        });
    return iterator != playlistEntry->items.cend() && iterator->enabled;
}

bool WorkspaceModel::isPlaylistPlayable(PlaylistId id) const noexcept
{
    return firstPlayableItem(id).has_value();
}

std::optional<ItemId> WorkspaceModel::firstPlayableItem(PlaylistId id) const noexcept
{
    const Domain::AuthoredPlaylist* const playlistEntry = authoredPlaylist(id);
    if (playlistEntry == nullptr || !playlistEntry->enabled)
    {
        return std::nullopt;
    }

    const auto iterator = std::find_if(
        playlistEntry->items.cbegin(),
        playlistEntry->items.cend(),
        [](const Domain::AuthoredItem& candidate)
        {
            return candidate.enabled;
        });
    return iterator == playlistEntry->items.cend()
        ? std::nullopt
        : std::optional<ItemId>{toPlayerId(iterator->id)};
}

WorkspaceMediaLayer* WorkspaceModel::mediaLayerMutable(MediaLayerId id) noexcept
{
    const auto iterator = std::find_if(
        mediaLayerAttachments_.begin(),
        mediaLayerAttachments_.end(),
        [id](const MediaLayerAttachment& candidate)
        {
            return candidate.layer.id == id;
        });
    return iterator == mediaLayerAttachments_.end() ? nullptr : &iterator->layer;
}

PlaylistId WorkspaceModel::selectedPlaylistId() const noexcept
{
    return selectedPlaylistId_;
}

PlaylistId WorkspaceModel::activePlaylistId() const noexcept
{
    return activePlaylistId_;
}

const std::optional<ItemId>& WorkspaceModel::selectedItemId() const noexcept
{
    return selectedItemId_;
}

const std::optional<ItemId>& WorkspaceModel::activeItemId() const noexcept
{
    return activeItemId_;
}

PlaylistId WorkspaceModel::addPlaylist()
{
    const PlaylistId id{QUuid::createUuid()};
    Domain::AuthoredPlaylist playlistEntry{};
    playlistEntry.id = toAuthoredId(id);
    playlistEntry.displayAlias = toUtf8(nextDefaultPlaylistAlias());
    authoredProject_.playlists.push_back(std::move(playlistEntry));
    rebuildCompatibilityView();
    bumpAuthoredRevision();
    return id;
}

bool WorkspaceModel::renamePlaylist(PlaylistId id, const QString& displayAlias)
{
    Domain::AuthoredPlaylist* entry = authoredPlaylistMutable(id);
    if (entry == nullptr || !hasMeaningfulAlias(displayAlias))
    {
        return false;
    }

    const std::string authoredAlias = toUtf8(displayAlias);
    if (entry->displayAlias == authoredAlias)
    {
        return true;
    }

    entry->displayAlias = authoredAlias;
    rebuildCompatibilityView();
    bumpAuthoredRevision();
    return true;
}

bool WorkspaceModel::removePlaylist(PlaylistId id)
{
    const Domain::PlaylistId authoredId = toAuthoredId(id);
    const auto iterator = std::find_if(
        authoredProject_.playlists.begin(),
        authoredProject_.playlists.end(),
        [&authoredId](const Domain::AuthoredPlaylist& candidate)
        {
            return candidate.id == authoredId;
        });
    if (iterator == authoredProject_.playlists.end())
    {
        return false;
    }

    if (authoredProject_.playlists.size() == 1U)
    {
        selectedPlaylistId_ = PlaylistId{};
        selectedItemId_.reset();
        activePlaylistId_ = PlaylistId{};
        activeItemId_.reset();
    }
    else
    {
        const auto index = static_cast<std::size_t>(
            std::distance(authoredProject_.playlists.begin(), iterator));
        const std::size_t replacementIndex = index + 1U < authoredProject_.playlists.size()
            ? index + 1U
            : index - 1U;
        const PlaylistId replacementId = toPlayerId(
            authoredProject_.playlists[replacementIndex].id);

        if (selectedPlaylistId_ == id)
        {
            selectedPlaylistId_ = replacementId;
            selectedItemId_.reset();
        }
        if (activePlaylistId_ == id)
        {
            activePlaylistId_ = replacementId;
            activeItemId_.reset();
        }
    }

    for (const Domain::AuthoredItem& itemEntry : iterator->items)
    {
        purgeMediaLayersForItem(itemEntry.id);
    }
    authoredProject_.playlists.erase(iterator);
    rebuildCompatibilityView();
    bumpAuthoredRevision();
    return true;
}

bool WorkspaceModel::selectPlaylist(PlaylistId id)
{
    if (authoredPlaylist(id) == nullptr)
    {
        return false;
    }

    selectedPlaylistId_ = id;
    selectedItemId_.reset();
    return true;
}

bool WorkspaceModel::activatePlaylist(PlaylistId id)
{
    if (authoredPlaylist(id) == nullptr)
    {
        return false;
    }

    activePlaylistId_ = id;
    activeItemId_.reset();
    selectedPlaylistId_ = id;
    selectedItemId_.reset();
    return true;
}

bool WorkspaceModel::setPlaylistEnabled(PlaylistId id, bool enabled)
{
    Domain::AuthoredPlaylist* const entry = authoredPlaylistMutable(id);
    if (entry == nullptr)
    {
        return false;
    }

    if (entry->enabled == enabled)
    {
        return true;
    }

    entry->enabled = enabled;
    rebuildCompatibilityView();
    bumpAuthoredRevision();
    return true;
}

bool WorkspaceModel::movePlaylist(PlaylistId id, int direction)
{
    if (direction != -1 && direction != 1)
    {
        return false;
    }

    const Domain::PlaylistId authoredId = toAuthoredId(id);
    const auto iterator = std::find_if(
        authoredProject_.playlists.begin(),
        authoredProject_.playlists.end(),
        [&authoredId](const Domain::AuthoredPlaylist& candidate)
        {
            return candidate.id == authoredId;
        });
    if (iterator == authoredProject_.playlists.end())
    {
        return false;
    }

    const auto currentIndex = std::distance(authoredProject_.playlists.begin(), iterator);
    const auto targetIndex = currentIndex + direction;
    if (targetIndex < 0
        || targetIndex >= static_cast<decltype(targetIndex)>(authoredProject_.playlists.size()))
    {
        return false;
    }

    std::iter_swap(iterator, authoredProject_.playlists.begin() + targetIndex);
    rebuildCompatibilityView();
    bumpAuthoredRevision();
    return true;
}

bool WorkspaceModel::setPlaylistPlaybackMode(PlaybackSequenceMode mode) noexcept
{
    if (!isPlaybackSequenceMode(mode))
    {
        return false;
    }

    const Domain::PlaybackSequenceMode authoredMode = toAuthoredPlaybackMode(mode);
    if (authoredProject_.playlistPlaybackMode == authoredMode)
    {
        return true;
    }

    authoredProject_.playlistPlaybackMode = authoredMode;
    bumpAuthoredRevision();
    return true;
}

bool WorkspaceModel::setPlaylistItemPlaybackMode(
    PlaylistId id,
    PlaybackSequenceMode mode
) noexcept
{
    Domain::AuthoredPlaylist* const entry = authoredPlaylistMutable(id);
    if (entry == nullptr || !isPlaybackSequenceMode(mode))
    {
        return false;
    }

    const Domain::PlaybackSequenceMode authoredMode = toAuthoredPlaybackMode(mode);
    if (entry->itemPlaybackMode == authoredMode)
    {
        return true;
    }

    entry->itemPlaybackMode = authoredMode;
    const auto compatibilityIterator = std::find_if(
        compatibilityPlaylists_.begin(),
        compatibilityPlaylists_.end(),
        [id](const WorkspacePlaylist& candidate)
        {
            return candidate.id == id;
        });
    if (compatibilityIterator != compatibilityPlaylists_.end())
    {
        compatibilityIterator->itemPlaybackMode =
            toWorkspacePlaybackMode(entry->itemPlaybackMode);
    }
    bumpAuthoredRevision();
    return true;
}

bool WorkspaceModel::setPlaylistMasterVolume(PlaylistId id, double volume)
{
    Domain::AuthoredPlaylist* entry = authoredPlaylistMutable(id);
    if (entry == nullptr || !std::isfinite(volume))
    {
        return false;
    }

    const double normalizedVolume = std::clamp(volume, 0.0, 1.0);
    if (entry->masterVolume == normalizedVolume)
    {
        return true;
    }

    entry->masterVolume = normalizedVolume;
    rebuildCompatibilityView();
    bumpAuthoredRevision();
    return true;
}

std::optional<TrackId> WorkspaceModel::addPlaylistMusicTrack(
    PlaylistId playlistId,
    const QString& sourcePath,
    const QString& displayAlias)
{
    Domain::AuthoredPlaylist* entry = authoredPlaylistMutable(playlistId);
    if (entry == nullptr)
    {
        return std::nullopt;
    }

    const auto id = appendMusicTrack(entry->globalMusicQueue, sourcePath, displayAlias);
    if (id.has_value())
    {
        rebuildCompatibilityView();
        bumpAuthoredRevision();
    }
    return id;
}

bool WorkspaceModel::removePlaylistMusicTrack(PlaylistId playlistId, TrackId trackId)
{
    Domain::AuthoredPlaylist* entry = authoredPlaylistMutable(playlistId);
    if (entry == nullptr || !removeMusicTrack(entry->globalMusicQueue, trackId))
    {
        return false;
    }

    rebuildCompatibilityView();
    bumpAuthoredRevision();
    return true;
}

bool WorkspaceModel::movePlaylistMusicTrack(
    PlaylistId playlistId,
    TrackId trackId,
    int direction)
{
    Domain::AuthoredPlaylist* entry = authoredPlaylistMutable(playlistId);
    if (entry == nullptr || !moveMusicTrack(entry->globalMusicQueue, trackId, direction))
    {
        return false;
    }

    rebuildCompatibilityView();
    bumpAuthoredRevision();
    return true;
}

bool WorkspaceModel::setPlaylistMusicEnabled(PlaylistId playlistId, bool enabled)
{
    Domain::AuthoredPlaylist* entry = authoredPlaylistMutable(playlistId);
    if (entry == nullptr)
    {
        return false;
    }

    if (entry->globalMusicQueue.enabled == enabled)
    {
        return true;
    }

    entry->globalMusicQueue.enabled = enabled;
    rebuildCompatibilityView();
    bumpAuthoredRevision();
    return true;
}

bool WorkspaceModel::setPlaylistMusicPlaybackMode(
    PlaylistId playlistId,
    MusicPlaybackMode playbackMode)
{
    Domain::AuthoredPlaylist* entry = authoredPlaylistMutable(playlistId);
    if (entry == nullptr || !isValidMusicPlaybackMode(playbackMode))
    {
        return false;
    }

    const Domain::MusicPlaybackMode authoredMode =
        toAuthoredMusicPlaybackMode(playbackMode);
    if (entry->globalMusicQueue.playbackMode == authoredMode)
    {
        return true;
    }

    entry->globalMusicQueue.playbackMode = authoredMode;
    rebuildCompatibilityView();
    bumpAuthoredRevision();
    return true;
}

bool WorkspaceModel::setPlaylistMusicVolume(PlaylistId playlistId, double volume)
{
    Domain::AuthoredPlaylist* entry = authoredPlaylistMutable(playlistId);
    if (entry == nullptr || !std::isfinite(volume))
    {
        return false;
    }

    const double normalizedVolume = std::clamp(volume, 0.0, 1.0);
    if (entry->globalMusicQueue.volume == normalizedVolume)
    {
        return true;
    }

    entry->globalMusicQueue.volume = normalizedVolume;
    rebuildCompatibilityView();
    bumpAuthoredRevision();
    return true;
}

bool WorkspaceModel::setPlaylistMusicTrackVolume(
    PlaylistId playlistId,
    TrackId trackId,
    double volume)
{
    Domain::AuthoredPlaylist* entry = authoredPlaylistMutable(playlistId);
    if (entry == nullptr)
    {
        return false;
    }

    const MutationResult result = setMusicTrackVolume(
        entry->globalMusicQueue,
        trackId,
        volume);
    if (result == MutationResult::Failed)
    {
        return false;
    }
    if (result == MutationResult::Unchanged)
    {
        return true;
    }

    rebuildCompatibilityView();
    bumpAuthoredRevision();
    return true;
}

std::optional<ItemId> WorkspaceModel::addItem(
    PlaylistId playlistId,
    WorkspaceMediaType mediaType,
    const QString& source,
    const QString& displayAlias)
{
    Domain::AuthoredPlaylist* playlistEntry = authoredPlaylistMutable(playlistId);
    if (playlistEntry == nullptr || source.trimmed().isEmpty())
    {
        return std::nullopt;
    }

    const QString resolvedAlias = displayAlias.isEmpty() ? aliasFromSource(source) : displayAlias;
    if (!hasMeaningfulAlias(resolvedAlias))
    {
        return std::nullopt;
    }

    const ItemId id{QUuid::createUuid()};
    Domain::AuthoredItem itemEntry{};
    itemEntry.id = toAuthoredId(id);
    itemEntry.displayAlias = toUtf8(resolvedAlias);
    itemEntry.mediaType = toAuthoredMediaType(mediaType);
    itemEntry.source = toUtf8(source);
    playlistEntry->items.push_back(std::move(itemEntry));
    rebuildCompatibilityView();
    bumpAuthoredRevision();
    return id;
}

bool WorkspaceModel::renameItem(ItemId id, const QString& displayAlias)
{
    Domain::AuthoredItem* entry = authoredItemMutable(id);
    if (entry == nullptr || !hasMeaningfulAlias(displayAlias))
    {
        return false;
    }

    const std::string authoredAlias = toUtf8(displayAlias);
    if (entry->displayAlias == authoredAlias)
    {
        return true;
    }

    entry->displayAlias = authoredAlias;
    rebuildCompatibilityView();
    bumpAuthoredRevision();
    return true;
}

bool WorkspaceModel::removeItem(ItemId id)
{
    const auto location = itemLocation(id);
    if (!location.has_value())
    {
        return false;
    }

    Domain::AuthoredPlaylist& playlistEntry =
        authoredProject_.playlists[location->playlistIndex];
    const Domain::ItemId authoredId = playlistEntry.items[location->itemIndex].id;
    playlistEntry.items.erase(playlistEntry.items.begin()
        + static_cast<std::ptrdiff_t>(location->itemIndex));
    purgeMediaLayersForItem(authoredId);

    if (selectedItemId_ == id)
    {
        selectedItemId_.reset();
    }
    if (activeItemId_ == id)
    {
        activeItemId_.reset();
    }

    rebuildCompatibilityView();
    bumpAuthoredRevision();
    return true;
}

bool WorkspaceModel::selectItem(ItemId id)
{
    const auto location = itemLocation(id);
    if (!location.has_value())
    {
        return false;
    }

    selectedPlaylistId_ = toPlayerId(
        authoredProject_.playlists[location->playlistIndex].id);
    selectedItemId_ = id;
    return true;
}

bool WorkspaceModel::activateItem(ItemId id)
{
    if (!setActiveItem(id))
    {
        return false;
    }

    selectedPlaylistId_ = activePlaylistId_;
    selectedItemId_ = id;
    return true;
}

bool WorkspaceModel::setActiveItem(ItemId id)
{
    const auto location = itemLocation(id);
    if (!location.has_value())
    {
        return false;
    }

    activePlaylistId_ = toPlayerId(
        authoredProject_.playlists[location->playlistIndex].id);
    activeItemId_ = id;
    return true;
}

bool WorkspaceModel::setItemEnabled(ItemId id, bool enabled)
{
    Domain::AuthoredItem* const entry = authoredItemMutable(id);
    if (entry == nullptr)
    {
        return false;
    }

    if (entry->enabled == enabled)
    {
        return true;
    }

    entry->enabled = enabled;
    rebuildCompatibilityView();
    bumpAuthoredRevision();
    return true;
}

bool WorkspaceModel::moveItem(ItemId id, int direction)
{
    if (direction != -1 && direction != 1)
    {
        return false;
    }

    const auto location = itemLocation(id);
    if (!location.has_value())
    {
        return false;
    }

    Domain::AuthoredPlaylist& playlistEntry =
        authoredProject_.playlists[location->playlistIndex];
    const auto targetIndex = static_cast<std::ptrdiff_t>(location->itemIndex) + direction;
    if (targetIndex < 0
        || targetIndex >= static_cast<std::ptrdiff_t>(playlistEntry.items.size()))
    {
        return false;
    }

    std::iter_swap(
        playlistEntry.items.begin() + static_cast<std::ptrdiff_t>(location->itemIndex),
        playlistEntry.items.begin() + targetIndex);
    rebuildCompatibilityView();
    bumpAuthoredRevision();
    return true;
}

void WorkspaceModel::clearActivePlayback() noexcept
{
    activePlaylistId_ = PlaylistId{};
    activeItemId_.reset();
}

bool WorkspaceModel::setImageDisplayDuration(ItemId id, double durationSeconds)
{
    Domain::AuthoredItem* entry = authoredItemMutable(id);
    if (entry == nullptr || entry->mediaType != Domain::AuthoredMediaType::Image
        || !std::isfinite(durationSeconds) || durationSeconds <= 0.0)
    {
        return false;
    }

    if (entry->imageDurationSeconds == durationSeconds)
    {
        return true;
    }

    entry->imageDurationSeconds = durationSeconds;
    rebuildCompatibilityView();
    bumpAuthoredRevision();
    return true;
}

bool WorkspaceModel::setVideoPlaybackRate(ItemId id, double playbackRate)
{
    Domain::AuthoredItem* entry = authoredItemMutable(id);
    if (entry == nullptr || entry->mediaType != Domain::AuthoredMediaType::Video
        || !isSupportedPlaybackRate(playbackRate))
    {
        return false;
    }

    if (entry->videoPlaybackRate == playbackRate)
    {
        return true;
    }

    entry->videoPlaybackRate = playbackRate;
    rebuildCompatibilityView();
    bumpAuthoredRevision();
    return true;
}

bool WorkspaceModel::setItemMediaVolume(ItemId id, double volume)
{
    Domain::AuthoredItem* entry = authoredItemMutable(id);
    if (entry == nullptr || entry->mediaType != Domain::AuthoredMediaType::Video
        || !std::isfinite(volume))
    {
        return false;
    }

    const double normalizedVolume = std::clamp(volume, 0.0, 1.0);
    if (entry->itemMediaVolume == normalizedVolume)
    {
        return true;
    }

    entry->itemMediaVolume = normalizedVolume;
    rebuildCompatibilityView();
    bumpAuthoredRevision();
    return true;
}

std::optional<TrackId> WorkspaceModel::addItemMusicTrack(
    ItemId itemId,
    const QString& sourcePath,
    const QString& displayAlias)
{
    Domain::AuthoredItem* entry = authoredItemMutable(itemId);
    if (entry == nullptr)
    {
        return std::nullopt;
    }

    const auto id = appendMusicTrack(entry->musicQueue, sourcePath, displayAlias);
    if (id.has_value())
    {
        rebuildCompatibilityView();
        bumpAuthoredRevision();
    }
    return id;
}

bool WorkspaceModel::removeItemMusicTrack(ItemId itemId, TrackId trackId)
{
    Domain::AuthoredItem* entry = authoredItemMutable(itemId);
    if (entry == nullptr || !removeMusicTrack(entry->musicQueue, trackId))
    {
        return false;
    }

    rebuildCompatibilityView();
    bumpAuthoredRevision();
    return true;
}

bool WorkspaceModel::moveItemMusicTrack(ItemId itemId, TrackId trackId, int direction)
{
    Domain::AuthoredItem* entry = authoredItemMutable(itemId);
    if (entry == nullptr || !moveMusicTrack(entry->musicQueue, trackId, direction))
    {
        return false;
    }

    rebuildCompatibilityView();
    bumpAuthoredRevision();
    return true;
}

bool WorkspaceModel::setItemMusicEnabled(ItemId itemId, bool enabled)
{
    Domain::AuthoredItem* entry = authoredItemMutable(itemId);
    if (entry == nullptr)
    {
        return false;
    }

    if (entry->musicQueue.enabled == enabled)
    {
        return true;
    }

    entry->musicQueue.enabled = enabled;
    rebuildCompatibilityView();
    bumpAuthoredRevision();
    return true;
}

bool WorkspaceModel::setItemMusicPlaybackMode(
    ItemId itemId,
    MusicPlaybackMode playbackMode)
{
    Domain::AuthoredItem* entry = authoredItemMutable(itemId);
    if (entry == nullptr || !isValidMusicPlaybackMode(playbackMode))
    {
        return false;
    }

    const Domain::MusicPlaybackMode authoredMode =
        toAuthoredMusicPlaybackMode(playbackMode);
    if (entry->musicQueue.playbackMode == authoredMode)
    {
        return true;
    }

    entry->musicQueue.playbackMode = authoredMode;
    rebuildCompatibilityView();
    bumpAuthoredRevision();
    return true;
}

bool WorkspaceModel::setItemMusicVolume(ItemId itemId, double volume)
{
    Domain::AuthoredItem* entry = authoredItemMutable(itemId);
    if (entry == nullptr || !std::isfinite(volume))
    {
        return false;
    }

    const double normalizedVolume = std::clamp(volume, 0.0, 1.0);
    if (entry->musicQueue.volume == normalizedVolume)
    {
        return true;
    }

    entry->musicQueue.volume = normalizedVolume;
    rebuildCompatibilityView();
    bumpAuthoredRevision();
    return true;
}

bool WorkspaceModel::setItemMusicTrackVolume(ItemId itemId, TrackId trackId, double volume)
{
    Domain::AuthoredItem* entry = authoredItemMutable(itemId);
    if (entry == nullptr)
    {
        return false;
    }

    const MutationResult result = setMusicTrackVolume(entry->musicQueue, trackId, volume);
    if (result == MutationResult::Failed)
    {
        return false;
    }
    if (result == MutationResult::Unchanged)
    {
        return true;
    }

    rebuildCompatibilityView();
    bumpAuthoredRevision();
    return true;
}

bool WorkspaceModel::setItemPresentation(
    ItemId id,
    const WorkspaceItemPresentation& presentation
)
{
    Domain::AuthoredItem* entry = authoredItemMutable(id);
    if (entry == nullptr || !isValidItemPresentation(presentation))
    {
        return false;
    }

    if (sameItemPresentation(entry->presentation, presentation))
    {
        return true;
    }

    entry->presentation = presentation;
    rebuildCompatibilityView();
    bumpAuthoredRevision();
    return true;
}

void WorkspaceModel::setDesktopCanvas(const DesktopCanvas& canvas) noexcept
{
    if (canvas.aspectRatio.isValid()
        && authoredProject_.desktopCanvas.aspectRatio != canvas.aspectRatio)
    {
        authoredProject_.desktopCanvas = canvas;
        bumpAuthoredRevision();
    }
}

std::optional<MediaLayerId> WorkspaceModel::addExternalMediaLayer(
    ItemId itemId,
    const QString& source,
    const QString& displayAlias)
{
    const Domain::AuthoredItem* entry = authoredItem(itemId);
    if (entry == nullptr || source.trimmed().isEmpty())
    {
        return std::nullopt;
    }

    const QString resolvedAlias = displayAlias.isEmpty() ? aliasFromSource(source) : displayAlias;
    if (!hasMeaningfulAlias(resolvedAlias))
    {
        return std::nullopt;
    }

    const auto type = mediaLayerTypeForSource(source);
    if (!type.has_value())
    {
        return std::nullopt;
    }

    const MediaLayerId id{QUuid::createUuid()};
    mediaLayerAttachments_.append(MediaLayerAttachment{
        entry->id,
        WorkspaceMediaLayer{
            id,
            *type,
            source,
            resolvedAlias,
            true,
            *type == MediaLayerType::Image ? std::nullopt : std::optional<double>{1.0}}});
    rebuildCompatibilityView();
    return id;
}

bool WorkspaceModel::removeMediaLayer(MediaLayerId id)
{
    const auto iterator = std::find_if(
        mediaLayerAttachments_.begin(),
        mediaLayerAttachments_.end(),
        [id](const MediaLayerAttachment& candidate)
        {
            return candidate.layer.id == id;
        });
    if (iterator == mediaLayerAttachments_.end())
    {
        return false;
    }

    mediaLayerAttachments_.erase(iterator);
    rebuildCompatibilityView();
    return true;
}

bool WorkspaceModel::setMediaLayerEnabled(MediaLayerId id, bool enabled)
{
    WorkspaceMediaLayer* layer = mediaLayerMutable(id);
    if (layer == nullptr)
    {
        return false;
    }

    layer->enabled = enabled;
    rebuildCompatibilityView();
    return true;
}

bool WorkspaceModel::setMediaLayerVolume(MediaLayerId id, double volume)
{
    WorkspaceMediaLayer* layer = mediaLayerMutable(id);
    if (layer == nullptr || layer->type == MediaLayerType::Image || !layer->volume.has_value()
        || !std::isfinite(volume))
    {
        return false;
    }

    layer->volume = std::clamp(volume, 0.0, 1.0);
    rebuildCompatibilityView();
    return true;
}

std::optional<WorkspaceModel::ItemLocation> WorkspaceModel::itemLocation(
    ItemId id) const noexcept
{
    const Domain::ItemId authoredId = toAuthoredId(id);
    for (std::size_t playlistIndex = 0;
        playlistIndex < authoredProject_.playlists.size();
        ++playlistIndex)
    {
        const std::vector<Domain::AuthoredItem>& items =
            authoredProject_.playlists[playlistIndex].items;
        const auto iterator = std::find_if(
            items.cbegin(),
            items.cend(),
            [&authoredId](const Domain::AuthoredItem& candidate)
            {
                return candidate.id == authoredId;
            });
        if (iterator != items.cend())
        {
            return ItemLocation{
                playlistIndex,
                static_cast<std::size_t>(std::distance(items.cbegin(), iterator))};
        }
    }

    return std::nullopt;
}

void WorkspaceModel::rebuildCompatibilityView()
{
    QVector<WorkspacePlaylist> rebuilt{};
    rebuilt.reserve(static_cast<qsizetype>(authoredProject_.playlists.size()));

    for (const Domain::AuthoredPlaylist& authoredPlaylistEntry : authoredProject_.playlists)
    {
        WorkspacePlaylist playlistEntry{};
        playlistEntry.id = toPlayerId(authoredPlaylistEntry.id);
        playlistEntry.displayAlias = fromUtf8(authoredPlaylistEntry.displayAlias);
        playlistEntry.masterVolume = authoredPlaylistEntry.masterVolume;
        playlistEntry.globalMusicQueue = makeCompatibilityQueue(
            authoredPlaylistEntry.globalMusicQueue);
        playlistEntry.itemPlaybackMode = toWorkspacePlaybackMode(
            authoredPlaylistEntry.itemPlaybackMode);
        playlistEntry.enabled = authoredPlaylistEntry.enabled;
        playlistEntry.items.reserve(static_cast<qsizetype>(authoredPlaylistEntry.items.size()));

        for (const Domain::AuthoredItem& authoredItemEntry : authoredPlaylistEntry.items)
        {
            WorkspaceItem itemEntry{};
            itemEntry.id = toPlayerId(authoredItemEntry.id);
            itemEntry.displayAlias = fromUtf8(authoredItemEntry.displayAlias);
            itemEntry.mediaType = toWorkspaceMediaType(authoredItemEntry.mediaType);
            itemEntry.source = fromUtf8(authoredItemEntry.source);
            itemEntry.displayDurationSeconds = authoredItemEntry.imageDurationSeconds;
            itemEntry.playbackRate = authoredItemEntry.videoPlaybackRate;
            itemEntry.presentation = authoredItemEntry.presentation;
            itemEntry.itemMediaVolume = authoredItemEntry.itemMediaVolume;
            itemEntry.musicQueue = makeCompatibilityQueue(authoredItemEntry.musicQueue);
            itemEntry.enabled = authoredItemEntry.enabled;

            for (const MediaLayerAttachment& attachment : mediaLayerAttachments_)
            {
                if (attachment.ownerItemId == authoredItemEntry.id)
                {
                    itemEntry.mediaLayers.append(attachment.layer);
                }
            }

            playlistEntry.items.append(std::move(itemEntry));
        }

        rebuilt.append(std::move(playlistEntry));
    }

    compatibilityPlaylists_ = std::move(rebuilt);
}

void WorkspaceModel::purgeMediaLayersForItem(const Domain::ItemId& ownerItemId)
{
    const auto newEnd = std::remove_if(
        mediaLayerAttachments_.begin(),
        mediaLayerAttachments_.end(),
        [&ownerItemId](const MediaLayerAttachment& attachment)
        {
            return attachment.ownerItemId == ownerItemId;
        });
    mediaLayerAttachments_.erase(newEnd, mediaLayerAttachments_.end());
}

void WorkspaceModel::recalculateNextDefaultPlaylistOrdinal() noexcept
{
    nextDefaultPlaylistOrdinal_ = 0U;
    for (const Domain::AuthoredPlaylist& playlist : authoredProject_.playlists)
    {
        const std::optional<std::size_t> ordinal = defaultPlaylistOrdinal(
            playlist.displayAlias);
        if (ordinal.has_value()
            && *ordinal >= nextDefaultPlaylistOrdinal_
            && *ordinal < std::numeric_limits<std::size_t>::max())
        {
            nextDefaultPlaylistOrdinal_ = *ordinal + 1U;
        }
    }
}

void WorkspaceModel::bumpAuthoredRevision() noexcept
{
    ++authoredRevision_;
    if (authoredChangedCallback_)
    {
        authoredChangedCallback_();
    }
}

QString WorkspaceModel::nextDefaultPlaylistAlias()
{
    return QStringLiteral("Playlist ") + spreadsheetSuffix(nextDefaultPlaylistOrdinal_++);
}

}
