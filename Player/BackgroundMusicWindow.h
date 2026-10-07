#pragma once

#include "WorkspaceModel.h"

#include <QDialog>
#include <QStringList>

#include <functional>
#include <optional>

class QComboBox;
class QCheckBox;
class QEvent;
class QLabel;
class QListWidget;
class QPushButton;
class QSlider;
class QSplitter;
class QTreeWidget;
class QWidget;

class MainWindowBackgroundMusicForwardingTestAccess;

namespace DesktopSceneEngine::Player
{

class BackgroundMusicWindow final : public QDialog
{
public:
    using QueueChangedCallback = std::function<void(PlaylistId, std::optional<ItemId>)>;
    using PlaylistEnabledChangedCallback = std::function<void(PlaylistId, bool)>;
    using ItemEnabledChangedCallback = std::function<void(PlaylistId, ItemId, bool)>;
    using PlaylistTrackSwitchRequestedCallback = std::function<void(PlaylistId, TrackId)>;
    using ItemTrackSwitchRequestedCallback = std::function<void(PlaylistId, ItemId, TrackId)>;

    explicit BackgroundMusicWindow(
        WorkspaceModel& workspaceModel,
        PlaylistId playlistId,
        QWidget* parent = nullptr);

    void setQueueChangedCallback(QueueChangedCallback callback);

    void setPlaylistEnabledChangedCallback(PlaylistEnabledChangedCallback callback);

    void setItemEnabledChangedCallback(ItemEnabledChangedCallback callback);

    void setPlaylistTrackSwitchRequestedCallback(PlaylistTrackSwitchRequestedCallback callback);

    void setItemTrackSwitchRequestedCallback(ItemTrackSwitchRequestedCallback callback);

    void setPlaybackDisplayState(
        std::optional<PlaylistId> activePlaylistId,
        std::optional<ItemId> activeItemId,
        std::optional<TrackId> playlistTrackId,
        std::optional<TrackId> itemTrackId);

    void refreshFromModel();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    friend class ::MainWindowBackgroundMusicForwardingTestAccess;

    void retranslateUi();
    void refreshPlaylistMusic(
        std::optional<TrackId> preferredTrackId = std::nullopt,
        int fallbackRow = -1);
    void refreshItemList();
    void refreshSelectedItemMusic(
        std::optional<TrackId> preferredTrackId = std::nullopt,
        int fallbackRow = -1);
    void updatePlaylistActionButtons();
    void updateItemActionButtons();
    void updatePlaylistSelectedTrackVolume();
    void updateItemSelectedTrackVolume();
    void updatePlaybackMarkers();
    void updatePlaylistPlaybackMarker();
    void updateItemPlaybackMarker();
    void choosePlaylistMusicTracks();
    void chooseItemMusicTracks();
    void addPlaylistMusicTracks(const QStringList& sources);
    void addItemMusicTracks(const QStringList& sources);
    void removePlaylistMusicTrack();
    void removeItemMusicTrack();
    void movePlaylistMusicTrack(int direction);
    void moveItemMusicTrack(int direction);
    void notifyQueueChanged(std::optional<ItemId> itemId);
    [[nodiscard]] bool selectedItemBelongsToPlaylist() const;

    WorkspaceModel& workspaceModel_;
    PlaylistId playlistId_{};
    std::optional<ItemId> selectedItemId_{};
    QueueChangedCallback queueChangedCallback_{};
    PlaylistEnabledChangedCallback playlistEnabledChangedCallback_{};
    ItemEnabledChangedCallback itemEnabledChangedCallback_{};
    PlaylistTrackSwitchRequestedCallback playlistTrackSwitchRequestedCallback_{};
    ItemTrackSwitchRequestedCallback itemTrackSwitchRequestedCallback_{};
    std::optional<PlaylistId> activePlaylistId_{};
    std::optional<ItemId> activeItemId_{};
    std::optional<TrackId> playlistTrackId_{};
    std::optional<TrackId> itemTrackId_{};

    QWidget* playlistPane_ = nullptr;
    QWidget* itemPane_ = nullptr;
    QSplitter* outerSplitter_ = nullptr;
    QSplitter* itemSplitter_ = nullptr;
    QLabel* playlistHeading_ = nullptr;
    QLabel* playlistModeLabel_ = nullptr;
    QLabel* playlistVolumeRowLabel_ = nullptr;
    QLabel* playlistTracksLabel_ = nullptr;
    QLabel* playlistTrackVolumeRowLabel_ = nullptr;
    QLabel* itemHeading_ = nullptr;
    QLabel* itemListLabel_ = nullptr;
    QLabel* itemModeLabel_ = nullptr;
    QLabel* itemVolumeRowLabel_ = nullptr;
    QLabel* itemTracksLabel_ = nullptr;
    QLabel* itemTrackVolumeRowLabel_ = nullptr;

    QCheckBox* playlistEnabled_ = nullptr;
    QComboBox* playlistMode_ = nullptr;
    QSlider* playlistVolume_ = nullptr;
    QLabel* playlistVolumeLabel_ = nullptr;
    QTreeWidget* playlistTracks_ = nullptr;
    QSlider* playlistTrackVolume_ = nullptr;
    QLabel* playlistTrackVolumeLabel_ = nullptr;
    QPushButton* playlistAddButton_ = nullptr;
    QPushButton* playlistRemoveButton_ = nullptr;
    QPushButton* playlistMoveUpButton_ = nullptr;
    QPushButton* playlistMoveDownButton_ = nullptr;

    QListWidget* itemList_ = nullptr;
    QWidget* itemEditorPane_ = nullptr;
    QLabel* selectedItemHeading_ = nullptr;
    QCheckBox* itemEnabled_ = nullptr;
    QComboBox* itemMode_ = nullptr;
    QSlider* itemVolume_ = nullptr;
    QLabel* itemVolumeLabel_ = nullptr;
    QTreeWidget* itemTracks_ = nullptr;
    QSlider* itemTrackVolume_ = nullptr;
    QLabel* itemTrackVolumeLabel_ = nullptr;
    QPushButton* itemAddButton_ = nullptr;
    QPushButton* itemRemoveButton_ = nullptr;
    QPushButton* itemMoveUpButton_ = nullptr;
    QPushButton* itemMoveDownButton_ = nullptr;
};

}
