#include "MainWindow.h"
#include "MainWindowTestDisplay.h"

#include <QApplication>
#include <QCoreApplication>
#include <QAbstractButton>
#include <QFontMetrics>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QWidget>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <iostream>
#include <random>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

class MainWindowPlaybackSequenceTestAccess
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

    static DesktopSceneEngine::Player::WorkspaceModel& rawWorkspaceModel(MainWindow& window)
    {
        return window.workspaceModel_;
    }

    static bool start(
        MainWindow& window,
        DesktopSceneEngine::Player::PlaylistId playlistId,
        DesktopSceneEngine::Player::ItemId itemId
    )
    {
        return window.startPlaylistPlayback(playlistId, itemId);
    }

    static bool startPlaylist(
        MainWindow& window,
        DesktopSceneEngine::Player::PlaylistId playlistId
    )
    {
        return window.startPlaylistPlayback(playlistId, std::nullopt);
    }

    static MainWindow::PlaybackSequence::Collections collections(const MainWindow& window)
    {
        return window.playbackSequenceCollections();
    }

    static bool setPlaylistAvailability(
        MainWindow& window,
        DesktopSceneEngine::Player::PlaylistId playlistId,
        bool enabled
    )
    {
        return window.setPlaylistAvailability(playlistId, enabled);
    }

    static bool setItemAvailability(
        MainWindow& window,
        DesktopSceneEngine::Player::ItemId itemId,
        bool enabled
    )
    {
        return window.setItemAvailability(itemId, enabled);
    }

    static void updateToNaturalCompletion(MainWindow& window, float deltaTime)
    {
        window.engine_.update(deltaTime);
        window.onTick();
    }

    static void setForwarder(
        MainWindow& window,
        std::function<bool(const DesktopSceneEngine::BackgroundMusicControl::Event&)> forwarder
    )
    {
        window.backgroundMusicControlForwarder_ = std::move(forwarder);
    }

    static void navigate(MainWindow& window, bool previous)
    {
        window.navigateScene(previous);
    }

    static void stop(MainWindow& window)
    {
        window.stopActivePlayback();
    }

    static void refreshLibrary(MainWindow& window)
    {
        window.refreshLibrary();
    }

    static DesktopSceneEngine::Player::PlaylistId volumeTarget(const MainWindow& window)
    {
        return window.volumeControlPlaylistId();
    }

    static QWidget* volumeCluster(MainWindow& window)
    {
        return window.findChild<QWidget*>(QStringLiteral("volumeCluster"));
    }

    static QLabel* activePlaylistNameLabel(MainWindow& window)
    {
        return window.findChild<QLabel*>(QStringLiteral("activePlaylistNameLabel"));
    }

    static int volumeValue(const MainWindow& window)
    {
        return window.volumeSlider_ != nullptr ? window.volumeSlider_->value() : -1;
    }

    static bool volumeClusterChildrenVisible(const MainWindow& window)
    {
        return window.volumeLabel_ != nullptr && window.volumeLabel_->isVisible()
            && window.volumeSlider_ != nullptr && window.volumeSlider_->isVisible()
            && window.muteButton_ != nullptr && window.muteButton_->isVisible();
    }

    static bool hasSequencePosition(const MainWindow& window)
    {
        return window.playbackSequence_.current().has_value();
    }

    static void forceOuterRandomIndex(MainWindow& window, std::size_t index)
    {
        using Controller = DesktopSceneEngine::Player::PlaybackSequenceController<
            DesktopSceneEngine::Player::PlaylistId,
            DesktopSceneEngine::Player::ItemId>;
        Controller::PlaylistIndexSampler sampler = [index](
            std::size_t count,
            std::mt19937_64&)
        {
            return index % count;
        };
        window.playbackSequence_ = Controller{42U, std::move(sampler)};
        window.rebasePlaybackSequenceToActivePosition();
    }

    static void tick(MainWindow& window)
    {
        window.onTick();
    }

    static void pause(MainWindow& window)
    {
        window.engine_.togglePause();
    }

    static bool isPaused(const MainWindow& window)
    {
        return window.engine_.isPaused();
    }

    static const std::optional<DesktopSceneEngine::Player::ActivePlaybackProjection>&
    activeProjection(const MainWindow& window)
    {
        return window.activePlaybackProjection_;
    }

    static std::size_t activeSceneIndex(const MainWindow& window)
    {
        return window.engine_.activeSceneIndex();
    }

    static void stopAutomaticTicks(MainWindow& window)
    {
        window.tickTimer_.stop();
    }

    static std::size_t sceneCount(const MainWindow& window)
    {
        return window.engine_.sceneCount();
    }

    static float playbackTime(const MainWindow& window)
    {
        return window.engine_.playbackTime();
    }

    static float playbackSpeed(const MainWindow& window)
    {
        return window.engine_.playbackSpeed();
    }

    static bool seek(MainWindow& window, float time)
    {
        return window.engine_.seek(time);
    }

    static void setPlaybackSpeed(MainWindow& window, float speed)
    {
        window.engine_.setPlaybackSpeed(speed);
    }

    static bool removeItem(
        MainWindow& window,
        DesktopSceneEngine::Player::ItemId id,
        QMessageBox::StandardButton response = QMessageBox::Yes,
        int expectedWarnings = 0)
    {
        int confirmations = 0;
        int warnings = 0;
        QTimer dialogTimer;
        QObject::connect(&dialogTimer, &QTimer::timeout, [&]
        {
            for (QWidget* widget : QApplication::topLevelWidgets())
            {
                auto* messageBox = qobject_cast<QMessageBox*>(widget);
                if (messageBox == nullptr || messageBox->parentWidget() != &window)
                {
                    continue;
                }

                if (QAbstractButton* button = messageBox->button(response))
                {
                    ++confirmations;
                    button->click();
                }
                else
                {
                    ++warnings;
                    messageBox->accept();
                }
                return;
            }
        });
        dialogTimer.start(0);
        window.removeItem(id);
        dialogTimer.stop();
        return confirmations == 1 && warnings == expectedWarnings;
    }

    static bool removePlaylist(
        MainWindow& window,
        DesktopSceneEngine::Player::PlaylistId id)
    {
        int confirmations = 0;
        QTimer dialogTimer;
        QObject::connect(&dialogTimer, &QTimer::timeout, [&]
        {
            for (QWidget* widget : QApplication::topLevelWidgets())
            {
                auto* messageBox = qobject_cast<QMessageBox*>(widget);
                if (messageBox == nullptr || messageBox->parentWidget() != &window)
                {
                    continue;
                }

                if (QAbstractButton* button = messageBox->button(QMessageBox::Yes))
                {
                    ++confirmations;
                    button->click();
                }
                return;
            }
        });
        dialogTimer.start(0);
        window.removePlaylist(id);
        dialogTimer.stop();
        return confirmations == 1;
    }

    static void shutdown(MainWindow& window)
    {
        window.shutdownEngine();
    }
};

namespace
{

using DesktopSceneEngine::Player::ApplicationTranslationState;
using DesktopSceneEngine::Player::ActivePlaybackProjection;
using DesktopSceneEngine::Player::ItemId;
using DesktopSceneEngine::Player::PlaybackSequenceMode;
using DesktopSceneEngine::Player::PlaylistId;
using DesktopSceneEngine::Player::WorkspaceMediaType;
using DesktopSceneEngine::Player::WorkspaceModel;
using DesktopSceneEngine::BackgroundMusicControl::ActivePlaybackScopeChanged;
using DesktopSceneEngine::BackgroundMusicControl::Event;
using DesktopSceneEngine::BackgroundMusicControl::PlaybackStopped;

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

using DeletionAccess = MainWindowPlaybackSequenceTestAccess;

bool initializeDeletionWindow(MainWindow& window)
{
    window.show();
    QCoreApplication::processEvents();
    const bool initialized = window.initializeEngine();
    DeletionAccess::stopAutomaticTicks(window);
    return require(initialized, "Deletion setup requires an initialized native-window Player Engine");
}

bool hasActiveBinding(const MainWindow& window, const WorkspaceModel& model, ItemId id)
{
    const auto& projection = DeletionAccess::activeProjection(window);
    return model.activeItemId() == id
        && projection.has_value()
        && model.activePlaylistId() == projection->sourcePlaylistId
        && DeletionAccess::sceneCount(window) == projection->project.scenes.size()
        && DeletionAccess::sceneCount(window) > 0U
        && projection->itemForSceneIndex(DeletionAccess::activeSceneIndex(window)) == id;
}

bool hasStoppedBinding(const MainWindow& window, const WorkspaceModel& model)
{
    return !model.activeItemId().has_value()
        && !model.activePlaylistId().isValid()
        && !DeletionAccess::activeProjection(window).has_value()
        && !DeletionAccess::hasSequencePosition(window)
        && DeletionAccess::sceneCount(window) == 0U
        && DeletionAccess::playbackTime(window) == 0.0F;
}

enum class DeletionCase { Availability, Reordered, NoPlayableReplacement };

bool verifyDeletionSuccessor(
    ApplicationTranslationState& translationState, DeletionCase testCase)
{
    const auto imagePath = firstImagePath();
    if (!require(imagePath.has_value(), "Deletion setup requires a local decodable image fixture"))
    {
        return false;
    }

    MainWindow window{
        DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(), translationState};
    if (!initializeDeletionWindow(window))
    {
        return false;
    }
    WorkspaceModel& model = DeletionAccess::workspaceModel(window);
    const PlaylistId playlist = model.selectedPlaylistId();
    const auto a = model.addItem(playlist, WorkspaceMediaType::Image, pathText(*imagePath), QStringLiteral("A"));
    const auto b = model.addItem(playlist, WorkspaceMediaType::Image, pathText(*imagePath), QStringLiteral("B"));
    const auto c = testCase == DeletionCase::NoPlayableReplacement
        ? std::optional<ItemId>{}
        : model.addItem(playlist, WorkspaceMediaType::Image, pathText(*imagePath), QStringLiteral("C"));
    if (!require(a.has_value() && b.has_value()
            && (testCase == DeletionCase::NoPlayableReplacement || c.has_value()),
            "Deletion setup requires the authored Items"))
    {
        DeletionAccess::shutdown(window);
        return false;
    }
    const ItemId removed = testCase == DeletionCase::Reordered ? *b : *a;
    const bool setup = (testCase == DeletionCase::Reordered || model.setItemEnabled(*b, false))
        && DeletionAccess::start(window, playlist, removed)
        && hasActiveBinding(window, model, removed)
        && DeletionAccess::volumeCluster(window) != nullptr
        && DeletionAccess::volumeCluster(window)->isVisible()
        && DeletionAccess::activePlaylistNameLabel(window) != nullptr
        && DeletionAccess::activePlaylistNameLabel(window)->text()
            == model.playlist(playlist)->displayAlias;
    if (!require(setup, "Deletion precondition: valid media must bind and the intended Item must be active"))
    {
        DeletionAccess::shutdown(window);
        return false;
    }
    if (testCase == DeletionCase::Reordered
        && !require(model.moveItem(*b, -1)
                && model.playlist(playlist)->items.front().id == *b
                && model.playlist(playlist)->items[1].id == *a
                && DeletionAccess::activeSceneIndex(window) == 1U
                && hasActiveBinding(window, model, *b),
                "Reorder precondition: authored [B,A,C] must coexist with the original B runtime binding at scene index 1"))
    {
        DeletionAccess::shutdown(window);
        return false;
    }
    std::cout << "Deletion precondition PASS: "
              << (testCase == DeletionCase::Reordered ? "[B,A,C], bound B at runtime index 1"
                  : testCase == DeletionCase::Availability ? "[A enabled,B disabled,C enabled], bound A"
                  : "[A enabled,B disabled], bound A") << std::endl;
    const auto revision = model.authoredRevision();
    const bool dialog = DeletionAccess::removeItem(window, removed);
    const bool mutation = dialog && model.item(removed) == nullptr
        && model.authoredRevision() == revision + 1U;
    bool result = false;
    if (testCase == DeletionCase::NoPlayableReplacement)
    {
        result = require(mutation && model.item(*b) != nullptr && !model.item(*b)->enabled
                && hasStoppedBinding(window, model)
                && DeletionAccess::volumeCluster(window) != nullptr
                && !DeletionAccess::volumeCluster(window)->isVisible(),
            "No-playable deletion must retain disabled B authored but clear active Item/Playlist, projection, sequence and Engine binding");
    }
    else
    {
        const ItemId expected = testCase == DeletionCase::Reordered ? *a : *c;
        result = require(mutation && hasActiveBinding(window, model, expected)
                && DeletionAccess::volumeCluster(window) != nullptr
                && DeletionAccess::volumeCluster(window)->isVisible()
                && DeletionAccess::activePlaylistNameLabel(window) != nullptr
                && DeletionAccess::activePlaylistNameLabel(window)->text()
                    == model.playlist(playlist)->displayAlias,
            testCase == DeletionCase::Reordered
                ? "Reordered deletion must activate A from current [B,A,C], not C from the old Engine scene index"
                : "Availability deletion must activate C and never activate disabled B");
    }
    if (!result)
    {
        const auto active = model.activeItemId();
        std::cerr << "Actual deletion result: active="
                  << (active == a ? "A" : active == b ? "B" : active == c && c.has_value() ? "C" : "none")
                  << ", Engine scenes=" << DeletionAccess::sceneCount(window) << std::endl;
    }
    DeletionAccess::shutdown(window);
    return result;
}

bool verifyActivePlaybackVolumeCluster(ApplicationTranslationState& translationState)
{
    const auto imagePath = firstImagePath();
    if (!require(imagePath.has_value(), "Volume-cluster coverage requires a local image fixture"))
    {
        return false;
    }

    MainWindow window{
        DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
        translationState};
    window.show();
    QCoreApplication::processEvents();
    if (!require(window.initializeEngine(), "Volume-cluster coverage requires an initialized Player Engine"))
    {
        return false;
    }
    MainWindowPlaybackSequenceTestAccess::stopAutomaticTicks(window);

    WorkspaceModel& model = MainWindowPlaybackSequenceTestAccess::rawWorkspaceModel(window);
    const PlaylistId playlistA = model.addPlaylist();
    const PlaylistId playlistB = model.addPlaylist();
    const auto itemA = model.addItem(
        playlistA,
        WorkspaceMediaType::Image,
        pathText(*imagePath),
        QStringLiteral("A"));
    const auto itemB = model.addItem(
        playlistB,
        WorkspaceMediaType::Image,
        pathText(*imagePath),
        QStringLiteral("B"));
    bool passed = require(itemA.has_value() && itemB.has_value()
            && model.renamePlaylist(playlistA, QStringLiteral("Playlist A"))
            && model.renamePlaylist(playlistB, QStringLiteral("Playlist B"))
            && model.setPlaylistMasterVolume(playlistA, 0.25)
            && model.setPlaylistMasterVolume(playlistB, 0.75)
            && model.selectPlaylist(playlistA),
        "Volume-cluster setup requires two named playable Playlists");
    MainWindowPlaybackSequenceTestAccess::refreshLibrary(window);
    QCoreApplication::processEvents();

    auto* cluster = MainWindowPlaybackSequenceTestAccess::volumeCluster(window);
    auto* name = MainWindowPlaybackSequenceTestAccess::activePlaylistNameLabel(window);
    passed = require(cluster != nullptr && name != nullptr,
        "The transport must expose one logical volume cluster and active-Playlist name") && passed;
    passed = require(cluster != nullptr && !cluster->isVisible()
            && !MainWindowPlaybackSequenceTestAccess::volumeTarget(window).isValid(),
        "Create/select without playback must keep the cluster hidden and leave no bottom volume target") && passed;

    passed = require(MainWindowPlaybackSequenceTestAccess::start(window, playlistA, *itemA),
        "Volume-cluster setup must start Playlist A") && passed;
    QCoreApplication::processEvents();
    passed = require(cluster != nullptr && cluster->isVisible()
            && MainWindowPlaybackSequenceTestAccess::volumeClusterChildrenVisible(window)
            && name != nullptr && name->text() == QStringLiteral("Playlist A")
            && MainWindowPlaybackSequenceTestAccess::volumeValue(window) == 25
            && MainWindowPlaybackSequenceTestAccess::volumeTarget(window) == playlistA,
        "Starting A must show one complete cluster targeting and naming A") && passed;

    MainWindowPlaybackSequenceTestAccess::pause(window);
    passed = require(MainWindowPlaybackSequenceTestAccess::isPaused(window)
            && cluster != nullptr && cluster->isVisible()
            && name != nullptr && name->text() == QStringLiteral("Playlist A"),
        "Paused playback must retain A's visible volume context") && passed;
    MainWindowPlaybackSequenceTestAccess::pause(window);
    passed = require(!MainWindowPlaybackSequenceTestAccess::isPaused(window)
            && cluster != nullptr && cluster->isVisible()
            && name != nullptr && name->text() == QStringLiteral("Playlist A"),
        "Resumed playback must retain A's visible volume context") && passed;

    passed = require(model.selectPlaylist(playlistB),
        "Selection-independence coverage must select Playlist B") && passed;
    MainWindowPlaybackSequenceTestAccess::refreshLibrary(window);
    QCoreApplication::processEvents();
    passed = require(name != nullptr && name->text() == QStringLiteral("Playlist A")
            && MainWindowPlaybackSequenceTestAccess::volumeValue(window) == 25
            && MainWindowPlaybackSequenceTestAccess::volumeTarget(window) == playlistA,
        "Selecting B while A is active must leave the bottom context on A") && passed;

    passed = require(MainWindowPlaybackSequenceTestAccess::start(window, playlistB, *itemB),
        "Volume-cluster coverage must actually start Playlist B") && passed;
    QCoreApplication::processEvents();
    passed = require(name != nullptr && name->text() == QStringLiteral("Playlist B")
            && MainWindowPlaybackSequenceTestAccess::volumeValue(window) == 75
            && MainWindowPlaybackSequenceTestAccess::volumeTarget(window) == playlistB,
        "Starting B must switch the visible name, value, and target to B") && passed;

    passed = require(model.renamePlaylist(playlistB, QStringLiteral("Renamed B")),
        "Active-rename coverage must mutate B's authored alias") && passed;
    MainWindowPlaybackSequenceTestAccess::refreshLibrary(window);
    QCoreApplication::processEvents();
    passed = require(name != nullptr && name->text() == QStringLiteral("Renamed B")
            && MainWindowPlaybackSequenceTestAccess::activeProjection(window).has_value()
            && MainWindowPlaybackSequenceTestAccess::activeProjection(window)->sourcePlaylistId
                == playlistB,
        "Renaming active B must update its derived label without restarting playback") && passed;

    const QString longAlias = QStringLiteral(
        "A very long active Playlist alias that must be derived and dynamically elided using its allocated width");
    passed = require(model.renamePlaylist(playlistB, longAlias),
        "Elision coverage must set a long authored alias") && passed;
    const int widthBeforeAlias = window.width();
    MainWindowPlaybackSequenceTestAccess::refreshLibrary(window);
    QCoreApplication::processEvents();
    passed = require(window.width() == widthBeforeAlias && name != nullptr && !name->wordWrap(),
        "The full alias must not expand the window and must remain single-line") && passed;

    if (cluster != nullptr && name != nullptr)
    {
        const int fullTextWidth = QFontMetrics{name->font()}.horizontalAdvance(longAlias);
        cluster->setMaximumWidth(QWIDGETSIZE_MAX);
        cluster->setMinimumWidth(fullTextWidth + 80);
        window.resize((std::max)(1800, fullTextWidth + 700), window.height());
        QCoreApplication::processEvents();
        passed = require(name->contentsRect().width() >= fullTextWidth && name->text() == longAlias,
            "A sufficiently wide allocated cluster must show the full authored alias") && passed;

        cluster->setMinimumWidth(0);
        cluster->setMaximumWidth(300);
        window.resize(window.minimumWidth(), window.height());
        QCoreApplication::processEvents();
        const QString narrowText = name->text();
        passed = require(name->contentsRect().width() < fullTextWidth
                && narrowText != longAlias
                && narrowText == QFontMetrics{name->font()}.elidedText(
                    longAlias, Qt::ElideRight, name->contentsRect().width())
                && !narrowText.contains(QChar{'\n'}),
            "A narrower actual allocation must right-elide the single-line alias") && passed;

        cluster->setMaximumWidth(QWIDGETSIZE_MAX);
        cluster->setMinimumWidth(fullTextWidth + 80);
        window.resize((std::max)(1800, fullTextWidth + 700), window.height());
        QCoreApplication::processEvents();
        passed = require(name->text() == longAlias,
            "Widening the allocated cluster again must restore the full alias") && passed;
        cluster->setMinimumWidth(0);
    }

    MainWindowPlaybackSequenceTestAccess::stop(window);
    QCoreApplication::processEvents();
    passed = require(cluster != nullptr && !cluster->isVisible()
            && !MainWindowPlaybackSequenceTestAccess::volumeTarget(window).isValid(),
        "Stopping playback must hide the complete cluster and clear its target") && passed;

    passed = require(MainWindowPlaybackSequenceTestAccess::start(window, playlistB, *itemB),
        "Active-Playlist deletion coverage must restart B") && passed;
    QCoreApplication::processEvents();
    passed = require(MainWindowPlaybackSequenceTestAccess::removePlaylist(window, playlistB),
        "Active-Playlist deletion coverage must confirm one deletion") && passed;
    QCoreApplication::processEvents();
    passed = require(cluster != nullptr && !cluster->isVisible()
            && !MainWindowPlaybackSequenceTestAccess::activeProjection(window).has_value()
            && !MainWindowPlaybackSequenceTestAccess::volumeTarget(window).isValid(),
        "Deleting the active Playlist must stop playback and hide the complete cluster") && passed;

    MainWindowPlaybackSequenceTestAccess::shutdown(window);
    return passed;
}

bool verifyDeletionSuccessorCases(ApplicationTranslationState& translationState)
{
    // Do not short-circuit: each independent causal case must execute even in RED.
    const bool availability = verifyDeletionSuccessor(translationState, DeletionCase::Availability);
    const bool reordered = verifyDeletionSuccessor(translationState, DeletionCase::Reordered);
    const bool noPlayable = verifyDeletionSuccessor(translationState, DeletionCase::NoPlayableReplacement);
    return availability && reordered && noPlayable;
}

bool verifyDeletionWrapStartsFreshAndPreservesSelection(ApplicationTranslationState& translationState)
{
    const auto imagePath = firstImagePath();
    std::optional<std::filesystem::path> videoPath;
    for (const auto& entry : std::filesystem::directory_iterator(
             DESKTOP_SCENE_ENGINE_TEST_RESOURCES_DIR "/Videos"))
    {
        if (entry.is_regular_file() && entry.path().extension() == L".mp4")
        {
            videoPath = entry.path();
            break;
        }
    }
    if (!require(imagePath.has_value() && videoPath.has_value(), "Deletion wrap requires local image/video fixtures"))
    {
        return false;
    }
    MainWindow window{
        DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(), translationState};
    if (!initializeDeletionWindow(window))
    {
        return false;
    }
    WorkspaceModel& model = DeletionAccess::workspaceModel(window);
    const PlaylistId playlist = model.selectedPlaylistId();
    const auto a = model.addItem(playlist, WorkspaceMediaType::Video, pathText(*videoPath), QStringLiteral("A"));
    const auto b = model.addItem(playlist, WorkspaceMediaType::Image, pathText(*imagePath), QStringLiteral("B"));
    const auto c = model.addItem(playlist, WorkspaceMediaType::Image, pathText(*imagePath), QStringLiteral("C"));
    std::vector<Event> events;
    DeletionAccess::setForwarder(window, [&events](const Event& event) { events.push_back(event); return true; });
    if (!require(a.has_value() && b.has_value() && c.has_value()
            && model.setVideoPlaybackRate(*a, 1.5)
            && model.setItemEnabled(*b, false)
            && model.setPlaylistItemPlaybackMode(playlist, PlaybackSequenceMode::RepeatCurrent)
            && model.setPlaylistPlaybackMode(PlaybackSequenceMode::Random)
            // Bind the video too: a bad replacement fixture is not a policy failure.
            && DeletionAccess::start(window, playlist, *a) && hasActiveBinding(window, model, *a)
            && DeletionAccess::start(window, playlist, *c) && hasActiveBinding(window, model, *c)
            && DeletionAccess::seek(window, 0.25F) && model.selectItem(*b),
            "Wrap setup requires playable A/C, disabled selected B and active C at 0.25 seconds"))
    {
        DeletionAccess::shutdown(window);
        return false;
    }
    DeletionAccess::setPlaybackSpeed(window, 1.75F);
    DeletionAccess::pause(window);
    events.clear();
    const auto revision = model.authoredRevision();
    const bool deleted = DeletionAccess::removeItem(window, *c);
    const bool passed = require(deleted && hasActiveBinding(window, model, *a)
            && DeletionAccess::playbackTime(window) == 0.0F
            && DeletionAccess::playbackSpeed(window) == 1.5F && !DeletionAccess::isPaused(window)
            && model.selectedPlaylistId() == playlist && model.selectedItemId() == b
            && model.item(*b) != nullptr && !model.item(*b)->enabled
            && model.authoredRevision() == revision + 1U
            && events.size() == 1U && std::holds_alternative<ActivePlaybackScopeChanged>(events.front())
            && DeletionAccess::hasSequencePosition(window),
        "End deletion must wrap within the Playlist regardless of RepeatCurrent/Random, start at zero with its own rate playing, preserve unrelated selection and emit only the existing BGM scope change");
    DeletionAccess::shutdown(window);
    return passed;
}

bool verifyInactiveDeletionAndCancelPreservePlayback(ApplicationTranslationState& translationState)
{
    const auto imagePath = firstImagePath();
    if (!require(imagePath.has_value(), "Inactive deletion requires a local image fixture"))
    {
        return false;
    }
    MainWindow window{
        DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(), translationState};
    if (!initializeDeletionWindow(window))
    {
        return false;
    }
    WorkspaceModel& model = DeletionAccess::workspaceModel(window);
    const PlaylistId playlist = model.selectedPlaylistId();
    const PlaylistId otherPlaylist = model.addPlaylist();
    const auto a = model.addItem(playlist, WorkspaceMediaType::Image, pathText(*imagePath), QStringLiteral("A"));
    const auto b = model.addItem(playlist, WorkspaceMediaType::Image, pathText(*imagePath), QStringLiteral("B"));
    const auto c = model.addItem(playlist, WorkspaceMediaType::Image, pathText(*imagePath), QStringLiteral("C"));
    const auto x = model.addItem(otherPlaylist, WorkspaceMediaType::Image, pathText(*imagePath), QStringLiteral("X"));
    std::vector<Event> events;
    DeletionAccess::setForwarder(window, [&events](const Event& event) { events.push_back(event); return true; });
    if (!require(a.has_value() && b.has_value() && c.has_value() && x.has_value()
            && DeletionAccess::start(window, playlist, *c) && hasActiveBinding(window, model, *c)
            && DeletionAccess::seek(window, 0.25F) && model.selectItem(*b),
            "Inactive deletion setup requires bound C and selected B"))
    {
        DeletionAccess::shutdown(window);
        return false;
    }
    DeletionAccess::setPlaybackSpeed(window, 1.75F);
    DeletionAccess::pause(window);
    events.clear();
    const auto preservedPlayback = [&]
    {
        return hasActiveBinding(window, model, *c) && DeletionAccess::playbackTime(window) == 0.25F
            && DeletionAccess::playbackSpeed(window) == 1.75F && DeletionAccess::isPaused(window)
            && DeletionAccess::hasSequencePosition(window) && events.empty();
    };
    const auto revision = model.authoredRevision();
    const bool samePlaylistDeleted = DeletionAccess::removeItem(window, *b);
    const bool restored = require(samePlaylistDeleted && model.item(*b) == nullptr
            && preservedPlayback() && DeletionAccess::activeSceneIndex(window) == 1U
            && DeletionAccess::sceneCount(window) == 2U && !model.selectedItemId().has_value()
            && model.selectedPlaylistId() == playlist && model.authoredRevision() == revision + 1U,
        "Non-current deletion in the active Playlist must restore surviving C time/speed/pause, clear only deleted selected B and increment revision once");
    if (!restored)
    {
        DeletionAccess::shutdown(window);
        return false;
    }

    // The inactive Playlist command and Cancel must not replace the bound scene storage.
    const auto* const sceneStorage = DeletionAccess::activeProjection(window)->project.scenes.data();
    const bool selectedOther = model.selectItem(*x);
    const auto otherRevision = model.authoredRevision();
    const bool otherDeleted = DeletionAccess::removeItem(window, *x);
    const bool otherInert = require(selectedOther && otherDeleted && model.item(*x) == nullptr
            && preservedPlayback() && model.authoredRevision() == otherRevision + 1U
            && model.selectedPlaylistId() == otherPlaylist && !model.selectedItemId().has_value()
            && DeletionAccess::activeProjection(window)->project.scenes.data() == sceneStorage,
        "Deletion in another Playlist must leave active binding untouched and clear only its deleted selection");
    const bool selectedSurvivor = model.selectItem(*a);
    const auto cancelRevision = model.authoredRevision();
    const bool cancelled = DeletionAccess::removeItem(window, *c, QMessageBox::Cancel);
    const bool cancelInert = require(selectedSurvivor && cancelled && model.item(*c) != nullptr
            && preservedPlayback() && model.authoredRevision() == cancelRevision
            && model.selectedPlaylistId() == playlist && model.selectedItemId() == a
            && DeletionAccess::activeProjection(window)->project.scenes.data() == sceneStorage,
        "Cancel must preserve authored revision, selection, active binding and time/speed/pause without BGM controls");
    DeletionAccess::shutdown(window);
    return restored && otherInert && cancelInert;
}

bool verifyDeletionStopsForDisabledParentAndFailedBinding(ApplicationTranslationState& translationState)
{
    const auto imagePath = firstImagePath();
    if (!require(imagePath.has_value(), "Deletion termination requires a local image fixture"))
    {
        return false;
    }
    bool passed = true;
    for (const bool disabledParent : {true, false})
    {
        MainWindow window{
            DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(), translationState};
        if (!initializeDeletionWindow(window))
        {
            return false;
        }
        WorkspaceModel& model = DeletionAccess::workspaceModel(window);
        const PlaylistId playlist = model.selectedPlaylistId();
        const auto a = model.addItem(playlist, WorkspaceMediaType::Image, pathText(*imagePath), QStringLiteral("A"));
        // An eligible but undecodable B must retain the existing bind-failure path, not skip to C.
        const auto b = model.addItem(playlist, WorkspaceMediaType::Image,
            disabledParent ? pathText(*imagePath) : QStringLiteral("C:/DesktopSceneEngine-Deletion-missing-successor.png"), QStringLiteral("B"));
        const auto c = model.addItem(playlist, WorkspaceMediaType::Image, pathText(*imagePath), QStringLiteral("C"));
        std::vector<Event> events;
        DeletionAccess::setForwarder(window, [&events](const Event& event) { events.push_back(event); return true; });
        if (!require(a.has_value() && b.has_value() && c.has_value()
                && DeletionAccess::start(window, playlist, *a) && hasActiveBinding(window, model, *a)
                // Change canonical eligibility directly to exercise deletion's parent gate;
                // the ordinary availability UI command would already stop playback.
                && (!disabledParent || model.setPlaylistEnabled(playlist, false)),
                "Termination setup requires successfully bound A before canonical parent disable or successor bind failure"))
        {
            DeletionAccess::shutdown(window);
            return false;
        }
        events.clear();
        const auto revision = model.authoredRevision();
        const bool deleted = DeletionAccess::removeItem(window, *a, QMessageBox::Yes, disabledParent ? 0 : 1);
        const bool stopped = require(deleted && model.item(*a) == nullptr
                && model.item(*b) != nullptr && model.item(*c) != nullptr
                && model.authoredRevision() == revision + 1U && hasStoppedBinding(window, model)
                && !model.selectedItemId().has_value() && model.selectedPlaylistId() == playlist
                && events.size() == 1U && std::holds_alternative<PlaybackStopped>(events.front()),
            disabledParent
                ? "Disabled parent must supply no replacement and stop normally without warning"
                : "An eligible successor bind failure must stop/warn once without skipping to C or changing the BGM stop contract");
        passed = stopped && passed;
        DeletionAccess::shutdown(window);
    }
    return passed;
}

bool verifyDeletionGuards(ApplicationTranslationState& translationState)
{
    const bool wrap = verifyDeletionWrapStartsFreshAndPreservesSelection(translationState);
    const bool inactive = verifyInactiveDeletionAndCancelPreservePlayback(translationState);
    const bool termination = verifyDeletionStopsForDisabledParentAndFailedBinding(translationState);
    return wrap && inactive && termination;
}

bool verifyPlayableSnapshotFiltersUnavailableTargetsAndFreshStartUsesFirstPlayable(
    ApplicationTranslationState& translationState)
{
    const auto imagePath = firstImagePath();
    if (!require(imagePath.has_value(), "Playable-snapshot coverage requires a local image fixture"))
    {
        return false;
    }

    MainWindow window{
        DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
        translationState};
    window.show();
    QCoreApplication::processEvents();
    if (!require(window.initializeEngine(), "Playable-snapshot coverage requires an initialized Player Engine"))
    {
        return false;
    }

    WorkspaceModel& model = MainWindowPlaybackSequenceTestAccess::workspaceModel(window);
    const PlaylistId playlistA = model.selectedPlaylistId();
    const PlaylistId playlistB = model.addPlaylist();
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
    const auto itemB = model.addItem(
        playlistB,
        WorkspaceMediaType::Image,
        pathText(*imagePath),
        QStringLiteral("B"));

    const bool setup = itemA1.has_value()
        && itemA2.has_value()
        && itemB.has_value()
        && model.setItemEnabled(*itemA1, false)
        && model.setPlaylistEnabled(playlistB, false);
    const auto collections = MainWindowPlaybackSequenceTestAccess::collections(window);
    const bool snapshotFiltered = setup
        && collections.playlistIds.size() == 1U
        && collections.playlistIds.front() == playlistA
        && collections.itemsByPlaylist.size() == 1U
        && collections.itemsByPlaylist.front().playlistId == playlistA
        && collections.itemsByPlaylist.front().itemIds.size() == 1U
        && collections.itemsByPlaylist.front().itemIds.front() == *itemA2;
    const bool freshStartUsesFirstPlayable = snapshotFiltered
        && MainWindowPlaybackSequenceTestAccess::startPlaylist(window, playlistA)
        && model.activePlaylistId() == playlistA
        && model.activeItemId() == itemA2;

    MainWindowPlaybackSequenceTestAccess::shutdown(window);
    return require(
        snapshotFiltered && freshStartUsesFirstPlayable,
        "Playable snapshots must omit unavailable targets and fresh Playlist playback must start at the first playable Item");
}

bool verifyManualAvailabilityGuardsAndActiveItemDisableStop(
    ApplicationTranslationState& translationState)
{
    const auto imagePath = firstImagePath();
    if (!require(imagePath.has_value(), "Availability-guard coverage requires a local image fixture"))
    {
        return false;
    }

    MainWindow window{
        DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
        translationState};
    window.show();
    QCoreApplication::processEvents();
    if (!require(window.initializeEngine(), "Availability-guard coverage requires an initialized Player Engine"))
    {
        return false;
    }

    WorkspaceModel& model = MainWindowPlaybackSequenceTestAccess::workspaceModel(window);
    const PlaylistId playlistA = model.selectedPlaylistId();
    const PlaylistId playlistB = model.addPlaylist();
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
    const auto itemB = model.addItem(
        playlistB,
        WorkspaceMediaType::Image,
        pathText(*imagePath),
        QStringLiteral("B"));
    std::vector<Event> events{};
    bool activeItemRemainedEnabledAtStop = false;
    MainWindowPlaybackSequenceTestAccess::setForwarder(
        window,
        [&events, &model, &itemA1, &activeItemRemainedEnabledAtStop](const Event& event)
        {
            events.push_back(event);
            if (std::holds_alternative<PlaybackStopped>(event) && itemA1.has_value())
            {
                activeItemRemainedEnabledAtStop = model.itemEnabled(*itemA1);
            }
            return true;
        });

    if (!require(itemA1.has_value() && itemA2.has_value() && itemB.has_value()
            && MainWindowPlaybackSequenceTestAccess::start(window, playlistA, *itemA1)
            && model.selectItem(*itemB)
            && model.setItemEnabled(*itemA2, false)
            && model.setPlaylistEnabled(playlistB, false),
            "Availability guards require an active A:a1 plus disabled A:a2 and Playlist B"))
    {
        MainWindowPlaybackSequenceTestAccess::shutdown(window);
        return false;
    }

    events.clear();
    const bool rejectedManualPlay = !MainWindowPlaybackSequenceTestAccess::start(
            window,
            playlistA,
            *itemA2)
        && !MainWindowPlaybackSequenceTestAccess::start(window, playlistB, *itemB)
        && !MainWindowPlaybackSequenceTestAccess::start(window, playlistA, *itemB)
        && model.activePlaylistId() == playlistA
        && model.activeItemId() == itemA1
        && model.selectedPlaylistId() == playlistB
        && model.selectedItemId() == itemB
        && MainWindowPlaybackSequenceTestAccess::activeProjection(window).has_value()
        && MainWindowPlaybackSequenceTestAccess::hasSequencePosition(window)
        && events.empty();

    if (!require(model.setPlaylistEnabled(playlistB, true),
            "Inactive-availability coverage requires restoring Playlist B"))
    {
        MainWindowPlaybackSequenceTestAccess::shutdown(window);
        return false;
    }

    const bool inactiveMutationIsInert = MainWindowPlaybackSequenceTestAccess::setPlaylistAvailability(
            window,
            playlistB,
            false)
        && !model.playlistEnabled(playlistB)
        && model.activePlaylistId() == playlistA
        && model.activeItemId() == itemA1
        && MainWindowPlaybackSequenceTestAccess::activeProjection(window).has_value()
        && MainWindowPlaybackSequenceTestAccess::hasSequencePosition(window)
        && events.empty();
    const auto disabledCollections = MainWindowPlaybackSequenceTestAccess::collections(window);
    const bool disabledPlaylistIsExcluded = disabledCollections.playlistIds.size() == 1U
        && disabledCollections.playlistIds.front() == playlistA;
    const bool enableIsInert = MainWindowPlaybackSequenceTestAccess::setPlaylistAvailability(
            window,
            playlistB,
            true)
        && model.playlistEnabled(playlistB)
        && model.activePlaylistId() == playlistA
        && model.activeItemId() == itemA1
        && events.empty();
    const auto enabledCollections = MainWindowPlaybackSequenceTestAccess::collections(window);
    const bool reenabledPlaylistIsEligible = enabledCollections.playlistIds.size() == 2U
        && enabledCollections.playlistIds.back() == playlistB;

    events.clear();
    const bool activeDisableStops = MainWindowPlaybackSequenceTestAccess::setItemAvailability(
            window,
            *itemA1,
            false)
        && !model.itemEnabled(*itemA1)
        && !model.activePlaylistId().isValid()
        && !model.activeItemId().has_value()
        && !MainWindowPlaybackSequenceTestAccess::activeProjection(window).has_value()
        && !MainWindowPlaybackSequenceTestAccess::hasSequencePosition(window)
        && model.selectedPlaylistId() == playlistB
        && model.selectedItemId() == itemB
        && events.size() == 1U
        && std::holds_alternative<PlaybackStopped>(events.front())
        && activeItemRemainedEnabledAtStop;

    events.clear();
    const bool allDisabledRejectsFreshStart = MainWindowPlaybackSequenceTestAccess::setPlaylistAvailability(
            window,
            playlistB,
            false)
        && !MainWindowPlaybackSequenceTestAccess::startPlaylist(
            window,
            playlistA)
        && events.empty();
    const auto allDisabledCollections = MainWindowPlaybackSequenceTestAccess::collections(window);
    const bool allDisabledSnapshotIsEmpty = allDisabledCollections.playlistIds.empty();

    MainWindowPlaybackSequenceTestAccess::shutdown(window);
    return require(
        rejectedManualPlay && inactiveMutationIsInert && disabledPlaylistIsExcluded
            && enableIsInert && reenabledPlaylistIsEligible
            && activeDisableStops && allDisabledRejectsFreshStart && allDisabledSnapshotIsEmpty,
        "Manual unavailable targets must be inert, inactive availability changes must not disturb playback, and active Item disable must stop exactly once");
}

bool verifyActivePlaylistDisableStopsWithoutRewritingChildren(
    ApplicationTranslationState& translationState)
{
    const auto imagePath = firstImagePath();
    if (!require(imagePath.has_value(), "Active-Playlist disable coverage requires a local image fixture"))
    {
        return false;
    }

    MainWindow window{
        DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
        translationState};
    window.show();
    QCoreApplication::processEvents();
    if (!require(window.initializeEngine(), "Active-Playlist disable coverage requires an initialized Player Engine"))
    {
        return false;
    }

    WorkspaceModel& model = MainWindowPlaybackSequenceTestAccess::workspaceModel(window);
    const PlaylistId playlistId = model.selectedPlaylistId();
    const auto enabledItem = model.addItem(
        playlistId,
        WorkspaceMediaType::Image,
        pathText(*imagePath),
        QStringLiteral("Enabled"));
    const auto disabledItem = model.addItem(
        playlistId,
        WorkspaceMediaType::Image,
        pathText(*imagePath),
        QStringLiteral("Disabled"));
    std::vector<Event> events{};
    MainWindowPlaybackSequenceTestAccess::setForwarder(
        window,
        [&events](const Event& event)
        {
            events.push_back(event);
            return true;
        });

    if (!require(enabledItem.has_value() && disabledItem.has_value()
            && model.setItemEnabled(*disabledItem, false)
            && MainWindowPlaybackSequenceTestAccess::start(window, playlistId, *enabledItem)
            && model.selectItem(*disabledItem),
            "Active-Playlist disable coverage requires an active enabled Item and a selected disabled sibling"))
    {
        MainWindowPlaybackSequenceTestAccess::shutdown(window);
        return false;
    }

    events.clear();
    const bool disabledStops = MainWindowPlaybackSequenceTestAccess::setPlaylistAvailability(
            window,
            playlistId,
            false)
        && !model.playlistEnabled(playlistId)
        && model.itemEnabled(*enabledItem)
        && !model.itemEnabled(*disabledItem)
        && !model.activePlaylistId().isValid()
        && !model.activeItemId().has_value()
        && !MainWindowPlaybackSequenceTestAccess::activeProjection(window).has_value()
        && !MainWindowPlaybackSequenceTestAccess::hasSequencePosition(window)
        && model.selectedPlaylistId() == playlistId
        && model.selectedItemId() == disabledItem
        && events.size() == 1U
        && std::holds_alternative<PlaybackStopped>(events.front());

    events.clear();
    const bool enableDoesNotRestart = MainWindowPlaybackSequenceTestAccess::setPlaylistAvailability(
            window,
            playlistId,
            true)
        && model.isItemPlayable(playlistId, *enabledItem)
        && !model.isItemPlayable(playlistId, *disabledItem)
        && !MainWindowPlaybackSequenceTestAccess::activeProjection(window).has_value()
        && !MainWindowPlaybackSequenceTestAccess::hasSequencePosition(window)
        && events.empty();

    MainWindowPlaybackSequenceTestAccess::shutdown(window);
    return require(
        disabledStops && enableDoesNotRestart,
        "Active Playlist disable must stop exactly once without rewriting child availability, and re-enable must not restart playback");
}

bool verifyReorderChangesFutureNavigationWithoutRebindingCurrentPlayback(
    ApplicationTranslationState& translationState)
{
    const auto imagePath = firstImagePath();
    if (!require(imagePath.has_value(), "Reorder integration requires a local image fixture"))
    {
        return false;
    }

    MainWindow itemWindow{
        DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
        translationState};
    itemWindow.show();
    QCoreApplication::processEvents();
    if (!require(itemWindow.initializeEngine(), "Item reorder integration requires an initialized Player Engine"))
    {
        return false;
    }

    WorkspaceModel& itemModel = MainWindowPlaybackSequenceTestAccess::workspaceModel(itemWindow);
    const PlaylistId itemPlaylist = itemModel.selectedPlaylistId();
    const auto itemA1 = itemModel.addItem(
        itemPlaylist,
        WorkspaceMediaType::Image,
        pathText(*imagePath),
        QStringLiteral("A1"));
    const auto itemA2 = itemModel.addItem(
        itemPlaylist,
        WorkspaceMediaType::Image,
        pathText(*imagePath),
        QStringLiteral("A2"));
    const auto itemA3 = itemModel.addItem(
        itemPlaylist,
        WorkspaceMediaType::Image,
        pathText(*imagePath),
        QStringLiteral("A3"));
    if (!require(itemA1.has_value() && itemA2.has_value() && itemA3.has_value()
            && MainWindowPlaybackSequenceTestAccess::start(itemWindow, itemPlaylist, *itemA1),
            "Item reorder integration requires active A:a1"))
    {
        MainWindowPlaybackSequenceTestAccess::shutdown(itemWindow);
        return false;
    }

    const auto& itemProjectionBefore = MainWindowPlaybackSequenceTestAccess::activeProjection(itemWindow);
    const ActivePlaybackProjection* const itemProjectionAddress = itemProjectionBefore.has_value()
        ? &*itemProjectionBefore
        : nullptr;
    const bool itemReorderIsInert = itemModel.moveItem(*itemA3, -1)
        && itemModel.activePlaylistId() == itemPlaylist
        && itemModel.activeItemId() == itemA1
        && MainWindowPlaybackSequenceTestAccess::activeSceneIndex(itemWindow) == 0U
        && MainWindowPlaybackSequenceTestAccess::activeProjection(itemWindow).has_value()
        && &*MainWindowPlaybackSequenceTestAccess::activeProjection(itemWindow) == itemProjectionAddress;
    MainWindowPlaybackSequenceTestAccess::navigate(itemWindow, false);
    const bool itemNextUsesLatestOrder = itemModel.activeItemId() == itemA3;
    MainWindowPlaybackSequenceTestAccess::shutdown(itemWindow);

    MainWindow playlistWindow{
        DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
        translationState};
    playlistWindow.show();
    QCoreApplication::processEvents();
    if (!require(playlistWindow.initializeEngine(), "Playlist reorder integration requires an initialized Player Engine"))
    {
        return false;
    }

    WorkspaceModel& playlistModel = MainWindowPlaybackSequenceTestAccess::workspaceModel(playlistWindow);
    const PlaylistId playlistA = playlistModel.selectedPlaylistId();
    const PlaylistId playlistB = playlistModel.addPlaylist();
    const PlaylistId playlistC = playlistModel.addPlaylist();
    const auto itemA = playlistModel.addItem(
        playlistA,
        WorkspaceMediaType::Image,
        pathText(*imagePath),
        QStringLiteral("A"));
    const auto itemB = playlistModel.addItem(
        playlistB,
        WorkspaceMediaType::Image,
        pathText(*imagePath),
        QStringLiteral("B"));
    const auto itemC = playlistModel.addItem(
        playlistC,
        WorkspaceMediaType::Image,
        pathText(*imagePath),
        QStringLiteral("C"));
    if (!require(itemA.has_value() && itemB.has_value() && itemC.has_value()
            && playlistModel.setPlaylistPlaybackMode(PlaybackSequenceMode::ListLoop)
            && MainWindowPlaybackSequenceTestAccess::start(playlistWindow, playlistA, *itemA),
            "Playlist reorder integration requires active Playlist A"))
    {
        MainWindowPlaybackSequenceTestAccess::shutdown(playlistWindow);
        return false;
    }

    const auto& playlistProjectionBefore = MainWindowPlaybackSequenceTestAccess::activeProjection(
        playlistWindow);
    const ActivePlaybackProjection* const playlistProjectionAddress = playlistProjectionBefore.has_value()
        ? &*playlistProjectionBefore
        : nullptr;
    const bool playlistReorderIsInert = playlistModel.movePlaylist(playlistC, -1)
        && playlistModel.activePlaylistId() == playlistA
        && playlistModel.activeItemId() == itemA
        && MainWindowPlaybackSequenceTestAccess::activeSceneIndex(playlistWindow) == 0U
        && MainWindowPlaybackSequenceTestAccess::activeProjection(playlistWindow).has_value()
        && &*MainWindowPlaybackSequenceTestAccess::activeProjection(playlistWindow)
            == playlistProjectionAddress;
    MainWindowPlaybackSequenceTestAccess::navigate(playlistWindow, false);
    const bool playlistNextUsesLatestOrder = playlistModel.activePlaylistId() == playlistC
        && playlistModel.activeItemId() == itemC;
    MainWindowPlaybackSequenceTestAccess::shutdown(playlistWindow);

    return require(
        itemReorderIsInert && itemNextUsesLatestOrder
            && playlistReorderIsInert && playlistNextUsesLatestOrder,
        "Reorder must retain the current stable-ID binding and apply the latest Workspace order only to future navigation");
}

bool verifyPreviousDoesNotRestoreFilteredHistoryTarget(
    ApplicationTranslationState& translationState)
{
    const auto imagePath = firstImagePath();
    if (!require(imagePath.has_value(), "Filtered-history coverage requires a local image fixture"))
    {
        return false;
    }

    MainWindow window{
        DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
        translationState};
    window.show();
    QCoreApplication::processEvents();
    if (!require(window.initializeEngine(), "Filtered-history coverage requires an initialized Player Engine"))
    {
        return false;
    }

    WorkspaceModel& model = MainWindowPlaybackSequenceTestAccess::workspaceModel(window);
    const PlaylistId playlistId = model.selectedPlaylistId();
    const auto itemA1 = model.addItem(
        playlistId,
        WorkspaceMediaType::Image,
        pathText(*imagePath),
        QStringLiteral("A1"));
    const auto itemA2 = model.addItem(
        playlistId,
        WorkspaceMediaType::Image,
        pathText(*imagePath),
        QStringLiteral("A2"));
    if (!require(itemA1.has_value() && itemA2.has_value()
            && MainWindowPlaybackSequenceTestAccess::start(window, playlistId, *itemA1),
            "Filtered-history coverage requires active A:a1"))
    {
        MainWindowPlaybackSequenceTestAccess::shutdown(window);
        return false;
    }

    MainWindowPlaybackSequenceTestAccess::navigate(window, false);
    if (!require(model.activeItemId() == itemA2 && model.setItemEnabled(*itemA1, false),
            "Filtered-history coverage requires a disabled prior Item after moving to A:a2"))
    {
        MainWindowPlaybackSequenceTestAccess::shutdown(window);
        return false;
    }

    MainWindowPlaybackSequenceTestAccess::navigate(window, true);
    const bool passed = model.activeItemId() == itemA2
        && model.isItemPlayable(playlistId, *itemA2)
        && !model.isItemPlayable(playlistId, *itemA1);
    MainWindowPlaybackSequenceTestAccess::shutdown(window);
    return require(
        passed,
        "Previous must not restore an Item omitted by the latest playable snapshot");
}

bool verifyNaturalCompletionCrossesPlaylistsWithoutChangingSelection(
    ApplicationTranslationState& translationState)
{
    const auto imagePath = firstImagePath();
    if (!require(imagePath.has_value(), "Sequence integration requires a local image fixture"))
    {
        return false;
    }

    MainWindow window{
        DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
        translationState};
    window.show();
    QCoreApplication::processEvents();
    if (!require(window.initializeEngine(), "Sequence integration requires an initialized Player Engine"))
    {
        return false;
    }

    WorkspaceModel& model = MainWindowPlaybackSequenceTestAccess::workspaceModel(window);
    const PlaylistId playlistA = model.selectedPlaylistId();
    const PlaylistId emptyPlaylist = model.addPlaylist();
    const PlaylistId playlistB = model.addPlaylist();
    const auto itemA = model.addItem(
        playlistA,
        WorkspaceMediaType::Image,
        pathText(*imagePath),
        QStringLiteral("A"));
    const auto itemB = model.addItem(
        playlistB,
        WorkspaceMediaType::Image,
        pathText(*imagePath),
        QStringLiteral("B"));
    std::vector<Event> events{};
    MainWindowPlaybackSequenceTestAccess::setForwarder(
        window,
        [&events](const Event& event)
        {
            events.push_back(event);
            return true;
        });

    if (!require(itemA.has_value() && itemB.has_value()
            && model.playlist(emptyPlaylist) != nullptr
            && model.setImageDisplayDuration(*itemA, 0.01)
            && model.setPlaylistPlaybackMode(PlaybackSequenceMode::ListLoop)
            && MainWindowPlaybackSequenceTestAccess::start(window, playlistA, *itemA),
            "Sequence integration requires A[a1] and B[b1] active playback"))
    {
        MainWindowPlaybackSequenceTestAccess::shutdown(window);
        return false;
    }

    const PlaylistId selectedPlaylistBefore = model.selectedPlaylistId();
    const std::optional<ItemId> selectedItemBefore = model.selectedItemId();
    events.clear();
    MainWindowPlaybackSequenceTestAccess::updateToNaturalCompletion(window, 1.0F);

    const auto& projection = MainWindowPlaybackSequenceTestAccess::activeProjection(window);
    const bool passed = require(
        projection.has_value()
            && projection->sourcePlaylistId == playlistB
            && MainWindowPlaybackSequenceTestAccess::activeSceneIndex(window) == 0U
            && model.activePlaylistId() == playlistB
            && model.activeItemId() == itemB
            && model.selectedPlaylistId() == selectedPlaylistBefore
            && model.selectedItemId() == selectedItemBefore
            && events.size() == 1U
            && std::holds_alternative<ActivePlaybackScopeChanged>(events.front()),
        "Natural completion must skip empty Playlists, transition A:a1 to B:b1, preserve Library selection, and forward BGM once");
    MainWindowPlaybackSequenceTestAccess::shutdown(window);
    return passed;
}

bool verifySameItemCompletionRestartsMediaWithoutRestartingBackgroundMusic(
    ApplicationTranslationState& translationState)
{
    const auto imagePath = firstImagePath();
    if (!require(imagePath.has_value(), "Same-item sequencing requires a local image fixture"))
    {
        return false;
    }

    MainWindow window{
        DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
        translationState};
    window.show();
    QCoreApplication::processEvents();
    if (!require(window.initializeEngine(), "Same-item sequencing requires an initialized Player Engine"))
    {
        return false;
    }

    WorkspaceModel& model = MainWindowPlaybackSequenceTestAccess::workspaceModel(window);
    const PlaylistId playlistId = model.selectedPlaylistId();
    const auto itemId = model.addItem(
        playlistId,
        WorkspaceMediaType::Image,
        pathText(*imagePath),
        QStringLiteral("Loop"));
    std::vector<Event> events{};
    MainWindowPlaybackSequenceTestAccess::setForwarder(
        window,
        [&events](const Event& event)
        {
            events.push_back(event);
            return true;
        });

    if (!require(itemId.has_value()
            && model.setImageDisplayDuration(*itemId, 0.01)
            && MainWindowPlaybackSequenceTestAccess::start(window, playlistId, *itemId),
            "Same-item sequencing requires an active one-Item Playlist"))
    {
        MainWindowPlaybackSequenceTestAccess::shutdown(window);
        return false;
    }

    events.clear();
    MainWindowPlaybackSequenceTestAccess::updateToNaturalCompletion(window, 1.0F);
    const bool passed = require(
        model.activePlaylistId() == playlistId
            && model.activeItemId() == itemId
            && !MainWindowPlaybackSequenceTestAccess::isPaused(window)
            && events.empty(),
        "A same-ID RepeatCurrent transition must restart visual media without forwarding a BGM scope change");
    MainWindowPlaybackSequenceTestAccess::shutdown(window);
    return passed;
}

bool verifyPausedTransportUsesControllerAndPreservesSelection(
    ApplicationTranslationState& translationState)
{
    const auto imagePath = firstImagePath();
    if (!require(imagePath.has_value(), "Paused transport sequencing requires a local image fixture"))
    {
        return false;
    }

    MainWindow window{
        DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
        translationState};
    window.show();
    QCoreApplication::processEvents();
    if (!require(window.initializeEngine(), "Paused transport sequencing requires an initialized Player Engine"))
    {
        return false;
    }

    WorkspaceModel& model = MainWindowPlaybackSequenceTestAccess::workspaceModel(window);
    const PlaylistId playlistId = model.selectedPlaylistId();
    const auto itemA = model.addItem(
        playlistId,
        WorkspaceMediaType::Image,
        pathText(*imagePath),
        QStringLiteral("A"));
    const auto itemB = model.addItem(
        playlistId,
        WorkspaceMediaType::Image,
        pathText(*imagePath),
        QStringLiteral("B"));
    if (!require(itemA.has_value() && itemB.has_value()
            && MainWindowPlaybackSequenceTestAccess::start(window, playlistId, *itemA),
            "Paused transport sequencing requires a two-Item active Playlist"))
    {
        MainWindowPlaybackSequenceTestAccess::shutdown(window);
        return false;
    }

    const PlaylistId selectedPlaylistBefore = model.selectedPlaylistId();
    const std::optional<ItemId> selectedItemBefore = model.selectedItemId();
    MainWindowPlaybackSequenceTestAccess::pause(window);
    MainWindowPlaybackSequenceTestAccess::navigate(window, false);
    const bool nextPassed = model.activeItemId() == itemB
        && !MainWindowPlaybackSequenceTestAccess::isPaused(window)
        && model.selectedPlaylistId() == selectedPlaylistBefore
        && model.selectedItemId() == selectedItemBefore;

    MainWindowPlaybackSequenceTestAccess::pause(window);
    MainWindowPlaybackSequenceTestAccess::navigate(window, true);
    const bool previousPassed = model.activeItemId() == itemA
        && !MainWindowPlaybackSequenceTestAccess::isPaused(window)
        && model.selectedPlaylistId() == selectedPlaylistBefore
        && model.selectedItemId() == selectedItemBefore;
    MainWindowPlaybackSequenceTestAccess::shutdown(window);
    return require(
        nextPassed && previousPassed,
        "Paused Next and Previous must use controller order, resume through Engine switching, and preserve selection");
}

bool verifyModeChangeOnlyAffectsFutureNavigation(
    ApplicationTranslationState& translationState)
{
    const auto imagePath = firstImagePath();
    if (!require(imagePath.has_value(), "Mode-change sequencing requires a local image fixture"))
    {
        return false;
    }

    MainWindow window{
        DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
        translationState};
    window.show();
    QCoreApplication::processEvents();
    if (!require(window.initializeEngine(), "Mode-change sequencing requires an initialized Player Engine"))
    {
        return false;
    }

    WorkspaceModel& model = MainWindowPlaybackSequenceTestAccess::workspaceModel(window);
    const PlaylistId playlistA = model.selectedPlaylistId();
    const PlaylistId playlistB = model.addPlaylist();
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
    const auto itemB = model.addItem(
        playlistB,
        WorkspaceMediaType::Image,
        pathText(*imagePath),
        QStringLiteral("B"));
    if (!require(itemA1.has_value() && itemA2.has_value() && itemB.has_value()
            && MainWindowPlaybackSequenceTestAccess::start(window, playlistA, *itemA1),
            "Mode-change sequencing requires an active Playlist A"))
    {
        MainWindowPlaybackSequenceTestAccess::shutdown(window);
        return false;
    }

    if (!require(model.setPlaylistItemPlaybackMode(
            playlistA,
            PlaybackSequenceMode::RepeatCurrent)
            && model.setPlaylistItemPlaybackMode(
                playlistB,
                PlaybackSequenceMode::RepeatCurrent)
            && model.setPlaylistPlaybackMode(PlaybackSequenceMode::ListLoop),
            "Mode-change sequencing requires accepted model configuration"))
    {
        MainWindowPlaybackSequenceTestAccess::shutdown(window);
        return false;
    }

    MainWindowPlaybackSequenceTestAccess::tick(window);
    const bool unchangedBeforeNext = model.activePlaylistId() == playlistA
        && model.activeItemId() == itemA1;
    MainWindowPlaybackSequenceTestAccess::navigate(window, false);
    const bool activeModeApplied = model.activePlaylistId() == playlistA
        && model.activeItemId() == itemA1;

    if (!require(unchangedBeforeNext && activeModeApplied,
            "Changing active Item mode must not restart current media and must affect the next decision"))
    {
        MainWindowPlaybackSequenceTestAccess::shutdown(window);
        return false;
    }

    if (!require(model.setPlaylistItemPlaybackMode(
            playlistA,
            PlaybackSequenceMode::ListLoop),
            "Mode-change sequencing requires restoring Playlist A ListLoop"))
    {
        MainWindowPlaybackSequenceTestAccess::shutdown(window);
        return false;
    }

    MainWindowPlaybackSequenceTestAccess::tick(window);
    MainWindowPlaybackSequenceTestAccess::navigate(window, false);
    const bool listLoopApplied = model.activeItemId() == itemA2;

    MainWindowPlaybackSequenceTestAccess::navigate(window, false);
    const bool inactiveModeAppliedOnEntry = model.activePlaylistId() == playlistB
        && model.activeItemId() == itemB;
    MainWindowPlaybackSequenceTestAccess::shutdown(window);
    return require(
        listLoopApplied && inactiveModeAppliedOnEntry,
        "Inactive Playlist mode changes must not affect current playback and must apply when entered later");
}

bool verifyStopClearsSequenceAndStoppedTransportIsNoOp(
    ApplicationTranslationState& translationState)
{
    const auto imagePath = firstImagePath();
    if (!require(imagePath.has_value(), "Stop sequencing requires a local image fixture"))
    {
        return false;
    }

    MainWindow window{
        DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
        translationState};
    window.show();
    QCoreApplication::processEvents();
    if (!require(window.initializeEngine(), "Stop sequencing requires an initialized Player Engine"))
    {
        return false;
    }

    WorkspaceModel& model = MainWindowPlaybackSequenceTestAccess::workspaceModel(window);
    const PlaylistId playlistId = model.selectedPlaylistId();
    const auto itemA = model.addItem(
        playlistId,
        WorkspaceMediaType::Image,
        pathText(*imagePath),
        QStringLiteral("A"));
    const auto itemB = model.addItem(
        playlistId,
        WorkspaceMediaType::Image,
        pathText(*imagePath),
        QStringLiteral("B"));
    if (!require(itemA.has_value() && itemB.has_value()
            && MainWindowPlaybackSequenceTestAccess::start(window, playlistId, *itemA)
            && MainWindowPlaybackSequenceTestAccess::hasSequencePosition(window),
            "Stop sequencing requires a rebased active controller"))
    {
        MainWindowPlaybackSequenceTestAccess::shutdown(window);
        return false;
    }

    MainWindowPlaybackSequenceTestAccess::navigate(window, false);
    MainWindowPlaybackSequenceTestAccess::stop(window);
    const bool cleared = !MainWindowPlaybackSequenceTestAccess::activeProjection(window).has_value()
        && !MainWindowPlaybackSequenceTestAccess::hasSequencePosition(window);

    MainWindowPlaybackSequenceTestAccess::navigate(window, false);
    MainWindowPlaybackSequenceTestAccess::navigate(window, true);
    const bool stoppedNavigationIsNoOp = !MainWindowPlaybackSequenceTestAccess::activeProjection(window)
        .has_value()
        && !MainWindowPlaybackSequenceTestAccess::hasSequencePosition(window);

    const bool restarted = MainWindowPlaybackSequenceTestAccess::start(window, playlistId, *itemA)
        && MainWindowPlaybackSequenceTestAccess::hasSequencePosition(window);
    MainWindowPlaybackSequenceTestAccess::navigate(window, true);
    const bool staleHistoryWasDiscarded = model.activeItemId() == itemA;
    MainWindowPlaybackSequenceTestAccess::shutdown(window);
    return require(
        cleared && stoppedNavigationIsNoOp && restarted && staleHistoryWasDiscarded,
        "Stop must clear controller state, stopped transport must stay inert, and restart must not restore stale Previous history");
}

bool verifyAllEmptyAndRandomEmptyResolutionStopSafely(
    ApplicationTranslationState& translationState)
{
    const auto imagePath = firstImagePath();
    if (!require(imagePath.has_value(), "Empty-sequence coverage requires a local image fixture"))
    {
        return false;
    }

    MainWindow allEmptyWindow{
        DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
        translationState};
    allEmptyWindow.show();
    QCoreApplication::processEvents();
    if (!require(allEmptyWindow.initializeEngine(), "All-empty coverage requires an initialized Player Engine"))
    {
        return false;
    }

    WorkspaceModel& allEmptyModel = MainWindowPlaybackSequenceTestAccess::workspaceModel(
        allEmptyWindow);
    const PlaylistId allEmptyPlaylist = allEmptyModel.selectedPlaylistId();
    const auto allEmptyItem = allEmptyModel.addItem(
        allEmptyPlaylist,
        WorkspaceMediaType::Image,
        pathText(*imagePath),
        QStringLiteral("Only"));
    if (!require(allEmptyItem.has_value()
            && allEmptyModel.setImageDisplayDuration(*allEmptyItem, 0.01)
            && MainWindowPlaybackSequenceTestAccess::start(
                allEmptyWindow,
                allEmptyPlaylist,
                *allEmptyItem)
            && allEmptyModel.removeItem(*allEmptyItem),
            "All-empty coverage requires removing the active source from the authoritative model"))
    {
        MainWindowPlaybackSequenceTestAccess::shutdown(allEmptyWindow);
        return false;
    }

    MainWindowPlaybackSequenceTestAccess::updateToNaturalCompletion(allEmptyWindow, 1.0F);
    const bool allEmptyStops = !MainWindowPlaybackSequenceTestAccess::activeProjection(allEmptyWindow)
        .has_value()
        && !MainWindowPlaybackSequenceTestAccess::hasSequencePosition(allEmptyWindow)
        && !allEmptyModel.activePlaylistId().isValid();
    MainWindowPlaybackSequenceTestAccess::shutdown(allEmptyWindow);

    MainWindow randomWindow{
        DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
        translationState};
    randomWindow.show();
    QCoreApplication::processEvents();
    if (!require(randomWindow.initializeEngine(), "Random-empty coverage requires an initialized Player Engine"))
    {
        return false;
    }

    WorkspaceModel& randomModel = MainWindowPlaybackSequenceTestAccess::workspaceModel(randomWindow);
    const PlaylistId playlistA = randomModel.selectedPlaylistId();
    const PlaylistId emptyPlaylist = randomModel.addPlaylist();
    const PlaylistId playlistB = randomModel.addPlaylist();
    const auto itemA = randomModel.addItem(
        playlistA,
        WorkspaceMediaType::Image,
        pathText(*imagePath),
        QStringLiteral("A"));
    const auto itemB = randomModel.addItem(
        playlistB,
        WorkspaceMediaType::Image,
        pathText(*imagePath),
        QStringLiteral("B"));
    if (!require(itemA.has_value() && itemB.has_value()
            && randomModel.setImageDisplayDuration(*itemA, 0.01)
            && randomModel.setPlaylistPlaybackMode(PlaybackSequenceMode::Random)
            && MainWindowPlaybackSequenceTestAccess::start(randomWindow, playlistA, *itemA),
            "Random-empty coverage requires A and B around one empty Playlist"))
    {
        MainWindowPlaybackSequenceTestAccess::shutdown(randomWindow);
        return false;
    }

    MainWindowPlaybackSequenceTestAccess::forceOuterRandomIndex(randomWindow, 1U);
    MainWindowPlaybackSequenceTestAccess::updateToNaturalCompletion(randomWindow, 1.0F);
    const auto& randomProjection = MainWindowPlaybackSequenceTestAccess::activeProjection(
        randomWindow);
    const bool randomSkipsEmptyPlaylist = randomModel.playlist(emptyPlaylist) != nullptr
        && randomProjection.has_value()
        && randomProjection->sourcePlaylistId == playlistB
        && randomModel.activePlaylistId() == playlistB
        && randomModel.activeItemId() == itemB;
    MainWindowPlaybackSequenceTestAccess::shutdown(randomWindow);

    return require(
        allEmptyStops && randomSkipsEmptyPlaylist,
        "All-empty playback must stop safely, and Random playback must omit empty Playlists before choosing a target");
}

}

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    ApplicationTranslationState translationState{};
    const auto arguments = application.arguments();
    if (arguments.contains(QStringLiteral("--deletion-availability")))
    {
        return verifyDeletionSuccessor(translationState, DeletionCase::Availability) ? 0 : 1;
    }
    if (arguments.contains(QStringLiteral("--deletion-reordered")))
    {
        return verifyDeletionSuccessor(translationState, DeletionCase::Reordered) ? 0 : 1;
    }
    if (arguments.contains(QStringLiteral("--deletion-no-playable")))
    {
        return verifyDeletionSuccessor(translationState, DeletionCase::NoPlayableReplacement) ? 0 : 1;
    }
    if (arguments.contains(QStringLiteral("--deletion-guards")))
    {
        return verifyDeletionGuards(translationState) ? 0 : 1;
    }
    if (arguments.contains(QStringLiteral("--volume-cluster")))
    {
        return verifyActivePlaybackVolumeCluster(translationState) ? 0 : 1;
    }
    return verifyDeletionSuccessorCases(translationState)
            && verifyDeletionGuards(translationState)
            && verifyPlayableSnapshotFiltersUnavailableTargetsAndFreshStartUsesFirstPlayable(
               translationState)
            && verifyManualAvailabilityGuardsAndActiveItemDisableStop(translationState)
            && verifyActivePlaylistDisableStopsWithoutRewritingChildren(translationState)
            && verifyReorderChangesFutureNavigationWithoutRebindingCurrentPlayback(
                translationState)
            && verifyPreviousDoesNotRestoreFilteredHistoryTarget(translationState)
            && verifyNaturalCompletionCrossesPlaylistsWithoutChangingSelection(translationState)
            && verifySameItemCompletionRestartsMediaWithoutRestartingBackgroundMusic(
                translationState)
            && verifyPausedTransportUsesControllerAndPreservesSelection(translationState)
            && verifyModeChangeOnlyAffectsFutureNavigation(translationState)
            && verifyStopClearsSequenceAndStoppedTransportIsNoOp(translationState)
            && verifyAllEmptyAndRandomEmptyResolutionStopSafely(translationState)
            && verifyActivePlaybackVolumeCluster(translationState)
        ? 0
        : 1;
}
