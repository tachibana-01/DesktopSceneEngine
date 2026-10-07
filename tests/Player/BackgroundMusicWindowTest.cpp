#include "BackgroundMusicWindow.h"
#include "MainWindowTestDisplay.h"
#include "WorkspaceModel.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QItemSelectionModel>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPushButton>
#include <QSlider>
#include <QTreeWidget>

#include <cstdint>
#include <iostream>
#include <tuple>
#include <utility>
#include <vector>

namespace
{

using DesktopSceneEngine::Player::BackgroundMusicWindow;
using DesktopSceneEngine::Player::ItemId;
using DesktopSceneEngine::Player::MusicPlaybackMode;
using DesktopSceneEngine::Player::PlaylistId;
using DesktopSceneEngine::Player::TrackId;
using DesktopSceneEngine::Player::WorkspaceMediaType;
using DesktopSceneEngine::Player::WorkspaceModel;

constexpr int identityRole = Qt::UserRole;

bool require(bool condition, const char* message)
{
    if (condition)
    {
        return true;
    }

    std::cerr << "Test failed: " << message << std::endl;
    return false;
}

TrackId trackIdFor(const QTreeWidgetItem* item)
{
    return item == nullptr ? TrackId{} : TrackId{item->data(0, identityRole).toUuid()};
}

bool hasOnlySelectedTrack(const QTreeWidget* tree, TrackId expectedId)
{
    const auto selectedItems = tree->selectedItems();
    if (selectedItems.size() != 1 || trackIdFor(selectedItems.constFirst()) != expectedId)
    {
        return false;
    }

    const auto selectedRows = tree->selectionModel()->selectedRows();
    return selectedRows.size() == 1
        && selectedRows.constFirst().row() == tree->indexOfTopLevelItem(selectedItems.constFirst());
}

ItemId itemIdFor(const QListWidgetItem* item)
{
    return item == nullptr ? ItemId{} : ItemId{item->data(identityRole).toUuid()};
}

template<typename Widget>
Widget* requiredChild(QWidget& parent, const char* objectName)
{
    return parent.findChild<Widget*>(QString::fromLatin1(objectName));
}

bool sendTrackDoubleClick(QTreeWidget& tree, QTreeWidgetItem* item, Qt::MouseButton button)
{
    if (item == nullptr)
    {
        return false;
    }

    const QPoint position = tree.visualItemRect(item).center();
    QMouseEvent event{
        QEvent::MouseButtonDblClick,
        QPointF{position},
        QPointF{tree.viewport()->mapToGlobal(position)},
        button,
        button,
        Qt::NoModifier};
    return QCoreApplication::sendEvent(tree.viewport(), &event);
}

bool verifyPlaybackPresentationAndActivation()
{
    WorkspaceModel model{DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayOwner()};
    const auto playlistId = model.selectedPlaylistId();
    const auto firstItemId = model.addItem(
        playlistId,
        WorkspaceMediaType::Image,
        QStringLiteral("C:/Media/first.png"),
        QStringLiteral("First"));
    const auto secondItemId = model.addItem(
        playlistId,
        WorkspaceMediaType::Video,
        QStringLiteral("C:/Media/second.mp4"),
        QStringLiteral("Second"));
    const auto firstPlaylistTrackId = model.addPlaylistMusicTrack(
        playlistId,
        QStringLiteral("C:/Music/duplicate.mp3"),
        QStringLiteral("Playlist A"));
    const auto secondPlaylistTrackId = model.addPlaylistMusicTrack(
        playlistId,
        QStringLiteral("C:/Music/duplicate.mp3"),
        QStringLiteral("Playlist B"));
    const auto firstItemTrackId = firstItemId.has_value()
        ? model.addItemMusicTrack(
            *firstItemId,
            QStringLiteral("C:/Music/item-a.wav"),
            QStringLiteral("Item A"))
        : std::nullopt;
    const auto secondItemTrackId = secondItemId.has_value()
        ? model.addItemMusicTrack(
            *secondItemId,
            QStringLiteral("C:/Music/item-b.wav"),
            QStringLiteral("Item B"))
        : std::nullopt;
    if (!require(firstItemId.has_value() && secondItemId.has_value()
            && firstPlaylistTrackId.has_value() && secondPlaylistTrackId.has_value()
            && firstItemTrackId.has_value() && secondItemTrackId.has_value(),
            "Playback-presentation testing requires stable playlist, Item, and Track identities"))
    {
        return false;
    }

    BackgroundMusicWindow window{model, playlistId};
    window.show();
    QCoreApplication::processEvents();
    auto* playlistTree = requiredChild<QTreeWidget>(window, "playlistMusicTracksTree");
    auto* itemList = requiredChild<QListWidget>(window, "musicItemList");
    auto* itemTree = requiredChild<QTreeWidget>(window, "itemMusicTracksTree");
    if (!require(playlistTree != nullptr && itemList != nullptr && itemTree != nullptr,
            "Playback-presentation testing requires both track trees and the Item selector"))
    {
        return false;
    }

    itemList->setCurrentRow(0);
    playlistTree->setCurrentItem(playlistTree->topLevelItem(0));
    const auto noItemTrack = std::optional<TrackId>{};
    window.setPlaybackDisplayState(
        playlistId,
        *firstItemId,
        *secondPlaylistTrackId,
        *firstItemTrackId);
    if (!require(playlistTree->topLevelItem(0)->text(0) == QStringLiteral("Playlist A")
            && playlistTree->topLevelItem(1)->text(0) == QString::fromUtf8("\xE2\x96\xB6 Playlist B")
            && trackIdFor(playlistTree->currentItem()) == *firstPlaylistTrackId
            && itemTree->topLevelItem(0)->text(0) == QString::fromUtf8("\xE2\x96\xB6 Item A")
            && model.playlistMusicQueue(playlistId)->tracks.at(1).displayAlias
                == QStringLiteral("Playlist B"),
            "Playback markers must follow public TrackId values without changing selection or model aliases"))
    {
        return false;
    }

    window.setPlaybackDisplayState(
        playlistId,
        *firstItemId,
        *firstPlaylistTrackId,
        *firstItemTrackId);
    if (!require(playlistTree->topLevelItem(0)->text(0) == QString::fromUtf8("\xE2\x96\xB6 Playlist A")
            && playlistTree->topLevelItem(1)->text(0) == QStringLiteral("Playlist B")
            && trackIdFor(playlistTree->currentItem()) == *firstPlaylistTrackId,
            "A changed display snapshot must move only the marker and preserve row selection"))
    {
        return false;
    }

    itemList->setCurrentRow(1);
    if (!require(itemTree->topLevelItem(0)->text(0) == QStringLiteral("Item B"),
            "An editor-local inactive Item must not display the active Item marker"))
    {
        return false;
    }
    itemList->setCurrentRow(0);
    if (!require(itemTree->topLevelItem(0)->text(0) == QString::fromUtf8("\xE2\x96\xB6 Item A"),
            "Returning to the active Item must reapply its stored marker immediately"))
    {
        return false;
    }

    window.setPlaybackDisplayState(
        playlistId,
        *firstItemId,
        *secondPlaylistTrackId,
        *firstItemTrackId);
    playlistTree->setCurrentItem(playlistTree->topLevelItem(1));
    auto* playlistMoveUp = requiredChild<QPushButton>(window, "playlistMusicMoveUpButton");
    if (!require(playlistMoveUp != nullptr, "Playback-presentation testing requires playlist reorder control"))
    {
        return false;
    }
    playlistMoveUp->click();
    if (!require(trackIdFor(playlistTree->topLevelItem(0)) == *secondPlaylistTrackId
            && playlistTree->topLevelItem(0)->text(0) == QString::fromUtf8("\xE2\x96\xB6 Playlist B")
            && playlistTree->currentItem() == playlistTree->topLevelItem(0),
            "Rebuilding after reorder must retain the marker and selection through the stable TrackId"))
    {
        return false;
    }

    std::vector<std::pair<PlaylistId, TrackId>> playlistRequests{};
    std::vector<std::tuple<PlaylistId, ItemId, TrackId>> itemRequests{};
    window.setPlaylistTrackSwitchRequestedCallback(
        [&playlistRequests](PlaylistId callbackPlaylistId, TrackId callbackTrackId)
        {
            playlistRequests.emplace_back(callbackPlaylistId, callbackTrackId);
        });
    window.setItemTrackSwitchRequestedCallback(
        [&itemRequests](PlaylistId callbackPlaylistId, ItemId callbackItemId, TrackId callbackTrackId)
        {
            itemRequests.emplace_back(callbackPlaylistId, callbackItemId, callbackTrackId);
        });
    QCoreApplication::processEvents();
    itemList->setCurrentRow(1);
    (void)sendTrackDoubleClick(*playlistTree, playlistTree->topLevelItem(0), Qt::LeftButton);
    if (!require(playlistRequests.size() == 1U
            && playlistRequests.front().first == playlistId
            && playlistRequests.front().second == *secondPlaylistTrackId,
            "Playlist-global activation must remain independent of the editor-local Item selection"))
    {
        return false;
    }
    playlistRequests.clear();
    itemList->setCurrentRow(0);
    (void)sendTrackDoubleClick(*playlistTree, playlistTree->topLevelItem(0), Qt::LeftButton);
    (void)sendTrackDoubleClick(*playlistTree, playlistTree->topLevelItem(0), Qt::RightButton);
    QMouseEvent emptyEvent{
        QEvent::MouseButtonDblClick,
        QPointF{playlistTree->viewport()->rect().bottomRight() - QPoint{2, 2}},
        QPointF{playlistTree->viewport()->mapToGlobal(
            playlistTree->viewport()->rect().bottomRight() - QPoint{2, 2})},
        Qt::LeftButton,
        Qt::LeftButton,
        Qt::NoModifier};
    (void)QCoreApplication::sendEvent(playlistTree->viewport(), &emptyEvent);
    (void)sendTrackDoubleClick(*itemTree, itemTree->topLevelItem(0), Qt::LeftButton);
    if (!require(playlistRequests.size() == 1U
            && playlistRequests.front().first == playlistId
            && playlistRequests.front().second == *secondPlaylistTrackId
            && itemRequests.size() == 1U
            && std::get<0>(itemRequests.front()) == playlistId
            && std::get<1>(itemRequests.front()) == *firstItemId
            && std::get<2>(itemRequests.front()) == *firstItemTrackId,
            "Only left row double-clicks must emit callbacks with exact stable editor identities"))
    {
        return false;
    }

    window.setPlaybackDisplayState({}, {}, noItemTrack, noItemTrack);
    return require(playlistTree->topLevelItem(0)->text(0) == QStringLiteral("Playlist B")
            && itemTree->topLevelItem(0)->text(0) == QStringLiteral("Item A"),
        "Stopped display state must clear both independent markers");
}

}

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);

    if (!verifyPlaybackPresentationAndActivation())
    {
        return 1;
    }

    WorkspaceModel model{DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayOwner()};
    const auto playlistId = model.selectedPlaylistId();
    BackgroundMusicWindow window{model, playlistId};

    auto* playlistTree = requiredChild<QTreeWidget>(window, "playlistMusicTracksTree");
    auto* playlistEnabled = requiredChild<QCheckBox>(window, "playlistMusicEnabledCheckBox");
    auto* playlistMode = requiredChild<QComboBox>(window, "playlistMusicModeCombo");
    auto* playlistVolume = requiredChild<QSlider>(window, "playlistMusicVolumeSlider");
    auto* playlistPercent = requiredChild<QLabel>(window, "playlistMusicVolumeLabel");
    auto* playlistTrackVolume = requiredChild<QSlider>(window, "playlistMusicTrackVolumeSlider");
    auto* playlistTrackPercent = requiredChild<QLabel>(window, "playlistMusicTrackVolumeLabel");
    auto* playlistAdd = requiredChild<QPushButton>(window, "playlistMusicAddButton");
    auto* playlistRemove = requiredChild<QPushButton>(window, "playlistMusicRemoveButton");
    auto* playlistMoveUp = requiredChild<QPushButton>(window, "playlistMusicMoveUpButton");
    auto* playlistMoveDown = requiredChild<QPushButton>(window, "playlistMusicMoveDownButton");
    auto* itemList = requiredChild<QListWidget>(window, "musicItemList");
    auto* itemEditor = requiredChild<QWidget>(window, "itemMusicEditorPane");
    auto* itemTree = requiredChild<QTreeWidget>(window, "itemMusicTracksTree");
    auto* itemEnabled = requiredChild<QCheckBox>(window, "itemMusicEnabledCheckBox");
    auto* itemMode = requiredChild<QComboBox>(window, "itemMusicModeCombo");
    auto* itemVolume = requiredChild<QSlider>(window, "itemMusicVolumeSlider");
    auto* itemPercent = requiredChild<QLabel>(window, "itemMusicVolumeLabel");
    auto* itemTrackVolume = requiredChild<QSlider>(window, "itemMusicTrackVolumeSlider");
    auto* itemTrackPercent = requiredChild<QLabel>(window, "itemMusicTrackVolumeLabel");
    auto* itemRemove = requiredChild<QPushButton>(window, "itemMusicRemoveButton");

    if (!require(playlistTree != nullptr && playlistEnabled != nullptr
            && playlistMode != nullptr && playlistVolume != nullptr
            && playlistPercent != nullptr && playlistTrackVolume != nullptr
            && playlistTrackPercent != nullptr && playlistAdd != nullptr && playlistRemove != nullptr
            && playlistMoveUp != nullptr && playlistMoveDown != nullptr && itemList != nullptr
            && itemEditor != nullptr && itemTree != nullptr && itemEnabled != nullptr
            && itemMode != nullptr
            && itemVolume != nullptr && itemPercent != nullptr && itemTrackVolume != nullptr
            && itemTrackPercent != nullptr && itemRemove != nullptr,
            "Background Music Window must expose its focused control seams")
        || !require(!window.isModal(), "Background Music Window must remain modeless-capable")
        || !require(playlistTree->selectionBehavior() == QAbstractItemView::SelectRows
                && playlistTree->selectionMode() == QAbstractItemView::SingleSelection
                && itemTree->selectionBehavior() == QAbstractItemView::SelectRows
                && itemTree->selectionMode() == QAbstractItemView::SingleSelection,
            "Both music track trees must use native single row selection")
        || !require(playlistAdd->isEnabled() && playlistTree->topLevelItemCount() == 0,
            "Empty playlists must keep the global Add action available")
        || !require(playlistEnabled->isChecked(),
            "A new Playlist must display its authored Enabled default as checked")
        || !require(!playlistRemove->isEnabled() && !playlistMoveUp->isEnabled()
                && !playlistMoveDown->isEnabled(),
            "Empty global queues must disable destructive and reorder actions")
        || !require(itemList->count() == 0 && !itemEditor->isEnabled(),
            "Empty playlists must disable the selected Item editor without disabling global music"))
    {
        return 1;
    }

    const auto imageItemId = model.addItem(
        playlistId,
        WorkspaceMediaType::Image,
        QStringLiteral("C:/Media/image.png"),
        QStringLiteral("Image"));
    const auto videoItemId = model.addItem(
        playlistId,
        WorkspaceMediaType::Video,
        QStringLiteral("C:/Media/video.mp4"),
        QStringLiteral("Video"));
    const auto duplicateFirstId = model.addPlaylistMusicTrack(
        playlistId,
        QStringLiteral("C:/Music/ambient.mp3"));
    const auto duplicateSecondId = model.addPlaylistMusicTrack(
        playlistId,
        QStringLiteral("C:/Music/ambient.mp3"));
    const auto thirdTrackId = model.addPlaylistMusicTrack(
        playlistId,
        QStringLiteral("C:/Music/third.flac"));
    if (!require(imageItemId.has_value() && videoItemId.has_value()
            && duplicateFirstId.has_value() && duplicateSecondId.has_value()
            && thirdTrackId.has_value(),
            "Widget testing requires independently owned Items and playlist music tracks"))
    {
        return 1;
    }

    window.refreshFromModel();
    if (!require(itemList->count() == 2 && itemEditor->isEnabled()
            && itemIdFor(itemList->currentItem()) == *imageItemId,
            "Item rows must store stable ItemId values and establish editor-local selection")
        || !require(itemEnabled->isChecked(),
            "A newly selected Item must display its authored Enabled default as checked")
        || !require(playlistTree->topLevelItemCount() == 3
            && trackIdFor(playlistTree->topLevelItem(0)) == *duplicateFirstId
            && trackIdFor(playlistTree->topLevelItem(1)) == *duplicateSecondId
            && *duplicateFirstId != *duplicateSecondId,
            "Duplicate source paths must render as independent stable TrackId rows")
        || !require(playlistTree->columnCount() == 3
                && playlistTree->topLevelItem(0)->text(2) == QStringLiteral("100%")
                && !playlistTrackVolume->isEnabled(),
            "Track trees must display default TrackId-scoped volume while the editor disables without a selection")
        || !require(playlistTree->topLevelItem(0)->toolTip(1)
                == QStringLiteral("C:/Music/ambient.mp3"),
            "The global track Source column must retain the full path tooltip"))
    {
        return 1;
    }

    std::vector<std::pair<PlaylistId, bool>> playlistEnabledChanges{};
    std::vector<std::tuple<PlaylistId, ItemId, bool>> itemEnabledChanges{};
    window.setPlaylistEnabledChangedCallback(
        [&model, &playlistEnabledChanges](PlaylistId id, bool enabled)
        {
            playlistEnabledChanges.emplace_back(id, enabled);
            (void)model.setPlaylistMusicEnabled(id, enabled);
        });
    window.setItemEnabledChangedCallback(
        [&model, &itemEnabledChanges](PlaylistId playlist, ItemId item, bool enabled)
        {
            itemEnabledChanges.emplace_back(playlist, item, enabled);
            (void)model.setItemMusicEnabled(item, enabled);
        });

    const std::uint64_t enabledRevision = model.authoredRevision();
    playlistEnabled->click();
    if (!require(!playlistEnabled->isChecked()
            && !model.playlistMusicQueue(playlistId)->enabled
            && model.itemMusicQueue(*imageItemId)->enabled
            && playlistEnabledChanges.size() == 1U
            && playlistEnabledChanges.front() == std::pair{playlistId, false}
            && model.authoredRevision() == enabledRevision + 1U,
            "Playlist Enabled must mutate only the exact authored Playlist through its callback"))
    {
        return 1;
    }
    window.refreshFromModel();
    if (!require(!playlistEnabled->isChecked() && playlistEnabledChanges.size() == 1U,
            "Refreshing Playlist Enabled from authored state must not invoke its callback"))
    {
        return 1;
    }

    itemEnabled->click();
    if (!require(!itemEnabled->isChecked()
            && !model.itemMusicQueue(*imageItemId)->enabled
            && !model.playlistMusicQueue(playlistId)->enabled
            && itemEnabledChanges.size() == 1U
            && itemEnabledChanges.front() == std::tuple{playlistId, *imageItemId, false}
            && model.authoredRevision() == enabledRevision + 2U,
            "Item Enabled must mutate only the exact selected authored Item through its callback")
        || !require(playlistMode->isEnabled() && playlistVolume->isEnabled()
                && playlistTree->isEnabled() && playlistAdd->isEnabled()
                && itemMode->isEnabled() && itemVolume->isEnabled()
                && itemTree->isEnabled(),
            "Disabled BGM scopes must keep their configuration editors available"))
    {
        return 1;
    }
    window.refreshFromModel();
    if (!require(!itemEnabled->isChecked() && itemEnabledChanges.size() == 1U,
            "Refreshing Item Enabled from authored state must not invoke its callback"))
    {
        return 1;
    }

    playlistEnabled->click();
    if (!require(model.playlistMusicQueue(playlistId)->enabled
            && !model.itemMusicQueue(*imageItemId)->enabled,
            "Playlist and Item Enabled states must support Playlist-on Item-off independently"))
    {
        return 1;
    }
    itemEnabled->click();
    if (!require(model.playlistMusicQueue(playlistId)->enabled
            && model.itemMusicQueue(*imageItemId)->enabled
            && playlistEnabledChanges.size() == 2U && itemEnabledChanges.size() == 2U
            && model.authoredRevision() == enabledRevision + 4U,
            "Playlist and Item Enabled controls must restore both-on without coupling"))
    {
        return 1;
    }

    playlistTree->setCurrentItem(nullptr);
    if (!require(!playlistRemove->isEnabled() && !playlistMoveUp->isEnabled()
            && !playlistMoveDown->isEnabled() && !playlistTrackVolume->isEnabled()
            && !playlistTrackPercent->isEnabled(),
            "No selected global track must disable Remove and both Move actions"))
    {
        return 1;
    }

    playlistTree->setCurrentItem(playlistTree->topLevelItem(1));
    if (!require(playlistRemove->isEnabled() && playlistMoveUp->isEnabled()
            && playlistMoveDown->isEnabled() && playlistTrackVolume->isEnabled()
            && playlistTrackVolume->value() == 100 && playlistTrackPercent->text() == QStringLiteral("100%")
            && playlistTree->currentItem()->isSelected()
            && hasOnlySelectedTrack(playlistTree, *duplicateSecondId),
            "A middle global track must enable Remove and both adjacent moves"))
    {
        return 1;
    }
    playlistMoveUp->click();
    if (!require(trackIdFor(playlistTree->currentItem()) == *duplicateSecondId
            && trackIdFor(playlistTree->topLevelItem(0)) == *duplicateSecondId
            && hasOnlySelectedTrack(playlistTree, *duplicateSecondId),
            "Moving a duplicate path must preserve selection through its TrackId"))
    {
        return 1;
    }
    playlistRemove->click();
    if (!require(model.playlistMusicQueue(playlistId)->tracks.size() == 2
            && model.playlistMusicQueue(playlistId)->tracks.at(0).id == *duplicateFirstId
            && playlistTrackVolume->isEnabled() && playlistTrackVolume->value() == 100,
            "Removing a selected duplicate row must remove only that TrackId"))
    {
        return 1;
    }
    playlistTree->setCurrentItem(playlistTree->topLevelItem(0));
    playlistTrackVolume->setValue(40);
    if (!require(model.playlistMusicQueue(playlistId)->tracks.at(0).id == *duplicateFirstId
            && model.playlistMusicQueue(playlistId)->tracks.at(0).volume == 0.40
            && model.playlistMusicQueue(playlistId)->tracks.at(1).volume == 1.0
            && model.playlistMusicQueue(playlistId)->volume == 1.0
            && playlistTree->topLevelItem(0)->text(2) == QStringLiteral("40%")
            && playlistTrackPercent->text() == QStringLiteral("40%")
            && hasOnlySelectedTrack(playlistTree, *duplicateFirstId),
            "Selected playlist TrackId volume must update only its display and model entry"))
    {
        return 1;
    }
    playlistMoveDown->click();
    if (!require(trackIdFor(playlistTree->currentItem()) == *duplicateFirstId
            && playlistTree->topLevelItem(1)->text(2) == QStringLiteral("40%")
            && playlistTrackVolume->value() == 40
            && hasOnlySelectedTrack(playlistTree, *duplicateFirstId),
            "Reordering must retain selected TrackId-scoped volume and its display"))
    {
        return 1;
    }

    playlistMode->setCurrentIndex(0);
    playlistVolume->setValue(25);
    if (!require(model.playlistMusicQueue(playlistId)->playbackMode
                == MusicPlaybackMode::CurrentSingleLoop
            && model.playlistMusicQueue(playlistId)->volume == 0.25
            && playlistPercent->text() == QStringLiteral("25%"),
            "Global mode and volume controls must update only the global queue"))
    {
        return 1;
    }

    itemList->setCurrentRow(1);
    if (!require(itemIdFor(itemList->currentItem()) == *videoItemId
            && itemEditor->isEnabled(),
            "Changing the music editor Item selection must use the selected row ItemId"))
    {
        return 1;
    }
    const auto itemTrackId = model.addItemMusicTrack(
        *videoItemId,
        QStringLiteral("C:/Music/item.wav"));
    if (!require(itemTrackId.has_value(), "The selected Item must accept its own music track"))
    {
        return 1;
    }
    window.refreshFromModel();
    if (!require(itemIdFor(itemList->currentItem()) == *videoItemId
            && itemTree->topLevelItemCount() == 1
            && trackIdFor(itemTree->topLevelItem(0)) == *itemTrackId,
            "Refreshing must preserve editor-local Item selection and show only its queue"))
    {
        return 1;
    }

    itemMode->setCurrentIndex(2);
    itemVolume->setValue(80);
    itemTree->setCurrentItem(itemTree->topLevelItem(0));
    if (!require(model.itemMusicQueue(*videoItemId)->playbackMode == MusicPlaybackMode::Shuffle
            && model.itemMusicQueue(*videoItemId)->volume == 0.80
            && model.itemMusicQueue(*imageItemId)->volume == 1.0
            && itemPercent->text() == QStringLiteral("80%")
            && itemRemove->isEnabled() && itemTrackVolume->isEnabled()
            && itemTrackVolume->value() == 100 && itemTrackPercent->text() == QStringLiteral("100%")
            && itemTree->currentItem()->isSelected()
            && hasOnlySelectedTrack(itemTree, *itemTrackId),
            "Item controls must update only the selected Item queue"))
    {
        return 1;
    }

    itemTrackVolume->setValue(70);
    if (!require(model.itemMusicQueue(*videoItemId)->tracks.at(0).volume == 0.70
            && model.itemMusicQueue(*videoItemId)->volume == 0.80
            && model.itemMusicQueue(*imageItemId)->volume == 1.0
            && itemTree->topLevelItem(0)->text(2) == QStringLiteral("70%")
            && itemTrackPercent->text() == QStringLiteral("70%")
            && hasOnlySelectedTrack(itemTree, *itemTrackId),
            "Selected Item TrackId volume must remain independent from queue and sibling Item state"))
    {
        return 1;
    }
    itemTree->setCurrentItem(nullptr);
    if (!require(!itemTrackVolume->isEnabled() && !itemTrackPercent->isEnabled(),
            "No selected Item track must disable its Track Volume editor without changing queue volume"))
    {
        return 1;
    }

    if (!require(model.renameItem(*videoItemId, QStringLiteral("Renamed video")),
            "Stable refresh testing requires an external Item alias mutation"))
    {
        return 1;
    }
    window.refreshFromModel();
    if (!require(itemIdFor(itemList->currentItem()) == *videoItemId,
            "Refreshing after an alias change must preserve local Item selection by ItemId"))
    {
        return 1;
    }
    if (!require(model.removeItem(*videoItemId),
            "Stable refresh testing requires deleting the selected Item"))
    {
        return 1;
    }
    window.refreshFromModel();
    return require(itemList->count() == 1
            && itemIdFor(itemList->currentItem()) == *imageItemId
            && itemEditor->isEnabled(),
        "Deleting the selected Item must deterministically select a valid remaining Item")
        ? 0
        : 1;
}
