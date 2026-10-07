#include "BackgroundMusicWindow.h"
#include "MainWindow.h"
#include "MainWindowTestDisplay.h"

#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QWidget>

#include <iostream>
#include <optional>

class MainWindowBackgroundMusicLifecycleTestAccess
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

    static DesktopSceneEngine::Player::BackgroundMusicWindow* open(
        MainWindow& window,
        DesktopSceneEngine::Player::PlaylistId playlistId)
    {
        return window.openBackgroundMusicWindow(playlistId);
    }

    static void refreshPlaylist(
        MainWindow& window,
        DesktopSceneEngine::Player::PlaylistId playlistId)
    {
        window.refreshOpenBackgroundMusicWindow(playlistId);
    }

    static void refreshLibrary(MainWindow& window)
    {
        window.refreshLibrary();
    }

    static void closePlaylist(
        MainWindow& window,
        DesktopSceneEngine::Player::PlaylistId playlistId)
    {
        window.closeBackgroundMusicWindow(playlistId);
    }

    static int editorCount(const MainWindow& window)
    {
        return window.backgroundMusicWindows_.size();
    }

    static QWidget* inspectorContent(const MainWindow& window)
    {
        return window.inspectorContent_;
    }

    static void refreshInspector(MainWindow& window)
    {
        window.refreshInspector();
    }

    static void markDocumentClean(MainWindow& window)
    {
        window.documentState_.markClean(window.workspaceModel_.authoredRevision());
    }
};

namespace
{

using DesktopSceneEngine::Player::ApplicationTranslationState;
using DesktopSceneEngine::Player::BackgroundMusicWindow;
using DesktopSceneEngine::Player::ItemId;
using DesktopSceneEngine::Player::PlaylistId;
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

ItemId itemIdFor(const QListWidgetItem* item)
{
    return item == nullptr ? ItemId{} : ItemId{item->data(identityRole).toUuid()};
}

void processDeferredDeletes()
{
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

bool containsLabelText(const QWidget& parent, const QString& text)
{
    for (const QLabel* label : parent.findChildren<QLabel*>())
    {
        if (label->text() == text)
        {
            return true;
        }
    }

    return false;
}

bool containsButtonText(const QWidget& parent, const QString& text)
{
    for (const QPushButton* button : parent.findChildren<QPushButton*>())
    {
        if (button->text() == text)
        {
            return true;
        }
    }

    return false;
}

QComboBox* visualPolicyControl(QWidget& parent)
{
    for (QComboBox* comboBox : parent.findChildren<QComboBox*>())
    {
        if (comboBox->findText(QStringLiteral("Native")) >= 0
            && comboBox->findText(QStringLiteral("Auto Fill")) >= 0)
        {
            return comboBox;
        }
    }

    return nullptr;
}

bool selectVisualPolicy(QComboBox& control, const QString& text)
{
    const int index = control.findText(text);
    if (index < 0)
    {
        return false;
    }

    control.setCurrentIndex(index);
    control.activated(index);
    QCoreApplication::processEvents();
    return true;
}

bool verifyVisualInspectorPresentationControls(
    ApplicationTranslationState& translationState)
{
    MainWindow mainWindow{
        DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
        translationState};
    mainWindow.show();
    QCoreApplication::processEvents();

    WorkspaceModel& model = MainWindowBackgroundMusicLifecycleTestAccess::workspaceModel(
        mainWindow);
    const PlaylistId playlistId = model.selectedPlaylistId();
    const auto itemId = model.addItem(
        playlistId,
        WorkspaceMediaType::Image,
        QStringLiteral("C:/Media/presentation.png"),
        QStringLiteral("Presentation"));
    if (!require(itemId.has_value() && model.selectItem(*itemId),
            "Visual Inspector testing requires a selected Image Item"))
    {
        return false;
    }

    MainWindowBackgroundMusicLifecycleTestAccess::refreshInspector(mainWindow);
    QCoreApplication::processEvents();
    QWidget* const inspector = MainWindowBackgroundMusicLifecycleTestAccess::inspectorContent(
        mainWindow);
    if (!require(inspector != nullptr,
            "Visual Inspector testing requires an Item Inspector"))
    {
        return false;
    }

    QComboBox* const policy = visualPolicyControl(*inspector);
    bool passed = require(policy != nullptr,
        "Visual Inspector must expose a presentation policy selector");
    if (policy == nullptr)
    {
        return false;
    }

    passed = require(containsLabelText(*inspector, QStringLiteral("Fit / Aspect"))
                && !containsLabelText(*inspector, QStringLiteral("Mode")),
                 "Visual Inspector must replace the Mode label with Fit / Aspect")
        && passed;
    passed = require(policy->findText(QStringLiteral("Native")) >= 0
                && policy->findText(QStringLiteral("Auto Fill")) >= 0
                && policy->findText(QStringLiteral("Custom Aspect")) >= 0,
                 "Visual Inspector must expose Native, Auto Fill, and Custom Aspect policies")
        && passed;
    passed = require(containsLabelText(*inspector, QStringLiteral("Scale"))
                && containsLabelText(*inspector, QStringLiteral("Position X"))
                && containsLabelText(*inspector, QStringLiteral("Position Y")),
                 "Native Inspector must expose always-editable Scale, Position X, and Position Y controls")
        && passed;
    if (!passed)
    {
        return false;
    }

    auto* const scale = inspector->findChild<QDoubleSpinBox*>(
        QStringLiteral("visualScaleSpinBox"));
    auto* const positionX = inspector->findChild<QDoubleSpinBox*>(
        QStringLiteral("visualPositionXSpinBox"));
    auto* const positionY = inspector->findChild<QDoubleSpinBox*>(
        QStringLiteral("visualPositionYSpinBox"));
    auto* const aspectRow = inspector->findChild<QWidget*>(
        QStringLiteral("visualAspectRatioRow"));
    auto* const numerator = inspector->findChild<QSpinBox*>(
        QStringLiteral("visualAspectNumeratorSpinBox"));
    auto* const denominator = inspector->findChild<QSpinBox*>(
        QStringLiteral("visualAspectDenominatorSpinBox"));
    if (!require(scale != nullptr && positionX != nullptr && positionY != nullptr
            && aspectRow != nullptr && numerator != nullptr && denominator != nullptr,
            "Visual Inspector must retain all policy controls in one widget lifetime"))
    {
        return false;
    }

    QPointer<QWidget> inspectorGuard{inspector};
    if (!require(selectVisualPolicy(*policy, QStringLiteral("Native"))
            && !inspectorGuard.isNull(),
            "Visual Inspector must select Native without rebuilding its controls"))
    {
        return false;
    }
    scale->setValue(1.20);
    positionX->setValue(0.10);
    positionY->setValue(-0.20);
    const auto* nativeItem = model.item(*itemId);
    if (!require(nativeItem != nullptr
            && nativeItem->presentation.mediaPresentation.mode
                == DesktopSceneEngine::Domain::MediaPresentationMode::Native
            && nativeItem->presentation.layout.scaleX == 1.20F
            && nativeItem->presentation.layout.scaleY == 1.20F
            && nativeItem->presentation.layout.positionX == 0.10F
            && nativeItem->presentation.layout.positionY == -0.20F,
            "Native must commit Scale and Position"))
    {
        return false;
    }
    if (!require(scale->isVisible() && scale->isEnabled()
            && positionX->isVisible() && positionX->isEnabled()
            && positionY->isVisible() && positionY->isEnabled(),
            "Native Scale and Position controls must remain visible and enabled"))
    {
        return false;
    }
    if (!require(!aspectRow->isVisible(),
            "Native must hide Custom Aspect"))
    {
        return false;
    }

    if (!require(selectVisualPolicy(*policy, QStringLiteral("Auto Fill"))
            && !inspectorGuard.isNull()
            && scale->isVisible() && scale->isEnabled()
            && positionX->isVisible() && positionX->isEnabled()
            && positionY->isVisible() && positionY->isEnabled()
            && !aspectRow->isVisible(),
            "Auto Fill must keep Scale and Position in place while hiding Custom Aspect"))
    {
        return false;
    }

    scale->setValue(0.80);
    positionX->setValue(-0.15);
    positionY->setValue(0.25);
    const auto* autoFillItem = model.item(*itemId);
    if (!require(autoFillItem != nullptr
            && autoFillItem->presentation.mediaPresentation.mode
                == DesktopSceneEngine::Domain::MediaPresentationMode::AutoFill
            && autoFillItem->presentation.layout.scaleX == 0.80F
            && autoFillItem->presentation.layout.scaleY == 0.80F
            && autoFillItem->presentation.layout.positionX == -0.15F
            && autoFillItem->presentation.layout.positionY == 0.25F,
            "Auto Fill must commit Scale and Position without changing its source-crop policy"))
    {
        return false;
    }

    if (!require(selectVisualPolicy(*policy, QStringLiteral("Custom Aspect"))
            && !inspectorGuard.isNull() && aspectRow->isVisible()
            && scale->isVisible() && positionX->isVisible() && positionY->isVisible(),
            "Custom Aspect must reveal Aspect Ratio without rebuilding the Inspector"))
    {
        return false;
    }

    numerator->setValue(4);
    denominator->setValue(3);
    if (!require(selectVisualPolicy(*policy, QStringLiteral("Native"))
            && selectVisualPolicy(*policy, QStringLiteral("Auto Fill"))
            && selectVisualPolicy(*policy, QStringLiteral("Custom Aspect")),
            "Visual Inspector must support policy changes without replacing its controls"))
    {
        return false;
    }

    const auto* restoredCustomItem = model.item(*itemId);
    return require(restoredCustomItem != nullptr
            && restoredCustomItem->presentation.mediaPresentation.mode
                == DesktopSceneEngine::Domain::MediaPresentationMode::Custom
            && restoredCustomItem->presentation.mediaPresentation.customAspect.numerator == 4
            && restoredCustomItem->presentation.mediaPresentation.customAspect.denominator == 3
            && restoredCustomItem->presentation.layout.scaleX == 0.80F
            && restoredCustomItem->presentation.layout.scaleY == 0.80F
            && restoredCustomItem->presentation.layout.positionX == -0.15F
            && restoredCustomItem->presentation.layout.positionY == 0.25F
            && aspectRow->isVisible(),
        "Policy changes must preserve Custom Aspect, Scale, and Position values");
}

bool verifyVideoVisualInspectorPresentationControls(
    ApplicationTranslationState& translationState)
{
    MainWindow mainWindow{
        DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
        translationState};
    mainWindow.show();
    QCoreApplication::processEvents();

    WorkspaceModel& model = MainWindowBackgroundMusicLifecycleTestAccess::workspaceModel(
        mainWindow);
    const auto itemId = model.addItem(
        model.selectedPlaylistId(),
        WorkspaceMediaType::Video,
        QStringLiteral("C:/Media/presentation.mp4"),
        QStringLiteral("Presentation video"));
    if (!require(itemId.has_value() && model.selectItem(*itemId),
            "Video Visual Inspector testing requires a selected Video Item"))
    {
        return false;
    }

    MainWindowBackgroundMusicLifecycleTestAccess::refreshInspector(mainWindow);
    QCoreApplication::processEvents();
    QWidget* const inspector = MainWindowBackgroundMusicLifecycleTestAccess::inspectorContent(
        mainWindow);
    QComboBox* const policy = inspector == nullptr ? nullptr : visualPolicyControl(*inspector);
    auto* const scale = inspector == nullptr ? nullptr : inspector->findChild<QDoubleSpinBox*>(
        QStringLiteral("visualScaleSpinBox"));
    auto* const positionX = inspector == nullptr ? nullptr : inspector->findChild<QDoubleSpinBox*>(
        QStringLiteral("visualPositionXSpinBox"));
    auto* const positionY = inspector == nullptr ? nullptr : inspector->findChild<QDoubleSpinBox*>(
        QStringLiteral("visualPositionYSpinBox"));
    if (!require(policy != nullptr && scale != nullptr && positionX != nullptr && positionY != nullptr
            && selectVisualPolicy(*policy, QStringLiteral("Native")),
            "Video Items must expose the same Fit / Aspect, Scale, and Position controls"))
    {
        return false;
    }

    scale->setValue(1.10);
    positionX->setValue(0.20);
    positionY->setValue(-0.10);
    const auto* item = model.item(*itemId);
    return require(item != nullptr
            && item->presentation.mediaPresentation.mode
                == DesktopSceneEngine::Domain::MediaPresentationMode::Native
            && item->presentation.layout.scaleX == 1.10F
            && item->presentation.layout.scaleY == 1.10F
            && item->presentation.layout.positionX == 0.20F
            && item->presentation.layout.positionY == -0.10F,
        "Video Native controls must commit through the same WorkspaceModel presentation path");
}

bool verifyExtendedMediaLayerControlsAreNotExposed(
    ApplicationTranslationState& translationState)
{
    MainWindow mainWindow{
        DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
        translationState};
    mainWindow.show();
    QCoreApplication::processEvents();

    WorkspaceModel& model = MainWindowBackgroundMusicLifecycleTestAccess::workspaceModel(
        mainWindow);
    const PlaylistId playlistId = model.selectedPlaylistId();
    const auto itemId = model.addItem(
        playlistId,
        WorkspaceMediaType::Image,
        QStringLiteral("C:/Media/primary.png"),
        QStringLiteral("Primary"));
    const auto mediaLayerId = itemId.has_value()
        ? model.addExternalMediaLayer(
            *itemId,
            QStringLiteral("C:/Media/retained-overlay.mp4"))
        : std::nullopt;
    if (!require(itemId.has_value() && mediaLayerId.has_value() && model.selectItem(*itemId),
            "Inspector retirement test requires an Image Item with retained media-layer data"))
    {
        return false;
    }

    MainWindowBackgroundMusicLifecycleTestAccess::refreshInspector(mainWindow);
    QCoreApplication::processEvents();
    QWidget* const inspector = MainWindowBackgroundMusicLifecycleTestAccess::inspectorContent(
        mainWindow);
    if (!require(inspector != nullptr,
            "Inspector retirement test requires the active Item Inspector"))
    {
        return false;
    }

    return require(!containsLabelText(*inspector, QStringLiteral("Extended Media Layers"))
                && !containsLabelText(
                    *inspector,
                    QStringLiteral("Metadata only — not used during playback."))
                && !containsButtonText(*inspector, QStringLiteral("+ Add Media"))
                && !containsButtonText(*inspector, QStringLiteral("Remove"))
                && inspector->findChildren<QCheckBox*>().isEmpty()
                && inspector->findChildren<QSlider*>().isEmpty(),
        "Item Inspector must not expose Extended Media Layer controls")
        && require(containsLabelText(*inspector, QStringLiteral("VISUAL"))
                && visualPolicyControl(*inspector) != nullptr,
            "Retiring Extended Media Layers must preserve the Visual Inspector")
        && require(model.mediaLayer(*mediaLayerId) != nullptr,
            "Retiring the Player UI must preserve existing media-layer model data");
}

}

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    ApplicationTranslationState translationState{};

    if (!verifyVisualInspectorPresentationControls(translationState))
    {
        return 1;
    }

    if (!verifyVideoVisualInspectorPresentationControls(translationState))
    {
        return 1;
    }

    if (!verifyExtendedMediaLayerControlsAreNotExposed(translationState))
    {
        return 1;
    }

    MainWindow mainWindow{
        DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
        translationState};
    mainWindow.show();
    QCoreApplication::processEvents();
    WorkspaceModel& model = MainWindowBackgroundMusicLifecycleTestAccess::workspaceModel(
        mainWindow);
    const PlaylistId playlistA = model.selectedPlaylistId();
    const PlaylistId playlistB = model.addPlaylist();

    const auto itemA1 = model.addItem(
        playlistA,
        WorkspaceMediaType::Image,
        QStringLiteral("C:/Media/a-1.png"),
        QStringLiteral("A1"));
    const auto itemA2 = model.addItem(
        playlistA,
        WorkspaceMediaType::Video,
        QStringLiteral("C:/Media/a-2.mp4"),
        QStringLiteral("A2"));
    if (!require(itemA1.has_value() && itemA2.has_value(),
            "Lifecycle testing requires two independently identified Items"))
    {
        return 1;
    }

    BackgroundMusicWindow* const editorA = MainWindowBackgroundMusicLifecycleTestAccess::open(
        mainWindow,
        playlistA);
    if (!require(editorA != nullptr
            && MainWindowBackgroundMusicLifecycleTestAccess::editorCount(mainWindow) == 1,
            "Opening a Playlist must create one registered editor"))
    {
        return 1;
    }
    if (!require(editorA->parent() == nullptr && editorA->parentWidget() == nullptr
            && editorA->isWindow(),
            "Background Music editors must be independent peer top-level windows without MainWindow ownership"))
    {
        return 1;
    }
    if (!require(
            MainWindowBackgroundMusicLifecycleTestAccess::open(mainWindow, playlistA) == editorA
                && MainWindowBackgroundMusicLifecycleTestAccess::editorCount(mainWindow) == 1,
            "Opening the same Playlist twice must reuse its editor"))
    {
        return 1;
    }

    BackgroundMusicWindow* const editorB = MainWindowBackgroundMusicLifecycleTestAccess::open(
        mainWindow,
        playlistB);
    if (!require(editorB != nullptr && editorB != editorA
            && MainWindowBackgroundMusicLifecycleTestAccess::editorCount(mainWindow) == 2,
            "Different Playlist identities must own independent editors"))
    {
        return 1;
    }

    if (!require(model.renamePlaylist(playlistA, QStringLiteral("Renamed A")),
            "Lifecycle testing requires a Playlist rename"))
    {
        return 1;
    }
    MainWindowBackgroundMusicLifecycleTestAccess::refreshPlaylist(mainWindow, playlistA);
    if (!require(editorA->windowTitle() == QStringLiteral("Background Music — Renamed A"),
            "Refreshing a renamed Playlist must preserve its editor and update its title"))
    {
        return 1;
    }

    editorA->refreshFromModel();
    auto* editorItemList = editorA->findChild<QListWidget*>(QStringLiteral("musicItemList"));
    if (!require(editorItemList != nullptr && editorItemList->count() == 2,
            "The Playlist editor must expose its current Item identities after refresh"))
    {
        return 1;
    }
    editorItemList->setCurrentRow(0);
    if (!require(itemIdFor(editorItemList->currentItem()) == *itemA1
            && model.selectItem(*itemA2),
            "Selection-independence testing requires distinct editor and MainWindow selections"))
    {
        return 1;
    }
    MainWindowBackgroundMusicLifecycleTestAccess::refreshLibrary(mainWindow);
    if (!require(itemIdFor(editorItemList->currentItem()) == *itemA1,
            "MainWindow Item selection must not rebind an editor-local Item selection"))
    {
        return 1;
    }
    editorItemList->setCurrentRow(1);
    if (!require(model.selectedItemId().has_value() && model.selectedItemId() == *itemA2,
            "Editor-local Item selection must not mutate MainWindow selection"))
    {
        return 1;
    }

    QPointer<BackgroundMusicWindow> editorAGuard{editorA};
    editorA->close();
    processDeferredDeletes();
    if (!require(editorAGuard.isNull()
            && MainWindowBackgroundMusicLifecycleTestAccess::editorCount(mainWindow) == 1,
            "Manual editor close must remove only that registry entry"))
    {
        return 1;
    }

    BackgroundMusicWindow* const reopenedA = MainWindowBackgroundMusicLifecycleTestAccess::open(
        mainWindow,
        playlistA);
    QPointer<BackgroundMusicWindow> reopenedAGuard{reopenedA};
    QPointer<BackgroundMusicWindow> editorBGuard{editorB};
    if (!require(reopenedA != nullptr
            && MainWindowBackgroundMusicLifecycleTestAccess::editorCount(mainWindow) == 2,
            "Reopening a manually closed editor must create a fresh registered window"))
    {
        return 1;
    }

    MainWindowBackgroundMusicLifecycleTestAccess::closePlaylist(mainWindow, playlistA);
    processDeferredDeletes();
    if (!require(reopenedAGuard.isNull() && !editorBGuard.isNull()
            && MainWindowBackgroundMusicLifecycleTestAccess::editorCount(mainWindow) == 1,
            "Closing a Playlist editor must leave unrelated Playlist editors alive"))
    {
        return 1;
    }
    if (!require(model.removePlaylist(playlistA),
            "Lifecycle testing requires deleting the closed Playlist identity"))
    {
        return 1;
    }

    if (!require(
            MainWindowBackgroundMusicLifecycleTestAccess::open(mainWindow, playlistA) == nullptr
                && !editorBGuard.isNull(),
            "Deleted Playlists must not reopen an editor or disturb other Playlist editors"))
    {
        return 1;
    }

    QPointer<BackgroundMusicWindow> destructionGuard{};
    {
        auto* owner = new MainWindow{
            DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
            translationState};
        WorkspaceModel& ownerModel = MainWindowBackgroundMusicLifecycleTestAccess::workspaceModel(
            *owner);
        const PlaylistId ownerPlaylistId = ownerModel.selectedPlaylistId();
        destructionGuard = MainWindowBackgroundMusicLifecycleTestAccess::open(*owner, ownerPlaylistId);
        if (!require(!destructionGuard.isNull() && destructionGuard->parent() == nullptr,
                "Explicit destruction testing requires a registered parentless peer editor"))
        {
            delete owner;
            return 1;
        }

        delete owner;
    }
    processDeferredDeletes();
    if (!require(
            destructionGuard.isNull(),
            "MainWindow destruction must explicitly destroy all parentless Background Music editors"))
    {
        return 1;
    }

    auto* closeOwner = new MainWindow{
        DesktopSceneEngine::Player::TestSupport::mainWindowTestDisplayId(),
        translationState};
    closeOwner->show();
    WorkspaceModel& closeOwnerModel = MainWindowBackgroundMusicLifecycleTestAccess::workspaceModel(
        *closeOwner);
    const PlaylistId closeOwnerPlaylistA = closeOwnerModel.selectedPlaylistId();
    const PlaylistId closeOwnerPlaylistB = closeOwnerModel.addPlaylist();
    QPointer<BackgroundMusicWindow> closeEventEditorAGuard{
        MainWindowBackgroundMusicLifecycleTestAccess::open(*closeOwner, closeOwnerPlaylistA)};
    QPointer<BackgroundMusicWindow> closeEventEditorBGuard{
        MainWindowBackgroundMusicLifecycleTestAccess::open(*closeOwner, closeOwnerPlaylistB)};
    if (!require(
            !closeEventEditorAGuard.isNull() && !closeEventEditorBGuard.isNull()
                && MainWindowBackgroundMusicLifecycleTestAccess::editorCount(*closeOwner) == 2,
            "Close-event testing requires two registered peer editors"))
    {
        delete closeOwner;
        return 1;
    }

    MainWindowBackgroundMusicLifecycleTestAccess::markDocumentClean(*closeOwner);
    closeOwner->close();
    processDeferredDeletes();
    const bool closeEventClosedAllEditors = closeEventEditorAGuard.isNull()
        && closeEventEditorBGuard.isNull()
        && MainWindowBackgroundMusicLifecycleTestAccess::editorCount(*closeOwner) == 0;
    delete closeOwner;
    processDeferredDeletes();
    return require(
        closeEventClosedAllEditors,
        "Closing MainWindow must close all parentless Background Music editors through its close event")
        ? 0
        : 1;
}
