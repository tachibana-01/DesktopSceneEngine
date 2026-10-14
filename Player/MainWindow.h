#pragma once

#include "ActivePlaybackProjection.h"
#include "ApplicationPreferences.h"
#include "DisplaySelectionState.h"
#include "PlaybackSequenceController.h"
#include <DesktopSceneEngine/Engine.h>
#include <DesktopSceneEngine/Platform/Display.h>
#include <DesktopSceneEngine/Platform/DesktopPresentationHost.h>

#include "PreviewViewportHost.h"
#include "ProjectDocumentState.h"
#include "TimelinePreviewPresentation.h"
#include "TimelinePreviewProvider.h"
#include "TimelinePreviewSource.h"
#include "TimelinePreviewWorker.h"
#include "WorkspaceModel.h"
#include "SessionPresentation.h"
#include "DesktopDeployment.h"

#include <QMainWindow>
#include <QHash>
#include <QImage>
#include <QPoint>
#include <QPointer>
#include <QSize>
#include <QThread>
#include <QTimer>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <vector>

class QLabel;
class QListWidget;
class QCloseEvent;
class QEvent;
class QAction;
class QActionGroup;
class QComboBox;
class QHBoxLayout;
class QMenu;
class QPoint;
class QPushButton;
class QScrollArea;
class QSlider;
class QSplitter;
class QToolButton;
class QVBoxLayout;
class QWidget;

namespace DesktopSceneEngine::Player
{

class BackgroundMusicWindow;
class CanvasChromeFrame;

}

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:

    explicit MainWindow(
        DesktopSceneEngine::DisplayId boundDisplayId,
        DesktopSceneEngine::Player::ApplicationTranslationState& translationState,
        QWidget* parent = nullptr
    );

    MainWindow(
        DesktopSceneEngine::DisplayId boundDisplayId,
        DesktopSceneEngine::Player::ApplicationTranslationState& translationState,
        DesktopSceneEngine::Player::ApplicationPreferencesStore& preferencesStore,
        DesktopSceneEngine::Player::ApplicationPreferences initialPreferences,
        QWidget* parent = nullptr
    );

    ~MainWindow() override;

    [[nodiscard]] bool initializeEngine();

    void setAcceptedCloseCallback(std::function<void()> callback) { acceptedCloseCallback_ = std::move(callback); }
    DesktopSceneEngine::Player::SessionPresentation& trayPresentation() { return trayPresentation_; }
    [[nodiscard]] DesktopSceneEngine::Player::DesktopDeploymentProjection desktopDeploymentProjection() const;
    [[nodiscard]] DesktopSceneEngine::Player::DesktopDeploymentResult requestDesktopDeployment(
        DesktopSceneEngine::Player::DesktopDeploymentIntent intent);

protected:

    bool event(QEvent* event) override;

    bool eventFilter(QObject* watched, QEvent* event) override;

    void closeEvent(QCloseEvent* event) override;

    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;

private:

    friend class MainWindowBackgroundMusicLifecycleTestAccess;
    friend class MainWindowBackgroundMusicForwardingTestAccess;
    friend class MainWindowAvailabilityOrderingUiTestAccess;
    friend class MainWindowDocumentLifecycleTestAccess;
    friend class MainWindowPlaybackModeUiTestAccess;
    friend class MainWindowPlaybackSequenceTestAccess;
    friend class MainWindowDesktopPresentationTestAccess;
    std::function<void()> acceptedCloseCallback_;
    DesktopSceneEngine::Player::SessionPresentation trayPresentation_{*this};
    QToolButton* hideToTrayButton_ = nullptr;

    class RenderWidget;
    class TimelineHoverPopup;

    using UiLanguage = DesktopSceneEngine::Player::UiLanguage;

    enum class SaveResult
    {
        Saved,
        Cancelled,
        Failed
    };

    enum class UnsavedChangesChoice
    {
        Save,
        Discard,
        Cancel
    };

    enum class DestructiveDecision
    {
        ProceedAfterSave,
        ProceedAfterDiscard,
        Abort
    };

    MainWindow(
        DesktopSceneEngine::DisplayId boundDisplayId,
        DesktopSceneEngine::Player::ApplicationTranslationState& translationState,
        DesktopSceneEngine::Player::ApplicationPreferencesStore* preferencesStore,
        DesktopSceneEngine::Player::ApplicationPreferences initialPreferences,
        QWidget* parent
    );

    struct OpenProjectCandidate
    {
        DesktopSceneEngine::Domain::AuthoredProject project;
        std::filesystem::path path{};
    };

    struct BackgroundMusicPlaybackDisplayState
    {
        std::optional<DesktopSceneEngine::Player::PlaylistId> activePlaylistId{};
        std::optional<DesktopSceneEngine::Player::ItemId> activeItemId{};
        std::optional<DesktopSceneEngine::BackgroundMusicControl::TrackId> playlistTrackId{};
        std::optional<DesktopSceneEngine::BackgroundMusicControl::TrackId> itemTrackId{};

        friend bool operator==(const BackgroundMusicPlaybackDisplayState&, const BackgroundMusicPlaybackDisplayState&) = default;
    };

    void createMenus();
    void requestNewProject();
    void requestOpenProject();
    [[nodiscard]] QString currentDocumentDisplayName() const;
    void updateDocumentTitle();
    [[nodiscard]] UnsavedChangesChoice promptForUnsavedChanges();
    [[nodiscard]] DestructiveDecision resolveUnsavedChanges(
        UnsavedChangesChoice choice
    );
    [[nodiscard]] DestructiveDecision maybeSaveCurrentDocument();
    [[nodiscard]] static DestructiveDecision decisionForSaveResult(
        SaveResult result
    ) noexcept;
    void applyNewProjectDecision(DestructiveDecision decision);
    void createNewDocument();
    [[nodiscard]] std::optional<OpenProjectCandidate> loadProjectCandidate(
        const std::filesystem::path& path
    );
    [[nodiscard]] bool openProjectFromPath(const std::filesystem::path& path);
    [[nodiscard]] bool completeOpenProject(
        OpenProjectCandidate candidate,
        DestructiveDecision decision
    );
    void commitOpenProject(OpenProjectCandidate candidate);
    [[nodiscard]] SaveResult saveCurrentProject();
    [[nodiscard]] SaveResult saveCurrentProjectAs();
    [[nodiscard]] SaveResult saveProjectToPath(
        const std::filesystem::path& path,
        bool adoptPath
    );
    void createWorkspace();
    [[nodiscard]] QWidget* createLibraryPanel(QWidget* parent);
    [[nodiscard]] QWidget* createPreviewPanel(QWidget* parent);
    [[nodiscard]] QWidget* createInspectorPanel(QWidget* parent);
    [[nodiscard]] QWidget* createTransport(QWidget* parent);
    void refreshLibrary();
    void refreshPlaylistList();
    void refreshItemList();
    void refreshPlaylistPlaybackModeControl();
    void refreshItemPlaybackModeControl();
    void refreshInspector();
    void queueInspectorRefresh();
    void buildNoSelectionInspector();
    void buildPlaylistInspector(DesktopSceneEngine::Player::PlaylistId id);
    void buildItemInspector(DesktopSceneEngine::Player::ItemId id);
    void addVideoItemAudioInspectorSection(DesktopSceneEngine::Player::ItemId id);
    void addVisualInspectorSection(DesktopSceneEngine::Player::ItemId itemId);
    void addPlaylist();
    void addMedia();
    [[nodiscard]] DesktopSceneEngine::Player::BackgroundMusicWindow*
        openBackgroundMusicWindow(DesktopSceneEngine::Player::PlaylistId id);
    void refreshOpenBackgroundMusicWindow(DesktopSceneEngine::Player::PlaylistId id);
    void refreshAllOpenBackgroundMusicWindows();
    void closeBackgroundMusicWindow(DesktopSceneEngine::Player::PlaylistId id);
    void closeAllBackgroundMusicWindows();
    [[nodiscard]] BackgroundMusicPlaybackDisplayState
        currentBackgroundMusicPlaybackDisplayState() const;
    void synchronizeBackgroundMusicPlaybackDisplay();
    void applyBackgroundMusicPlaybackDisplayState(
        DesktopSceneEngine::Player::BackgroundMusicWindow& window
    ) const;
    void showPlaylistContextMenu(const QPoint& position);
    void showItemContextMenu(const QPoint& position);
    void renamePlaylist(DesktopSceneEngine::Player::PlaylistId id);
    void removePlaylist(DesktopSceneEngine::Player::PlaylistId id);
    void activatePlaylist(DesktopSceneEngine::Player::PlaylistId id);
    void renameItem(DesktopSceneEngine::Player::ItemId id);
    void removeItem(DesktopSceneEngine::Player::ItemId id);
    void activateItem(DesktopSceneEngine::Player::ItemId id);
    [[nodiscard]] bool setPlaylistAvailability(
        DesktopSceneEngine::Player::PlaylistId id,
        bool enabled
    );
    [[nodiscard]] bool setItemAvailability(
        DesktopSceneEngine::Player::ItemId id,
        bool enabled
    );
    [[nodiscard]] bool startPlaylistPlayback(
        DesktopSceneEngine::Player::PlaylistId playlistId,
        std::optional<DesktopSceneEngine::Player::ItemId> startItemId,
        bool synchronizeSelection = true
    );
    enum class ActiveProjectionRebuildResult
    {
        Restored,
        ReboundAtStart,
        StartedTarget,
        Unchanged,
        Stopped
    };
    enum class ActiveProjectionRebuildMode
    {
        RestoreCurrentItem,
        StartTargetItem
    };
    [[nodiscard]] ActiveProjectionRebuildResult
    rebuildActiveProjection(
        DesktopSceneEngine::Player::ItemId targetItemId,
        ActiveProjectionRebuildMode mode
    );
    [[nodiscard]] std::optional<
        DesktopSceneEngine::BackgroundMusicControl::ActiveScopeSnapshot>
        activeBackgroundMusicScopeSnapshot() const;
    [[nodiscard]] bool forwardBackgroundMusicControl(
        const DesktopSceneEngine::BackgroundMusicControl::Event& event
    );
    void forwardBackgroundMusicScopeAfterStart(
        bool hadPreviousActivePlayback,
        const std::optional<DesktopSceneEngine::BackgroundMusicControl::ActiveScopeSnapshot>&
            previousScope
    );
    void forwardBackgroundMusicScopeChange();
    void handleBackgroundMusicQueueChanged(
        DesktopSceneEngine::Player::PlaylistId playlistId,
        std::optional<DesktopSceneEngine::Player::ItemId> itemId
    );
    void handlePlaylistMusicEnabledChanged(
        DesktopSceneEngine::Player::PlaylistId playlistId,
        bool enabled
    );
    void handleItemMusicEnabledChanged(
        DesktopSceneEngine::Player::PlaylistId playlistId,
        DesktopSceneEngine::Player::ItemId itemId,
        bool enabled
    );
    void handlePlaylistTrackSwitchRequested(
        DesktopSceneEngine::Player::PlaylistId playlistId,
        DesktopSceneEngine::Player::TrackId trackId
    );
    void handleItemTrackSwitchRequested(
        DesktopSceneEngine::Player::PlaylistId playlistId,
        DesktopSceneEngine::Player::ItemId itemId,
        DesktopSceneEngine::Player::TrackId trackId
    );
    void togglePlaybackPause();
    void stopActivePlayback() noexcept;
    void synchronizeActivePlaybackItem();
    void navigateScene(bool previous);
    using PlaybackSequence = DesktopSceneEngine::Player::PlaybackSequenceController<
        DesktopSceneEngine::Player::PlaylistId,
        DesktopSceneEngine::Player::ItemId>;
    [[nodiscard]] PlaybackSequence::Collections playbackSequenceCollections() const;
    void synchronizePlaybackSequenceConfiguration(
        const PlaybackSequence::Collections& collections
    );
    void rebasePlaybackSequenceToActivePosition();
    [[nodiscard]] bool advancePlaybackSequence();
    [[nodiscard]] bool transitionToSequencePosition(
        const PlaybackSequence::Position& position
    );
    void refreshDisplaySelection();
    void resetWorkspaceForNewDocument();
    void applyDisplayEnumerationResult(
        const DesktopSceneEngine::Platform::DisplayEnumerationResult& result
    );
    void updateCanvasAspectUi();
    void updateBoundDisplayUi();
    [[nodiscard]] bool hasDesktopPresentation() const noexcept;
    void updateDesktopDeploymentButton();
    void toggleDesktopDeployment();
    void identifyBoundDisplay();
    [[nodiscard]] QString displayDescriptorLabel(
        const DesktopSceneEngine::Platform::DisplayDescriptor& descriptor
    ) const;

    void applyApplicationPreferences();
    void applyHighlightCanvasBoundary(bool highlighted);
    void selectCanvasOutsideMode(DesktopSceneEngine::CanvasOutsideMode mode);
    void projectCanvasOutsideMode();
    [[nodiscard]] std::optional<DesktopSceneEngine::CanvasOutsideInstruction>
        canvasOutsideInstructionForFrame() const noexcept;
    void applyCommittedLanguage(UiLanguage language);
    void reloadCommittedLanguage();
    void retranslateUi();

    void closeAfterDecision(QCloseEvent* event, DestructiveDecision decision);

    enum class DesktopDeploymentAttempt
    {
        Ready,
        TransientlyUnavailable,
        Failed
    };
    void shutdownEngine() noexcept;
    [[nodiscard]] DesktopDeploymentAttempt tryDeployDesktopPresentation();
    [[nodiscard]] bool removeDesktopPresentation() noexcept;
    [[nodiscard]] bool reconcileDesktopPresentation(bool allowContinuation);
    void requestDesktopPresentationReconciliation();
    void runDesktopPresentationContinuation();
    void cancelDesktopPresentationReconciliation() noexcept;
    void onTick();
    void refreshStatus();
    void beginTimelineDrag();
    void previewTimelinePosition(int sliderValue);
    void commitTimelineDrag();
    void cancelTimelineDrag() noexcept;
    void createTimelineHoverPopup();
    void startTimelinePreviewWorker();
    void shutdownTimelinePreviewWorker() noexcept;
    void updateTimelineHoverPreview(const QPoint& pointInSlider);
    void decodeTimelineHoverPreview();
    void applyTimelineHoverPreviewResult(
        const DesktopSceneEngine::Player::TimelinePreviewResult& result
    );
    void refreshTimelineHoverPreviewPresentation(bool force = false);
    [[nodiscard]] QSize timelineHoverPopupCanvasSize() const noexcept;
    void setTimelineHoverPreviewPending();
    void setTimelineHoverPreviewUnavailable();
    void updateTimelineHoverPopupGeometry();
    void hideTimelineHoverPopup() noexcept;
    void invalidateTimelineHoverPreview() noexcept;
    void clearTimelinePreviewWorkerSource() noexcept;
    void reconcileTimelinePreviewPreloadTarget();
    void invalidateTimelinePreviewPreloadTarget() noexcept;
    [[nodiscard]] bool reconcileTimelineHoverPreviewSource(
        const DesktopSceneEngine::Player::TimelinePreviewSource& source
    );
    [[nodiscard]] std::optional<DesktopSceneEngine::Player::TimelinePreviewSource>
        activeTimelinePreviewSource() const;
    [[nodiscard]] const DesktopSceneEngine::Domain::Scene*
        activeTimelinePreviewScene() const noexcept;
    [[nodiscard]] std::optional<DesktopSceneEngine::Player::ItemId>
        activeTimelineItemId() const noexcept;
    [[nodiscard]] bool timelineDragTargetsCurrentScene(float duration) const noexcept;
    void setPlaybackButtonText();
    void updateEffectiveOutputVolume();
    void synchronizeActivePlaybackVolumeCluster();
    void updateActivePlaylistNameElision();
    void synchronizePlaylistInspectorMasterVolume(
        DesktopSceneEngine::Player::PlaylistId id
    );
    [[nodiscard]] DesktopSceneEngine::Player::PlaylistId
        volumeControlPlaylistId() const noexcept;
    [[nodiscard]] float currentPlaylistMasterVolume() const noexcept;
    [[nodiscard]] float activePlaylistMasterVolume() const noexcept;
    [[nodiscard]] double activeItemMediaVolume() const noexcept;

    DesktopSceneEngine::Player::ApplicationTranslationState& translationState_;
    DesktopSceneEngine::Player::ApplicationPreferencesStore* preferencesStore_ = nullptr;
    DesktopSceneEngine::Player::ApplicationPreferences applicationPreferences_{};
    DesktopSceneEngine::CanvasOutsideMode currentCanvasOutsideMode_ =
        DesktopSceneEngine::CanvasOutsideMode::Opaque;
    DesktopSceneEngine::Player::WorkspaceModel workspaceModel_;
    DesktopSceneEngine::Player::ProjectDocumentState documentState_{};
    PlaybackSequence playbackSequence_{};
    QHash<QUuid, QPointer<DesktopSceneEngine::Player::BackgroundMusicWindow>>
        backgroundMusicWindows_{};
    BackgroundMusicPlaybackDisplayState backgroundMusicPlaybackDisplayState_{};
    std::optional<DesktopSceneEngine::Player::ActivePlaybackProjection>
        activePlaybackProjection_{};
    // Reverse member destruction releases Engine's borrowed graphics bindings
    // before the Desktop HWND even if normal explicit shutdown is bypassed.
    DesktopSceneEngine::Platform::DesktopPresentationHost desktopHost_{};
    DesktopSceneEngine::Engine engine_{};
    DesktopSceneEngine::PresentationTargetId previewPresentationTargetId_{};
    DesktopSceneEngine::PresentationTargetId desktopPresentationTargetId_{};
    std::uint32_t taskbarCreatedMessage_ = 0U;
    std::uint32_t globalSettingsChangedMessage_ = 0U;
    // User intent, not realization: survives endpoint loss after deployment,
    // but an unsuccessful initial explicit Deploy clears it.
    bool desktopDeploymentDesired_ = false;
    bool desktopReplacementInProgress_ = false;
    bool desktopEndpointReconciliationQueued_ = false;
    // Private native-creation test seam; empty on every production path.
    std::function<DesktopSceneEngine::Platform::DesktopPresentationHostStatus(
        DesktopSceneEngine::Platform::DesktopPresentationHost&,
        const DesktopSceneEngine::DisplayId&)> desktopHostCreatorForTesting_{};
    // Private current-topology test seam; production re-observes Explorer.
    std::function<DesktopSceneEngine::Platform::DesktopPresentationHostAttachmentStatus(
        DesktopSceneEngine::Platform::DesktopPresentationHost&,
        const DesktopSceneEngine::DisplayId&)> desktopHostReconcilerForTesting_{};
    // Private value-snapshot seam; production always enumerates Platform displays.
    std::function<DesktopSceneEngine::Platform::DisplayEnumerationResult()>
        displayEnumeratorForTesting_{};
    std::function<bool(const DesktopSceneEngine::BackgroundMusicControl::Event&)>
        backgroundMusicControlForwarder_{};
    QMenu* fileMenu_ = nullptr;
    QMenu* projectMenu_ = nullptr;
    QMenu* playbackMenu_ = nullptr;
    QMenu* viewMenu_ = nullptr;
    QMenu* desktopBackgroundMenu_ = nullptr;
    QAction* newProjectAction_ = nullptr;
    QAction* openProjectAction_ = nullptr;
    QAction* saveProjectAction_ = nullptr;
    QAction* saveProjectAsAction_ = nullptr;
    QAction* exitAction_ = nullptr;
    QAction* addPlaylistAction_ = nullptr;
    QAction* addMediaAction_ = nullptr;
    QAction* playPauseAction_ = nullptr;
    QAction* previousAction_ = nullptr;
    QAction* nextAction_ = nullptr;
    QAction* resetLayoutAction_ = nullptr;
    QAction* highlightCanvasBoundaryAction_ = nullptr;
    QAction* canvasOutsideOpaqueAction_ = nullptr;
    QAction* canvasOutsideTransparentAction_ = nullptr;
    RenderWidget* renderWidget_ = nullptr;
    DesktopSceneEngine::Player::CanvasChromeFrame* canvasFrame_ = nullptr;
    PreviewViewportHost* previewViewportHost_ = nullptr;
    QLabel* libraryHeader_ = nullptr;
    QLabel* playlistsHeader_ = nullptr;
    QLabel* itemsHeader_ = nullptr;
    QLabel* previewHeader_ = nullptr;
    QLabel* currentCanvasAspectLabel_ = nullptr;
    QLabel* mapToLabel_ = nullptr;
    QLabel* playlistPlaybackModeLabel_ = nullptr;
    QLabel* itemPlaybackModeLabel_ = nullptr;
    QListWidget* playlistList_ = nullptr;
    QListWidget* itemList_ = nullptr;
    QComboBox* playlistPlaybackModeCombo_ = nullptr;
    QComboBox* itemPlaybackModeCombo_ = nullptr;
    QPushButton* addPlaylistButton_ = nullptr;
    QPushButton* addMediaButton_ = nullptr;
    QScrollArea* inspectorScrollArea_ = nullptr;
    QWidget* inspectorContent_ = nullptr;
    QVBoxLayout* inspectorLayout_ = nullptr;
    QSplitter* workspaceSplitter_ = nullptr;
    QLabel* boundDisplayValueLabel_ = nullptr;
    QToolButton* identifyDisplayButton_ = nullptr;
    QToolButton* deployDisplayButton_ = nullptr;
    DesktopSceneEngine::Player::DisplaySelectionState displaySelection_;
    QLabel* transportTimeLabel_ = nullptr;
    QPushButton* previousButton_ = nullptr;
    QPushButton* playPauseButton_ = nullptr;
    QPushButton* nextButton_ = nullptr;
    QWidget* volumeCluster_ = nullptr;
    QPushButton* muteButton_ = nullptr;
    QLabel* volumeLabel_ = nullptr;
    QLabel* activePlaylistNameLabel_ = nullptr;
    QLabel* currentPlaybackRateValueLabel_ = nullptr;
    QSlider* volumeSlider_ = nullptr;
    QSlider* inspectorPlaylistMasterVolumeSlider_ = nullptr;
    QLabel* inspectorPlaylistMasterVolumeValueLabel_ = nullptr;
    DesktopSceneEngine::Player::PlaylistId inspectorPlaylistMasterVolumePlaylistId_{};
    QSlider* positionSlider_ = nullptr;
    TimelineHoverPopup* timelineHoverPopup_ = nullptr;
    DesktopSceneEngine::Player::TimelinePreviewProvider timelinePreviewProvider_{};
    DesktopSceneEngine::Player::TimelinePreviewRequestState
        timelinePreviewRequestState_{};
    QThread timelinePreviewWorkerThread_{};
    DesktopSceneEngine::Player::TimelinePreviewWorker* timelinePreviewWorker_ = nullptr;
    QTimer desktopReconciliationContinuationTimer_{};
    QTimer tickTimer_{};
    QTimer timelineHoverPreviewTimer_{};
    std::chrono::steady_clock::time_point previousTick_{};
    float statusElapsed_ = 0.0F;
    bool timelineDragging_ = false;
    std::optional<DesktopSceneEngine::Player::ItemId> timelineDragItemId_{};
    float timelineDragDuration_ = 0.0F;
    std::optional<DesktopSceneEngine::Player::TimelinePreviewSource>
        timelineHoverPreviewSource_{};
    std::optional<DesktopSceneEngine::Player::TimelinePreviewSource>
        timelinePreviewPreloadSource_{};
    std::optional<std::int64_t> timelinePreviewPreloadDurationMilliseconds_{};
    struct PendingTimelineHoverPreview
    {
        DesktopSceneEngine::Player::TimelinePreviewSource source{};
        float timeSeconds = 0.0F;
        std::int64_t durationMilliseconds = 0;
        std::uint64_t generation = 0U;
        std::uint64_t serial = 0U;
        std::optional<std::int64_t> representativeTimestampMilliseconds{};
        DesktopSceneEngine::Player::TimelinePreviewTiming timing{};
        bool submittedToWorker = false;
    };
    struct DisplayedTimelineHoverPreview
    {
        QImage sourceFrame{};
        DesktopSceneEngine::Player::TimelinePreviewDisplayedFrame identity{};
    };
    std::optional<PendingTimelineHoverPreview> pendingTimelineHoverPreview_{};
    std::optional<DisplayedTimelineHoverPreview> displayedTimelineHoverPreview_{};
    std::optional<QPoint> timelineHoverPoint_{};
    std::optional<float> timelineHoverRawTimeSeconds_{};
    std::optional<DesktopSceneEngine::Player::TimelinePreviewTiming::TimePoint>
        lastTimelineHoverEvent_{};
    std::optional<DesktopSceneEngine::Player::TimelinePreviewTiming>
        lastTimelineHoverPreviewTiming_{};
    DesktopSceneEngine::Player::TimelinePreviewStoreOutcome
        lastTimelineHoverPreviewStoreOutcome_ =
            DesktopSceneEngine::Player::TimelinePreviewStoreOutcome::Unavailable;
    std::uint64_t timelineHoverEventCount_ = 0U;
    std::uint64_t timelineHoverVideoRequestSubmissionCount_ = 0U;
    std::uint64_t timelineHoverPreviewGeneration_ = 0U;
    std::uint64_t timelineHoverPreviewRequestSerial_ = 0U;
    std::uint64_t timelineHoverPreviewPresentationGeneration_ = 0U;
    std::uint64_t timelinePreviewPreloadGeneration_ = 0U;
    QSize timelineHoverPreviewCanvasSize_{};
    bool timelineHoverActive_ = false;
    bool timelinePreviewCompositionDirty_ = false;
    bool inspectorRefreshPending_ = false;
    bool engineInitialized_ = false;
};
