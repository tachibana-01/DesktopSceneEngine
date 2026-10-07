#include "BackgroundMusicWindow.h"
#include "BackgroundMusicControlAdapter.h"
#include "MainWindow.h"
#include "MainWindowTestDisplay.h"

#include <QApplication>
#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSlider>
#include <QTreeWidget>
#include <QTimer>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <variant>
#include <vector>

class MainWindowBackgroundMusicForwardingTestAccess
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

    static void setForwarder(
        MainWindow& window,
        std::function<bool(const DesktopSceneEngine::BackgroundMusicControl::Event&)> forwarder
    )
    {
        window.backgroundMusicControlForwarder_ = std::move(forwarder);
    }

    static bool start(
        MainWindow& window,
        DesktopSceneEngine::Player::PlaylistId playlistId,
        std::optional<DesktopSceneEngine::Player::ItemId> itemId
    )
    {
        return window.startPlaylistPlayback(playlistId, itemId);
    }

    static void togglePause(MainWindow& window)
    {
        window.togglePlaybackPause();
    }

    static void synchronize(MainWindow& window)
    {
        window.synchronizeActivePlaybackItem();
    }

    static void stop(MainWindow& window)
    {
        window.stopActivePlayback();
    }

    static void removePlaylistConfirmed(
        MainWindow& window,
        DesktopSceneEngine::Player::PlaylistId playlistId)
    {
        QTimer::singleShot(0, []
        {
            for (QWidget* widget : QApplication::topLevelWidgets())
            {
                auto* messageBox = qobject_cast<QMessageBox*>(widget);
                if (messageBox == nullptr)
                {
                    continue;
                }

                if (QAbstractButton* yes = messageBox->button(QMessageBox::Yes))
                {
                    yes->click();
                    return;
                }
            }
        });
        window.removePlaylist(playlistId);
    }

    static bool hasActiveProjection(const MainWindow& window)
    {
        return window.activePlaybackProjection_.has_value();
    }

    static std::size_t sceneCount(const MainWindow& window)
    {
        return window.engine_.sceneCount();
    }

    static void shutdown(MainWindow& window)
    {
        window.shutdownEngine();
    }

    static bool rebuildToSuccessorAfterActiveItemRemoval(
        MainWindow& window,
        DesktopSceneEngine::Player::ItemId itemId
    )
    {
        return window.rebuildActiveProjection(
                   itemId,
                   MainWindow::ActiveProjectionRebuildMode::StartTargetItem)
            == MainWindow::ActiveProjectionRebuildResult::Stopped;
    }

    static DesktopSceneEngine::Player::BackgroundMusicWindow* openEditor(
        MainWindow& window,
        DesktopSceneEngine::Player::PlaylistId playlistId
    )
    {
        return window.openBackgroundMusicWindow(playlistId);
    }

    static void addPlaylistMusicTracks(
        DesktopSceneEngine::Player::BackgroundMusicWindow& window,
        const QStringList& sources
    )
    {
        window.addPlaylistMusicTracks(sources);
    }

    static void addItemMusicTracks(
        DesktopSceneEngine::Player::BackgroundMusicWindow& window,
        const QStringList& sources
    )
    {
        window.addItemMusicTracks(sources);
    }

    static void seek(MainWindow& window, float seconds)
    {
        (void)window.engine_.seek(seconds);
    }

    static void setSpeed(MainWindow& window, float speed)
    {
        window.engine_.setPlaybackSpeed(speed);
    }

    static void requestPlaylistTrackSwitch(
        MainWindow& window,
        DesktopSceneEngine::Player::PlaylistId playlistId,
        DesktopSceneEngine::Player::TrackId trackId
    )
    {
        window.handlePlaylistTrackSwitchRequested(playlistId, trackId);
    }

    static void requestItemTrackSwitch(
        MainWindow& window,
        DesktopSceneEngine::Player::PlaylistId playlistId,
        DesktopSceneEngine::Player::ItemId itemId,
        DesktopSceneEngine::Player::TrackId trackId
    )
    {
        window.handleItemTrackSwitchRequested(playlistId, itemId, trackId);
    }
};

namespace
{

using DesktopSceneEngine::BackgroundMusicControl::ActivePlaybackScopeChanged;
using DesktopSceneEngine::BackgroundMusicControl::Event;
using DesktopSceneEngine::BackgroundMusicControl::ItemMusicConfigChanged;
using DesktopSceneEngine::BackgroundMusicControl::PlaybackPaused;
using DesktopSceneEngine::BackgroundMusicControl::PlaybackResumed;
using DesktopSceneEngine::BackgroundMusicControl::PlaybackSessionStarted;
using DesktopSceneEngine::BackgroundMusicControl::PlaybackStopped;
using DesktopSceneEngine::BackgroundMusicControl::PlaylistMusicConfigChanged;
using DesktopSceneEngine::BackgroundMusicControl::PlaylistMusicTrackSwitchRequested;
using DesktopSceneEngine::BackgroundMusicControl::ItemMusicTrackSwitchRequested;
using DesktopSceneEngine::Player::ApplicationTranslationState;
using DesktopSceneEngine::Player::BackgroundMusicWindow;
using DesktopSceneEngine::Player::ItemId;
using DesktopSceneEngine::Player::PlaylistId;
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

std::optional<std::filesystem::path> firstImagePath()
{
    const std::filesystem::path imageDirectory{
        DESKTOP_SCENE_ENGINE_TEST_RESOURCES_DIR "/Images"};
    for (const auto& entry : std::filesystem::directory_iterator(imageDirectory))
    {
        if (!entry.is_regular_file())
        {
            continue;
        }

        const std::filesystem::path path = entry.path();
        const std::wstring extension = path.extension().wstring();
        if (extension == L".png" || extension == L".jpg" || extension == L".jpeg")
        {
            return path;
        }
    }

    return std::nullopt;
}

QString pathText(const std::filesystem::path& path)
{
    return QString::fromStdWString(path.wstring());
}

template<typename EventType>
bool hasOnlyEvent(const std::vector<Event>& events)
{
    return events.size() == 1U && std::holds_alternative<EventType>(events.front());
}

template<typename Widget>
Widget* requiredChild(QWidget& parent, const char* objectName)
{
    return parent.findChild<Widget*>(QString::fromLatin1(objectName));
}

bool verifyForwarding(ApplicationTranslationState& translationState)
{
    const auto imagePath = firstImagePath();
    if (!require(imagePath.has_value(), "Forwarding tests require a local image fixture"))
    {
        return false;
    }

    MainWindow window{
        DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
        translationState};
    window.show();
    QCoreApplication::processEvents();
    if (!require(window.initializeEngine(), "Forwarding tests require an initialized Player Engine"))
    {
        return false;
    }

    WorkspaceModel& model = MainWindowBackgroundMusicForwardingTestAccess::workspaceModel(window);
    const PlaylistId playlistA = model.selectedPlaylistId();
    const auto itemA1 = model.addItem(
        playlistA,
        WorkspaceMediaType::Image,
        pathText(*imagePath),
        QStringLiteral("A1"));
    const auto itemA2 = model.addItem(
        playlistA,
        WorkspaceMediaType::Image,
        pathText(*imagePath),
        QStringLiteral("A2"));
    const PlaylistId playlistB = model.addPlaylist();
    const auto itemB1 = model.addItem(
        playlistB,
        WorkspaceMediaType::Image,
        pathText(*imagePath),
        QStringLiteral("B1"));
    if (!require(itemA1.has_value() && itemA2.has_value() && itemB1.has_value(),
            "Forwarding tests require two Items in Playlist A and one in Playlist B"))
    {
        MainWindowBackgroundMusicForwardingTestAccess::shutdown(window);
        return false;
    }

    std::vector<Event> events{};
    MainWindowBackgroundMusicForwardingTestAccess::setForwarder(
        window,
        [&events](const Event& event)
        {
            events.push_back(event);
            return true;
        });

    if (!require(MainWindowBackgroundMusicForwardingTestAccess::start(
            window, playlistA, *itemA1)
            && hasOnlyEvent<PlaybackSessionStarted>(events),
            "Stopped to active playback must emit exactly one SessionStarted control"))
    {
        MainWindowBackgroundMusicForwardingTestAccess::shutdown(window);
        return false;
    }
    events.clear();

    if (!require(MainWindowBackgroundMusicForwardingTestAccess::start(
            window, playlistA, *itemA1)
            && events.empty(),
            "Replaying the same active scope must not restart background music"))
    {
        MainWindowBackgroundMusicForwardingTestAccess::shutdown(window);
        return false;
    }

    if (!require(model.selectItem(*itemA2),
            "Selection isolation requires a non-active selected Item"))
    {
        MainWindowBackgroundMusicForwardingTestAccess::shutdown(window);
        return false;
    }
    MainWindowBackgroundMusicForwardingTestAccess::synchronize(window);
    if (!require(events.empty(),
            "Changing selected Item without changing playback must emit no BGM scope control"))
    {
        MainWindowBackgroundMusicForwardingTestAccess::shutdown(window);
        return false;
    }

    if (!require(MainWindowBackgroundMusicForwardingTestAccess::start(
            window, playlistA, *itemA2)
            && hasOnlyEvent<ActivePlaybackScopeChanged>(events),
            "Item changes within a Playlist must emit exactly one ScopeChanged control"))
    {
        MainWindowBackgroundMusicForwardingTestAccess::shutdown(window);
        return false;
    }
    events.clear();

    if (!require(MainWindowBackgroundMusicForwardingTestAccess::start(
            window, playlistB, *itemB1)
            && hasOnlyEvent<ActivePlaybackScopeChanged>(events),
            "Playlist switches must emit ScopeChanged without a Stop plus Start pair"))
    {
        MainWindowBackgroundMusicForwardingTestAccess::shutdown(window);
        return false;
    }
    events.clear();

    MainWindowBackgroundMusicForwardingTestAccess::togglePause(window);
    if (!require(hasOnlyEvent<PlaybackPaused>(events),
            "An actual playing-to-paused transition must emit PlaybackPaused once"))
    {
        MainWindowBackgroundMusicForwardingTestAccess::shutdown(window);
        return false;
    }
    events.clear();

    MainWindowBackgroundMusicForwardingTestAccess::togglePause(window);
    if (!require(hasOnlyEvent<PlaybackResumed>(events),
            "An actual paused-to-playing transition must emit PlaybackResumed once"))
    {
        MainWindowBackgroundMusicForwardingTestAccess::shutdown(window);
        return false;
    }
    events.clear();

    MainWindowBackgroundMusicForwardingTestAccess::seek(window, 0.25F);
    MainWindowBackgroundMusicForwardingTestAccess::setSpeed(window, 1.5F);
    if (!require(events.empty(), "Seek and speed changes must not emit BGM controls"))
    {
        MainWindowBackgroundMusicForwardingTestAccess::shutdown(window);
        return false;
    }

    if (!require(MainWindowBackgroundMusicForwardingTestAccess::start(
            window, playlistA, *itemA1),
            "Live edit testing requires restoring active Playlist A Item A1"))
    {
        MainWindowBackgroundMusicForwardingTestAccess::shutdown(window);
        return false;
    }
    events.clear();

    BackgroundMusicWindow* const editorA =
        MainWindowBackgroundMusicForwardingTestAccess::openEditor(window, playlistA);
    BackgroundMusicWindow* const editorB =
        MainWindowBackgroundMusicForwardingTestAccess::openEditor(window, playlistB);
    auto* const activePlaylistEnabled = requiredChild<QCheckBox>(
        *editorA, "playlistMusicEnabledCheckBox");
    auto* const activePlaylistVolume = requiredChild<QSlider>(
        *editorA, "playlistMusicVolumeSlider");
    auto* const activePlaylistMode = requiredChild<QComboBox>(
        *editorA, "playlistMusicModeCombo");
    auto* const activePlaylistTracks = requiredChild<QTreeWidget>(
        *editorA, "playlistMusicTracksTree");
    auto* const activePlaylistTrackVolume = requiredChild<QSlider>(
        *editorA, "playlistMusicTrackVolumeSlider");
    auto* const activePlaylistMoveDown = requiredChild<QPushButton>(
        *editorA, "playlistMusicMoveDownButton");
    auto* const activePlaylistRemove = requiredChild<QPushButton>(
        *editorA, "playlistMusicRemoveButton");
    auto* const activeItemList = requiredChild<QListWidget>(*editorA, "musicItemList");
    auto* const activeItemEnabled = requiredChild<QCheckBox>(
        *editorA, "itemMusicEnabledCheckBox");
    auto* const activeItemMode = requiredChild<QComboBox>(*editorA, "itemMusicModeCombo");
    auto* const activeItemVolume = requiredChild<QSlider>(*editorA, "itemMusicVolumeSlider");
    auto* const activeItemTracks = requiredChild<QTreeWidget>(*editorA, "itemMusicTracksTree");
    auto* const activeItemTrackVolume = requiredChild<QSlider>(
        *editorA, "itemMusicTrackVolumeSlider");
    auto* const activeItemMoveDown = requiredChild<QPushButton>(
        *editorA, "itemMusicMoveDownButton");
    auto* const activeItemRemove = requiredChild<QPushButton>(
        *editorA, "itemMusicRemoveButton");
    auto* const inactivePlaylistVolume = requiredChild<QSlider>(
        *editorB, "playlistMusicVolumeSlider");
    auto* const inactivePlaylistEnabled = requiredChild<QCheckBox>(
        *editorB, "playlistMusicEnabledCheckBox");
    if (!require(editorA != nullptr && editorB != nullptr
            && activePlaylistEnabled != nullptr && activePlaylistVolume != nullptr
            && activePlaylistMode != nullptr
            && activePlaylistTracks != nullptr && activePlaylistTrackVolume != nullptr
            && activePlaylistMoveDown != nullptr && activePlaylistRemove != nullptr
            && activeItemList != nullptr && activeItemEnabled != nullptr
            && activeItemMode != nullptr
            && activeItemVolume != nullptr && activeItemTracks != nullptr
            && activeItemTrackVolume != nullptr && activeItemMoveDown != nullptr
            && activeItemRemove != nullptr && inactivePlaylistVolume != nullptr
            && inactivePlaylistEnabled != nullptr,
            "Live edit tests require the existing Background Music editor controls"))
    {
        MainWindowBackgroundMusicForwardingTestAccess::shutdown(window);
        return false;
    }

    const std::uint64_t enabledRevision = model.authoredRevision();
    activePlaylistEnabled->click();
    if (!require(hasOnlyEvent<PlaylistMusicConfigChanged>(events)
            && !std::get<PlaylistMusicConfigChanged>(events.front()).config.enabled
            && !model.playlistMusicQueue(playlistA)->enabled
            && model.authoredRevision() == enabledRevision + 1U,
            "Disabling the active Playlist must mutate authored state and forward one disabled snapshot"))
    {
        MainWindowBackgroundMusicForwardingTestAccess::shutdown(window);
        return false;
    }
    events.clear();

    activePlaylistEnabled->click();
    if (!require(hasOnlyEvent<PlaylistMusicConfigChanged>(events)
            && std::get<PlaylistMusicConfigChanged>(events.front()).config.enabled
            && model.playlistMusicQueue(playlistA)->enabled
            && model.authoredRevision() == enabledRevision + 2U,
            "Enabling the active Playlist must forward one fresh enabled snapshot"))
    {
        MainWindowBackgroundMusicForwardingTestAccess::shutdown(window);
        return false;
    }
    events.clear();

    inactivePlaylistEnabled->click();
    if (!require(events.empty() && !model.playlistMusicQueue(playlistB)->enabled
            && model.playlistMusicQueue(playlistA)->enabled,
            "An inactive Playlist Enabled edit must remain authored-only"))
    {
        MainWindowBackgroundMusicForwardingTestAccess::shutdown(window);
        return false;
    }

    activeItemList->setCurrentRow(0);
    activeItemEnabled->click();
    if (!require(hasOnlyEvent<ItemMusicConfigChanged>(events)
            && !std::get<ItemMusicConfigChanged>(events.front()).config.enabled
            && !model.itemMusicQueue(*itemA1)->enabled,
            "Disabling the active Item must mutate authored state and forward one disabled snapshot"))
    {
        MainWindowBackgroundMusicForwardingTestAccess::shutdown(window);
        return false;
    }
    events.clear();

    activeItemEnabled->click();
    if (!require(hasOnlyEvent<ItemMusicConfigChanged>(events)
            && std::get<ItemMusicConfigChanged>(events.front()).config.enabled
            && model.itemMusicQueue(*itemA1)->enabled,
            "Enabling the active Item must forward one fresh enabled snapshot"))
    {
        MainWindowBackgroundMusicForwardingTestAccess::shutdown(window);
        return false;
    }
    events.clear();

    activeItemList->setCurrentRow(1);
    activeItemEnabled->click();
    if (!require(events.empty() && !model.itemMusicQueue(*itemA2)->enabled
            && model.itemMusicQueue(*itemA1)->enabled,
            "An inactive Item Enabled edit must remain authored-only"))
    {
        MainWindowBackgroundMusicForwardingTestAccess::shutdown(window);
        return false;
    }
    activeItemList->setCurrentRow(0);

    activePlaylistVolume->setValue(55);
    if (!require(hasOnlyEvent<PlaylistMusicConfigChanged>(events),
            "One active Playlist queue mutation must emit one full Playlist config control"))
    {
        MainWindowBackgroundMusicForwardingTestAccess::shutdown(window);
        return false;
    }
    events.clear();

    activePlaylistMode->setCurrentIndex(0);
    if (!require(hasOnlyEvent<PlaylistMusicConfigChanged>(events),
            "An active Playlist mode change must emit exactly one config control"))
    {
        MainWindowBackgroundMusicForwardingTestAccess::shutdown(window);
        return false;
    }
    events.clear();

    MainWindowBackgroundMusicForwardingTestAccess::addPlaylistMusicTracks(
        *editorA,
        {QStringLiteral("C:/Music/first.mp3"), QStringLiteral("C:/Music/second.flac")});
    if (!require(hasOnlyEvent<PlaylistMusicConfigChanged>(events)
            && std::get<PlaylistMusicConfigChanged>(events.front()).config.tracks.size() == 2U,
            "One multi-file add batch must emit one Playlist config with the complete latest queue"))
    {
        MainWindowBackgroundMusicForwardingTestAccess::shutdown(window);
        return false;
    }
    events.clear();

    activePlaylistTracks->setCurrentItem(activePlaylistTracks->topLevelItem(0));
    activePlaylistTrackVolume->setValue(40);
    if (!require(hasOnlyEvent<PlaylistMusicConfigChanged>(events),
            "An active Playlist track-volume change must emit exactly one config control"))
    {
        MainWindowBackgroundMusicForwardingTestAccess::shutdown(window);
        return false;
    }
    events.clear();

    activePlaylistMoveDown->click();
    if (!require(hasOnlyEvent<PlaylistMusicConfigChanged>(events),
            "An active Playlist reorder must emit exactly one config control"))
    {
        MainWindowBackgroundMusicForwardingTestAccess::shutdown(window);
        return false;
    }
    events.clear();

    activePlaylistRemove->click();
    if (!require(hasOnlyEvent<PlaylistMusicConfigChanged>(events),
            "An active Playlist track removal must emit exactly one config control"))
    {
        MainWindowBackgroundMusicForwardingTestAccess::shutdown(window);
        return false;
    }
    events.clear();

    activeItemList->setCurrentRow(0);
    activeItemVolume->setValue(65);
    if (!require(hasOnlyEvent<ItemMusicConfigChanged>(events),
            "One active Item queue mutation must emit one full Item config control"))
    {
        MainWindowBackgroundMusicForwardingTestAccess::shutdown(window);
        return false;
    }
    events.clear();

    activeItemMode->setCurrentIndex(0);
    if (!require(hasOnlyEvent<ItemMusicConfigChanged>(events),
            "An active Item mode change must emit exactly one config control"))
    {
        MainWindowBackgroundMusicForwardingTestAccess::shutdown(window);
        return false;
    }
    events.clear();

    MainWindowBackgroundMusicForwardingTestAccess::addItemMusicTracks(
        *editorA,
        {QStringLiteral("C:/Music/item-first.mp3"), QStringLiteral("C:/Music/item-second.flac")});
    if (!require(hasOnlyEvent<ItemMusicConfigChanged>(events)
            && std::get<ItemMusicConfigChanged>(events.front()).config.tracks.size() == 2U,
            "One active Item multi-file add must emit one complete Item config"))
    {
        MainWindowBackgroundMusicForwardingTestAccess::shutdown(window);
        return false;
    }
    events.clear();

    activeItemTracks->setCurrentItem(activeItemTracks->topLevelItem(0));
    activeItemTrackVolume->setValue(40);
    if (!require(hasOnlyEvent<ItemMusicConfigChanged>(events),
            "An active Item track-volume change must emit exactly one config control"))
    {
        MainWindowBackgroundMusicForwardingTestAccess::shutdown(window);
        return false;
    }
    events.clear();

    activeItemMoveDown->click();
    if (!require(hasOnlyEvent<ItemMusicConfigChanged>(events),
            "An active Item reorder must emit exactly one config control"))
    {
        MainWindowBackgroundMusicForwardingTestAccess::shutdown(window);
        return false;
    }
    events.clear();

    activeItemRemove->click();
    if (!require(hasOnlyEvent<ItemMusicConfigChanged>(events),
            "An active Item track removal must emit exactly one config control"))
    {
        MainWindowBackgroundMusicForwardingTestAccess::shutdown(window);
        return false;
    }
    events.clear();

    activeItemList->setCurrentRow(1);
    activeItemVolume->setValue(75);
    inactivePlaylistVolume->setValue(45);
    if (!require(events.empty(),
            "Inactive Item and Playlist edits must not reach the active Engine session"))
    {
        MainWindowBackgroundMusicForwardingTestAccess::shutdown(window);
        return false;
    }

    MainWindowBackgroundMusicForwardingTestAccess::togglePause(window);
    events.clear();
    activePlaylistEnabled->click();
    if (!require(hasOnlyEvent<PlaylistMusicConfigChanged>(events)
            && !std::get<PlaylistMusicConfigChanged>(events.front()).config.enabled,
            "Paused active playback must forward an Enabled change without a start event"))
    {
        MainWindowBackgroundMusicForwardingTestAccess::shutdown(window);
        return false;
    }
    events.clear();
    activePlaylistEnabled->click();
    if (!require(hasOnlyEvent<PlaylistMusicConfigChanged>(events)
            && std::get<PlaylistMusicConfigChanged>(events.front()).config.enabled,
            "Paused active playback must forward re-enable as one config event"))
    {
        MainWindowBackgroundMusicForwardingTestAccess::shutdown(window);
        return false;
    }
    events.clear();
    activePlaylistVolume->setValue(56);
    if (!require(hasOnlyEvent<PlaylistMusicConfigChanged>(events),
            "Paused active playback must continue forwarding active queue changes"))
    {
        MainWindowBackgroundMusicForwardingTestAccess::shutdown(window);
        return false;
    }
    events.clear();

    MainWindowBackgroundMusicForwardingTestAccess::stop(window);
    if (!require(hasOnlyEvent<PlaybackStopped>(events),
            "Stopping active playback must emit PlaybackStopped once before detachment"))
    {
        MainWindowBackgroundMusicForwardingTestAccess::shutdown(window);
        return false;
    }
    events.clear();

    activePlaylistEnabled->click();
    activePlaylistEnabled->click();
    activePlaylistVolume->setValue(57);
    MainWindowBackgroundMusicForwardingTestAccess::stop(window);
    if (!require(events.empty(),
            "Stopped edits and repeated Stop requests must not emit BGM controls"))
    {
        MainWindowBackgroundMusicForwardingTestAccess::shutdown(window);
        return false;
    }

    if (!require(MainWindowBackgroundMusicForwardingTestAccess::start(
            window, playlistA, *itemA1),
            "Shutdown forwarding requires one final active playback session"))
    {
        MainWindowBackgroundMusicForwardingTestAccess::shutdown(window);
        return false;
    }
    events.clear();
    MainWindowBackgroundMusicForwardingTestAccess::shutdown(window);
    return require(hasOnlyEvent<PlaybackStopped>(events),
        "Shutdown must route active playback through the single central Stop control path");
}

bool verifyFailedActiveSuccessorStopsBackgroundMusic(
    ApplicationTranslationState& translationState)
{
    const auto imagePath = firstImagePath();
    if (!require(imagePath.has_value(), "Failed-successor testing requires a local image fixture"))
    {
        return false;
    }

    MainWindow window{
        DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
        translationState};
    window.show();
    QCoreApplication::processEvents();
    if (!require(window.initializeEngine(), "Failed-successor testing requires an initialized Player Engine"))
    {
        return false;
    }

    WorkspaceModel& model = MainWindowBackgroundMusicForwardingTestAccess::workspaceModel(window);
    const PlaylistId playlistId = model.selectedPlaylistId();
    const auto activeItemId = model.addItem(
        playlistId,
        WorkspaceMediaType::Image,
        pathText(*imagePath),
        QStringLiteral("Active"));
    const auto unavailableSuccessorId = model.addItem(
        playlistId,
        WorkspaceMediaType::Image,
        QStringLiteral("C:/DesktopSceneEngine-A4-7-missing-successor.png"),
        QStringLiteral("Unavailable successor"));
    if (!require(activeItemId.has_value() && unavailableSuccessorId.has_value()
            && MainWindowBackgroundMusicForwardingTestAccess::start(
                window,
                playlistId,
                *activeItemId),
            "Failed-successor testing requires active playback with one unavailable successor"))
    {
        MainWindowBackgroundMusicForwardingTestAccess::shutdown(window);
        return false;
    }

    std::vector<Event> events{};
    MainWindowBackgroundMusicForwardingTestAccess::setForwarder(
        window,
        [&events](const Event& event)
        {
            events.push_back(event);
            return true;
        });

    if (!require(model.removeItem(*activeItemId)
            && MainWindowBackgroundMusicForwardingTestAccess::rebuildToSuccessorAfterActiveItemRemoval(
                window,
                *unavailableSuccessorId),
            "Active Item removal must leave a failed successor rebuild in the stopped state"))
    {
        MainWindowBackgroundMusicForwardingTestAccess::shutdown(window);
        return false;
    }
    MainWindowBackgroundMusicForwardingTestAccess::stop(window);

    const bool emittedOnlyStop = hasOnlyEvent<PlaybackStopped>(events);
    MainWindowBackgroundMusicForwardingTestAccess::shutdown(window);
    return require(
        emittedOnlyStop,
        "A failed active-Item successor rebuild must emit exactly one PlaybackStopped control");
}

bool verifyManualTrackSwitchAuthority(ApplicationTranslationState& translationState)
{
    const auto imagePath = firstImagePath();
    if (!require(imagePath.has_value(), "Manual-switch forwarding requires a local image fixture"))
    {
        return false;
    }

    MainWindow window{
        DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
        translationState};
    window.show();
    QCoreApplication::processEvents();
    if (!require(window.initializeEngine(), "Manual-switch forwarding requires an initialized Player Engine"))
    {
        return false;
    }

    WorkspaceModel& model = MainWindowBackgroundMusicForwardingTestAccess::workspaceModel(window);
    const PlaylistId playlistA = model.selectedPlaylistId();
    const auto itemA1 = model.addItem(
        playlistA,
        WorkspaceMediaType::Image,
        pathText(*imagePath),
        QStringLiteral("A1"));
    const auto itemA2 = model.addItem(
        playlistA,
        WorkspaceMediaType::Image,
        pathText(*imagePath),
        QStringLiteral("A2"));
    const PlaylistId playlistB = model.addPlaylist();
    const auto itemB1 = model.addItem(
        playlistB,
        WorkspaceMediaType::Image,
        pathText(*imagePath),
        QStringLiteral("B1"));
    if (!require(itemA1.has_value() && itemA2.has_value() && itemB1.has_value()
            && MainWindowBackgroundMusicForwardingTestAccess::start(window, playlistA, *itemA1),
            "Manual-switch forwarding requires active Playlist A Item A1 playback"))
    {
        MainWindowBackgroundMusicForwardingTestAccess::shutdown(window);
        return false;
    }

    const auto playlistTrackId = model.addPlaylistMusicTrack(
        playlistA,
        QStringLiteral("C:/Music/duplicate.mp3"));
    const auto duplicatePlaylistTrackId = model.addPlaylistMusicTrack(
        playlistA,
        QStringLiteral("C:/Music/duplicate.mp3"));
    const auto itemA1TrackId = model.addItemMusicTrack(
        *itemA1,
        QStringLiteral("C:/Music/a1.wav"));
    const auto itemA2TrackId = model.addItemMusicTrack(
        *itemA2,
        QStringLiteral("C:/Music/a2.wav"));
    const auto itemB1TrackId = model.addItemMusicTrack(
        *itemB1,
        QStringLiteral("C:/Music/b1.wav"));
    if (!require(playlistTrackId.has_value() && duplicatePlaylistTrackId.has_value()
            && itemA1TrackId.has_value() && itemA2TrackId.has_value() && itemB1TrackId.has_value(),
            "Manual-switch forwarding requires stable TrackIds in active and inactive scopes"))
    {
        MainWindowBackgroundMusicForwardingTestAccess::shutdown(window);
        return false;
    }

    std::vector<Event> events{};
    MainWindowBackgroundMusicForwardingTestAccess::setForwarder(
        window,
        [&events](const Event& event)
        {
            events.push_back(event);
            return true;
        });

    MainWindowBackgroundMusicForwardingTestAccess::requestPlaylistTrackSwitch(
        window,
        playlistA,
        *duplicatePlaylistTrackId);
    const auto* const authoredPlaylistA = model.authoredPlaylist(playlistA);
    const auto publicPlaylistId = authoredPlaylistA != nullptr
        ? DesktopSceneEngine::Player::makeBackgroundMusicPlaylistId(authoredPlaylistA->id)
        : std::nullopt;
    const auto publicPlaylistTrackId = DesktopSceneEngine::Player::makeBackgroundMusicTrackId(
        *duplicatePlaylistTrackId);
    if (!require(events.size() == 1U
            && std::holds_alternative<PlaylistMusicTrackSwitchRequested>(events.front())
            && publicPlaylistId.has_value() && publicPlaylistTrackId.has_value()
            && std::get<PlaylistMusicTrackSwitchRequested>(events.front()).playlistId
                == *publicPlaylistId
            && std::get<PlaylistMusicTrackSwitchRequested>(events.front()).trackId
                == *publicPlaylistTrackId,
            "An active Playlist request must forward exactly the clicked stable TrackId"))
    {
        MainWindowBackgroundMusicForwardingTestAccess::shutdown(window);
        return false;
    }
    events.clear();

    if (!require(model.setPlaylistMusicEnabled(playlistA, false)
            && model.setItemMusicEnabled(*itemA1, false),
            "Disabled manual-switch testing requires both active authored scopes disabled"))
    {
        MainWindowBackgroundMusicForwardingTestAccess::shutdown(window);
        return false;
    }
    const std::uint64_t disabledRevision = model.authoredRevision();
    MainWindowBackgroundMusicForwardingTestAccess::requestPlaylistTrackSwitch(
        window,
        playlistA,
        *playlistTrackId);
    MainWindowBackgroundMusicForwardingTestAccess::requestItemTrackSwitch(
        window,
        playlistA,
        *itemA1,
        *itemA1TrackId);
    if (!require(events.empty()
            && !model.playlistMusicQueue(playlistA)->enabled
            && !model.itemMusicQueue(*itemA1)->enabled
            && model.authoredRevision() == disabledRevision,
            "Disabled active scopes must reject manual Track switches without implicitly enabling"))
    {
        MainWindowBackgroundMusicForwardingTestAccess::shutdown(window);
        return false;
    }
    if (!require(model.setPlaylistMusicEnabled(playlistA, true)
            && model.setItemMusicEnabled(*itemA1, true),
            "Enabled manual-switch regression requires restoring both authored scopes"))
    {
        MainWindowBackgroundMusicForwardingTestAccess::shutdown(window);
        return false;
    }

    MainWindowBackgroundMusicForwardingTestAccess::requestItemTrackSwitch(
        window,
        playlistA,
        *itemA1,
        *itemA1TrackId);
    const auto* const authoredItemA1 = model.authoredItem(*itemA1);
    const auto publicItemA1Id = authoredItemA1 != nullptr
        ? DesktopSceneEngine::Player::makeBackgroundMusicItemId(authoredItemA1->id)
        : std::nullopt;
    const auto publicItemA1TrackId = DesktopSceneEngine::Player::makeBackgroundMusicTrackId(
        *itemA1TrackId);
    if (!require(events.size() == 1U
            && std::holds_alternative<ItemMusicTrackSwitchRequested>(events.front())
            && publicPlaylistId.has_value() && publicItemA1Id.has_value()
            && publicItemA1TrackId.has_value()
            && std::get<ItemMusicTrackSwitchRequested>(events.front()).playlistId
                == *publicPlaylistId
            && std::get<ItemMusicTrackSwitchRequested>(events.front()).itemId == *publicItemA1Id
            && std::get<ItemMusicTrackSwitchRequested>(events.front()).trackId
                == *publicItemA1TrackId,
            "An active Item request must forward exact Playlist, Item, and TrackIds"))
    {
        MainWindowBackgroundMusicForwardingTestAccess::shutdown(window);
        return false;
    }
    events.clear();

    MainWindowBackgroundMusicForwardingTestAccess::requestItemTrackSwitch(
        window,
        playlistA,
        *itemA2,
        *itemA2TrackId);
    MainWindowBackgroundMusicForwardingTestAccess::requestItemTrackSwitch(
        window,
        playlistB,
        *itemB1,
        *itemB1TrackId);
    MainWindowBackgroundMusicForwardingTestAccess::requestPlaylistTrackSwitch(
        window,
        playlistB,
        *playlistTrackId);
    if (!require(events.empty(),
            "Inactive Playlist and Item editor scopes must not forward background-music switches"))
    {
        MainWindowBackgroundMusicForwardingTestAccess::shutdown(window);
        return false;
    }

    MainWindowBackgroundMusicForwardingTestAccess::togglePause(window);
    events.clear();
    MainWindowBackgroundMusicForwardingTestAccess::requestPlaylistTrackSwitch(
        window,
        playlistA,
        *playlistTrackId);
    MainWindowBackgroundMusicForwardingTestAccess::requestItemTrackSwitch(
        window,
        playlistA,
        *itemA1,
        *itemA1TrackId);
    if (!require(events.size() == 2U
            && std::holds_alternative<PlaylistMusicTrackSwitchRequested>(events.at(0))
            && std::holds_alternative<ItemMusicTrackSwitchRequested>(events.at(1)),
            "Paused active playback must continue authorizing matching Playlist and Item switches"))
    {
        MainWindowBackgroundMusicForwardingTestAccess::shutdown(window);
        return false;
    }
    events.clear();

    MainWindowBackgroundMusicForwardingTestAccess::stop(window);
    events.clear();
    MainWindowBackgroundMusicForwardingTestAccess::requestPlaylistTrackSwitch(
        window,
        playlistA,
        *playlistTrackId);
    MainWindowBackgroundMusicForwardingTestAccess::requestItemTrackSwitch(
        window,
        playlistA,
        *itemA1,
        *itemA1TrackId);
    const bool stoppedRejected = events.empty();
    MainWindowBackgroundMusicForwardingTestAccess::shutdown(window);
    return require(stoppedRejected,
        "Stopped playback must reject manual Playlist and Item switch requests");
}

bool verifyFinalPlaylistDeletionStopsActivePlayback(
    ApplicationTranslationState& translationState)
{
    const auto imagePath = firstImagePath();
    if (!require(imagePath.has_value(), "Final Playlist deletion requires a local image fixture"))
    {
        return false;
    }

    MainWindow window{
        DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
        translationState};
    window.show();
    QCoreApplication::processEvents();
    if (!require(window.initializeEngine(),
            "Final Playlist deletion requires an initialized Player Engine"))
    {
        return false;
    }

    WorkspaceModel& model = MainWindowBackgroundMusicForwardingTestAccess::workspaceModel(window);
    const PlaylistId playlistId = model.selectedPlaylistId();
    const auto itemId = model.addItem(
        playlistId,
        WorkspaceMediaType::Image,
        pathText(*imagePath),
        QStringLiteral("Final"));
    std::vector<Event> events{};
    MainWindowBackgroundMusicForwardingTestAccess::setForwarder(
        window,
        [&events](const Event& event)
        {
            events.push_back(event);
            return true;
        });
    if (!require(itemId.has_value()
            && MainWindowBackgroundMusicForwardingTestAccess::start(window, playlistId, itemId),
            "Final Playlist deletion requires active playback"))
    {
        MainWindowBackgroundMusicForwardingTestAccess::shutdown(window);
        return false;
    }

    events.clear();
    MainWindowBackgroundMusicForwardingTestAccess::removePlaylistConfirmed(window, playlistId);
    QCoreApplication::processEvents();
    const bool stopped = events.size() == 1U
        && std::holds_alternative<PlaybackStopped>(events.front())
        && !MainWindowBackgroundMusicForwardingTestAccess::hasActiveProjection(window)
        && MainWindowBackgroundMusicForwardingTestAccess::sceneCount(window) == 0U
        && model.authoredProject().playlists.empty()
        && !model.activePlaylistId().isValid()
        && !model.activeItemId().has_value();
    MainWindowBackgroundMusicForwardingTestAccess::shutdown(window);
    return require(stopped,
        "Deleting the active final Playlist must stop BGM, detach Engine media, and clear playback state");
}

}

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    ApplicationTranslationState translationState{};
    return verifyForwarding(translationState)
            && verifyFinalPlaylistDeletionStopsActivePlayback(translationState)
            && verifyFailedActiveSuccessorStopsBackgroundMusic(translationState)
            && verifyManualTrackSwitchAuthority(translationState)
        ? 0
        : 1;
}
