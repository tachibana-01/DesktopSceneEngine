#pragma once

#include "ActivePlaybackProjection.h"
#include "ApplicationPreferences.h"
#include "DisplaySelectionState.h"
#include "PlaybackSequenceController.h"
#include "ProjectDocumentState.h"
#include "SessionDocumentTransaction.h"
#include "SessionPlatform.h"
#include "SessionRuntimeOperation.h"
#include "TimelinePreviewSource.h"
#include <QObject>
#include <QHash>
#include <QJsonObject>
#include <QTimer>
#include <array>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <variant>

class SessionDocumentTransactionTestAccess;
class SessionAuthoredOperationTestAccess;
class SessionRuntimeOperationTestAccess;
class SessionApplicationTestAccess;

namespace DesktopSceneEngine::Player
{
class SessionReadModel;

// One canonical Session application owner. Ordinary control remains on this
// QObject's executor. Engine keeps all playback/audio/renderer runtime truth.
class SessionApplication final : public QObject
{
public:
    using PlaybackSequence = PlaybackSequenceController<PlaylistId, ItemId>;
    using AudioEffectScope = Engine::AudioEffectScope;
    using AudioEffectToken = Engine::AudioEffectToken;
    using UnsavedChangesChoice = SessionUnsavedChoice;
    enum class SaveResult { Saved, Cancelled, Failed };
    enum class DestructiveDecision { ProceedAfterSave, ProceedAfterDiscard, Abort };
    enum class ActiveProjectionRebuildResult { Restored, ReboundAtStart, StartedTarget, Unchanged, Stopped, Deferred };
    enum class ActiveProjectionRebuildMode { RestoreCurrentItem, StartTargetItem };
    enum class CloseState { Open, Deciding, Retiring, Ready, Accepted };
    enum class ViewPreferenceResult { Applied, AlreadySatisfied, Unavailable };
    struct OpenProjectCandidate
    {
        Domain::AuthoredProject project;
        std::filesystem::path path{};
    };
    enum class DocumentStage { Queued, OpenPicker, SavePicker, Decision, Executing };
    struct PendingDocumentOperation
    {
        QUuid token;
        DocumentAction action;
        DocumentStage stage = DocumentStage::Queued;
        std::optional<OpenProjectCandidate> candidate;
        bool externalClose = false;
    };
    struct BgmEditPermit { QUuid documentIncarnation; PlaylistId playlistId; };
    struct VisualMediaImportTarget { PlaylistId playlistId; };
    using MediaImportTarget = std::variant<VisualMediaImportTarget, PlaylistBgmTarget, ItemBgmTarget>;
    struct PendingMediaImport { QUuid token; QUuid documentIncarnation; MediaImportTarget target; };
    struct NativeAudioEffect
    {
        QUuid operation, document;
        QString binding;
        AudioEffectToken token;
        AudioEffectScope scope = AudioEffectScope::Structural;
        PlaylistId playlist;
        std::optional<ItemId> item;
    };

    SessionApplication(Engine& engine, DisplayId boundDisplay,
        ApplicationTranslationState& translation, ApplicationPreferencesStore* store = nullptr,
        ApplicationPreferences preferences = {}, QObject* parent = nullptr);
    SessionApplication(Engine& engine, DisplayId boundDisplay,
        ApplicationPreferencesStore* store, ApplicationPreferences preferences, QObject* parent = nullptr);
    ~SessionApplication() override;
    void attachPlatform(SessionPlatform& platform);
    void detachPlatform(SessionPlatform& platform) noexcept;
    void setPresentationCapabilities(bool document, bool authored, bool runtime);
    [[nodiscard]] SessionPlatformFacts platformFacts() const;
    [[nodiscard]] const WorkspaceModel& workspace() const { return workspaceModel_; }
    // Runtime handle for the native rendering adapter; never an authored owner.
    [[nodiscard]] Engine& presentationEngine() const { return engine_; }
    [[nodiscard]] const ProjectDocumentState& document() const { return documentState_; }
    [[nodiscard]] const std::optional<ActivePlaybackProjection>& activeProjection() const { return activePlaybackProjection_; }
    [[nodiscard]] const QString& bindingToken() const { return runtimeBindingToken_; }
    [[nodiscard]] const QUuid& documentIncarnation() const { return documentIncarnation_; }
    [[nodiscard]] const DisplaySelectionState& displaySelection() const { return displaySelection_; }
    [[nodiscard]] bool engineInitialized() const { return engineInitialized_; }
    [[nodiscard]] bool closing() const { return closeState_ == CloseState::Retiring || closeState_ == CloseState::Ready || closeState_ == CloseState::Accepted; }
    [[nodiscard]] bool closePending() const { return closeState_ != CloseState::Open && closeState_ != CloseState::Accepted; }
    [[nodiscard]] CloseState closeState() const { return closeState_; }
    [[nodiscard]] bool finalClosePermitted() const { return closeState_ == CloseState::Ready || closeState_ == CloseState::Accepted; }
    [[nodiscard]] bool busy() const { return pendingDocumentOperation_.has_value() || pendingMediaImport_.has_value() || authoredOperationExecuting_ || pendingRuntimeIntent_.has_value() || closePending(); }
    [[nodiscard]] const std::optional<PendingDocumentOperation>& pendingDocumentOperation() const { return pendingDocumentOperation_; }
    [[nodiscard]] const std::optional<PendingMediaImport>& pendingMediaImport() const { return pendingMediaImport_; }
    [[nodiscard]] const std::optional<AuthoredResult>& lastMediaImport() const { return lastMediaImport_; }
    [[nodiscard]] const QHash<QUuid, BgmEditPermit>& bgmEditPermits() const { return bgmEditPermits_; }
    [[nodiscard]] CanvasOutsideMode canvasOutsideMode() const { return currentCanvasOutsideMode_; }
    [[nodiscard]] bool desktopDeploymentDesired() const { return desktopDeploymentDesired_; }
    [[nodiscard]] bool authoredOperationExecuting() const { return authoredOperationExecuting_; }
    [[nodiscard]] bool documentCommandsPrepared() const { return documentCommandsPrepared_; }
    [[nodiscard]] bool authoredCommandsPrepared() const { return authoredCommandsPrepared_; }
    [[nodiscard]] bool runtimeCommandsPrepared() const { return runtimeCommandsPrepared_; }
    [[nodiscard]] const ApplicationPreferences& applicationPreferences() const { return applicationPreferences_; }
    [[nodiscard]] UiLanguage effectiveLanguage() const noexcept;

    [[nodiscard]] bool initializeEngine();
    [[nodiscard]] bool initializeAutonomousRuntime(void* controlWakeEvent);
    [[nodiscard]] bool enableAutonomousMediaPreparation();
    void observePresentationFacts(SessionPlatformFacts facts);
    void publishDetachedFrame();
    void acceptPresentationResult(Engine::MediaPreparationToken ticket, PresentationUploadResult result);
    void shutdownEngine() noexcept;
    void onTick();
    void requestClose(bool external = false);
    void closeAfterDecision(DestructiveDecision decision);
    void completePlatformClose(bool accepted);
    void requestNewProject();
    void requestOpenProject();
    [[nodiscard]] QString currentDocumentDisplayName() const;
    [[nodiscard]] UnsavedChangesChoice promptForUnsavedChanges();
    [[nodiscard]] DestructiveDecision resolveUnsavedChanges(UnsavedChangesChoice choice);
    [[nodiscard]] DestructiveDecision maybeSaveCurrentDocument();
    [[nodiscard]] static DestructiveDecision decisionForSaveResult(SaveResult result) noexcept;
    void applyNewProjectDecision(DestructiveDecision decision);
    void createNewDocument();
    [[nodiscard]] std::optional<OpenProjectCandidate> loadProjectCandidate(const std::filesystem::path& path, QString* failure = nullptr);
    [[nodiscard]] bool openProjectFromPath(const std::filesystem::path& path);
    [[nodiscard]] bool completeOpenProject(OpenProjectCandidate candidate, DestructiveDecision decision, QString* failure = nullptr);
    void commitOpenProject(OpenProjectCandidate candidate);
    [[nodiscard]] SaveResult saveCurrentProject();
    [[nodiscard]] SaveResult saveCurrentProjectAs();
    [[nodiscard]] SaveResult saveProjectToPath(const std::filesystem::path& path, bool adoptPath, QString* failure = nullptr);
    [[nodiscard]] DocumentCommandResult beginDocumentOperation(DocumentAction action);
    [[nodiscard]] DocumentCommandResult continueDocumentOperation(const QUuid& token, DocumentChoice choice);
    [[nodiscard]] QJsonObject documentTransactionSnapshot() const;
    void executeDocumentOperation(const QUuid& token);
    void executeDocumentChoice(const QUuid& token, DocumentChoice choice);
    void completeDocumentReplacement(const QUuid& token, DestructiveDecision decision);
    void finishDocumentOperation(const QUuid& token, const QString& outcome, const QString& message = {});
    [[nodiscard]] std::optional<std::filesystem::path> pickDocumentPath(bool save);
    void closeExternalDocumentOperation();
    [[nodiscard]] bool prepareInternalDocumentProof(const QString& path);
    [[nodiscard]] bool prepareInternalAuthoredProof(const QString& path);
    [[nodiscard]] bool prepareInternalPreviewMedia(const QString& path);

    [[nodiscard]] AuthoredResult applyAuthoredOperation(const AuthoredRequest& request);
    using AuthoredCompletion = std::function<void(AuthoredResult)>;
    void applyAuthoredOperationDeferred(const AuthoredRequest& request, AuthoredCompletion complete);
    [[nodiscard]] AuthoredResult executeAuthoredOperation(const AuthoredRequest& request);
    [[nodiscard]] AuthoredResult addMediaSources(PlaylistId playlist, const QStringList& sources);
    [[nodiscard]] AuthoredResult removePlaylistAuthored(PlaylistId id);
    [[nodiscard]] AuthoredResult removeItemAuthored(PlaylistId playlist, ItemId id);
    void executeMediaImport(const QUuid& token);
    [[nodiscard]] AuthoredResult beginNativeMediaImport(PlaylistId playlist,
        std::optional<ItemId> item = std::nullopt, bool backgroundMusic = false);
    [[nodiscard]] QUuid issueBgmEditPermit(PlaylistId playlist);
    void revokeBgmEditPermit(const QUuid& permit);
    void retireBgmEditAuthority(bool closing = false);
    [[nodiscard]] AuthoredStatus validateBgmTarget(const BgmTarget& target) const;
    [[nodiscard]] AuthoredResult applyBgmAuthoredOperation(const BgmAuthoredRequest& request);
    [[nodiscard]] AuthoredResult executeBgmMutation(const BgmAuthoredRequest& request);
    [[nodiscard]] AuthoredResult addBgmSources(const BgmTarget& target, const QStringList& sources);
    [[nodiscard]] BgmRuntimeResult bgmRuntimeState(const QUuid& permit, const BgmTarget& target) const;
    [[nodiscard]] BgmRuntimeResult useBgmTrack(const QUuid& permit, const BgmTarget& target, const Domain::TrackId& track);
    [[nodiscard]] RuntimeResult applyRuntimeOperation(const RuntimeRequest& request);
    using RuntimeCompletion = std::function<void(RuntimeResult)>;
    void applyRuntimeOperationDeferred(const RuntimeRequest& request, RuntimeCompletion complete);
    [[nodiscard]] QJsonObject runtimePlaybackSnapshot() const;

    [[nodiscard]] bool selectPlaylist(PlaylistId id);
    [[nodiscard]] bool selectItem(ItemId id);
    [[nodiscard]] bool setPlaylistAvailability(PlaylistId id, bool enabled);
    [[nodiscard]] bool setItemAvailability(ItemId id, bool enabled);
    [[nodiscard]] bool startPlaylistPlayback(PlaylistId playlist, std::optional<ItemId> item, bool synchronizeSelection = true, bool showFailureDialog = true, QString* failureStatus = nullptr);
    [[nodiscard]] PlaybackSequence::Collections playbackSequenceCollections() const;
    void synchronizePlaybackSequenceConfiguration(const PlaybackSequence::Collections& collections);
    void rebasePlaybackSequenceToActivePosition();
    [[nodiscard]] bool transitionToSequencePosition(const PlaybackSequence::Position& position);
    [[nodiscard]] bool advancePlaybackSequence();
    [[nodiscard]] bool navigateScene(bool previous);
    void togglePlaybackPause();
    [[nodiscard]] std::optional<AudioEffectToken> toggleOutputMute();
    [[nodiscard]] bool seekPlayback(float seconds);
    [[nodiscard]] ActiveProjectionRebuildResult rebuildActiveProjection(ItemId target, ActiveProjectionRebuildMode mode);
    void describeAuthoredRebuild(AuthoredResult& result, ActiveProjectionRebuildResult rebuild, bool startsTarget = false);
    void renewRuntimeBinding();
    void stopActivePlayback() noexcept;
    void synchronizeActivePlaybackItem();
    [[nodiscard]] std::optional<ItemId> activeTimelineItemId() const noexcept;
    [[nodiscard]] std::optional<TimelinePreviewSource> activeTimelinePreviewSource() const;
    [[nodiscard]] const Domain::Scene* activeTimelinePreviewScene() const noexcept;
    [[nodiscard]] std::optional<BackgroundMusicControl::ActiveScopeSnapshot> activeBackgroundMusicScopeSnapshot() const;
    [[nodiscard]] bool forwardBackgroundMusicControl(const BackgroundMusicControl::Event& event);
    void forwardBackgroundMusicScopeAfterStart(bool hadPrevious, const std::optional<BackgroundMusicControl::ActiveScopeSnapshot>& previous);
    void forwardBackgroundMusicScopeChange();
    void handleBackgroundMusicQueueChanged(PlaylistId playlist, std::optional<ItemId> item);
    [[nodiscard]] RuntimeOutcome synchronizeBackgroundMusicConfig(PlaylistId playlist, std::optional<ItemId> item);
    void handlePlaylistMusicEnabledChanged(PlaylistId playlist, bool enabled);
    void handleItemMusicEnabledChanged(PlaylistId playlist, ItemId item, bool enabled);
    void handlePlaylistTrackSwitchRequested(PlaylistId playlist, TrackId track);
    void handleItemTrackSwitchRequested(PlaylistId playlist, ItemId item, TrackId track);
    [[nodiscard]] std::optional<AudioEffectToken> beginNativeAudioEffect(AudioEffectScope scope, PlaylistId playlist = {}, std::optional<ItemId> item = std::nullopt);
    [[nodiscard]] QJsonObject finishNativeAudioEffect(AudioEffectToken token, bool accepted = true);
    [[nodiscard]] QJsonObject nativeAudioEffect(const NativeAudioEffect& operation) const;
    [[nodiscard]] QJsonObject audioEffectsSnapshot() const;
    [[nodiscard]] QJsonObject audioEffectsSnapshotAfterDrain() const;
    void pollNativeAudioEffects();
    [[nodiscard]] PlaylistId volumeControlPlaylistId() const noexcept;
    [[nodiscard]] float currentPlaylistMasterVolume() const noexcept;
    [[nodiscard]] float activePlaylistMasterVolume() const noexcept;
    [[nodiscard]] double activeItemMediaVolume() const noexcept;
    void updateEffectiveOutputVolume();
    void refreshDisplaySelection();
    void resetWorkspaceForNewDocument();
    void applyDisplayEnumerationResult(const Platform::DisplayEnumerationResult& result);
    [[nodiscard]] DesktopDeploymentProjection desktopDeploymentProjection() const;
    [[nodiscard]] DesktopDeploymentResult requestDesktopDeployment(DesktopDeploymentIntent intent);
    using DesktopDeploymentCompletion = std::function<void(DesktopDeploymentResult)>;
    void requestDetachedDesktopDeployment(DesktopDeploymentIntent intent, DesktopDeploymentCompletion complete);
    void reconcileDetachedDesktopTopology(bool allowContinuation = true);
    [[nodiscard]] bool reconcileDesktopPresentation(bool allowContinuation);
    void requestDesktopPresentationReconciliation();
    void runDesktopPresentationContinuation();
    void cancelDesktopPresentationReconciliation() noexcept;
    [[nodiscard]] ViewPreferenceResult selectCanvasOutsideMode(CanvasOutsideMode mode);
    [[nodiscard]] ViewPreferenceResult applyHighlightCanvasBoundary(bool highlighted);
    void applyCommittedLanguage(UiLanguage language);
    void reloadCommittedLanguage();

private:
    friend class SessionReadModel;
    friend class ::SessionDocumentTransactionTestAccess;
    friend class ::SessionAuthoredOperationTestAccess;
    friend class ::SessionRuntimeOperationTestAccess;
    friend class ::SessionApplicationTestAccess;
    SessionApplication(Engine& engine, DisplayId boundDisplay,
        ApplicationTranslationState* translation, ApplicationPreferencesStore* store,
        ApplicationPreferences preferences, QObject* parent);
    void requireExecutor() const;
    struct AuthoredRebuildPlan
    {
        ActivePlaybackProjection replacement;
        ItemId item;
        ActiveProjectionRebuildMode mode;
        float position = 0;
        float rate = 1;
        bool paused = false;
        bool reboundAtStart = false;
    };
    struct PreparedAuthoredRebuild
    {
        AuthoredRebuildPlan plan;
        AuthoredResult result;
        AuthoredCompletion complete;
    };
    struct PreparedRuntimeIntent
    {
        Engine::MediaPreparationToken media;
        QString binding;
        std::uint64_t authoredRevision;
        PlaylistId playlist;
        ItemId item;
        std::optional<ActivePlaybackProjection> replacement;
        std::optional<PlaybackSequence> sequence;
        bool seek = false;
        bool selectItem = false;
        bool natural = false;
        RuntimeCompletion complete;
        std::optional<PreparedAuthoredRebuild> authored;
    };
    void servicePreparedRuntimeIntent();
    void servicePresentationResult();
    void cancelPreparedRuntimeIntent();
    void startPreparedSequenceAdvance(bool previous, bool natural, RuntimeCompletion complete);
    void prepareRuntimeScene(PlaylistId playlist, ItemId item, bool select, bool natural,
        std::optional<PlaybackSequence> sequence, RuntimeCompletion complete);
    void completePreparedRuntime(PreparedRuntimeIntent intent, QString status, QString message,
        std::optional<AudioEffectToken> effect = {});
    void beginPreparedAuthoredRebuild(AuthoredRebuildPlan plan, AuthoredResult result, AuthoredCompletion complete);
    void completeAuthoredRebuild(PreparedAuthoredRebuild authored, ActiveProjectionRebuildResult rebuild,
        std::optional<AudioEffectToken> effect = {});
    void runAuthoredWithDeferredRebuild(const std::function<AuthoredResult()>& execute, AuthoredCompletion complete);
    void finishMediaImportResult(const QUuid& token, AuthoredResult result);
    [[nodiscard]] std::optional<Engine::MediaPreparationToken> requestAuthoredRebuildMedia(const AuthoredRebuildPlan& plan);
    static QString tr(const char* text);
    void view(SessionViewChange change, bool immediate = false) const;
    void notify(SessionNotice kind, const QString& title, const QString& message) const;
    void beginCloseRetirement(DestructiveDecision decision);
    void abortClose(const std::optional<QString>& externalSaveFailure = std::nullopt);
    void saveDocumentForOperation(const QUuid& token, bool saveAs, bool replacing);
    void completeDocumentSave(const QUuid& token, std::optional<std::filesystem::path> path,
        bool adoptPath, bool replacing);
    void completeOpenDocumentPicker(const QUuid& token, std::optional<std::filesystem::path> path);
    void readDocumentOnResourceThread(const QUuid& token, std::filesystem::path path, bool replacementRead);
    void saveDocumentOnResourceThread(const QUuid& token, std::filesystem::path path, bool adoptPath, bool replacing);
    void completeDocumentSaveResult(const QUuid& token, SaveResult result, bool replacing, const QString& error);
    bool consumeDocumentResourceResult(const QUuid& token, const QUuid& incarnation, const QUuid& dispatch);
    void decideDocumentReplacement(const QUuid& token);
    void completeMediaImportPicker(const QUuid& token, QStringList sources);
    void requestPlatformLanguage();
    void schedulePreferenceResourceWork();
    [[nodiscard]] bool preferenceResourceWorkPending() const noexcept;
    [[nodiscard]] bool initialCanvasOutsideModeReady() const noexcept;
    [[nodiscard]] std::optional<UiLanguage> persistedLanguage() const;
    [[nodiscard]] CanvasOutsidePreferenceRead persistedCanvasOutsideMode() const;
    enum class PlatformRequestKind { Open, Save, SaveThenReplace, Media };
    struct PendingPlatformRequest
    {
        SessionPlatformRequest identity;
        PlatformRequestKind kind;
        bool retiring = false;
        bool resultReturned = false;
        SessionPlatformRetirement retirement = SessionPlatformRetirement::AwaitAcknowledgment;
    };
    void startPlatformRequest(const QUuid& operation, PlatformRequestKind kind);
    void dispatchPlatformRequest();
    bool consumePlatformResult(const SessionPlatformRequest& identity);
    void notePlatformRequestRetired(const SessionPlatformRequest& identity);
    void publishReadOnlyInvalidation();
    void publishRuntimePlayback(bool immediate = false);
    void updateDocumentTitle();
    void updateCanvasAspectUi();
    void refreshLibrary();
    void refreshStatus();
    void refreshPlaylistList();
    void refreshItemList();
    void queueInspectorRefresh();
    void synchronizeActivePlaybackVolumeCluster();
    void synchronizeBackgroundMusicPlaybackDisplay();
    void refreshOpenBackgroundMusicWindow(PlaylistId playlist);
    void closeBackgroundMusicWindow(PlaylistId playlist);
    void closeAllBackgroundMusicWindows();
    void cancelTimelineDrag();
    void invalidateTimelineHoverPreview();
    void invalidateTimelinePreviewPreloadTarget();
    void reconcileTimelinePreviewPreloadTarget();
    void cancelRuntimeHover();
    void updateBoundDisplayUi();
    void updateDesktopDeploymentButton();

    Engine& engine_;
    SessionPlatform* platform_ = nullptr;
    ApplicationTranslationState* translationState_;
    ApplicationPreferencesStore* preferencesStore_ = nullptr;
    ApplicationPreferences applicationPreferences_{};
    struct AutonomousPreferences
    {
        ApplicationPreferencesStore::Location location;
        CanvasOutsidePreferenceRead persistedCanvas{std::nullopt, QSettings::AccessError};
        std::optional<UiLanguage> persistedLanguage;
        std::optional<CanvasOutsideMode> canvasToSave;
        std::optional<bool> highlightToSave;
        bool reloadLanguage = true;
        bool initialCanvasRead = true;
        bool initialCanvasReadComplete = false;
        bool explicitCanvasMode = false;
        bool working = false;
    };
    std::optional<AutonomousPreferences> autonomousPreferences_;
    UiLanguage effectivePlatformLanguage_ = UiLanguage::English;
    QUuid platformLanguageRequest_;
    CanvasOutsideMode currentCanvasOutsideMode_ = CanvasOutsideMode::Opaque;
    WorkspaceModel workspaceModel_;
    ProjectDocumentState documentState_{};
    QUuid documentIncarnation_{QUuid::createUuid()};
    PlaybackSequence playbackSequence_{};
    std::optional<ActivePlaybackProjection> activePlaybackProjection_{};
    QString runtimeBindingToken_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    DisplaySelectionState displaySelection_;
    std::array<std::optional<NativeAudioEffect>, 5> nativeAudioEffects_{};
    std::optional<AudioEffectToken> activeAudioCapture_;
    QJsonObject observedAudioEffects_;
    std::optional<PendingDocumentOperation> pendingDocumentOperation_;
    QJsonObject lastDocumentOperation_;
    std::function<void()> deferredDocumentContinuation_;
    std::optional<PendingPlatformRequest> pendingPlatformRequest_;
    class FileResources;
    std::unique_ptr<FileResources> fileResources_;
    QUuid documentResourceDispatch_;
    std::optional<PendingMediaImport> pendingMediaImport_;
    std::optional<AuthoredResult> lastMediaImport_;
    QHash<QUuid, BgmEditPermit> bgmEditPermits_;
    bool authoredOperationExecuting_ = false;
    bool mediaImportStarted_ = false;
    bool documentCommandsPrepared_ = false;
    bool authoredCommandsPrepared_ = false;
    bool runtimeCommandsPrepared_ = false;
    bool documentClosePermitted_ = false;
    bool engineInitialized_ = false;
    bool autonomousRuntime_ = false;
    bool autonomousMediaPreparation_ = false;
    std::optional<AuthoredRebuildPlan>* authoredRebuildCapture_ = nullptr;
    std::optional<PreparedRuntimeIntent> pendingRuntimeIntent_;
    struct RollbackPresentation
    {
        Engine::MediaPreparationToken ticket;
        std::optional<ActivePlaybackProjection> priorProjection;
        PlaybackSequence priorSequence;
        std::optional<ItemId> priorActiveItem;
        PlaylistId priorSelectedPlaylist;
        std::optional<ItemId> priorSelectedItem;
        ItemId committedItem;
        bool changedSelection;
        bool seek;
        std::optional<PresentationUploadResult> result;
    };
    std::optional<RollbackPresentation> rollbackPresentation_;
    bool desktopDeploymentDesired_ = false;
    bool desktopReplacementInProgress_ = false;
    bool detachedDesktopCommandPending_ = false;
    bool desktopEndpointReconciliationQueued_ = false;
    bool detachedDesktopTopologyDirty_ = false;
    QUuid detachedDesktopDispatch_;
    void dispatchDetachedDesktop(bool allowContinuation, DesktopDeploymentCompletion complete = {});
    CloseState closeState_ = CloseState::Open;
    QTimer audioEffectsPollTimer_;
    QTimer tickTimer_;
    QTimer desktopReconciliationContinuationTimer_;
    std::chrono::steady_clock::time_point previousTick_{};
    float statusElapsed_ = 0.0F;
    // Private deterministic native seams; no presentation owns these values.
    std::function<std::optional<std::filesystem::path>(bool)> documentPickerForTesting_;
    std::function<QStringList()> mediaPickerForTesting_;
    std::function<Platform::DisplayEnumerationResult()> displayEnumeratorForTesting_;
    std::function<bool(const BackgroundMusicControl::Event&)> backgroundMusicControlForwarder_;
};
}
