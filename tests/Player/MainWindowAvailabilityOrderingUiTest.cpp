#include "MainWindow.h"
#include "MainWindowTestDisplay.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QListWidget>
#include <QMenu>
#include <QPalette>
#include <QTimer>

#include <functional>
#include <iostream>
#include <optional>
#include <QStringList>

class MainWindowAvailabilityOrderingUiTestAccess
{
public:
    static DesktopSceneEngine::Player::WorkspaceModel& workspaceModel(MainWindow& window)
    {
        auto& model = window.workspaceModel_;
        if (model.playlists().isEmpty())
        {
            const auto playlistId = model.addPlaylist();
            (void)model.activatePlaylist(playlistId);
        }
        return model;
    }

    static void refreshLibrary(MainWindow& window)
    {
        window.refreshLibrary();
    }

    static QListWidget* playlistList(MainWindow& window)
    {
        return window.playlistList_;
    }

    static QListWidget* itemList(MainWindow& window)
    {
        return window.itemList_;
    }

    static void showPlaylistContextMenu(MainWindow& window, const QPoint& position)
    {
        window.showPlaylistContextMenu(position);
    }

    static void showItemContextMenu(MainWindow& window, const QPoint& position)
    {
        window.showItemContextMenu(position);
    }
};

namespace
{

using DesktopSceneEngine::Player::ApplicationTranslationState;
using DesktopSceneEngine::Player::ItemId;
using DesktopSceneEngine::Player::PlaylistId;
using DesktopSceneEngine::Player::UiLanguage;
using DesktopSceneEngine::Player::WorkspaceMediaType;
using DesktopSceneEngine::Player::WorkspaceModel;

bool require(bool condition, const char* message)
{
    if (condition)
    {
        return true;
    }

    std::cerr << "Test failed: " << message << std::endl;
    return false;
}

QListWidgetItem* listItemFor(QListWidget& list, const QUuid& id)
{
    for (int row = 0; row < list.count(); ++row)
    {
        QListWidgetItem* const item = list.item(row);
        if (item != nullptr && item->data(Qt::UserRole).toUuid() == id)
        {
            return item;
        }
    }

    return nullptr;
}

QAction* actionNamed(QMenu& menu, const QString& text)
{
    for (QAction* const action : menu.actions())
    {
        if (action != nullptr && action->text() == text)
        {
            return action;
        }
    }

    return nullptr;
}

QStringList nonSeparatorActionTexts(const QMenu& menu)
{
    QStringList texts{};
    for (QAction* const action : menu.actions())
    {
        if (action != nullptr && !action->isSeparator())
        {
            texts.append(action->text());
        }
    }

    return texts;
}

template <typename OpenMenu, typename InspectMenu>
bool inspectContextMenu(OpenMenu&& openMenu, InspectMenu&& inspectMenu)
{
    bool inspected = false;
    QTimer::singleShot(0, [&inspected, &inspectMenu]
    {
        auto* const menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (menu != nullptr)
        {
            inspectMenu(*menu);
            inspected = true;
            menu->close();
        }
    });
    openMenu();
    return inspected;
}

template <typename OpenMenu>
bool triggerContextAction(OpenMenu&& openMenu, const QString& actionText)
{
    bool triggered = false;
    const bool inspected = inspectContextMenu(
        std::forward<OpenMenu>(openMenu),
        [&triggered, &actionText](QMenu& menu)
        {
            QAction* const action = actionNamed(menu, actionText);
            if (action != nullptr && action->isEnabled())
            {
                action->trigger();
                triggered = true;
            }
        });
    return inspected && triggered;
}

void prepareForContextMenu(MainWindow& window)
{
    window.resize(720, 480);
    window.show();
    QCoreApplication::processEvents();
}

bool verifyDisabledVisualsAndContextAvailability(
    ApplicationTranslationState& translationState)
{
    MainWindow window{
        DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
        translationState};
    prepareForContextMenu(window);
    WorkspaceModel& model = MainWindowAvailabilityOrderingUiTestAccess::workspaceModel(window);
    const PlaylistId playlistA = model.selectedPlaylistId();
    const auto itemA = model.addItem(
        playlistA,
        WorkspaceMediaType::Image,
        QStringLiteral("A.png"),
        QStringLiteral("A"));
    const auto itemB = model.addItem(
        playlistA,
        WorkspaceMediaType::Image,
        QStringLiteral("B.png"),
        QStringLiteral("B"));
    const PlaylistId playlistB = model.addPlaylist();
    const auto itemC = model.addItem(
        playlistB,
        WorkspaceMediaType::Image,
        QStringLiteral("C.png"),
        QStringLiteral("C"));
    if (!require(
            itemA.has_value() && itemB.has_value() && itemC.has_value()
                && model.setPlaylistEnabled(playlistA, false)
                && model.selectPlaylist(playlistA),
            "Availability UI coverage requires two populated Playlists"))
    {
        return false;
    }

    MainWindowAvailabilityOrderingUiTestAccess::refreshLibrary(window);
    QListWidget* const playlists = MainWindowAvailabilityOrderingUiTestAccess::playlistList(window);
    QListWidget* const items = MainWindowAvailabilityOrderingUiTestAccess::itemList(window);
    QListWidgetItem* const playlistRow = listItemFor(*playlists, playlistA.value);
    QListWidgetItem* const itemRow = listItemFor(*items, itemA->value);
    const QColor disabledText = window.palette().color(QPalette::Disabled, QPalette::Text);
    const bool disabledRowsUsePalette = playlistRow != nullptr && itemRow != nullptr
        && playlistRow->foreground().color() == disabledText
        && itemRow->foreground().color() == disabledText;

    bool playlistActionsCorrect = false;
    if (playlistRow != nullptr)
    {
        playlistActionsCorrect = inspectContextMenu(
            [&window, playlistRow, playlists]
            {
                MainWindowAvailabilityOrderingUiTestAccess::showPlaylistContextMenu(
                    window,
                    playlists->visualItemRect(playlistRow).center());
            },
            [&playlistActionsCorrect](QMenu& menu)
            {
                const QAction* const play = actionNamed(menu, QStringLiteral("Play"));
                const QAction* const enable = actionNamed(menu, QStringLiteral("Enable"));
                const QAction* const moveUp = actionNamed(menu, QStringLiteral("Move Up"));
                const QAction* const moveDown = actionNamed(menu, QStringLiteral("Move Down"));
                playlistActionsCorrect = play != nullptr && enable != nullptr && moveUp != nullptr
                    && moveDown != nullptr && !play->isEnabled() && enable->isEnabled()
                    && !moveUp->isEnabled() && moveDown->isEnabled()
                    && nonSeparatorActionTexts(menu) == QStringList{
                        QStringLiteral("Play"),
                        QStringLiteral("Enable"),
                        QStringLiteral("Move Up"),
                        QStringLiteral("Move Down"),
                        QStringLiteral("Rename"),
                        QStringLiteral("Configure Background Music..."),
                        QStringLiteral("Delete")};
            });
    }

    bool itemActionsCorrect = false;
    if (itemRow != nullptr)
    {
        itemActionsCorrect = inspectContextMenu(
            [&window, itemRow, items]
            {
                MainWindowAvailabilityOrderingUiTestAccess::showItemContextMenu(
                    window,
                    items->visualItemRect(itemRow).center());
            },
            [&itemActionsCorrect](QMenu& menu)
            {
                const QAction* const play = actionNamed(menu, QStringLiteral("Play"));
                const QAction* const disable = actionNamed(menu, QStringLiteral("Disable"));
                const QAction* const moveUp = actionNamed(menu, QStringLiteral("Move Up"));
                const QAction* const moveDown = actionNamed(menu, QStringLiteral("Move Down"));
                itemActionsCorrect = play != nullptr && disable != nullptr && moveUp != nullptr
                    && moveDown != nullptr && !play->isEnabled() && !disable->isEnabled()
                    && !moveUp->isEnabled() && moveDown->isEnabled()
                    && nonSeparatorActionTexts(menu) == QStringList{
                        QStringLiteral("Play"),
                        QStringLiteral("Disable"),
                        QStringLiteral("Move Up"),
                        QStringLiteral("Move Down"),
                        QStringLiteral("Rename"),
                        QStringLiteral("Delete")};
            });
    }

    return require(
        disabledRowsUsePalette && playlistActionsCorrect && itemActionsCorrect,
        "Disabled rows and parent-gated context actions must use the availability UI contract");
}

bool verifyContextActionsUpdateModelAndPreserveSelection(
    ApplicationTranslationState& translationState)
{
    MainWindow window{
        DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
        translationState};
    prepareForContextMenu(window);
    WorkspaceModel& model = MainWindowAvailabilityOrderingUiTestAccess::workspaceModel(window);
    const PlaylistId playlistA = model.selectedPlaylistId();
    const auto itemA = model.addItem(
        playlistA,
        WorkspaceMediaType::Image,
        QStringLiteral("A.png"),
        QStringLiteral("A"));
    const auto itemB = model.addItem(
        playlistA,
        WorkspaceMediaType::Image,
        QStringLiteral("B.png"),
        QStringLiteral("B"));
    const auto itemC = model.addItem(
        playlistA,
        WorkspaceMediaType::Image,
        QStringLiteral("C.png"),
        QStringLiteral("C"));
    const PlaylistId playlistB = model.addPlaylist();
    const auto itemD = model.addItem(
        playlistB,
        WorkspaceMediaType::Image,
        QStringLiteral("D.png"),
        QStringLiteral("D"));
    if (!require(
            itemA.has_value() && itemB.has_value() && itemC.has_value() && itemD.has_value()
                && model.selectPlaylist(playlistA) && model.selectItem(*itemB)
                && model.setPlaylistEnabled(playlistA, false)
                && model.setItemEnabled(*itemB, false),
            "Context action coverage requires a selected Playlist and Item"))
    {
        return false;
    }

    MainWindowAvailabilityOrderingUiTestAccess::refreshLibrary(window);
    QListWidget* const playlists = MainWindowAvailabilityOrderingUiTestAccess::playlistList(window);
    QListWidget* const items = MainWindowAvailabilityOrderingUiTestAccess::itemList(window);
    QListWidgetItem* playlistRow = listItemFor(*playlists, playlistA.value);
    const bool disabledPlaylistMoved = playlistRow != nullptr && triggerContextAction(
        [&window, playlistRow, playlists]
        {
            MainWindowAvailabilityOrderingUiTestAccess::showPlaylistContextMenu(
                window,
                playlists->visualItemRect(playlistRow).center());
        },
        QStringLiteral("Move Down"))
        && model.playlists().size() == 2U && model.playlists().at(1).id == playlistA
        && !model.playlistEnabled(playlistA) && model.selectedPlaylistId() == playlistA;

    MainWindowAvailabilityOrderingUiTestAccess::refreshLibrary(window);
    playlistRow = listItemFor(*playlists, playlistA.value);
    const bool playlistEnabled = playlistRow != nullptr && triggerContextAction(
        [&window, playlistRow, playlists]
        {
            MainWindowAvailabilityOrderingUiTestAccess::showPlaylistContextMenu(
                window,
                playlists->visualItemRect(playlistRow).center());
        },
        QStringLiteral("Enable"))
        && model.playlistEnabled(playlistA) && model.selectedPlaylistId() == playlistA;

    MainWindowAvailabilityOrderingUiTestAccess::refreshLibrary(window);
    QListWidgetItem* itemRow = listItemFor(*items, itemB->value);
    const bool itemEnabled = itemRow != nullptr && triggerContextAction(
        [&window, itemRow, items]
        {
            MainWindowAvailabilityOrderingUiTestAccess::showItemContextMenu(
                window,
                items->visualItemRect(itemRow).center());
        },
        QStringLiteral("Enable"))
        && model.itemEnabled(*itemB) && model.selectedItemId() == itemB;

    if (!model.selectPlaylist(playlistA))
    {
        return false;
    }
    MainWindowAvailabilityOrderingUiTestAccess::refreshLibrary(window);
    itemRow = listItemFor(*items, itemB->value);
    const bool itemMoved = itemRow != nullptr && triggerContextAction(
        [&window, itemRow, items]
        {
            MainWindowAvailabilityOrderingUiTestAccess::showItemContextMenu(
                window,
                items->visualItemRect(itemRow).center());
        },
        QStringLiteral("Move Up"))
        && model.playlist(playlistA) != nullptr
        && model.playlist(playlistA)->items.at(0).id == *itemB
        && model.selectedItemId() == itemB;

    return require(
        disabledPlaylistMoved && playlistEnabled && itemEnabled && itemMoved,
        "Availability and move actions must update only the model and preserve stable selection");
}

bool verifyLastAndSingleMoveBoundaries(ApplicationTranslationState& translationState)
{
    MainWindow window{
        DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
        translationState};
    prepareForContextMenu(window);
    WorkspaceModel& model = MainWindowAvailabilityOrderingUiTestAccess::workspaceModel(window);
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
    if (!require(
            itemA.has_value() && itemB.has_value() && model.selectPlaylist(playlistB),
            "Boundary coverage requires a single-item final Playlist"))
    {
        return false;
    }

    MainWindowAvailabilityOrderingUiTestAccess::refreshLibrary(window);
    QListWidget* const playlists = MainWindowAvailabilityOrderingUiTestAccess::playlistList(window);
    QListWidget* const items = MainWindowAvailabilityOrderingUiTestAccess::itemList(window);
    QListWidgetItem* const playlistRow = listItemFor(*playlists, playlistB.value);
    QListWidgetItem* const itemRow = listItemFor(*items, itemB->value);
    bool lastPlaylistBoundaries = false;
    if (playlistRow != nullptr)
    {
        lastPlaylistBoundaries = inspectContextMenu(
            [&window, playlistRow, playlists]
            {
                MainWindowAvailabilityOrderingUiTestAccess::showPlaylistContextMenu(
                    window,
                    playlists->visualItemRect(playlistRow).center());
            },
            [&lastPlaylistBoundaries](QMenu& menu)
            {
                const QAction* const moveUp = actionNamed(menu, QStringLiteral("Move Up"));
                const QAction* const moveDown = actionNamed(menu, QStringLiteral("Move Down"));
                lastPlaylistBoundaries = moveUp != nullptr && moveDown != nullptr
                    && moveUp->isEnabled() && !moveDown->isEnabled();
            });
    }

    bool singleItemBoundaries = false;
    if (itemRow != nullptr)
    {
        singleItemBoundaries = inspectContextMenu(
            [&window, itemRow, items]
            {
                MainWindowAvailabilityOrderingUiTestAccess::showItemContextMenu(
                    window,
                    items->visualItemRect(itemRow).center());
            },
            [&singleItemBoundaries](QMenu& menu)
            {
                const QAction* const moveUp = actionNamed(menu, QStringLiteral("Move Up"));
                const QAction* const moveDown = actionNamed(menu, QStringLiteral("Move Down"));
                singleItemBoundaries = moveUp != nullptr && moveDown != nullptr
                    && !moveUp->isEnabled() && !moveDown->isEnabled();
            });
    }

    return require(
        lastPlaylistBoundaries && singleItemBoundaries,
        "Move actions must honor last-row and single-item boundaries");
}

bool verifyContextActionsRetranslate(ApplicationTranslationState& translationState)
{
    MainWindow window{
        DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
        translationState};
    prepareForContextMenu(window);
    WorkspaceModel& model = MainWindowAvailabilityOrderingUiTestAccess::workspaceModel(window);
    const PlaylistId playlistId = model.selectedPlaylistId();
    const auto item = model.addItem(
        playlistId,
        WorkspaceMediaType::Image,
        QStringLiteral("A.png"),
        QStringLiteral("A"));
    if (!require(item.has_value(), "Localization coverage requires an Item"))
    {
        return false;
    }

    MainWindowAvailabilityOrderingUiTestAccess::refreshLibrary(window);
    if (!require(
            translationState.setLanguage(UiLanguage::SimplifiedChinese)
                == UiLanguage::SimplifiedChinese,
            "Availability localization coverage requires the generated Chinese catalog"))
    {
        return false;
    }

    QListWidget* const playlists = MainWindowAvailabilityOrderingUiTestAccess::playlistList(window);
    QListWidgetItem* const playlistRow = listItemFor(*playlists, playlistId.value);
    bool translated = false;
    if (playlistRow != nullptr)
    {
        translated = inspectContextMenu(
            [&window, playlistRow, playlists]
            {
                MainWindowAvailabilityOrderingUiTestAccess::showPlaylistContextMenu(
                    window,
                    playlists->visualItemRect(playlistRow).center());
            },
            [&translated](QMenu& menu)
            {
                translated = actionNamed(menu, QString::fromUtf8("\xE5\x81\x9C\xE7\x94\xA8")) != nullptr
                    && actionNamed(menu, QString::fromUtf8("\xE4\xB8\x8A\xE7\xA7\xBB")) != nullptr
                    && actionNamed(menu, QString::fromUtf8("\xE4\xB8\x8B\xE7\xA7\xBB")) != nullptr;
            });
    }
    static_cast<void>(translationState.setLanguage(UiLanguage::English));

    return require(
        translated,
        "Availability and ordering context actions must use the Qt translation catalog");
}

}

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    ApplicationTranslationState translationState{};
    return verifyDisabledVisualsAndContextAvailability(translationState)
            && verifyContextActionsUpdateModelAndPreserveSelection(translationState)
            && verifyLastAndSingleMoveBoundaries(translationState)
            && verifyContextActionsRetranslate(translationState)
        ? 0
        : 1;
}
