#include "MainWindow.h"
#include "MainWindowTestDisplay.h"

#include <QApplication>
#include <QBoxLayout>
#include <QComboBox>
#include <QCoreApplication>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>

#include <functional>
#include <iostream>
#include <optional>
#include <utility>

class MainWindowPlaybackModeUiTestAccess
{
public:
    static const DesktopSceneEngine::Player::WorkspaceModel& rawWorkspaceModel(
        const MainWindow& window)
    {
        return window.workspaceModel_;
    }

    static const DesktopSceneEngine::Player::ProjectDocumentState& documentState(
        const MainWindow& window)
    {
        return window.documentState_;
    }

    static DesktopSceneEngine::Player::WorkspaceModel& workspaceModel(MainWindow& window)
    {
        auto& model = window.workspaceModel_;
        if (model.playlists().isEmpty())
        {
            const auto playlistId = model.addPlaylist();
            (void)model.activatePlaylist(playlistId);
            window.refreshLibrary();
        }
        return model;
    }

    static void refreshLibrary(MainWindow& window)
    {
        window.refreshLibrary();
    }

    static void refreshPlaylistPlaybackModeControl(MainWindow& window)
    {
        window.refreshPlaylistPlaybackModeControl();
    }

    static void refreshItemPlaybackModeControl(MainWindow& window)
    {
        window.refreshItemPlaybackModeControl();
    }

    static void retranslateUi(MainWindow& window)
    {
        window.retranslateUi();
    }

    static void setBackgroundMusicForwarder(
        MainWindow& window,
        std::function<bool(const DesktopSceneEngine::BackgroundMusicControl::Event&)> forwarder
    )
    {
        window.backgroundMusicControlForwarder_ = std::move(forwarder);
    }

    static QComboBox* playlistPlaybackModeCombo(MainWindow& window)
    {
        return window.playlistPlaybackModeCombo_;
    }

    static QComboBox* itemPlaybackModeCombo(MainWindow& window)
    {
        return window.itemPlaybackModeCombo_;
    }

    static QLabel* playlistPlaybackModeLabel(MainWindow& window)
    {
        return window.playlistPlaybackModeLabel_;
    }

    static QLabel* itemPlaybackModeLabel(MainWindow& window)
    {
        return window.itemPlaybackModeLabel_;
    }

    static QListWidget* playlistList(MainWindow& window)
    {
        return window.playlistList_;
    }

    static QListWidget* itemList(MainWindow& window)
    {
        return window.itemList_;
    }

    static QPushButton* addPlaylistButton(MainWindow& window)
    {
        return window.addPlaylistButton_;
    }

    static QPushButton* addMediaButton(MainWindow& window)
    {
        return window.addMediaButton_;
    }

    static void addPlaylist(MainWindow& window)
    {
        window.addPlaylist();
    }
};

namespace
{

using DesktopSceneEngine::Player::ApplicationTranslationState;
using DesktopSceneEngine::Player::ItemId;
using DesktopSceneEngine::Player::PlaybackSequenceMode;
using DesktopSceneEngine::Player::PlaylistId;
using DesktopSceneEngine::Player::UiLanguage;
using DesktopSceneEngine::Player::WorkspaceMediaType;
using DesktopSceneEngine::Player::WorkspaceModel;

bool require(bool condition, const char* message);

bool verifyStartupDocumentIsEmptyAndClean(ApplicationTranslationState& translationState)
{
    MainWindow window{
        DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
        translationState};
    const auto& model = MainWindowPlaybackModeUiTestAccess::rawWorkspaceModel(window);
    const auto& documentState = MainWindowPlaybackModeUiTestAccess::documentState(window);

    return require(model.authoredProject().playlists.empty()
            && model.playlists().isEmpty()
            && !model.selectedPlaylistId().isValid()
            && !model.selectedItemId().has_value()
            && !model.activePlaylistId().isValid()
            && !model.activeItemId().has_value(),
        "MainWindow startup must establish an empty authored document")
        && require(!documentState.currentPath.has_value()
                && !documentState.isDirty(model.authoredRevision()),
            "MainWindow startup document state must be clean and pathless");
}

bool require(bool condition, const char* message)
{
    if (condition)
    {
        return true;
    }

    std::cerr << "Test failed: " << message << std::endl;
    return false;
}

bool verifyPlaybackModeControlsExist(ApplicationTranslationState& translationState)
{
    MainWindow window{
        DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
        translationState};
    QComboBox* const playlistMode = MainWindowPlaybackModeUiTestAccess::
        playlistPlaybackModeCombo(window);
    QComboBox* const itemMode = MainWindowPlaybackModeUiTestAccess::itemPlaybackModeCombo(window);
    QLabel* const playlistLabel = MainWindowPlaybackModeUiTestAccess::
        playlistPlaybackModeLabel(window);
    QLabel* const itemLabel = MainWindowPlaybackModeUiTestAccess::itemPlaybackModeLabel(window);

    if (!require(
            playlistMode != nullptr && itemMode != nullptr
                && playlistLabel != nullptr && itemLabel != nullptr,
            "Playlist and Item playback mode controls must exist"))
    {
        return false;
    }

    const auto hasExpectedItems = [](const QComboBox* combo)
    {
        return combo != nullptr
            && combo->count() == 3
            && combo->itemText(0) == QStringLiteral("List Loop")
            && combo->itemText(1) == QStringLiteral("Random")
            && combo->itemText(2) == QStringLiteral("Repeat Current")
            && combo->itemData(0).toInt()
                == static_cast<int>(PlaybackSequenceMode::ListLoop)
            && combo->itemData(1).toInt()
                == static_cast<int>(PlaybackSequenceMode::Random)
            && combo->itemData(2).toInt()
                == static_cast<int>(PlaybackSequenceMode::RepeatCurrent);
    };

    const auto* playlistLayout = MainWindowPlaybackModeUiTestAccess::playlistList(window)
        ->parentWidget()->layout();
    const auto* itemLayout = MainWindowPlaybackModeUiTestAccess::itemList(window)
        ->parentWidget()->layout();
    const bool playlistPlacement = playlistLayout != nullptr
        && playlistLayout->indexOf(MainWindowPlaybackModeUiTestAccess::playlistList(window))
            < playlistLayout->indexOf(playlistMode->parentWidget())
        && playlistLayout->indexOf(playlistMode->parentWidget())
            < playlistLayout->indexOf(MainWindowPlaybackModeUiTestAccess::addPlaylistButton(window));
    const bool itemPlacement = itemLayout != nullptr
        && itemLayout->indexOf(MainWindowPlaybackModeUiTestAccess::itemList(window))
            < itemLayout->indexOf(itemMode->parentWidget())
        && itemLayout->indexOf(itemMode->parentWidget())
            < itemLayout->indexOf(MainWindowPlaybackModeUiTestAccess::addMediaButton(window));

    return require(
        playlistLabel->text() == QStringLiteral("Playback")
            && itemLabel->text() == QStringLiteral("Playback")
            && hasExpectedItems(playlistMode)
            && hasExpectedItems(itemMode)
            && playlistPlacement
            && itemPlacement,
        "Playback controls must have the expected labels, enum data, and list-row-button order");
}

[[nodiscard]] int indexForMode(const QComboBox& combo, PlaybackSequenceMode mode)
{
    return combo.findData(static_cast<int>(mode));
}

bool verifyDefaultsAndModeWritesPreserveIdentity(
    ApplicationTranslationState& translationState)
{
    MainWindow window{
        DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
        translationState};
    WorkspaceModel& model = MainWindowPlaybackModeUiTestAccess::workspaceModel(window);
    QComboBox* const playlistMode = MainWindowPlaybackModeUiTestAccess::
        playlistPlaybackModeCombo(window);
    QComboBox* const itemMode = MainWindowPlaybackModeUiTestAccess::itemPlaybackModeCombo(window);
    const PlaylistId playlistA = model.selectedPlaylistId();
    const bool defaults = model.playlistPlaybackMode() == PlaybackSequenceMode::RepeatCurrent
        && model.playlistItemPlaybackMode(playlistA) == PlaybackSequenceMode::ListLoop
        && playlistMode->currentIndex()
            == indexForMode(*playlistMode, PlaybackSequenceMode::RepeatCurrent)
        && itemMode->currentIndex()
            == indexForMode(*itemMode, PlaybackSequenceMode::ListLoop);
    const auto itemA = model.addItem(
        playlistA,
        WorkspaceMediaType::Image,
        QStringLiteral("A.png"),
        QStringLiteral("A"));
    const PlaylistId playlistB = model.addPlaylist();
    const auto itemB = model.addItem(
        playlistB,
        WorkspaceMediaType::Image,
        QStringLiteral("B.png"),
        QStringLiteral("B"));
    if (!require(
            itemA.has_value() && itemB.has_value()
                && model.setActiveItem(*itemA)
                && model.selectPlaylist(playlistB)
                && model.setPlaylistItemPlaybackMode(playlistA, PlaybackSequenceMode::ListLoop)
                && model.setPlaylistItemPlaybackMode(playlistB, PlaybackSequenceMode::Random),
            "Mode-write coverage requires distinct active and selected Playlists"))
    {
        return false;
    }

    MainWindowPlaybackModeUiTestAccess::refreshLibrary(window);
    const PlaylistId selectedBefore = model.selectedPlaylistId();
    const PlaylistId activePlaylistBefore = model.activePlaylistId();
    const std::optional<ItemId> activeItemBefore = model.activeItemId();
    int forwardedEvents = 0;
    MainWindowPlaybackModeUiTestAccess::setBackgroundMusicForwarder(
        window,
        [&forwardedEvents](const DesktopSceneEngine::BackgroundMusicControl::Event&)
        {
            ++forwardedEvents;
            return true;
        });

    playlistMode->setCurrentIndex(indexForMode(*playlistMode, PlaybackSequenceMode::Random));
    const bool playlistWrite = model.playlistPlaybackMode() == PlaybackSequenceMode::Random
        && model.selectedPlaylistId() == selectedBefore
        && model.activePlaylistId() == activePlaylistBefore
        && model.activeItemId() == activeItemBefore;

    itemMode->setCurrentIndex(indexForMode(*itemMode, PlaybackSequenceMode::RepeatCurrent));
    const bool inactiveItemWrite = model.playlistItemPlaybackMode(playlistB)
            == PlaybackSequenceMode::RepeatCurrent
        && model.playlistItemPlaybackMode(playlistA) == PlaybackSequenceMode::ListLoop
        && model.selectedPlaylistId() == selectedBefore
        && model.activePlaylistId() == activePlaylistBefore
        && model.activeItemId() == activeItemBefore;

    if (!model.selectPlaylist(playlistA))
    {
        return false;
    }
    MainWindowPlaybackModeUiTestAccess::refreshLibrary(window);
    itemMode->setCurrentIndex(indexForMode(*itemMode, PlaybackSequenceMode::Random));
    const bool activeItemWrite = model.playlistItemPlaybackMode(playlistA)
            == PlaybackSequenceMode::Random
        && model.activePlaylistId() == activePlaylistBefore
        && model.activeItemId() == activeItemBefore;

    return require(
        defaults && playlistWrite && inactiveItemWrite && activeItemWrite && forwardedEvents == 0,
        "Playback-mode writes must update only their model fields without changing identities or forwarding BGM");
}

[[nodiscard]] int rowForPlaylist(const QListWidget& list, PlaylistId id)
{
    for (int row = 0; row < list.count(); ++row)
    {
        const QListWidgetItem* const item = list.item(row);
        if (item != nullptr && item->data(Qt::UserRole).toUuid() == id.value)
        {
            return row;
        }
    }

    return -1;
}

bool verifySelectedPlaylistBindingAndRefreshSafety(
    ApplicationTranslationState& translationState)
{
    MainWindow window{
        DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
        translationState};
    WorkspaceModel& model = MainWindowPlaybackModeUiTestAccess::workspaceModel(window);
    QComboBox* const itemMode = MainWindowPlaybackModeUiTestAccess::itemPlaybackModeCombo(window);
    QComboBox* const playlistMode = MainWindowPlaybackModeUiTestAccess::
        playlistPlaybackModeCombo(window);
    QListWidget* const playlistList = MainWindowPlaybackModeUiTestAccess::playlistList(window);
    const PlaylistId playlistA = model.selectedPlaylistId();
    const auto itemA = model.addItem(
        playlistA,
        WorkspaceMediaType::Image,
        QStringLiteral("A.png"),
        QStringLiteral("A"));
    const PlaylistId playlistB = model.addPlaylist();
    const auto itemB = model.addItem(
        playlistB,
        WorkspaceMediaType::Image,
        QStringLiteral("B.png"),
        QStringLiteral("B"));
    const PlaylistId playlistC = model.addPlaylist();
    const auto itemC = model.addItem(
        playlistC,
        WorkspaceMediaType::Image,
        QStringLiteral("C.png"),
        QStringLiteral("C"));
    if (!require(
            itemA.has_value() && itemB.has_value() && itemC.has_value()
                && model.setPlaylistItemPlaybackMode(playlistA, PlaybackSequenceMode::ListLoop)
                && model.setPlaylistItemPlaybackMode(playlistB, PlaybackSequenceMode::Random)
                && model.setPlaylistItemPlaybackMode(playlistC, PlaybackSequenceMode::RepeatCurrent)
                && model.setActiveItem(*itemA),
            "Selected-vs-active coverage requires three independent Playlist modes"))
    {
        return false;
    }

    MainWindowPlaybackModeUiTestAccess::refreshLibrary(window);
    const int playlistBRow = rowForPlaylist(*playlistList, playlistB);
    const int playlistARow = rowForPlaylist(*playlistList, playlistA);
    if (!require(
            playlistBRow >= 0 && playlistARow >= 0,
            "Playlist selection coverage requires A and B list rows"))
    {
        return false;
    }
    playlistList->setCurrentRow(playlistBRow);
    const bool selectedBinding = model.selectedPlaylistId() == playlistB
        && itemMode->currentIndex()
        == indexForMode(*itemMode, PlaybackSequenceMode::Random);

    playlistList->setCurrentRow(playlistARow);
    const bool selectedPlaylistChange = model.selectedPlaylistId() == playlistA
        && itemMode->currentIndex() == indexForMode(*itemMode, PlaybackSequenceMode::ListLoop);
    playlistList->setCurrentRow(playlistBRow);

    if (!model.setActiveItem(*itemC))
    {
        return false;
    }
    MainWindowPlaybackModeUiTestAccess::refreshLibrary(window);
    const bool activeChangeDoesNotHijackSelection = model.selectedPlaylistId() == playlistB
        && itemMode->currentIndex() == indexForMode(*itemMode, PlaybackSequenceMode::Random);

    int playlistSignals = 0;
    int itemSignals = 0;
    const auto playlistConnection = QObject::connect(
        playlistMode,
        QOverload<int>::of(&QComboBox::currentIndexChanged),
        [&playlistSignals](int)
        {
            ++playlistSignals;
        });
    const auto itemConnection = QObject::connect(
        itemMode,
        QOverload<int>::of(&QComboBox::currentIndexChanged),
        [&itemSignals](int)
        {
            ++itemSignals;
        });
    if (!model.setPlaylistPlaybackMode(PlaybackSequenceMode::ListLoop)
        || !model.setPlaylistItemPlaybackMode(playlistB, PlaybackSequenceMode::RepeatCurrent))
    {
        return false;
    }
    MainWindowPlaybackModeUiTestAccess::refreshPlaylistPlaybackModeControl(window);
    MainWindowPlaybackModeUiTestAccess::refreshItemPlaybackModeControl(window);
    QObject::disconnect(playlistConnection);
    QObject::disconnect(itemConnection);
    const bool programmaticRefreshIsBlocked = playlistSignals == 0
        && itemSignals == 0
        && model.playlistPlaybackMode() == PlaybackSequenceMode::ListLoop
        && model.playlistItemPlaybackMode(playlistB) == PlaybackSequenceMode::RepeatCurrent;

    MainWindowPlaybackModeUiTestAccess::addPlaylist(window);
    const PlaylistId addedPlaylist = model.selectedPlaylistId();
    const bool addRefreshesDefault = addedPlaylist.isValid()
        && model.playlistItemPlaybackMode(addedPlaylist) == PlaybackSequenceMode::ListLoop
        && itemMode->currentIndex() == indexForMode(*itemMode, PlaybackSequenceMode::ListLoop);

    if (!model.removePlaylist(addedPlaylist))
    {
        return false;
    }
    MainWindowPlaybackModeUiTestAccess::refreshLibrary(window);
    const bool deletionRefreshesNeighbor = model.selectedPlaylistId() == playlistC
        && itemMode->currentIndex() == indexForMode(*itemMode, PlaybackSequenceMode::RepeatCurrent);

    return require(
        selectedBinding && selectedPlaylistChange && activeChangeDoesNotHijackSelection
            && programmaticRefreshIsBlocked && addRefreshesDefault && deletionRefreshesNeighbor,
        "Item playback control must follow selected Playlist through active changes, refreshes, add, and deletion");
}

bool verifyPlaybackModeOptionsRetranslateWithoutChangingModes(
    ApplicationTranslationState& translationState)
{
    MainWindow window{
        DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
        translationState};
    WorkspaceModel& model = MainWindowPlaybackModeUiTestAccess::workspaceModel(window);
    QComboBox* const playlistMode = MainWindowPlaybackModeUiTestAccess::
        playlistPlaybackModeCombo(window);
    QComboBox* const itemMode = MainWindowPlaybackModeUiTestAccess::itemPlaybackModeCombo(window);
    playlistMode->setCurrentIndex(indexForMode(*playlistMode, PlaybackSequenceMode::Random));
    itemMode->setCurrentIndex(indexForMode(*itemMode, PlaybackSequenceMode::RepeatCurrent));
    const int playlistModeIndex = playlistMode->currentIndex();
    const int itemModeIndex = itemMode->currentIndex();

    if (!require(
            translationState.setLanguage(UiLanguage::SimplifiedChinese)
                == UiLanguage::SimplifiedChinese,
            "Playback mode localization coverage requires the generated Chinese catalog"))
    {
        return false;
    }

    MainWindowPlaybackModeUiTestAccess::retranslateUi(window);
    const bool translated = MainWindowPlaybackModeUiTestAccess::playlistPlaybackModeLabel(window)
            ->text() == QString::fromUtf8("\xE6\x92\xAD\xE6\x94\xBE\xE6\x96\xB9\xE5\xBC\x8F")
        && MainWindowPlaybackModeUiTestAccess::itemPlaybackModeLabel(window)
                ->text() == QString::fromUtf8("\xE6\x92\xAD\xE6\x94\xBE\xE6\x96\xB9\xE5\xBC\x8F")
        && playlistMode->itemText(0) == QString::fromUtf8("\xE5\x88\x97\xE8\xA1\xA8\xE5\xBE\xAA\xE7\x8E\xAF")
        && playlistMode->itemText(1) == QString::fromUtf8("\xE9\x9A\x8F\xE6\x9C\xBA")
        && playlistMode->itemText(2) == QString::fromUtf8("\xE9\x87\x8D\xE5\xA4\x8D\xE5\xBD\x93\xE5\x89\x8D")
        && itemMode->itemText(0) == QString::fromUtf8("\xE5\x88\x97\xE8\xA1\xA8\xE5\xBE\xAA\xE7\x8E\xAF")
        && itemMode->itemText(1) == QString::fromUtf8("\xE9\x9A\x8F\xE6\x9C\xBA")
        && itemMode->itemText(2) == QString::fromUtf8("\xE9\x87\x8D\xE5\xA4\x8D\xE5\xBD\x93\xE5\x89\x8D")
        && playlistMode->currentIndex() == playlistModeIndex
        && itemMode->currentIndex() == itemModeIndex
        && model.playlistPlaybackMode() == PlaybackSequenceMode::Random
        && model.playlistItemPlaybackMode(model.selectedPlaylistId())
            == PlaybackSequenceMode::RepeatCurrent;
    static_cast<void>(translationState.setLanguage(UiLanguage::English));
    MainWindowPlaybackModeUiTestAccess::retranslateUi(window);

    return require(
        translated,
        "Chinese retranslation must localize both playback mode ComboBoxes without changing modes");
}

}

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    ApplicationTranslationState translationState{};
    return verifyStartupDocumentIsEmptyAndClean(translationState)
            && verifyPlaybackModeControlsExist(translationState)
            && verifyDefaultsAndModeWritesPreserveIdentity(translationState)
            && verifySelectedPlaylistBindingAndRefreshSafety(translationState)
            && verifyPlaybackModeOptionsRetranslateWithoutChangingModes(translationState)
        ? 0
        : 1;
}
