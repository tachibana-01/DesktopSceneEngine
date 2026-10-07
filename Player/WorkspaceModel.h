#pragma once

#include "PlaybackSequenceMode.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>

#include <DesktopSceneEngine/Domain/AuthoredProject.h>

#include <QString>
#include <QUuid>
#include <QVector>

namespace DesktopSceneEngine::Player
{

struct PlaylistId
{
    QUuid value{};

    [[nodiscard]] bool isValid() const noexcept;

    friend bool operator==(const PlaylistId&, const PlaylistId&) = default;
};

struct ItemId
{
    QUuid value{};

    [[nodiscard]] bool isValid() const noexcept;

    friend bool operator==(const ItemId&, const ItemId&) = default;
};

struct MediaLayerId
{
    QUuid value{};

    [[nodiscard]] bool isValid() const noexcept;

    friend bool operator==(const MediaLayerId&, const MediaLayerId&) = default;
};

struct TrackId
{
    QUuid value{};

    [[nodiscard]] bool isValid() const noexcept;

    friend bool operator==(const TrackId&, const TrackId&) = default;
};

enum class WorkspaceMediaType
{
    Image,
    Video
};

// Logical workspace Canvas state. This remains distinct from the editor-only
// Preview Viewport and can match any resolved physical display aspect.
using DesktopCanvas = Domain::DesktopCanvas;

// Reuses the existing Domain value types instead of defining a second Crop or
// Layout representation in Player.
using WorkspaceItemPresentation = Domain::AuthoredItemPresentation;

enum class MediaLayerType
{
    Video,
    Image
};

[[nodiscard]] std::optional<MediaLayerType> mediaLayerTypeForSource(const QString& source);

enum class MusicPlaybackMode
{
    CurrentSingleLoop,
    ListLoop,
    Shuffle
};

struct MusicTrack
{
    TrackId id{};
    QString sourcePath{};
    QString displayAlias{};
    double volume = 1.0;
};

struct MusicQueueConfig
{
    QVector<MusicTrack> tracks{};
    MusicPlaybackMode playbackMode = MusicPlaybackMode::ListLoop;
    double volume = 1.0;
    bool enabled = true;
};

[[nodiscard]] bool isSupportedBackgroundMusicSource(const QString& source);

struct WorkspaceMediaLayer
{
    MediaLayerId id{};
    MediaLayerType type = MediaLayerType::Video;
    QString source{};
    QString displayAlias{};
    bool enabled = true;
    std::optional<double> volume{};
};

struct WorkspaceItem
{
    ItemId id{};
    QString displayAlias{};
    WorkspaceMediaType mediaType = WorkspaceMediaType::Image;
    QString source{};
    double displayDurationSeconds = 10.0;
    double playbackRate = 1.0;
    QVector<WorkspaceMediaLayer> mediaLayers{};
    WorkspaceItemPresentation presentation{};
    double itemMediaVolume = 1.0;
    MusicQueueConfig musicQueue{};
    bool enabled = true;
};

struct WorkspacePlaylist
{
    PlaylistId id{};
    // Duplicate aliases are allowed; stable IDs are the lookup identity.
    QString displayAlias{};
    QVector<WorkspaceItem> items{};
    double masterVolume = 1.0;
    MusicQueueConfig globalMusicQueue{};
    PlaybackSequenceMode itemPlaybackMode = PlaybackSequenceMode::ListLoop;
    bool enabled = true;
};

class WorkspaceModel final
{
public:
    using AuthoredChangedCallback = std::function<void()>;

    explicit WorkspaceModel(Domain::DisplayOwner owner);

    void setAuthoredChangedCallback(AuthoredChangedCallback callback);
    void replaceAuthoredProject(Domain::AuthoredProject project);
    void resetAuthoredProject();
    [[nodiscard]] std::uint64_t authoredRevision() const noexcept;
    [[nodiscard]] const Domain::AuthoredProject& authoredProject() const noexcept;
    [[nodiscard]] const Domain::AuthoredPlaylist* authoredPlaylist(
        PlaylistId id) const noexcept;
    [[nodiscard]] const Domain::AuthoredItem* authoredItem(ItemId id) const noexcept;
    [[nodiscard]] const QVector<WorkspacePlaylist>& playlists() const noexcept;
    [[nodiscard]] const WorkspacePlaylist* playlist(PlaylistId id) const noexcept;
    [[nodiscard]] const WorkspaceItem* item(ItemId id) const noexcept;
    [[nodiscard]] const WorkspaceMediaLayer* mediaLayer(MediaLayerId id) const noexcept;
    [[nodiscard]] const MusicQueueConfig* playlistMusicQueue(PlaylistId id) const noexcept;
    [[nodiscard]] const MusicQueueConfig* itemMusicQueue(ItemId id) const noexcept;
    [[nodiscard]] const DesktopCanvas& desktopCanvas() const noexcept;
    [[nodiscard]] PlaybackSequenceMode playlistPlaybackMode() const noexcept;
    [[nodiscard]] std::optional<PlaybackSequenceMode> playlistItemPlaybackMode(
        PlaylistId id
    ) const noexcept;
    [[nodiscard]] bool playlistEnabled(PlaylistId id) const noexcept;
    [[nodiscard]] bool itemEnabled(ItemId id) const noexcept;
    [[nodiscard]] bool isItemPlayable(PlaylistId playlistId, ItemId itemId) const noexcept;
    [[nodiscard]] bool isPlaylistPlayable(PlaylistId id) const noexcept;
    [[nodiscard]] std::optional<ItemId> firstPlayableItem(PlaylistId id) const noexcept;

    [[nodiscard]] PlaylistId selectedPlaylistId() const noexcept;
    [[nodiscard]] PlaylistId activePlaylistId() const noexcept;
    [[nodiscard]] const std::optional<ItemId>& selectedItemId() const noexcept;
    [[nodiscard]] const std::optional<ItemId>& activeItemId() const noexcept;

    [[nodiscard]] PlaylistId addPlaylist();
    [[nodiscard]] bool renamePlaylist(PlaylistId id, const QString& displayAlias);
    [[nodiscard]] bool removePlaylist(PlaylistId id);
    [[nodiscard]] bool selectPlaylist(PlaylistId id);
    [[nodiscard]] bool activatePlaylist(PlaylistId id);
    [[nodiscard]] bool setPlaylistEnabled(PlaylistId id, bool enabled);
    [[nodiscard]] bool movePlaylist(PlaylistId id, int direction);
    [[nodiscard]] bool setPlaylistPlaybackMode(PlaybackSequenceMode mode) noexcept;
    [[nodiscard]] bool setPlaylistItemPlaybackMode(
        PlaylistId id,
        PlaybackSequenceMode mode
    ) noexcept;
    [[nodiscard]] bool setPlaylistMasterVolume(PlaylistId id, double volume);
    [[nodiscard]] std::optional<TrackId> addPlaylistMusicTrack(
        PlaylistId playlistId,
        const QString& sourcePath,
        const QString& displayAlias = {});
    [[nodiscard]] bool removePlaylistMusicTrack(PlaylistId playlistId, TrackId trackId);
    [[nodiscard]] bool movePlaylistMusicTrack(PlaylistId playlistId, TrackId trackId, int direction);
    [[nodiscard]] bool setPlaylistMusicEnabled(PlaylistId playlistId, bool enabled);
    [[nodiscard]] bool setPlaylistMusicPlaybackMode(
        PlaylistId playlistId,
        MusicPlaybackMode playbackMode);
    [[nodiscard]] bool setPlaylistMusicVolume(PlaylistId playlistId, double volume);
    [[nodiscard]] bool setPlaylistMusicTrackVolume(
        PlaylistId playlistId,
        TrackId trackId,
        double volume);

    [[nodiscard]] std::optional<ItemId> addItem(
        PlaylistId playlistId,
        WorkspaceMediaType mediaType,
        const QString& source,
        const QString& displayAlias = {});
    [[nodiscard]] bool renameItem(ItemId id, const QString& displayAlias);
    [[nodiscard]] bool removeItem(ItemId id);
    [[nodiscard]] bool selectItem(ItemId id);
    [[nodiscard]] bool activateItem(ItemId id);
    [[nodiscard]] bool setActiveItem(ItemId id);
    [[nodiscard]] bool setItemEnabled(ItemId id, bool enabled);
    [[nodiscard]] bool moveItem(ItemId id, int direction);
    void clearActivePlayback() noexcept;
    [[nodiscard]] bool setImageDisplayDuration(ItemId id, double durationSeconds);
    [[nodiscard]] bool setVideoPlaybackRate(ItemId id, double playbackRate);
    [[nodiscard]] bool setItemMediaVolume(ItemId id, double volume);
    [[nodiscard]] std::optional<TrackId> addItemMusicTrack(
        ItemId itemId,
        const QString& sourcePath,
        const QString& displayAlias = {});
    [[nodiscard]] bool removeItemMusicTrack(ItemId itemId, TrackId trackId);
    [[nodiscard]] bool moveItemMusicTrack(ItemId itemId, TrackId trackId, int direction);
    [[nodiscard]] bool setItemMusicEnabled(ItemId itemId, bool enabled);
    [[nodiscard]] bool setItemMusicPlaybackMode(ItemId itemId, MusicPlaybackMode playbackMode);
    [[nodiscard]] bool setItemMusicVolume(ItemId itemId, double volume);
    [[nodiscard]] bool setItemMusicTrackVolume(ItemId itemId, TrackId trackId, double volume);
    [[nodiscard]] bool setItemPresentation(
        ItemId id,
        const WorkspaceItemPresentation& presentation
    );
    void setDesktopCanvas(const DesktopCanvas& canvas) noexcept;

    [[nodiscard]] std::optional<MediaLayerId> addExternalMediaLayer(
        ItemId itemId,
        const QString& source,
        const QString& displayAlias = {});
    [[nodiscard]] bool removeMediaLayer(MediaLayerId id);
    [[nodiscard]] bool setMediaLayerEnabled(MediaLayerId id, bool enabled);
    [[nodiscard]] bool setMediaLayerVolume(MediaLayerId id, double volume);

private:
    struct ItemLocation
    {
        std::size_t playlistIndex = 0;
        std::size_t itemIndex = 0;
    };

    struct MediaLayerAttachment
    {
        Domain::ItemId ownerItemId{};
        WorkspaceMediaLayer layer{};
    };

    [[nodiscard]] Domain::AuthoredPlaylist* authoredPlaylistMutable(
        PlaylistId id) noexcept;
    [[nodiscard]] Domain::AuthoredItem* authoredItemMutable(ItemId id) noexcept;
    [[nodiscard]] WorkspaceMediaLayer* mediaLayerMutable(MediaLayerId id) noexcept;
    [[nodiscard]] std::optional<ItemLocation> itemLocation(ItemId id) const noexcept;
    void rebuildCompatibilityView();
    void purgeMediaLayersForItem(const Domain::ItemId& ownerItemId);
    void recalculateNextDefaultPlaylistOrdinal() noexcept;
    void bumpAuthoredRevision() noexcept;
    [[nodiscard]] QString nextDefaultPlaylistAlias();

    Domain::AuthoredProject authoredProject_;
    // Disposable, one-way projection for A3-deferred Qt-facing readers.
    QVector<WorkspacePlaylist> compatibilityPlaylists_{};
    QVector<MediaLayerAttachment> mediaLayerAttachments_{};
    PlaylistId selectedPlaylistId_{};
    PlaylistId activePlaylistId_{};
    std::optional<ItemId> selectedItemId_{};
    std::optional<ItemId> activeItemId_{};
    std::size_t nextDefaultPlaylistOrdinal_ = 0;
    std::uint64_t authoredRevision_ = 0U;
    AuthoredChangedCallback authoredChangedCallback_{};
};

}
