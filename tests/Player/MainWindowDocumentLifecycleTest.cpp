#include "BackgroundMusicWindow.h"
#include "ApplicationPreferences.h"
#include "CanvasChromeFrame.h"
#include "MainWindow.h"
#include "MainWindowTestDisplay.h"

#include "Infrastructure/Persistence/ProjectFile.h"

#include <QAction>
#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QFrame>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>

class MainWindowDocumentLifecycleTestAccess
{
public:

    enum class Choice
    {
        Save,
        Discard,
        Cancel
    };

    static DesktopSceneEngine::Player::WorkspaceModel& workspaceModel(MainWindow& window)
    {
        return window.workspaceModel_;
    }

    static DesktopSceneEngine::Player::ProjectDocumentState& documentState(MainWindow& window)
    {
        return window.documentState_;
    }

    static bool actionsAreEnabledWithStandardShortcuts(const MainWindow& window)
    {
        return window.newProjectAction_ != nullptr
            && window.openProjectAction_ != nullptr
            && window.saveProjectAction_ != nullptr
            && window.saveProjectAsAction_ != nullptr
            && window.newProjectAction_->isEnabled()
            && window.openProjectAction_->isEnabled()
            && window.saveProjectAction_->isEnabled()
            && window.saveProjectAsAction_->isEnabled()
            && window.newProjectAction_->shortcut() == QKeySequence{QKeySequence::New}
            && window.openProjectAction_->shortcut() == QKeySequence{QKeySequence::Open}
            && window.saveProjectAction_->shortcut() == QKeySequence{QKeySequence::Save}
            && window.saveProjectAsAction_->shortcut() == QKeySequence{QKeySequence::SaveAs};
    }

    static void createNewDocument(MainWindow& window)
    {
        window.createNewDocument();
    }

    static bool cleanMaybeSaveProceeds(MainWindow& window)
    {
        return window.maybeSaveCurrentDocument()
            != MainWindow::DestructiveDecision::Abort;
    }

    static bool saveCancellationAborts()
    {
        return MainWindow::decisionForSaveResult(MainWindow::SaveResult::Cancelled)
            == MainWindow::DestructiveDecision::Abort;
    }

    static void markDocumentClean(MainWindow& window)
    {
        window.documentState_.markClean(window.workspaceModel_.authoredRevision());
        window.updateDocumentTitle();
    }

    static bool applyNewWithChoice(MainWindow& window, Choice choice)
    {
        const auto decision = window.resolveUnsavedChanges(toUnsavedChangesChoice(choice));
        window.applyNewProjectDecision(decision);
        return decision != MainWindow::DestructiveDecision::Abort;
    }

    static bool loadAndCommit(MainWindow& window, const std::filesystem::path& path)
    {
        auto candidate = window.loadProjectCandidate(path);
        if (!candidate.has_value())
        {
            return false;
        }

        window.commitOpenProject(std::move(*candidate));
        return true;
    }

    static void commitProject(
        MainWindow& window,
        DesktopSceneEngine::Domain::AuthoredProject project,
        const std::filesystem::path& path)
    {
        window.commitOpenProject(MainWindow::OpenProjectCandidate{
            std::move(project),
            path});
    }

    static bool loadFails(MainWindow& window, const std::filesystem::path& path)
    {
        return !window.loadProjectCandidate(path).has_value();
    }

    static bool openFromPath(MainWindow& window, const std::filesystem::path& path)
    {
        return window.openProjectFromPath(path);
    }

    static DesktopSceneEngine::Player::BackgroundMusicWindow* openBackgroundMusicWindow(
        MainWindow& window,
        DesktopSceneEngine::Player::PlaylistId playlistId)
    {
        return window.openBackgroundMusicWindow(playlistId);
    }

    static bool openWithChoice(
        MainWindow& window,
        const std::filesystem::path& path,
        Choice choice)
    {
        auto candidate = window.loadProjectCandidate(path);
        if (!candidate.has_value())
        {
            return false;
        }

        return window.completeOpenProject(
            std::move(*candidate),
            window.resolveUnsavedChanges(toUnsavedChangesChoice(choice)));
    }

    static bool reloadAfterCandidateReplacement(
        MainWindow& window,
        const std::filesystem::path& path,
        DesktopSceneEngine::Domain::AuthoredProject replacement)
    {
        auto candidate = window.loadProjectCandidate(path);
        if (!candidate.has_value())
        {
            return false;
        }

        DesktopSceneEngine::Infrastructure::Persistence::saveAuthoredProjectFile(
            replacement,
            path);
        return window.completeOpenProject(
            std::move(*candidate),
            MainWindow::DestructiveDecision::ProceedAfterSave);
    }

    static bool closeWithChoice(MainWindow& window, Choice choice)
    {
        QCloseEvent event{};
        window.closeAfterDecision(
            &event,
            window.resolveUnsavedChanges(toUnsavedChangesChoice(choice)));
        return event.isAccepted();
    }

    static bool closeClean(MainWindow& window)
    {
        QCloseEvent event{};
        window.closeAfterDecision(&event, window.maybeSaveCurrentDocument());
        return event.isAccepted();
    }

    static bool closeAfterSaveCancellation(MainWindow& window)
    {
        QCloseEvent event{};
        window.closeAfterDecision(
            &event,
            MainWindow::decisionForSaveResult(MainWindow::SaveResult::Cancelled));
        return event.isAccepted();
    }

    static bool saveCurrent(MainWindow& window)
    {
        return window.saveCurrentProject() == MainWindow::SaveResult::Saved;
    }

    static bool saveToPath(
        MainWindow& window,
        const std::filesystem::path& path,
        bool adoptPath)
    {
        return window.saveProjectToPath(path, adoptPath) == MainWindow::SaveResult::Saved;
    }

    static void addPlaylist(MainWindow& window)
    {
        window.addPlaylist();
    }

    static void removePlaylistConfirmed(
        MainWindow& window,
        DesktopSceneEngine::Player::PlaylistId id)
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
        window.removePlaylist(id);
    }

    static bool hasEmptyLibrary(const MainWindow& window)
    {
        return window.playlistList_ != nullptr && window.itemList_ != nullptr
            && window.playlistList_->count() == 0 && window.itemList_->count() == 0;
    }

    static bool inspectorShowsNoSelection(const MainWindow& window)
    {
        if (window.inspectorContent_ == nullptr)
        {
            return false;
        }

        for (const QLabel* label : window.inspectorContent_->findChildren<QLabel*>())
        {
            if (label->text() == QStringLiteral("No selection"))
            {
                return true;
            }
        }

        return false;
    }

    static std::size_t backgroundMusicWindowCount(const MainWindow& window)
    {
        return static_cast<std::size_t>(window.backgroundMusicWindows_.size());
    }

    static bool saveToPathFails(
        MainWindow& window,
        const std::filesystem::path& path,
        bool adoptPath)
    {
        return window.saveProjectToPath(path, adoptPath) == MainWindow::SaveResult::Failed;
    }

    static void applyDisplayEnumeration(
        MainWindow& window,
        const DesktopSceneEngine::Platform::DisplayEnumerationResult& result)
    {
        window.displayEnumeratorForTesting_ = [result] { return result; };
        window.applyDisplayEnumerationResult(result);
    }

    static void refreshDisplayEnumeration(
        MainWindow& window,
        const DesktopSceneEngine::Platform::DisplayEnumerationResult& result)
    {
        window.applyDisplayEnumerationResult(result);
    }

    static DesktopSceneEngine::DisplayId boundDisplayId(
        const MainWindow& window)
    {
        return window.displaySelection_.boundDisplayId();
    }

    static QString currentCanvasAspectText(const MainWindow& window)
    {
        const auto* label = window.findChild<QLabel*>(
            QStringLiteral("currentCanvasAspectLabel"));
        return label != nullptr ? label->text() : QString{};
    }

    static void setDesktopCanvas(
        MainWindow& window,
        const DesktopSceneEngine::Player::DesktopCanvas& canvas)
    {
        window.workspaceModel_.setDesktopCanvas(canvas);
        window.updateCanvasAspectUi();
    }

    static QString targetDisplayLabelText(const MainWindow& window)
    {
        return window.mapToLabel_ != nullptr ? window.mapToLabel_->text() : QString{};
    }

    static QString boundDisplayText(const MainWindow& window)
    {
        return window.boundDisplayValueLabel_ != nullptr
            ? window.boundDisplayValueLabel_->text()
            : QString{};
    }

    static QToolButton* deployButton(MainWindow& window)
    {
        return window.findChild<QToolButton*>(QStringLiteral("deployDisplayButton"));
    }

    static bool previewCanvasHasAspect(
        MainWindow& window,
        const DesktopSceneEngine::Domain::AspectRatio& aspect)
    {
        if (window.previewViewportHost_ == nullptr)
        {
            return false;
        }

        window.previewViewportHost_->resize(1600, 1000);
        QApplication::processEvents();
        const auto* frame = window.canvasFrame_;
        if (frame == nullptr || frame->canvasRect().isEmpty())
        {
            return false;
        }

        const QSize size = frame->canvasRect().size();
        const long long difference =
            static_cast<long long>(size.width()) * aspect.denominator
            - static_cast<long long>(size.height()) * aspect.numerator;
        return std::llabs(difference) <= 16LL;
    }

    static void setSimplifiedChinese(MainWindow& window)
    {
        window.applyCommittedLanguage(MainWindow::UiLanguage::SimplifiedChinese);
    }

    static void setEnglish(MainWindow& window)
    {
        window.applyCommittedLanguage(MainWindow::UiLanguage::English);
    }

    static DesktopSceneEngine::Player::UiLanguage uiLanguage(const MainWindow& window)
    {
        return window.translationState_.activeLanguage();
    }

    static DesktopSceneEngine::CanvasOutsideMode canvasOutsideMode(const MainWindow& window)
    {
        return window.currentCanvasOutsideMode_;
    }

    static bool highlightCanvasBoundaryIsChecked(const MainWindow& window)
    {
        return window.highlightCanvasBoundaryAction_ != nullptr
            && window.highlightCanvasBoundaryAction_->isChecked();
    }

    static bool canvasBoundaryIsVisible(const MainWindow& window)
    {
        return window.canvasFrame_ != nullptr && window.canvasFrame_->isHighlighted();
    }

    static void reloadLanguage(MainWindow& window)
    {
        window.reloadCommittedLanguage();
    }

    static void triggerHighlightCanvasBoundary(MainWindow& window)
    {
        window.highlightCanvasBoundaryAction_->trigger();
    }

private:

    static MainWindow::UnsavedChangesChoice toUnsavedChangesChoice(Choice choice)
    {
        switch (choice)
        {
        case Choice::Save:
            return MainWindow::UnsavedChangesChoice::Save;
        case Choice::Discard:
            return MainWindow::UnsavedChangesChoice::Discard;
        default:
            return MainWindow::UnsavedChangesChoice::Cancel;
        }
    }
};

namespace
{

using DesktopSceneEngine::Domain::AspectRatio;
using DesktopSceneEngine::Domain::AuthoredItem;
using DesktopSceneEngine::Domain::AuthoredMediaType;
using DesktopSceneEngine::Domain::AuthoredPlaylist;
using DesktopSceneEngine::Domain::AuthoredProject;
using DesktopSceneEngine::Player::ApplicationTranslationState;
using DesktopSceneEngine::Player::WorkspaceMediaType;

constexpr const char* PlaylistIdText = "11111111-1111-4111-8111-111111111111";
constexpr const char* ItemIdText = "22222222-2222-4222-8222-222222222222";

bool require(bool condition, const char* message)
{
    if (condition)
    {
        return true;
    }

    std::cerr << "Test failed: " << message << std::endl;
    return false;
}

std::filesystem::path pathFromQString(const QString& value)
{
    return std::filesystem::path{value.toStdWString()};
}

std::filesystem::path temporaryFile(QTemporaryDir& directory, const QString& name)
{
    return pathFromQString(directory.filePath(name));
}

void dismissNextMessageBox()
{
    QTimer::singleShot(0, []
    {
        for (QWidget* widget : QApplication::topLevelWidgets())
        {
            if (auto* messageBox = qobject_cast<QMessageBox*>(widget))
            {
                messageBox->accept();
            }
        }
    });
}

AuthoredProject makeAuthoredProject(
    DesktopSceneEngine::Domain::DisplayOwner owner,
    AspectRatio canvasAspect = AspectRatio{21, 9})
{
    AuthoredProject project{std::move(owner)};
    project.desktopCanvas.aspectRatio = canvasAspect;
    project.playlistPlaybackMode = DesktopSceneEngine::Domain::PlaybackSequenceMode::Random;

    AuthoredPlaylist playlist{};
    playlist.id = DesktopSceneEngine::Domain::PlaylistId{std::string{PlaylistIdText}};
    playlist.displayAlias = "Loaded Playlist";
    playlist.masterVolume = 0.4;

    AuthoredItem item{};
    item.id = DesktopSceneEngine::Domain::ItemId{std::string{ItemIdText}};
    item.displayAlias = "Loaded Item";
    item.mediaType = AuthoredMediaType::Image;
    item.source = "media/loaded.png";
    item.imageDurationSeconds = 12.0;
    playlist.items.push_back(std::move(item));
    project.playlists.push_back(std::move(playlist));
    return project;
}

AuthoredProject makeAuthoredProject(AspectRatio canvasAspect = AspectRatio{21, 9})
{
    return makeAuthoredProject(
        DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayOwner(),
        canvasAspect);
}

DesktopSceneEngine::Domain::DisplayOwner differentDisplayOwner()
{
    return *DesktopSceneEngine::Domain::DisplayOwner::fromDisplayId(
        DesktopSceneEngine::DisplayId::fromTargetDevicePath(
            "\\\\?\\DISPLAY#MAIN_WINDOW_TEST#DISPLAY_2"));
}

DesktopSceneEngine::Platform::DisplayEnumerationResult syntheticDisplayResult(
    int pixelWidth = 1920,
    int pixelHeight = 1080,
    const char* sourceDeviceName = "\\\\.\\DISPLAY-C2")
{
    DesktopSceneEngine::Platform::DisplayDescriptor descriptor{};
    descriptor.id = DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId();
    descriptor.friendlyName = "C2 Display";
    descriptor.sourceDeviceName = sourceDeviceName;
    descriptor.pixelWidth = pixelWidth;
    descriptor.pixelHeight = pixelHeight;
    descriptor.primary = true;
    descriptor.displayNumber = 1;
    return {
        DesktopSceneEngine::Platform::DisplayEnumerationStatus::Success,
        DesktopSceneEngine::Platform::DisplaySnapshot{{descriptor}}};
}

bool verifyFileActionsAndCleanNew(ApplicationTranslationState& translationState)
{
    MainWindow window{
        DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
        translationState};
    if (!require(window.windowTitle()
                == QStringLiteral("DesktopSceneEngine \u2014 Untitled[*]")
            && !window.isWindowModified(),
            "Startup must show a clean Untitled document title"))
    {
        return false;
    }

    auto& model = MainWindowDocumentLifecycleTestAccess::workspaceModel(window);
    auto& state = MainWindowDocumentLifecycleTestAccess::documentState(window);
    const auto displayResult = syntheticDisplayResult();
    MainWindowDocumentLifecycleTestAccess::applyDisplayEnumeration(window, displayResult);
    const auto selectedDisplay = MainWindowDocumentLifecycleTestAccess::boundDisplayId(window);

    const auto playlistId = model.addPlaylist();
    const auto itemId = model.addItem(
        playlistId,
        WorkspaceMediaType::Image,
        QStringLiteral("C:/Media/new-reset.png"));
    const auto layerId = itemId.has_value()
        ? model.addExternalMediaLayer(*itemId, QStringLiteral("C:/Media/overlay.png"))
        : std::nullopt;
    if (!require(itemId.has_value() && layerId.has_value() && model.activateItem(*itemId),
            "Clean New setup requires authored and transient state"))
    {
        return false;
    }

    state.currentPath = std::filesystem::path{L"C:\\Projects\\old.json"};
    state.markClean(model.authoredRevision());
    MainWindowDocumentLifecycleTestAccess::createNewDocument(window);

    return require(
               MainWindowDocumentLifecycleTestAccess::actionsAreEnabledWithStandardShortcuts(
                   window),
               "File actions must be enabled with conventional standard shortcuts")
        && require(model.authoredProject().playlists.empty() && model.playlists().isEmpty(),
            "New must replace the current document with an empty authored project")
        && require(!state.currentPath.has_value()
                && !state.isDirty(model.authoredRevision()),
            "New must establish a clean, untitled document")
        && require(!model.selectedPlaylistId().isValid()
                && !model.selectedItemId().has_value()
                && !model.activePlaylistId().isValid()
                && !model.activeItemId().has_value()
                && model.mediaLayer(*layerId) == nullptr,
            "New must clear selected, active, and transient document state")
        && require(model.desktopCanvas().aspectRatio == AspectRatio{16, 9},
            "New must restore the authored default 16:9 Canvas")
        && require(MainWindowDocumentLifecycleTestAccess::boundDisplayId(window)
                == selectedDisplay,
            "New must preserve machine-local Display selection")
        && require(model.authoredProject().displayOwner
                == DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayOwner(),
            "New must preserve the Session DisplayOwner");
}

bool verifyFinalPlaylistDeletionAndEmptyProjectRoundTrip(
    QTemporaryDir& directory,
    ApplicationTranslationState& translationState)
{
    MainWindow window{
        DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
        translationState};
    auto& model = MainWindowDocumentLifecycleTestAccess::workspaceModel(window);
    auto& state = MainWindowDocumentLifecycleTestAccess::documentState(window);
    MainWindowDocumentLifecycleTestAccess::addPlaylist(window);
    const auto finalPlaylistId = model.selectedPlaylistId();
    const auto finalItemId = model.addItem(
        finalPlaylistId,
        WorkspaceMediaType::Image,
        QStringLiteral("C:/Media/final-delete.png"));
    if (!require(finalPlaylistId.isValid() && finalItemId.has_value()
            && model.activatePlaylist(finalPlaylistId)
            && MainWindowDocumentLifecycleTestAccess::openBackgroundMusicWindow(
                window,
                finalPlaylistId) != nullptr,
            "Final Playlist deletion setup requires selected, active, and BGM window state"))
    {
        return false;
    }

    MainWindowDocumentLifecycleTestAccess::markDocumentClean(window);
    const std::uint64_t revisionBeforeDelete = model.authoredRevision();
    MainWindowDocumentLifecycleTestAccess::removePlaylistConfirmed(window, finalPlaylistId);
    QApplication::processEvents();
    if (!require(model.authoredProject().playlists.empty() && model.playlists().isEmpty()
            && !model.selectedPlaylistId().isValid()
            && !model.selectedItemId().has_value()
            && !model.activePlaylistId().isValid()
            && !model.activeItemId().has_value(),
            "Final Playlist deletion must leave the Workspace in its valid empty state")
        || !require(model.authoredRevision() == revisionBeforeDelete + 1U
                && state.isDirty(model.authoredRevision()) && window.isWindowModified(),
            "Final Playlist deletion must be a dirty authored mutation")
        || !require(MainWindowDocumentLifecycleTestAccess::hasEmptyLibrary(window)
                && MainWindowDocumentLifecycleTestAccess::inspectorShowsNoSelection(window),
            "Final Playlist deletion must refresh the empty Library and Inspector")
        || !require(MainWindowDocumentLifecycleTestAccess::backgroundMusicWindowCount(window) == 0U,
            "Final Playlist deletion must close its BGM editor before invalidating its identity"))
    {
        return false;
    }

    MainWindowDocumentLifecycleTestAccess::addPlaylist(window);
    const auto replacementPlaylistId = model.selectedPlaylistId();
    const auto replacementItemId = model.addItem(
        replacementPlaylistId,
        WorkspaceMediaType::Image,
        QStringLiteral("C:/Media/after-final-delete.png"));
    const auto* replacementPlaylist = model.playlist(replacementPlaylistId);
    if (!require(replacementPlaylistId != finalPlaylistId && replacementPlaylist != nullptr
            && replacementPlaylist->displayAlias == QStringLiteral("Playlist B")
            && replacementPlaylist->globalMusicQueue.enabled
            && replacementItemId.has_value(),
            "Adding after final deletion must use the monotonic alias and clean defaults"))
    {
        return false;
    }

    MainWindowDocumentLifecycleTestAccess::removePlaylistConfirmed(window, replacementPlaylistId);
    const std::filesystem::path path = temporaryFile(
        directory,
        QStringLiteral("final-playlist-deletion.json"));
    if (!require(model.authoredProject().playlists.empty()
            && MainWindowDocumentLifecycleTestAccess::saveToPath(window, path, true),
            "An empty project after final Playlist deletion must save"))
    {
        return false;
    }

    MainWindow reopened{
        DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
        translationState};
    if (!require(MainWindowDocumentLifecycleTestAccess::loadAndCommit(reopened, path),
            "A saved empty project must reopen"))
    {
        return false;
    }

    const auto& reopenedModel = MainWindowDocumentLifecycleTestAccess::workspaceModel(reopened);
    const auto& reopenedState = MainWindowDocumentLifecycleTestAccess::documentState(reopened);
    return require(reopenedModel.authoredProject().playlists.empty()
            && reopenedModel.playlists().isEmpty()
            && !reopenedModel.selectedPlaylistId().isValid()
            && !reopenedModel.activePlaylistId().isValid()
            && MainWindowDocumentLifecycleTestAccess::hasEmptyLibrary(reopened)
            && MainWindowDocumentLifecycleTestAccess::inspectorShowsNoSelection(reopened)
            && !reopenedState.isDirty(reopenedModel.authoredRevision()),
        "Reopening an empty project must not create a replacement Playlist");
}

bool verifyDirtyDecisionAndNewMatrix(
    QTemporaryDir& directory,
    ApplicationTranslationState& translationState)
{
    {
        MainWindow window{
            DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
            translationState};
        if (!require(MainWindowDocumentLifecycleTestAccess::cleanMaybeSaveProceeds(window),
                "A clean document must proceed without destructive prompting"))
        {
            return false;
        }
    }

    {
        MainWindow window{
            DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
            translationState};
        auto& model = MainWindowDocumentLifecycleTestAccess::workspaceModel(window);
        auto& state = MainWindowDocumentLifecycleTestAccess::documentState(window);
        const auto playlistId = model.addPlaylist();
        const auto itemId = model.addItem(
            playlistId,
            WorkspaceMediaType::Image,
            QStringLiteral("C:/Media/new-save.png"));
        const std::filesystem::path path = temporaryFile(
            directory,
            QStringLiteral("new-save.json"));
        state.currentPath = path;
        if (!require(window.isWindowModified(),
                "An authored mutation must immediately set the title modified state")
            || !require(MainWindowDocumentLifecycleTestAccess::applyNewWithChoice(
                    window,
                    MainWindowDocumentLifecycleTestAccess::Choice::Save),
                "Dirty New with a successful Save must proceed")
            || !require(std::filesystem::exists(path),
                "Dirty New Save must persist the current document before replacement")
            || !require(model.authoredProject().playlists.empty()
                    && !state.currentPath.has_value()
                    && !state.isDirty(model.authoredRevision()),
                "Dirty New Save must finish with a clean untitled document")
            || !require(window.windowTitle()
                    == QStringLiteral("DesktopSceneEngine \u2014 Untitled[*]")
                    && !window.isWindowModified(),
                "Successful New must refresh the clean Untitled title")
            || !require(playlistId.isValid() && itemId.has_value(),
                "Dirty New Save setup must create valid authored content"))
        {
            return false;
        }
    }

    {
        MainWindow window{
            DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
            translationState};
        auto& model = MainWindowDocumentLifecycleTestAccess::workspaceModel(window);
        (void)model.addPlaylist();
        if (!require(MainWindowDocumentLifecycleTestAccess::applyNewWithChoice(
                    window,
                    MainWindowDocumentLifecycleTestAccess::Choice::Discard),
                "Dirty New with Discard must proceed")
            || !require(model.authoredProject().playlists.empty(),
                "Dirty New with Discard must replace without saving"))
        {
            return false;
        }
    }

    {
        MainWindow window{
            DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
            translationState};
        auto& model = MainWindowDocumentLifecycleTestAccess::workspaceModel(window);
        const auto playlistId = model.addPlaylist();
        const std::uint64_t revision = model.authoredRevision();
        if (!require(!MainWindowDocumentLifecycleTestAccess::applyNewWithChoice(
                    window,
                    MainWindowDocumentLifecycleTestAccess::Choice::Cancel),
                "Dirty New with Cancel must abort")
            || !require(model.authoredRevision() == revision
                    && model.playlist(playlistId) != nullptr,
                "Dirty New Cancel must leave the current document unchanged"))
        {
            return false;
        }
    }

    {
        MainWindow window{
            DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
            translationState};
        auto& model = MainWindowDocumentLifecycleTestAccess::workspaceModel(window);
        auto& state = MainWindowDocumentLifecycleTestAccess::documentState(window);
        const auto playlistId = model.addPlaylist();
        const auto itemId = model.addItem(
            playlistId,
            WorkspaceMediaType::Image,
            QStringLiteral("C:/Media/new-save-failure.png"));
        if (!require(itemId.has_value() && model.activateItem(*itemId),
                "Dirty New Save-failure setup requires active valid authored content"))
        {
            return false;
        }
        const std::uint64_t revision = model.authoredRevision();
        const std::filesystem::path badPath = temporaryFile(
            directory,
            QStringLiteral("new-missing-parent/project.json"));
        state.currentPath = badPath;
        dismissNextMessageBox();
        if (!require(!MainWindowDocumentLifecycleTestAccess::applyNewWithChoice(
                    window,
                    MainWindowDocumentLifecycleTestAccess::Choice::Save),
                "Dirty New must abort after Save failure")
            || !require(model.authoredRevision() == revision
                    && model.playlist(playlistId) != nullptr
                    && model.activeItemId() == itemId
                    && state.currentPath == badPath
                    && state.isDirty(model.authoredRevision()),
                "Dirty New Save failure must preserve the current document"))
        {
            return false;
        }
    }

    return require(MainWindowDocumentLifecycleTestAccess::saveCancellationAborts(),
        "Save As cancellation must map to Abort for every destructive caller");
}

bool verifyAuthoredTitleNotification(ApplicationTranslationState& translationState)
{
    MainWindow window{
        DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
        translationState};
    auto& model = MainWindowDocumentLifecycleTestAccess::workspaceModel(window);
    const auto playlistId = model.addPlaylist();
    MainWindowDocumentLifecycleTestAccess::markDocumentClean(window);
    if (!require(!window.isWindowModified(),
            "Title notification setup must establish a clean revision baseline"))
    {
        return false;
    }

    const auto trackId = model.addPlaylistMusicTrack(
        playlistId,
        QStringLiteral("C:/Music/title-notification.m4a"));
    return require(trackId.has_value() && window.isWindowModified(),
        "A BGM-authored mutation must immediately refresh the title modified state");
}

bool verifyOpenDecisionMatrixAndStaleReload(
    QTemporaryDir& directory,
    ApplicationTranslationState& translationState)
{
    const std::filesystem::path candidatePath = temporaryFile(
        directory,
        QStringLiteral("open-candidate.json"));
    DesktopSceneEngine::Infrastructure::Persistence::saveAuthoredProjectFile(
        makeAuthoredProject(),
        candidatePath);

    {
        MainWindow window{
            DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
            translationState};
        auto& model = MainWindowDocumentLifecycleTestAccess::workspaceModel(window);
        auto& state = MainWindowDocumentLifecycleTestAccess::documentState(window);
        const auto currentPlaylistId = model.addPlaylist();
        const auto currentItemId = model.addItem(
            currentPlaylistId,
            WorkspaceMediaType::Image,
            QStringLiteral("C:/Media/open-save-failure.png"));
        if (!require(currentItemId.has_value() && model.activateItem(*currentItemId),
                "Open Save-failure setup requires active valid authored content"))
        {
            return false;
        }
        const std::uint64_t revision = model.authoredRevision();
        if (!require(!MainWindowDocumentLifecycleTestAccess::openWithChoice(
                    window,
                    candidatePath,
                    MainWindowDocumentLifecycleTestAccess::Choice::Cancel),
                "Dirty Open with Cancel must abort")
            || !require(model.authoredRevision() == revision
                    && model.playlist(currentPlaylistId) != nullptr
                    && model.activeItemId() == currentItemId
                    && !state.currentPath.has_value(),
                "Dirty Open Cancel must preserve the current document"))
        {
            return false;
        }
    }

    {
        MainWindow window{
            DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
            translationState};
        auto& model = MainWindowDocumentLifecycleTestAccess::workspaceModel(window);
        (void)model.addPlaylist();
        if (!require(MainWindowDocumentLifecycleTestAccess::openWithChoice(
                    window,
                    candidatePath,
                    MainWindowDocumentLifecycleTestAccess::Choice::Discard),
                "Dirty Open with Discard must commit the validated candidate")
            || !require(model.authoredProject().playlists.size() == 1U
                    && model.authoredProject().playlists.front().displayAlias
                        == "Loaded Playlist",
                "Dirty Open Discard must use the preloaded candidate without saving"))
        {
            return false;
        }
    }

    {
        MainWindow window{
            DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
            translationState};
        auto& model = MainWindowDocumentLifecycleTestAccess::workspaceModel(window);
        auto& state = MainWindowDocumentLifecycleTestAccess::documentState(window);
        const auto currentPlaylistId = model.addPlaylist();
        const auto currentItemId = model.addItem(
            currentPlaylistId,
            WorkspaceMediaType::Image,
            QStringLiteral("C:/Media/open-save-failure.png"));
        if (!require(currentItemId.has_value() && model.activateItem(*currentItemId),
                "Open Save-failure setup requires active valid authored content"))
        {
            return false;
        }
        const std::uint64_t revision = model.authoredRevision();
        const std::filesystem::path badSavePath = temporaryFile(
            directory,
            QStringLiteral("open-missing-parent/project.json"));
        state.currentPath = badSavePath;
        dismissNextMessageBox();
        if (!require(!MainWindowDocumentLifecycleTestAccess::openWithChoice(
                    window,
                    candidatePath,
                    MainWindowDocumentLifecycleTestAccess::Choice::Save),
                "Dirty Open must abort when Save fails")
            || !require(model.authoredRevision() == revision
                    && model.playlist(currentPlaylistId) != nullptr
                    && model.activeItemId() == currentItemId
                    && state.currentPath == badSavePath,
                "Open Save failure must not commit the candidate"))
        {
            return false;
        }
    }

    {
        MainWindow window{
            DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
            translationState};
        if (!require(MainWindowDocumentLifecycleTestAccess::loadAndCommit(
                    window,
                    candidatePath),
                "Same-path stale-candidate setup must open the baseline file"))
        {
            return false;
        }

        auto& model = MainWindowDocumentLifecycleTestAccess::workspaceModel(window);
        const auto playlistId = model.playlists().front().id;
        if (!require(model.renamePlaylist(
                    playlistId,
                    QStringLiteral("Saved Current State")),
                "Same-path stale-candidate setup must dirty the current document")
            || !require(MainWindowDocumentLifecycleTestAccess::openWithChoice(
                    window,
                    candidatePath,
                    MainWindowDocumentLifecycleTestAccess::Choice::Save),
                "Same-path Open with Save must succeed after reloading")
            || !require(model.authoredProject().playlists.front().displayAlias
                    == "Saved Current State",
                "Open after Save must reload and must not commit the stale candidate")
            || !require(!window.isWindowModified(),
                "Same-path Save and reload must leave the reopened document clean"))
        {
            return false;
        }
    }

    {
        MainWindow window{
            DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
            translationState};
        auto& model = MainWindowDocumentLifecycleTestAccess::workspaceModel(window);
        const auto currentPlaylistId = model.addPlaylist();
        const std::uint64_t revision = model.authoredRevision();
        const std::filesystem::path malformed = temporaryFile(
            directory,
            QStringLiteral("open-malformed-before-prompt.json"));
        {
            std::ofstream output{malformed, std::ios::binary | std::ios::trunc};
            output << "{not-json";
        }
        dismissNextMessageBox();
        if (!require(!MainWindowDocumentLifecycleTestAccess::openFromPath(
                    window,
                    malformed),
                "Malformed Open candidate must fail before dirty resolution")
            || !require(model.authoredRevision() == revision
                    && model.playlist(currentPlaylistId) != nullptr
                    && window.isWindowModified(),
                "Invalid candidate loading must preserve the dirty current document"))
        {
            return false;
        }
    }

    return true;
}

bool verifyCloseDecisionMatrix(
    QTemporaryDir& directory,
    ApplicationTranslationState& translationState)
{
    {
        MainWindow window{
            DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
            translationState};
        if (!require(MainWindowDocumentLifecycleTestAccess::closeClean(window),
                "Clean close must proceed without prompting"))
        {
            return false;
        }
    }

    {
        MainWindow window{
            DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
            translationState};
        auto& model = MainWindowDocumentLifecycleTestAccess::workspaceModel(window);
        const auto playlistId = model.addPlaylist();
        if (!require(!MainWindowDocumentLifecycleTestAccess::closeWithChoice(
                    window,
                    MainWindowDocumentLifecycleTestAccess::Choice::Cancel),
                "Dirty close with Cancel must ignore the close event")
            || !require(model.playlist(playlistId) != nullptr && window.isWindowModified(),
                "Close Cancel must leave the application document operational"))
        {
            return false;
        }
    }

    {
        MainWindow window{
            DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
            translationState};
        auto& model = MainWindowDocumentLifecycleTestAccess::workspaceModel(window);
        (void)model.addPlaylist();
        if (!require(MainWindowDocumentLifecycleTestAccess::closeWithChoice(
                    window,
                    MainWindowDocumentLifecycleTestAccess::Choice::Discard),
                "Dirty close with Discard must accept the close event"))
        {
            return false;
        }
    }

    {
        MainWindow window{
            DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
            translationState};
        auto& model = MainWindowDocumentLifecycleTestAccess::workspaceModel(window);
        auto& state = MainWindowDocumentLifecycleTestAccess::documentState(window);
        const auto playlistId = model.addPlaylist();
        const auto itemId = model.addItem(
            playlistId,
            WorkspaceMediaType::Image,
            QStringLiteral("C:/Media/close-save.png"));
        if (!require(itemId.has_value(),
                "Close Save setup must create valid authored content"))
        {
            return false;
        }
        state.currentPath = temporaryFile(directory, QStringLiteral("close-save.json"));
        if (!require(MainWindowDocumentLifecycleTestAccess::closeWithChoice(
                    window,
                    MainWindowDocumentLifecycleTestAccess::Choice::Save),
                "Dirty close with successful Save must proceed")
            || !require(!state.isDirty(model.authoredRevision()),
                "Close Save must mark the document clean before shutdown"))
        {
            return false;
        }
    }

    {
        MainWindow window{
            DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
            translationState};
        auto& model = MainWindowDocumentLifecycleTestAccess::workspaceModel(window);
        auto& state = MainWindowDocumentLifecycleTestAccess::documentState(window);
        const auto playlistId = model.addPlaylist();
        const auto itemId = model.addItem(
            playlistId,
            WorkspaceMediaType::Image,
            QStringLiteral("C:/Media/close-save-failure.png"));
        if (!require(itemId.has_value(),
                "Close Save-failure setup must create valid authored content"))
        {
            return false;
        }
        state.currentPath = temporaryFile(
            directory,
            QStringLiteral("close-missing-parent/project.json"));
        dismissNextMessageBox();
        if (!require(!MainWindowDocumentLifecycleTestAccess::closeWithChoice(
                    window,
                    MainWindowDocumentLifecycleTestAccess::Choice::Save),
                "Close must be ignored after Save failure")
            || !require(state.isDirty(model.authoredRevision()),
                "Close Save failure must retain dirty state"))
        {
            return false;
        }
    }

    {
        MainWindow window{
            DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
            translationState};
        auto& model = MainWindowDocumentLifecycleTestAccess::workspaceModel(window);
        (void)model.addPlaylist();
        if (!require(!MainWindowDocumentLifecycleTestAccess::closeAfterSaveCancellation(window),
                "Close must be ignored after Save As cancellation"))
        {
            return false;
        }
    }

    return true;
}

bool verifyV3OpenAndCanvasPreservation(
    QTemporaryDir& directory,
    ApplicationTranslationState& translationState)
{
    const std::filesystem::path path = temporaryFile(
        directory,
        QString::fromUtf8("\xE9\xA1\xB9\xE7\x9B\xAE-v3.json"));
    DesktopSceneEngine::Infrastructure::Persistence::saveAuthoredProjectFile(
        makeAuthoredProject(),
        path);

    MainWindow window{
        DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
        translationState};
    const auto displayResult = syntheticDisplayResult();
    MainWindowDocumentLifecycleTestAccess::applyDisplayEnumeration(window, displayResult);
    const auto selectedDisplay = MainWindowDocumentLifecycleTestAccess::boundDisplayId(window);

    if (!require(MainWindowDocumentLifecycleTestAccess::loadAndCommit(window, path),
            "A valid same-owner v3 file must load and commit"))
    {
        return false;
    }

    const auto& model = MainWindowDocumentLifecycleTestAccess::workspaceModel(window);
    const auto& state = MainWindowDocumentLifecycleTestAccess::documentState(window);
    return require(model.authoredProject().playlists.size() == 1U
            && model.authoredProject().playlists.front().id.value() == PlaylistIdText
            && model.authoredProject().playlists.front().items.front().id.value() == ItemIdText,
        "V3 Open must preserve loaded strong identities and authored fields")
        && require(state.currentPath == path
                && !state.isDirty(model.authoredRevision()),
            "V3 Open must adopt the path and mark the document clean")
        && require(!model.selectedPlaylistId().isValid()
                && !model.activePlaylistId().isValid()
                && !model.selectedItemId().has_value()
                && !model.activeItemId().has_value(),
            "Open must not auto-select or auto-activate loaded content")
        && require(model.desktopCanvas().aspectRatio == AspectRatio{21, 9},
            "Open must preserve the loaded authored Canvas")
        && require(MainWindowDocumentLifecycleTestAccess::boundDisplayId(window)
                == selectedDisplay,
            "Open must preserve machine-local Display selection")
        && require(window.windowTitle()
                == QString::fromUtf8("DesktopSceneEngine \xE2\x80\x94 \xE9\xA1\xB9\xE7\x9B\xAE-v3.json[*]")
                && !window.isWindowModified(),
            "V3 Open must show the filename and a clean title state");
}

bool verifyBackgroundMusicEnabledSaveReopenAndRetranslation(
    QTemporaryDir& directory,
    ApplicationTranslationState& translationState)
{
    const std::filesystem::path path = temporaryFile(
        directory,
        QStringLiteral("bgm-enabled-v3.json"));

    MainWindow sourceWindow{
        DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
        translationState};
    auto& sourceModel = MainWindowDocumentLifecycleTestAccess::workspaceModel(sourceWindow);
    const auto playlistId = sourceModel.addPlaylist();
    const auto itemId = sourceModel.addItem(
        playlistId,
        WorkspaceMediaType::Image,
        QStringLiteral("media/still.png"),
        QStringLiteral("Still"));
    if (!require(itemId.has_value()
            && sourceModel.setPlaylistMusicEnabled(playlistId, false)
            && sourceModel.setItemMusicEnabled(*itemId, false)
            && MainWindowDocumentLifecycleTestAccess::saveToPath(sourceWindow, path, true),
            "BGM Enabled persistence testing requires a saved authored project"))
    {
        return false;
    }

    MainWindow reopenedWindow{
        DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
        translationState};
    if (!require(MainWindowDocumentLifecycleTestAccess::loadAndCommit(reopenedWindow, path),
            "The saved BGM Enabled project must reopen successfully"))
    {
        return false;
    }

    auto& reopenedModel = MainWindowDocumentLifecycleTestAccess::workspaceModel(reopenedWindow);
    auto* const editor = MainWindowDocumentLifecycleTestAccess::openBackgroundMusicWindow(
        reopenedWindow,
        playlistId);
    auto* const playlistEnabled = editor != nullptr
        ? editor->findChild<QCheckBox*>(QStringLiteral("playlistMusicEnabledCheckBox"))
        : nullptr;
    auto* const itemEnabled = editor != nullptr
        ? editor->findChild<QCheckBox*>(QStringLiteral("itemMusicEnabledCheckBox"))
        : nullptr;
    if (!require(editor != nullptr && playlistEnabled != nullptr && itemEnabled != nullptr
            && !reopenedModel.playlistMusicQueue(playlistId)->enabled
            && !reopenedModel.itemMusicQueue(*itemId)->enabled
            && !playlistEnabled->isChecked() && !itemEnabled->isChecked(),
            "Save and reopen must restore both authored BGM Enabled values in the editor"))
    {
        return false;
    }

    MainWindowDocumentLifecycleTestAccess::setSimplifiedChinese(reopenedWindow);
    QCoreApplication::processEvents();
    if (!require(playlistEnabled->text() == QString::fromUtf8("\xE5\x90\xAF\xE7\x94\xA8")
            && itemEnabled->text() == QString::fromUtf8("\xE5\x90\xAF\xE7\x94\xA8")
            && !playlistEnabled->isChecked() && !itemEnabled->isChecked(),
            "Chinese retranslation must update Enabled labels without changing authored state"))
    {
        return false;
    }

    MainWindowDocumentLifecycleTestAccess::setEnglish(reopenedWindow);
    QCoreApplication::processEvents();
    if (!require(playlistEnabled->text() == QStringLiteral("Enabled")
            && itemEnabled->text() == QStringLiteral("Enabled")
            && !playlistEnabled->isChecked() && !itemEnabled->isChecked(),
            "English retranslation must restore labels while preserving checkbox state"))
    {
        return false;
    }

    const std::uint64_t revision = reopenedModel.authoredRevision();
    playlistEnabled->click();
    return require(reopenedModel.playlistMusicQueue(playlistId)->enabled
            && !reopenedModel.itemMusicQueue(*itemId)->enabled
            && reopenedModel.authoredRevision() == revision + 1U
            && reopenedWindow.isWindowModified(),
        "A real Enabled toggle must use authored revision tracking to mark the document dirty");
}

bool verifyCanvasAndTargetDisplayUi(ApplicationTranslationState& translationState)
{
    {
        MainWindow window{
            DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
            translationState};
        if (!require(
                MainWindowDocumentLifecycleTestAccess::currentCanvasAspectText(window)
                    == QStringLiteral("Current Canvas Aspect \u2014 16:9"),
                "Startup must display the authored default 16:9 Canvas"))
        {
            return false;
        }

        const auto displayResult = syntheticDisplayResult(
            2560,
            1600,
            "\\\\.\\DISPLAY-CANVAS-16-10");
        MainWindowDocumentLifecycleTestAccess::applyDisplayEnumeration(window, displayResult);
        const auto selectedDisplay =
            MainWindowDocumentLifecycleTestAccess::boundDisplayId(window);
        MainWindowDocumentLifecycleTestAccess::commitProject(
            window,
            makeAuthoredProject(AspectRatio{16, 9}),
            std::filesystem::path{L"C:\\Projects\\canvas-16-9.json"});

        auto& model = MainWindowDocumentLifecycleTestAccess::workspaceModel(window);
        const std::uint64_t revision = model.authoredRevision();
        MainWindowDocumentLifecycleTestAccess::refreshDisplayEnumeration(window, displayResult);
        auto* deploy = MainWindowDocumentLifecycleTestAccess::deployButton(window);
        if (!require(model.desktopCanvas().aspectRatio == AspectRatio{16, 9},
                "Open must preserve the authored 16:9 Canvas")
            || !require(
                MainWindowDocumentLifecycleTestAccess::previewCanvasHasAspect(
                    window,
                    AspectRatio{16, 9}),
                "Preview must reflect the authored 16:9 Canvas")
            || !require(
                MainWindowDocumentLifecycleTestAccess::currentCanvasAspectText(window)
                    == QStringLiteral("Current Canvas Aspect \u2014 16:9"),
                "Canvas readout must display the authored 16:9 aspect")
            || !require(
                MainWindowDocumentLifecycleTestAccess::targetDisplayLabelText(window)
                    == QStringLiteral("Target Display"),
                "The physical display control must be labeled Target Display")
            || !require(
                MainWindowDocumentLifecycleTestAccess::boundDisplayText(window)
                    .contains(QStringLiteral("2560\u00d71600")),
                "Target Display must retain its independent physical 16:10 resolution")
            || !require(
                MainWindowDocumentLifecycleTestAccess::boundDisplayId(window)
                    == selectedDisplay,
                "Open and passive refresh must preserve the selected DisplayId")
            || !require(model.authoredRevision() == revision,
                "Passive Display refresh must not mutate the authored Canvas revision")
            || !require(deploy != nullptr && deploy->text() == QStringLiteral("Deploy")
                    && !deploy->isEnabled(),
                "Deploy must exist as a disabled UI-only placeholder"))
        {
            return false;
        }

        deploy->click();
        if (!require(model.authoredRevision() == revision
                && MainWindowDocumentLifecycleTestAccess::boundDisplayId(window)
                    == selectedDisplay,
                "Disabled Deploy must not mutate Canvas or Display state"))
        {
            return false;
        }

        MainWindowDocumentLifecycleTestAccess::createNewDocument(window);
        if (!require(model.desktopCanvas().aspectRatio == AspectRatio{8, 5}
                && MainWindowDocumentLifecycleTestAccess::currentCanvasAspectText(window)
                    == QStringLiteral("Current Canvas Aspect \u2014 8:5")
                && MainWindowDocumentLifecycleTestAccess::boundDisplayId(window)
                    == selectedDisplay
                && MainWindowDocumentLifecycleTestAccess::boundDisplayText(window)
                    .contains(QStringLiteral("2560\u00d71600")),
                "New must seed Canvas from the exact bound native display while preserving identity"))
        {
            return false;
        }

        MainWindowDocumentLifecycleTestAccess::setSimplifiedChinese(window);
        if (!require(
                MainWindowDocumentLifecycleTestAccess::currentCanvasAspectText(window)
                    == QString::fromUtf8("\xE5\xBD\x93\xE5\x89\x8D\xE7\x94\xBB\xE5\xB8\x83\xE6\xAF\x94\xE4\xBE\x8B \xE2\x80\x94 8:5"),
                "Chinese UI must translate the Current Canvas Aspect readout")
            || !require(
                MainWindowDocumentLifecycleTestAccess::targetDisplayLabelText(window)
                    == QString::fromUtf8("\xE7\x9B\xAE\xE6\xA0\x87\xE6\x98\xBE\xE7\xA4\xBA\xE5\x99\xA8"),
                "Chinese UI must translate Target Display")
            || !require(deploy->text()
                    == QString::fromUtf8("\xE9\x83\xA8\xE7\xBD\xB2")
                    && !deploy->isEnabled(),
                "Chinese UI must translate the disabled Deploy placeholder")
            || !require(
                MainWindowDocumentLifecycleTestAccess::boundDisplayText(window)
                    .contains(QString::fromUtf8(
                        "\xE6\x98\xBE\xE7\xA4\xBA\xE5\x99\xA8" "1"))
                    && MainWindowDocumentLifecycleTestAccess::boundDisplayText(window)
                        .contains(QString::fromUtf8("\xE4\xB8\xBB\xE6\x98\xBE\xE7\xA4\xBA\xE5\x99\xA8")),
                "Chinese Target Display text must retain localized display and Primary markers"))
        {
            return false;
        }
    }

    static_cast<void>(translationState.setLanguage(
        DesktopSceneEngine::Player::UiLanguage::English));

    {
        MainWindow window{
            DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
            translationState};
        const auto displayResult = syntheticDisplayResult(
            1920,
            1080,
            "\\\\.\\DISPLAY-CANVAS-16-9");
        MainWindowDocumentLifecycleTestAccess::applyDisplayEnumeration(window, displayResult);
        const auto selectedDisplay =
            MainWindowDocumentLifecycleTestAccess::boundDisplayId(window);
        MainWindowDocumentLifecycleTestAccess::commitProject(
            window,
            makeAuthoredProject(AspectRatio{16, 10}),
            std::filesystem::path{L"C:\\Projects\\canvas-16-10.json"});

        const auto& model = MainWindowDocumentLifecycleTestAccess::workspaceModel(window);
        if (!require(model.desktopCanvas().aspectRatio == AspectRatio{16, 10}
                && MainWindowDocumentLifecycleTestAccess::currentCanvasAspectText(window)
                    == QStringLiteral("Current Canvas Aspect \u2014 16:10"),
                "Canvas readout must update immediately to the opened 16:10 project")
            || !require(
                MainWindowDocumentLifecycleTestAccess::previewCanvasHasAspect(
                    window,
                    AspectRatio{16, 10}),
                "Preview must update immediately to the opened 16:10 project")
            || !require(
                MainWindowDocumentLifecycleTestAccess::boundDisplayText(window)
                    .contains(QStringLiteral("1920\u00d71080"))
                    && MainWindowDocumentLifecycleTestAccess::boundDisplayId(window)
                        == selectedDisplay,
                "Opening a 16:10 Canvas must retain the independent physical 16:9 target"))
        {
            return false;
        }
    }

    return true;
}

bool verifyUnsupportedPreReleaseOpen(
    QTemporaryDir& directory,
    ApplicationTranslationState& translationState)
{
    MainWindow window{
        DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
        translationState};
    auto& model = MainWindowDocumentLifecycleTestAccess::workspaceModel(window);
    auto& state = MainWindowDocumentLifecycleTestAccess::documentState(window);
    const auto playlistId = model.addPlaylist();
    const std::uint64_t revision = model.authoredRevision();
    const std::uint64_t cleanRevision = state.cleanRevision;

    bool result = true;
    for (const char* version : {"1.0", "2.0"})
    {
        const std::filesystem::path path = temporaryFile(
            directory,
            version[0] == '1'
                ? QStringLiteral("pre-release-v1.json")
                : QStringLiteral("pre-release-v2.json"));
        {
            std::ofstream output{path, std::ios::binary | std::ios::trunc};
            output << "{\"type\":\"DesktopSceneEngineProject\",\"version\":\""
                << version << "\"}";
        }

        dismissNextMessageBox();
        result = require(
            !MainWindowDocumentLifecycleTestAccess::openFromPath(window, path),
            "v1/v2 authored Open must reject before document handling") && result;
        result = require(model.authoredRevision() == revision
                && model.playlist(playlistId) != nullptr
                && state.cleanRevision == cleanRevision
                && !state.currentPath.has_value()
                && window.isWindowModified(),
            "pre-release Open rejection must preserve the dirty current document") && result;
    }

    return result;
}

bool verifySessionOwnerAdmission(
    QTemporaryDir& directory,
    ApplicationTranslationState& translationState)
{
    bool weakRejected = false;
    try
    {
        MainWindow weakWindow{
            DesktopSceneEngine::DisplayId::fromSourceDeviceName(
                "\\\\.\\DISPLAY-WEAK"),
            translationState};
    }
    catch (const std::invalid_argument&)
    {
        weakRejected = true;
    }
    if (!require(weakRejected,
            "MainWindow must reject a weak SourceDeviceName Session identity"))
    {
        return false;
    }

    MainWindow window{
        DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
        translationState};
    auto& model = MainWindowDocumentLifecycleTestAccess::workspaceModel(window);
    auto& state = MainWindowDocumentLifecycleTestAccess::documentState(window);
    if (!require(model.authoredProject().displayOwner
                == DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayOwner(),
            "MainWindow initial AuthoredProject owner must equal the bound Session identity"))
    {
        return false;
    }

    const auto playlistId = model.addPlaylist();
    const auto itemId = model.addItem(
        playlistId,
        WorkspaceMediaType::Image,
        QStringLiteral("C:/Media/current-owner.png"));
    if (!require(itemId.has_value() && model.activateItem(*itemId),
            "Owner admission setup requires selected and active current state"))
    {
        return false;
    }
    state.currentPath = temporaryFile(directory, QStringLiteral("current-owner.json"));
    state.cleanRevision = model.authoredRevision() - 1U;
    const std::uint64_t revision = model.authoredRevision();
    const std::uint64_t cleanRevision = state.cleanRevision;
    const auto selectedPlaylist = model.selectedPlaylistId();
    const auto selectedItem = model.selectedItemId();
    const auto activePlaylist = model.activePlaylistId();
    const auto activeItem = model.activeItemId();

    const std::filesystem::path mismatchPath = temporaryFile(
        directory,
        QStringLiteral("different-owner-v3.json"));
    DesktopSceneEngine::Infrastructure::Persistence::saveAuthoredProjectFile(
        makeAuthoredProject(differentDisplayOwner()), mismatchPath);
    dismissNextMessageBox();
    if (!require(!MainWindowDocumentLifecycleTestAccess::openFromPath(window, mismatchPath),
            "different-owner v3 Open must fail during candidate admission")
        || !require(model.authoredRevision() == revision
                && model.authoredProject().displayOwner
                    == DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayOwner()
                && state.cleanRevision == cleanRevision
                && state.currentPath == temporaryFile(
                    directory, QStringLiteral("current-owner.json"))
                && model.selectedPlaylistId() == selectedPlaylist
                && model.selectedItemId() == selectedItem
                && model.activePlaylistId() == activePlaylist
                && model.activeItemId() == activeItem,
            "owner mismatch must preserve the complete dirty current document"))
    {
        return false;
    }

    const std::filesystem::path reloadPath = temporaryFile(
        directory,
        QStringLiteral("reload-owner-v3.json"));
    DesktopSceneEngine::Infrastructure::Persistence::saveAuthoredProjectFile(
        makeAuthoredProject(), reloadPath);
    dismissNextMessageBox();
    if (!require(!MainWindowDocumentLifecycleTestAccess::reloadAfterCandidateReplacement(
                    window,
                    reloadPath,
                    makeAuthoredProject(differentDisplayOwner())),
            "candidate reload must repeat owner admission")
        || !require(model.authoredRevision() == revision
                && model.activeItemId() == activeItem,
            "failed reload admission must preserve current Workspace state"))
    {
        return false;
    }

    auto unresolved = syntheticDisplayResult();
    unresolved.snapshot.displays.front().id =
        DesktopSceneEngine::DisplayId::fromTargetDevicePath(
            "\\\\?\\DISPLAY#UNRELATED#DISPLAY_9");
    MainWindowDocumentLifecycleTestAccess::refreshDisplayEnumeration(
        window,
        unresolved);
    const std::filesystem::path sameOwnerPath = temporaryFile(
        directory,
        QStringLiteral("same-owner-unresolved-v3.json"));
    DesktopSceneEngine::Infrastructure::Persistence::saveAuthoredProjectFile(
        makeAuthoredProject(), sameOwnerPath);
    return require(MainWindowDocumentLifecycleTestAccess::loadAndCommit(
                window,
                sameOwnerPath),
            "same-owner admission must not depend on current descriptor availability")
        && require(model.authoredProject().displayOwner
                == DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayOwner(),
            "same-owner Open must retain exact owner identity");
}

bool verifyOpenAndSaveFailureAtomicity(
    QTemporaryDir& directory,
    ApplicationTranslationState& translationState)
{
    MainWindow window{
        DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
        translationState};
    auto& model = MainWindowDocumentLifecycleTestAccess::workspaceModel(window);
    auto& state = MainWindowDocumentLifecycleTestAccess::documentState(window);
    const auto playlistId = model.addPlaylist();
    const auto itemId = model.addItem(
        playlistId,
        WorkspaceMediaType::Image,
        QStringLiteral("C:/Media/current.png"));
    if (!require(itemId.has_value() && model.activateItem(*itemId),
            "Failure atomicity setup requires an active current Item"))
    {
        return false;
    }

    const std::filesystem::path oldPath = temporaryFile(
        directory,
        QStringLiteral("old-current.json"));
    state.currentPath = oldPath;
    state.cleanRevision = model.authoredRevision() - 1U;
    const std::uint64_t revision = model.authoredRevision();
    const std::uint64_t cleanRevision = state.cleanRevision;
    const auto selectedPlaylist = model.selectedPlaylistId();
    const auto selectedItem = model.selectedItemId();
    const auto activePlaylist = model.activePlaylistId();
    const auto activeItem = model.activeItemId();

    const std::filesystem::path malformed = temporaryFile(
        directory,
        QStringLiteral("malformed.json"));
    {
        std::ofstream output{malformed, std::ios::binary | std::ios::trunc};
        output << "{not-json";
    }
    dismissNextMessageBox();
    if (!require(MainWindowDocumentLifecycleTestAccess::loadFails(window, malformed),
            "Malformed Open candidate must fail"))
    {
        return false;
    }

    const auto unchanged = [&]
    {
        return model.authoredRevision() == revision
            && model.authoredProject().playlists.size() == 1U
            && state.currentPath == oldPath
            && state.cleanRevision == cleanRevision
            && model.selectedPlaylistId() == selectedPlaylist
            && model.selectedItemId() == selectedItem
            && model.activePlaylistId() == activePlaylist
            && model.activeItemId() == activeItem;
    };
    if (!require(unchanged(),
            "Open load failure must leave the complete current document untouched"))
    {
        return false;
    }

    const std::filesystem::path badSavePath = temporaryFile(
        directory,
        QStringLiteral("missing-parent/project.json"));
    dismissNextMessageBox();
    return require(MainWindowDocumentLifecycleTestAccess::saveToPathFails(
                window,
                badSavePath,
                true),
            "Save As to a missing parent must fail deterministically")
        && require(unchanged(),
            "Save As failure must retain old path, dirty state, authored state, and playback identity");
}

bool verifySaveAsAndDisplayRefresh(
    QTemporaryDir& directory,
    ApplicationTranslationState& translationState)
{
    MainWindow window{
        DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
        translationState};
    auto& model = MainWindowDocumentLifecycleTestAccess::workspaceModel(window);
    auto& state = MainWindowDocumentLifecycleTestAccess::documentState(window);
    const auto playlistId = model.addPlaylist();
    const auto itemId = model.addItem(
        playlistId,
        WorkspaceMediaType::Video,
        QStringLiteral("C:/Media/save.mp4"));
    if (!require(itemId.has_value() && model.setActiveItem(*itemId),
            "Save As setup requires authored and active state"))
    {
        return false;
    }

    const std::uint64_t revision = model.authoredRevision();
    const std::filesystem::path path = temporaryFile(
        directory,
        QString::fromUtf8("\xE4\xBF\x9D\xE5\xAD\x98-as.json"));
    if (!require(MainWindowDocumentLifecycleTestAccess::saveToPath(window, path, true),
            "Save As direct-path transaction must succeed for a Unicode path"))
    {
        return false;
    }

    if (!require(state.currentPath == path && !state.isDirty(model.authoredRevision())
            && model.authoredRevision() == revision
            && model.activeItemId() == itemId,
            "Save As must adopt path only after success without stopping playback or mutating authored revision"))
    {
        return false;
    }
    if (!require(window.windowTitle()
                == QString::fromUtf8("DesktopSceneEngine \xE2\x80\x94 \xE4\xBF\x9D\xE5\xAD\x98-as.json[*]")
            && !window.isWindowModified(),
            "Successful Save As must adopt the filename and clear the modified marker"))
    {
        return false;
    }

    MainWindowDocumentLifecycleTestAccess::setDesktopCanvas(
        window,
        {AspectRatio{4, 3}});
    const std::uint64_t canvasRevision = model.authoredRevision();
    MainWindowDocumentLifecycleTestAccess::markDocumentClean(window);
    const auto documentPath = state.currentPath;
    const auto displayResult = syntheticDisplayResult();
    MainWindowDocumentLifecycleTestAccess::applyDisplayEnumeration(window, displayResult);
    const auto boundDisplayId = MainWindowDocumentLifecycleTestAccess::boundDisplayId(window);
    if (!require(model.desktopCanvas().aspectRatio == AspectRatio{4, 3}
            && model.authoredRevision() == canvasRevision
            && MainWindowDocumentLifecycleTestAccess::currentCanvasAspectText(window)
                == QStringLiteral("Current Canvas Aspect \u2014 4:3")
            && MainWindowDocumentLifecycleTestAccess::boundDisplayText(window)
                .contains(QStringLiteral("1920\u00d71080"))
            && !state.isDirty(model.authoredRevision())
            && state.currentPath == documentPath,
            "Resolved Display refresh must update metadata without changing document state"))
    {
        return false;
    }

    auto unavailableResult = syntheticDisplayResult();
    unavailableResult.snapshot.displays.front().id =
        DesktopSceneEngine::DisplayId::fromSourceDeviceName("\\\\.\\DISPLAY-OTHER");
    MainWindowDocumentLifecycleTestAccess::refreshDisplayEnumeration(window, unavailableResult);
    if (!require(MainWindowDocumentLifecycleTestAccess::boundDisplayId(window) == boundDisplayId
            && MainWindowDocumentLifecycleTestAccess::boundDisplayText(window)
                == QStringLiteral("Selected display unavailable")
            && model.desktopCanvas().aspectRatio == AspectRatio{4, 3}
            && model.authoredRevision() == canvasRevision
            && !state.isDirty(model.authoredRevision())
            && state.currentPath == documentPath,
            "Missing bound Display must not fall back or change document state"))
    {
        return false;
    }

    MainWindowDocumentLifecycleTestAccess::refreshDisplayEnumeration(window, displayResult);
    return require(MainWindowDocumentLifecycleTestAccess::boundDisplayId(window) == boundDisplayId
            && model.desktopCanvas().aspectRatio == AspectRatio{4, 3}
            && model.authoredRevision() == canvasRevision
            && MainWindowDocumentLifecycleTestAccess::currentCanvasAspectText(window)
                == QStringLiteral("Current Canvas Aspect \u2014 4:3")
            && MainWindowDocumentLifecycleTestAccess::boundDisplayText(window)
                .contains(QStringLiteral("1920\u00d71080"))
            && !state.isDirty(model.authoredRevision())
            && state.currentPath == documentPath,
        "Reappearing bound Display must re-resolve without changing document state");
}

bool verifySaveAsOwnerPolicy(
    QTemporaryDir& directory,
    ApplicationTranslationState& translationState)
{
    MainWindow window{
        DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
        translationState};
    auto& model = MainWindowDocumentLifecycleTestAccess::workspaceModel(window);
    auto& state = MainWindowDocumentLifecycleTestAccess::documentState(window);
    const auto playlistId = model.addPlaylist();
    const std::uint64_t revision = model.authoredRevision();
    const auto expectedOwner = model.authoredProject().displayOwner;

    const std::filesystem::path crossOwnerPath = temporaryFile(
        directory,
        QStringLiteral("save-as-cross-owner.json"));
    DesktopSceneEngine::Infrastructure::Persistence::saveAuthoredProjectFile(
        makeAuthoredProject(differentDisplayOwner()), crossOwnerPath);
    std::ifstream crossOwnerInput{crossOwnerPath, std::ios::binary};
    const std::string originalBytes{
        std::istreambuf_iterator<char>{crossOwnerInput},
        std::istreambuf_iterator<char>{}};
    dismissNextMessageBox();
    if (!require(MainWindowDocumentLifecycleTestAccess::saveToPathFails(
                    window, crossOwnerPath, true),
            "Save As must reject a valid v3 destination owned by another Session"))
    {
        return false;
    }
    std::ifstream preservedInput{crossOwnerPath, std::ios::binary};
    const std::string preservedBytes{
        std::istreambuf_iterator<char>{preservedInput},
        std::istreambuf_iterator<char>{}};
    if (!require(originalBytes == preservedBytes
            && !state.currentPath.has_value()
            && model.authoredRevision() == revision
            && model.authoredProject().displayOwner == expectedOwner
            && model.playlist(playlistId) != nullptr,
            "cross-owner Save As rejection must occur before write or path adoption"))
    {
        return false;
    }

    bool result = true;
    for (const char* version : {"1.0", "2.0"})
    {
        const std::filesystem::path path = temporaryFile(
            directory,
            version[0] == '1'
                ? QStringLiteral("save-as-replace-v1.json")
                : QStringLiteral("save-as-replace-v2.json"));
        {
            std::ofstream output{path, std::ios::binary | std::ios::trunc};
            output << "{\"type\":\"DesktopSceneEngineProject\",\"version\":\""
                << version << "\"}";
        }
        result = require(MainWindowDocumentLifecycleTestAccess::saveToPath(
                    window, path, true),
            "Save As must allow ordinary v1/v2 file replacement") && result;
        const auto restored = DesktopSceneEngine::Infrastructure::Persistence::
            loadAuthoredProjectFile(path);
        result = require(restored.displayOwner == expectedOwner
                && model.authoredProject().displayOwner == expectedOwner
                && model.authoredRevision() == revision,
            "Save As replacement must preserve owner and authored revision") && result;
    }

    return result;
}

bool verifyStandardMessageBoxButtonLocalization(
    ApplicationTranslationState& translationState)
{
    struct StandardButtonTexts
    {
        QString save{};
        QString discard{};
        QString cancel{};
    };

    const auto buttonTexts = []
    {
        QMessageBox messageBox{
            QMessageBox::Warning,
            QStringLiteral("Test"),
            QStringLiteral("Test"),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel};
        return StandardButtonTexts{
            messageBox.button(QMessageBox::Save)->text(),
            messageBox.button(QMessageBox::Discard)->text(),
            messageBox.button(QMessageBox::Cancel)->text()};
    };

    MainWindow window{
        DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
        translationState};
    MainWindowDocumentLifecycleTestAccess::setEnglish(window);
    const StandardButtonTexts english = buttonTexts();

    MainWindowDocumentLifecycleTestAccess::setSimplifiedChinese(window);
    const StandardButtonTexts chinese = buttonTexts();
    if (!require(!chinese.save.isEmpty()
            && !chinese.discard.isEmpty()
            && !chinese.cancel.isEmpty(),
            "Chinese standard QMessageBox button labels must be non-empty")
        || !require(chinese.save != english.save
                && chinese.discard != english.discard
                && chinese.cancel != english.cancel,
            "Qt's zh_CN catalog must localize Save, Discard, and Cancel")
        || !require(window.windowTitle().contains(QString::fromUtf8("\xE6\x9C\xAA\xE5\x91\xBD\xE5\x90\x8D")),
            "Application-specific Chinese translation must remain active"))
    {
        return false;
    }

    MainWindowDocumentLifecycleTestAccess::setEnglish(window);
    const StandardButtonTexts restoredEnglish = buttonTexts();
    return require(restoredEnglish.save == english.save
            && restoredEnglish.discard == english.discard
            && restoredEnglish.cancel == english.cancel,
        "Removing Chinese translators must restore English standard buttons");
}

bool verifyLanguageFallbackPreservesSavedPreference(
    QTemporaryDir& directory,
    ApplicationTranslationState& translationState)
{
    using DesktopSceneEngine::Player::ApplicationPreferences;
    using DesktopSceneEngine::Player::ApplicationPreferencesStore;
    using DesktopSceneEngine::Player::UiLanguage;

    static_cast<void>(translationState.setLanguage(UiLanguage::English));

    const QString preferencesPath = directory.filePath(
        QStringLiteral("fallback-preferences.ini"));
    QSettings settings{preferencesPath, QSettings::IniFormat};
    ApplicationPreferencesStore store{settings};
    store.saveLanguage(UiLanguage::SimplifiedChinese);
    store.saveHighlightCanvasBoundary(false);
    const ApplicationPreferences initialPreferences = store.load();

    ApplicationTranslationState missingCatalog{directory.filePath(QStringLiteral("missing-catalog.qm"))};
    MainWindow window{
        DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
        missingCatalog,
        store,
        initialPreferences};

    if (!require(
            MainWindowDocumentLifecycleTestAccess::uiLanguage(window) == UiLanguage::English,
            "A failed saved-language activation must project the effective English fallback"))
    {
        return false;
    }

    MainWindowDocumentLifecycleTestAccess::triggerHighlightCanvasBoundary(window);
    const ApplicationPreferences afterHighlight = store.load();
    if (!require(
            afterHighlight.language == UiLanguage::SimplifiedChinese
                && afterHighlight.highlightCanvasBoundary,
            "A highlight-only save must preserve the saved language across startup fallback"))
    {
        return false;
    }

    store.saveLanguage(UiLanguage::English);
    MainWindowDocumentLifecycleTestAccess::reloadLanguage(window);
    const ApplicationPreferences afterExplicitEnglish = store.load();
    return require(
        afterExplicitEnglish.language == UiLanguage::English
            && afterExplicitEnglish.highlightCanvasBoundary,
        "Consuming an external English commit after fallback must retain the independent highlight value");
}

bool verifyMachineLocalApplicationPreferences(
    QTemporaryDir& directory,
    ApplicationTranslationState& translationState)
{
    using DesktopSceneEngine::Player::ApplicationPreferences;
    using DesktopSceneEngine::Player::ApplicationPreferencesStore;
    using DesktopSceneEngine::Player::UiLanguage;

    const QString preferencesPath = directory.filePath(QStringLiteral("preferences.ini"));
    QSettings settings{preferencesPath, QSettings::IniFormat};
    ApplicationPreferencesStore store{settings};
    store.saveLanguage(UiLanguage::SimplifiedChinese);
    store.saveHighlightCanvasBoundary(true);
    ApplicationPreferences initialPreferences = store.load();
    initialPreferences.language = translationState.setLanguage(initialPreferences.language);

    {
        MainWindow window{
            DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
            translationState,
            store,
            initialPreferences};
        window.show();
        QApplication::processEvents();
        auto& model = MainWindowDocumentLifecycleTestAccess::workspaceModel(window);
        auto& document = MainWindowDocumentLifecycleTestAccess::documentState(window);
        const std::uint64_t initialRevision = model.authoredRevision();

        if (!require(
                MainWindowDocumentLifecycleTestAccess::uiLanguage(window)
                        == UiLanguage::SimplifiedChinese
                    && MainWindowDocumentLifecycleTestAccess::highlightCanvasBoundaryIsChecked(window)
                    && MainWindowDocumentLifecycleTestAccess::canvasBoundaryIsVisible(window),
                "Startup must restore persisted language and Canvas boundary preference")
            || !require(!document.currentPath.has_value()
                    && !document.isDirty(initialRevision),
                "Loading application preferences must not dirty or bind the document"))
        {
            return false;
        }

        store.saveLanguage(UiLanguage::English);
        MainWindowDocumentLifecycleTestAccess::reloadLanguage(window);
        MainWindowDocumentLifecycleTestAccess::triggerHighlightCanvasBoundary(window);
        QApplication::processEvents();
        const ApplicationPreferences livePreferences = store.load();
        if (!require(
                MainWindowDocumentLifecycleTestAccess::uiLanguage(window) == UiLanguage::English
                    && !MainWindowDocumentLifecycleTestAccess::highlightCanvasBoundaryIsChecked(window)
                    && !MainWindowDocumentLifecycleTestAccess::canvasBoundaryIsVisible(window)
                    && livePreferences.language == UiLanguage::English
                    && !livePreferences.highlightCanvasBoundary,
                "Successful user actions must apply and persist preferences immediately")
            || !require(model.authoredRevision() == initialRevision
                    && !document.currentPath.has_value()
                    && !document.isDirty(model.authoredRevision()),
                "Live preference changes must not mutate document state"))
        {
            return false;
        }

        MainWindowDocumentLifecycleTestAccess::createNewDocument(window);
        const ApplicationPreferences afterNew = store.load();
        MainWindowDocumentLifecycleTestAccess::commitProject(
            window,
            makeAuthoredProject(),
            temporaryFile(directory, QStringLiteral("preferences-open.json")));
        const ApplicationPreferences afterOpen = store.load();
        const bool saved = MainWindowDocumentLifecycleTestAccess::saveToPath(
            window,
            temporaryFile(directory, QStringLiteral("preferences-save.json")),
            true);
        const ApplicationPreferences afterSave = store.load();
        if (!require(saved
                && afterNew.language == UiLanguage::English
                && !afterNew.highlightCanvasBoundary
                && afterOpen.language == UiLanguage::English
                && !afterOpen.highlightCanvasBoundary
                && afterSave.language == UiLanguage::English
                && !afterSave.highlightCanvasBoundary,
                "New, Open, and Save must not change machine-local preferences"))
        {
            return false;
        }
    }

    QSettings reopenedSettings{preferencesPath, QSettings::IniFormat};
    ApplicationPreferencesStore reopenedStore{reopenedSettings};
    ApplicationPreferences reopenedPreferences = reopenedStore.load();
    reopenedPreferences.language = translationState.setLanguage(reopenedPreferences.language);
    MainWindow reopened{
        DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
        translationState,
        reopenedStore,
        reopenedPreferences};
    reopened.show();
    QApplication::processEvents();
    return require(
        MainWindowDocumentLifecycleTestAccess::uiLanguage(reopened) == UiLanguage::English
            && !MainWindowDocumentLifecycleTestAccess::highlightCanvasBoundaryIsChecked(reopened)
            && !MainWindowDocumentLifecycleTestAccess::canvasBoundaryIsVisible(reopened),
        "A later MainWindow instance must restore the latest persisted preferences");
}

}

bool verifyCanvasOutsideDocumentPreferences(
    QTemporaryDir& directory,
    ApplicationTranslationState& translationState)
{
    using DesktopSceneEngine::CanvasOutsideMode;
    using DesktopSceneEngine::Player::ApplicationPreferencesStore;
    const auto displayA = DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId();
    const auto displayB = DesktopSceneEngine::DisplayId::fromTargetDevicePath(
        "canvas-outside-independent-route");
    const QString path = directory.filePath(QStringLiteral("canvas-outside.ini"));
    QSettings settings{path, QSettings::IniFormat};
    ApplicationPreferencesStore store{settings};
    if (!require(store.saveCanvasOutsideMode(displayA, CanvasOutsideMode::Transparent)
            == QSettings::NoError, "Initial per-display preference must save"))
        return false;
    const QStringList initialKeys = settings.allKeys();
    MainWindow a{displayA, translationState, store, store.load()};
    MainWindow b{displayB, translationState, store, store.load()};
    auto* aOpaque = a.findChild<QAction*>(QStringLiteral("canvasOutsideOpaqueAction"));
    auto* aTransparent = a.findChild<QAction*>(QStringLiteral("canvasOutsideTransparentAction"));
    auto* bOpaque = b.findChild<QAction*>(QStringLiteral("canvasOutsideOpaqueAction"));
    auto* bTransparent = b.findChild<QAction*>(QStringLiteral("canvasOutsideTransparentAction"));
    if (!require(aOpaque && aTransparent && bOpaque && bTransparent,
            "Each MainWindow must expose its own Desktop Background radio actions"))
        return false;
    if (!require(aTransparent->isChecked() && !aOpaque->isChecked()
            && bOpaque->isChecked() && !bTransparent->isChecked()
            && aTransparent->isEnabled() && bTransparent->isEnabled()
            && settings.allKeys() == initialKeys,
            "Construction must restore A, derive Opaque for unseen B, and perform no writes"))
        return false;
    auto& model = MainWindowDocumentLifecycleTestAccess::workspaceModel(a);
    auto& document = MainWindowDocumentLifecycleTestAccess::documentState(a);
    const auto revision = model.authoredRevision();
    const auto snapshotPath = temporaryFile(directory, QStringLiteral("canvas-outside-snapshot.json"));
    const auto serializedProject = [&]
    {
        DesktopSceneEngine::Infrastructure::Persistence::saveAuthoredProjectFile(
            model.authoredProject(), snapshotPath);
        std::ifstream stream{snapshotPath};
        return std::string{std::istreambuf_iterator<char>{stream},
            std::istreambuf_iterator<char>{}};
    };
    const auto authored = serializedProject();
    bTransparent->trigger();
    aOpaque->trigger();
    if (!require(aOpaque->isChecked() && !aTransparent->isChecked()
            && bTransparent->isChecked() && !bOpaque->isChecked()
            && store.loadCanvasOutsideMode(displayA).value == CanvasOutsideMode::Opaque
            && store.loadCanvasOutsideMode(displayB).value == CanvasOutsideMode::Transparent
            && model.authoredRevision() == revision && !document.isDirty(revision)
            && serializedProject() == authored,
            "User selections must be independent and leave authored state/revision/JSON untouched"))
        return false;
    store.saveCanvasOutsideMode(displayA, CanvasOutsideMode::Transparent);
    QApplication::processEvents();
    aOpaque->trigger(); // Same live selection must not overwrite the external persisted value.
    if (!require(aOpaque->isChecked() && bTransparent->isChecked(),
            "External storage writes must not become live MainWindow mode reloads"))
        return false;
    const auto savedPath = temporaryFile(directory, QStringLiteral("canvas-outside-project.json"));
    if (!require(MainWindowDocumentLifecycleTestAccess::saveToPath(a, savedPath, true),
            "Canvas-outside lifecycle fixture must save"))
        return false;
    MainWindowDocumentLifecycleTestAccess::createNewDocument(a);
    if (!require(aOpaque->isChecked()
            && MainWindowDocumentLifecycleTestAccess::loadAndCommit(a, savedPath)
            && aOpaque->isChecked()
            && store.loadCanvasOutsideMode(displayA).value == CanvasOutsideMode::Transparent
            && bTransparent->isChecked(),
            "New/Open/Save must preserve live selection without writing it back"))
        return false;
    MainWindowDocumentLifecycleTestAccess::setSimplifiedChinese(a);
    QApplication::processEvents();
    const bool translated = aOpaque->text()
            == QStringLiteral("\u8986\u76d6\u6574\u4e2a\u684c\u9762\u80cc\u666f")
        && aTransparent->text()
            == QStringLiteral("\u663e\u793a\u672a\u8986\u76d6\u90e8\u5206 Windows \u58c1\u7eb8");
    MainWindowDocumentLifecycleTestAccess::setEnglish(a);
    QApplication::processEvents();
    return require(translated && aOpaque->isChecked() && bTransparent->isChecked()
            && store.loadCanvasOutsideMode(displayA).value == CanvasOutsideMode::Transparent,
        "Retranslation must preserve projection and must not persist the live value");
}

bool verifyCanvasOutsideMainWindowWriteFailure(
    QTemporaryDir& directory,
    ApplicationTranslationState& translationState)
{
    using DesktopSceneEngine::CanvasOutsideMode;
    QSettings inaccessible{directory.path(), QSettings::IniFormat};
    DesktopSceneEngine::Player::ApplicationPreferencesStore store{inaccessible};
    MainWindow window{DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
        translationState, store, {}};
    auto* opaque = window.findChild<QAction*>(QStringLiteral("canvasOutsideOpaqueAction"));
    auto* transparent = window.findChild<QAction*>(QStringLiteral("canvasOutsideTransparentAction"));
    if (!require(opaque && transparent && opaque->isChecked()
            && MainWindowDocumentLifecycleTestAccess::canvasOutsideMode(window) == CanvasOutsideMode::Opaque,
            "Preference read failure must initialize Opaque without preventing the session"))
        return false;
    const auto revision = MainWindowDocumentLifecycleTestAccess::workspaceModel(window).authoredRevision();
    int warnings = 0;
    bool correctWarning = false;
    const auto closeWarning = [&]
    {
        if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
        {
            ++warnings;
            correctWarning = box->text() == QCoreApplication::translate("MainWindow",
                "The desktop background setting could not be saved. This session keeps the selected mode.");
            box->accept();
        }
    };
    QTimer::singleShot(0, &window, closeWarning);
    transparent->trigger();
    QTimer::singleShot(0, &window, closeWarning);
    transparent->trigger();
    QApplication::processEvents();
    return require(warnings == 1 && correctWarning && transparent->isChecked()
            && !opaque->isChecked()
            && MainWindowDocumentLifecycleTestAccess::canvasOutsideMode(window)
                == CanvasOutsideMode::Transparent
            && MainWindowDocumentLifecycleTestAccess::workspaceModel(window).authoredRevision() == revision
            && !MainWindowDocumentLifecycleTestAccess::documentState(window).isDirty(revision),
        "Save failure must retain live intent, warn once, and never dirty Project or retry same selection");
}

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    ApplicationTranslationState translationState{};
    QTemporaryDir directory{};
    if (!require(directory.isValid(), "A temporary directory is required"))
    {
        return 1;
    }

    return verifyCanvasOutsideDocumentPreferences(directory, translationState)
            && verifyCanvasOutsideMainWindowWriteFailure(directory, translationState)
            && verifyCanvasAndTargetDisplayUi(translationState)
            && verifyFileActionsAndCleanNew(translationState)
            && verifyFinalPlaylistDeletionAndEmptyProjectRoundTrip(
                directory, translationState)
            && verifyDirtyDecisionAndNewMatrix(directory, translationState)
            && verifyAuthoredTitleNotification(translationState)
            && verifyOpenDecisionMatrixAndStaleReload(directory, translationState)
            && verifyCloseDecisionMatrix(directory, translationState)
            && verifyV3OpenAndCanvasPreservation(directory, translationState)
            && verifyBackgroundMusicEnabledSaveReopenAndRetranslation(
                directory, translationState)
            && verifyUnsupportedPreReleaseOpen(directory, translationState)
            && verifySessionOwnerAdmission(directory, translationState)
            && verifyOpenAndSaveFailureAtomicity(directory, translationState)
            && verifySaveAsAndDisplayRefresh(directory, translationState)
            && verifySaveAsOwnerPolicy(directory, translationState)
            && verifyStandardMessageBoxButtonLocalization(translationState)
            && verifyLanguageFallbackPreservesSavedPreference(directory, translationState)
            && verifyMachineLocalApplicationPreferences(directory, translationState)
        ? 0
        : 1;
}
