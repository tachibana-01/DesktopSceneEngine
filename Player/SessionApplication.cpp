#include "SessionApplication.h"
#include "BackgroundMusicControlAdapter.h"
#include "EffectiveVolume.h"
#include "TimelineUtils.h"
#include "Infrastructure/Persistence/ProjectFile.h"
#include <QCoreApplication>
#include <QDebug>
#include <QFileInfo>
#include <QJsonArray>
#include <QPointer>
#include <QScopedValueRollback>
#include <QThread>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <iterator>
#include <limits>
#include <random>
#include <stdexcept>
#include <utility>

namespace DesktopSceneEngine::Player
{
namespace
{
constexpr std::size_t MaximumRandomRoundResolutionAttempts = 2U;
Domain::DisplayOwner requireDisplayOwner(const DisplayId& display)
{
    auto owner = Domain::DisplayOwner::fromDisplayId(display);
    if (!owner) throw std::invalid_argument("SessionApplication requires a persistent-owner-capable DisplayId");
    return *owner;
}
std::uint64_t playbackSequenceSeed()
{
    std::random_device entropy;
    return (static_cast<std::uint64_t>(entropy()) << 32U) ^ static_cast<std::uint64_t>(entropy());
}
bool sameBackgroundMusicScope(const BackgroundMusicControl::ActiveScopeSnapshot& left,
    const BackgroundMusicControl::ActiveScopeSnapshot& right) noexcept
{ return left.playlistId == right.playlistId && left.itemId == right.itemId; }
std::optional<WorkspaceMediaType> mediaTypeForSource(const QString& source)
{
    const auto extension = QFileInfo{source}.suffix().toLower();
    if (extension == "png" || extension == "jpg" || extension == "jpeg" || extension == "webp") return WorkspaceMediaType::Image;
    if (extension == "mp4" || extension == "avi" || extension == "mov" || extension == "mkv" || extension == "webm" || extension == "video") return WorkspaceMediaType::Video;
    return std::nullopt;
}
QString documentActionName(DocumentAction action)
{
    switch (action) {
    case DocumentAction::New: return "new";
    case DocumentAction::Open: return "open";
    case DocumentAction::Save: return "save";
    case DocumentAction::SaveAs: return "save-as";
    case DocumentAction::Close: return "close";
    }
    return {};
}

void persistDocument(const Domain::AuthoredProject& project, const std::filesystem::path& path, bool adoptPath)
{
    namespace Persistence = DesktopSceneEngine::Infrastructure::Persistence;
    if (adoptPath) Persistence::validateAuthoredProjectSaveAsDestination(project.displayOwner, path);
    Persistence::saveAuthoredProjectFile(project, path);
}
}

// The document and preference lanes each admit one operation. File work takes
// value inputs and returns values through the control QObject queue.
class SessionApplication::FileResources final
{
public:
    FileResources()
    {
        auto worker = std::make_unique<QObject>();
        QObject::connect(&thread_, &QThread::finished, worker.get(), &QObject::deleteLater);
        thread_.setObjectName(QStringLiteral("Session files"));
        thread_.start();
        if (!thread_.isRunning()) throw std::runtime_error("Unable to start Session file thread");
        worker->moveToThread(&thread_);
        worker_ = worker.release();
    }

    ~FileResources()
    {
        thread_.quit();
        thread_.wait();
    }

    template<class Work, class Complete>
    bool submit(SessionApplication& owner, Work work, Complete complete)
    {
        if (!thread_.isRunning()) return false;
        return QMetaObject::invokeMethod(worker_, [target = &owner, work = std::move(work),
            complete = std::move(complete)]() mutable {
            auto result = work();
            QMetaObject::invokeMethod(target, [complete = std::move(complete), result = std::move(result)]() mutable {
                complete(std::move(result));
            }, Qt::QueuedConnection);
        }, Qt::QueuedConnection);
    }

private:
    QThread thread_;
    QObject* worker_ = nullptr;
};

SessionApplication::SessionApplication(Engine& engine, DisplayId boundDisplay,
    ApplicationTranslationState& translation, ApplicationPreferencesStore* store,
    ApplicationPreferences preferences, QObject* parent)
    : SessionApplication(engine, std::move(boundDisplay), &translation, store, preferences, parent)
{}

SessionApplication::SessionApplication(Engine& engine, DisplayId boundDisplay, ApplicationPreferencesStore* store,
    ApplicationPreferences preferences, QObject* parent)
    : SessionApplication(engine, std::move(boundDisplay), nullptr, store, preferences, parent)
{}

SessionApplication::SessionApplication(Engine& engine, DisplayId boundDisplay,
    ApplicationTranslationState* translation, ApplicationPreferencesStore* store,
    ApplicationPreferences preferences, QObject* parent)
    : QObject(parent), engine_(engine), translationState_(translation),
      preferencesStore_(store), applicationPreferences_(preferences),
      workspaceModel_(requireDisplayOwner(boundDisplay)),
      playbackSequence_(playbackSequenceSeed()), displaySelection_(std::move(boundDisplay)),
      audioEffectsPollTimer_(this), tickTimer_(this), desktopReconciliationContinuationTimer_(this)
{
    if (preferencesStore_)
    {
        if (translationState_)
            currentCanvasOutsideMode_ = preferencesStore_->loadCanvasOutsideMode(
                displaySelection_.boundDisplayId()).value.value_or(CanvasOutsideMode::Opaque);
        else
            autonomousPreferences_ = AutonomousPreferences{preferencesStore_->location()};
    }
    resetWorkspaceForNewDocument();
    documentState_.markClean(workspaceModel_.authoredRevision());
    workspaceModel_.setAuthoredChangedCallback([this] { updateDocumentTitle(); });
    audioEffectsPollTimer_.setInterval(10);
    connect(&audioEffectsPollTimer_, &QTimer::timeout, this, [this] { pollNativeAudioEffects(); });
    connect(&tickTimer_, &QTimer::timeout, this, [this] { onTick(); });
    desktopReconciliationContinuationTimer_.setSingleShot(true);
    desktopReconciliationContinuationTimer_.setInterval(250);
    connect(&desktopReconciliationContinuationTimer_, &QTimer::timeout, this,
        [this] { runDesktopPresentationContinuation(); });
}

SessionApplication::~SessionApplication()
{
    tickTimer_.stop();
    audioEffectsPollTimer_.stop();
    // Join before destroying this QObject, the posted result receiver.
    fileResources_.reset();
    cancelDesktopPresentationReconciliation();
    workspaceModel_.setAuthoredChangedCallback({});
    retireBgmEditAuthority(true);
    // Engine's synchronous containment boundary drains actual audio retirement
    // before any borrowed Project storage owned by this object is released.
    shutdownEngine();
    platform_ = nullptr;
}

void SessionApplication::requireExecutor() const
{
    if (QThread::currentThread() != thread())
        throw std::logic_error("Session application requires its native control executor");
}

QString SessionApplication::tr(const char* text)
{ return QCoreApplication::translate("MainWindow", text); }

void SessionApplication::attachPlatform(SessionPlatform& platform)
{
    requireExecutor();
    if (platform_ && platform_ != &platform)
        throw std::logic_error("Session already has an attached presentation adapter");
    if (closeState_ == CloseState::Accepted) throw std::logic_error("Cannot attach presentation after Session Close acceptance");
    platform_ = &platform;
    updateDocumentTitle();
    if (!translationState_) requestPlatformLanguage();
    if (pendingPlatformRequest_ && pendingPlatformRequest_->identity.dispatch.isNull())
        QTimer::singleShot(0, this, [this] { dispatchPlatformRequest(); });
    if (pendingDocumentOperation_ && pendingDocumentOperation_->stage == DocumentStage::Queued)
        QTimer::singleShot(0, this, [this, token = pendingDocumentOperation_->token] { executeDocumentOperation(token); });
    if (pendingDocumentOperation_ && pendingDocumentOperation_->stage == DocumentStage::Decision
        && pendingDocumentOperation_->externalClose)
        platform.surfaceSessionCloseDecision();
    if (deferredDocumentContinuation_)
    {
        auto continuation = std::move(deferredDocumentContinuation_);
        deferredDocumentContinuation_ = {};
        QTimer::singleShot(0, this, std::move(continuation));
    }
    if (pendingMediaImport_ && !mediaImportStarted_)
        QTimer::singleShot(0, this, [this, token = pendingMediaImport_->token] { executeMediaImport(token); });
    if (closeState_ == CloseState::Ready)
        QTimer::singleShot(0, this, [this] {
            if (platform_ && closeState_ == CloseState::Ready) platform_->requestSessionFinalClose();
        });
}

void SessionApplication::detachPlatform(SessionPlatform& platform) noexcept
{
    if (platform_ != &platform) return;
    std::optional<SessionPlatformRequest> retiring;
    if (pendingPlatformRequest_ && !pendingPlatformRequest_->identity.dispatch.isNull()
        && !pendingPlatformRequest_->retiring)
    {
        pendingPlatformRequest_->retiring = true;
        retiring = pendingPlatformRequest_->identity;
    }
    if (!autonomousRuntime_) tickTimer_.stop();
    cancelDesktopPresentationReconciliation();
    // A presentation may retire without retiring an admitted application
    // operation, canonical document, sequence, or accepted runtime binding.
    if (!autonomousRuntime_) platform.detachSessionPresentation(engine_);
    platform_ = nullptr;
    platformLanguageRequest_ = {};
    if (!retiring) return;
    const auto level = platform.retireSessionPlatformRequest(*retiring,
        [owner = QPointer<SessionApplication>{this}, identity = *retiring] {
            if (owner) owner->notePlatformRequestRetired(identity);
        });
    if (pendingPlatformRequest_ && pendingPlatformRequest_->identity == *retiring)
    {
        pendingPlatformRequest_->retirement = level;
        if (level == SessionPlatformRetirement::AwaitResult && pendingPlatformRequest_->resultReturned)
            notePlatformRequestRetired(*retiring);
    }
}

void SessionApplication::startPlatformRequest(const QUuid& operation, PlatformRequestKind kind)
{
    requireExecutor();
    if (pendingPlatformRequest_)
        throw std::logic_error("Session already has an admitted platform interaction");
    pendingPlatformRequest_ = PendingPlatformRequest{{operation, {}}, kind};
    dispatchPlatformRequest();
}

void SessionApplication::dispatchPlatformRequest()
{
    requireExecutor();
    if (!platform_ || !pendingPlatformRequest_ || !pendingPlatformRequest_->identity.dispatch.isNull()) return;
    pendingPlatformRequest_->identity.dispatch = QUuid::createUuid();
    const auto identity = pendingPlatformRequest_->identity;
    const auto kind = pendingPlatformRequest_->kind;
    const auto owner = QPointer<SessionApplication>{this};
    switch (kind)
    {
    case PlatformRequestKind::Open:
    case PlatformRequestKind::Save:
    case PlatformRequestKind::SaveThenReplace:
        platform_->requestSessionDocumentPath(identity, kind != PlatformRequestKind::Open, documentState_.currentPath,
            [owner, identity, kind](std::optional<std::filesystem::path> path) {
                if (!owner || !owner->consumePlatformResult(identity)) return;
                if (kind == PlatformRequestKind::Open)
                    owner->completeOpenDocumentPicker(identity.operation, std::move(path));
                else
                    owner->completeDocumentSave(identity.operation, std::move(path), true,
                        kind == PlatformRequestKind::SaveThenReplace);
            });
        break;
    case PlatformRequestKind::Media:
        if (!pendingMediaImport_ || pendingMediaImport_->token != identity.operation)
        {
            pendingPlatformRequest_.reset();
            return;
        }
        platform_->requestSessionMediaSources(identity,
            !std::holds_alternative<VisualMediaImportTarget>(pendingMediaImport_->target),
            [owner, identity](QStringList sources) {
                if (owner && owner->consumePlatformResult(identity))
                    owner->completeMediaImportPicker(identity.operation, std::move(sources));
            });
        break;
    }
}

bool SessionApplication::consumePlatformResult(const SessionPlatformRequest& identity)
{
    requireExecutor();
    if (!pendingPlatformRequest_ || pendingPlatformRequest_->identity != identity) return false;
    if (pendingPlatformRequest_->retiring)
    {
        pendingPlatformRequest_->resultReturned = true;
        if (pendingPlatformRequest_->retirement == SessionPlatformRetirement::AwaitResult)
            notePlatformRequestRetired(identity);
        return false;
    }
    pendingPlatformRequest_.reset();
    return true;
}

void SessionApplication::notePlatformRequestRetired(const SessionPlatformRequest& identity)
{
    requireExecutor();
    if (!pendingPlatformRequest_ || pendingPlatformRequest_->identity != identity || !pendingPlatformRequest_->retiring)
        return;
    pendingPlatformRequest_ = PendingPlatformRequest{{identity.operation, {}}, pendingPlatformRequest_->kind};
    if (platform_) QTimer::singleShot(0, this, [this] { dispatchPlatformRequest(); });
}

void SessionApplication::setPresentationCapabilities(bool document, bool authored, bool runtime)
{
    requireExecutor();
    documentCommandsPrepared_ = document;
    authoredCommandsPrepared_ = authored;
    runtimeCommandsPrepared_ = runtime;
}

SessionPlatformFacts SessionApplication::platformFacts() const
{ return platform_ ? platform_->sessionPlatformFacts() : SessionPlatformFacts{}; }

void SessionApplication::view(SessionViewChange change, bool immediate) const
{ if (platform_) platform_->sessionViewChanged(change, immediate); }

void SessionApplication::notify(SessionNotice kind, const QString& title, const QString& message) const
{ if (platform_) platform_->showSessionNotice(kind, title, message); }

void SessionApplication::publishReadOnlyInvalidation() { view(SessionViewChange::ReadSnapshot); }
void SessionApplication::publishRuntimePlayback(bool immediate) { view(SessionViewChange::RuntimePlayback, immediate); }
void SessionApplication::updateDocumentTitle() { view(SessionViewChange::DocumentTitle); }
void SessionApplication::updateCanvasAspectUi() { view(SessionViewChange::CanvasAspect); }
void SessionApplication::refreshLibrary() { view(SessionViewChange::Library); }
void SessionApplication::refreshStatus() { view(SessionViewChange::Status); }
void SessionApplication::refreshPlaylistList() { view(SessionViewChange::PlaylistList); }
void SessionApplication::refreshItemList() { view(SessionViewChange::ItemList); }
void SessionApplication::queueInspectorRefresh() { view(SessionViewChange::Inspector); }
void SessionApplication::synchronizeActivePlaybackVolumeCluster() { view(SessionViewChange::ActivePlaybackVolume); }
void SessionApplication::synchronizeBackgroundMusicPlaybackDisplay() { view(SessionViewChange::BgmPlayback); }
void SessionApplication::refreshOpenBackgroundMusicWindow(PlaylistId playlist)
{ if (platform_) platform_->refreshSessionBgmWindow(playlist); }
void SessionApplication::closeBackgroundMusicWindow(PlaylistId playlist)
{ if (platform_) platform_->closeSessionBgmWindow(playlist); }
void SessionApplication::closeAllBackgroundMusicWindows()
{ if (platform_) platform_->closeAllSessionBgmWindows(); }
void SessionApplication::cancelTimelineDrag() { view(SessionViewChange::CancelTimelineDrag); }
void SessionApplication::invalidateTimelineHoverPreview() { view(SessionViewChange::InvalidateTimeline); }
void SessionApplication::invalidateTimelinePreviewPreloadTarget() { view(SessionViewChange::InvalidateTimelinePreload); }
void SessionApplication::reconcileTimelinePreviewPreloadTarget() { view(SessionViewChange::ReconcileTimelinePreload); }
void SessionApplication::cancelRuntimeHover() { view(SessionViewChange::CancelRuntimeHover); }
void SessionApplication::updateBoundDisplayUi() { view(SessionViewChange::BoundDisplay); }
void SessionApplication::updateDesktopDeploymentButton() { view(SessionViewChange::DesktopDeployment); }

bool SessionApplication::initializeEngine()
{
    requireExecutor();
    if (closing() || !platform_) return false;
    if (!platform_->initializeSessionPresentation(engine_) || !engine_.isInitialized()) return false;
    // Presentation reattachment must not clear an already-pending native EOF.
    // Only the first runtime initialization establishes the completion policy.
    if (!engineInitialized_)
        engine_.setSceneCompletionPolicy(Engine::SceneCompletionPolicy::ExternalSceneCompletion);
    engineInitialized_ = true;
    updateEffectiveOutputVolume();
    updateBoundDisplayUi();
    previousTick_ = std::chrono::steady_clock::now();
    tickTimer_.start(16);
    if (desktopDeploymentDesired_) requestDesktopPresentationReconciliation();
    refreshStatus();
    return true;
}

bool SessionApplication::initializeAutonomousRuntime(void* controlWakeEvent)
{
    requireExecutor();
    if (closing() || engineInitialized_ || translationState_) return false;
    if (!engine_.setControlWakeEvent(controlWakeEvent) || !engine_.initializeRuntimeOnly()) return false;
    engine_.setSceneCompletionPolicy(Engine::SceneCompletionPolicy::ExternalSceneCompletion);
    autonomousRuntime_ = true;
    engineInitialized_ = true;
    updateEffectiveOutputVolume();
    previousTick_ = std::chrono::steady_clock::now();
    tickTimer_.setTimerType(Qt::PreciseTimer);
    tickTimer_.start(16);
    schedulePreferenceResourceWork();
    refreshStatus();
    return true;
}

void SessionApplication::observePresentationFacts(SessionPlatformFacts facts)
{
    requireExecutor();
    if (!autonomousRuntime_ || !platform_ || platform_->sessionPlatformFacts() == facts) return;
    platform_->observePresentationFacts(facts);
    publishReadOnlyInvalidation();
}

bool SessionApplication::enableAutonomousMediaPreparation()
{
    requireExecutor();
    if (!autonomousRuntime_ || !engineInitialized_ || activePlaybackProjection_) return false;
    autonomousMediaPreparation_ = engine_.enableAsyncMediaPreparation();
    return autonomousMediaPreparation_;
}

void SessionApplication::publishDetachedFrame()
{
    requireExecutor();
    if (autonomousRuntime_ && platform_ && engineInitialized_ && !closing() && initialCanvasOutsideModeReady())
        platform_->publishDetachedFrame(
            engine_.presentationInput(workspaceModel_.desktopCanvas().aspectRatio),
            runtimeBindingToken_, currentCanvasOutsideMode_, engine_.pendingMediaPresentation());
}

void SessionApplication::shutdownEngine() noexcept
{
    tickTimer_.stop();
    cancelDesktopPresentationReconciliation();
    engineInitialized_ = false;
    cancelPreparedRuntimeIntent();
    rollbackPresentation_.reset();
    desktopDeploymentDesired_ = false;
    detachedDesktopCommandPending_ = false;
    if (platform_ && !autonomousRuntime_) platform_->prepareSessionEngineShutdown(engine_);
    cancelRuntimeHover();
    invalidateTimelineHoverPreview();
    invalidateTimelinePreviewPreloadTarget();
    // Retire the application playback scope and binding before final runtime
    // teardown. Engine's visual detach alone does not send PlaybackStopped to
    // its separately-owned background-music policy/runtime.
    if (activePlaybackProjection_) stopActivePlayback();
    else engine_.detachProject();
    // Begin/drain the separate CP1 owner before freeing any accepted binding.
    // Normal Close reaches here only after its asynchronous barrier completed.
    engine_.shutdown();
    activeAudioCapture_.reset();
    activePlaybackProjection_.reset();
    playbackSequence_.clear();
    workspaceModel_.clearActivePlayback();
    if (platform_ && !autonomousRuntime_) platform_->finishSessionEngineShutdown();
    updateDesktopDeploymentButton();
}

void SessionApplication::onTick()
{
    requireExecutor();
    pollNativeAudioEffects();
    // The Host checks its delivery deadline before reading the clock. Ordinary
    // discontinuity/BGM reconciliation remains at status/explicit hooks.
    view(SessionViewChange::RuntimeSample);
    if (!engineInitialized_ || closing()) return;
    const bool inlineRendering = !autonomousRuntime_ && platform_ && !desktopReplacementInProgress_
        && platform_->sessionRuntimePresentationReady();
    if (!autonomousRuntime_ && !inlineRendering) return;
    const auto now = std::chrono::steady_clock::now();
    const float delta = (std::min)(std::chrono::duration<float>(now - previousTick_).count(), 0.1F);
    previousTick_ = now;
    engine_.update();
    if (autonomousMediaPreparation_)
    {
        servicePresentationResult();
        servicePreparedRuntimeIntent();
        if (!pendingRuntimeIntent_ && engine_.naturalSceneCompletionPending())
            startPreparedSequenceAdvance(false, true, {});
    }
    else if (engine_.naturalSceneCompletionPending())
        if (const auto effect = beginNativeAudioEffect(AudioEffectScope::Structural))
        {
            const bool consumed = engine_.consumeNaturalSceneCompletion();
            if (consumed) (void)advancePlaybackSequence();
            (void)finishNativeAudioEffect(*effect, consumed);
        }
    synchronizeActivePlaybackItem();
    synchronizeBackgroundMusicPlaybackDisplay();
    if (inlineRendering)
        platform_->renderSession(engine_, workspaceModel_.desktopCanvas().aspectRatio, currentCanvasOutsideMode_);
    else
        publishDetachedFrame();
    statusElapsed_ += delta;
    if (statusElapsed_ >= 0.1F) { statusElapsed_ = 0.0F; refreshStatus(); }
}

std::optional<SessionApplication::AudioEffectToken> SessionApplication::beginNativeAudioEffect(
    AudioEffectScope scope, PlaylistId playlist, std::optional<ItemId> item)
{
    requireExecutor();
    if (activeAudioCapture_ || closing()) return std::nullopt;
    engine_.pollAudioExecution();
    const auto token = engine_.beginAudioEffect(scope);
    if (!token) return std::nullopt;
    if (scope == AudioEffectScope::Structural) view(SessionViewChange::DiscardAudioDrafts);
    nativeAudioEffects_[static_cast<std::size_t>(scope)] = NativeAudioEffect{
        QUuid::createUuid(), documentIncarnation_, runtimeBindingToken_, *token, scope, playlist, item};
    activeAudioCapture_ = token;
    if (!audioEffectsPollTimer_.isActive()) audioEffectsPollTimer_.start();
    return token;
}

void SessionApplication::pollNativeAudioEffects()
{
    requireExecutor();
    engine_.pollAudioExecution();
    const auto native = engine_.audioExecutionState();
    if (!activeAudioCapture_ && !documentIncarnation_.isNull() && !closing())
        for (std::size_t i = 0; i < native.effects.size(); ++i)
        {
            const auto& effect = native.effects[i];
            if (!effect.token || (nativeAudioEffects_[i] && nativeAudioEffects_[i]->token == effect.token)) continue;
            nativeAudioEffects_[i] = NativeAudioEffect{QUuid::createUuid(), documentIncarnation_, runtimeBindingToken_,
                effect.token, effect.scope, {}, std::nullopt};
        }
    const auto state = audioEffectsSnapshotAfterDrain();
    if (state != observedAudioEffects_)
    {
        observedAudioEffects_ = state;
        view(SessionViewChange::AudioEffects);
        publishReadOnlyInvalidation();
    }
    if (closeState_ == CloseState::Retiring && engine_.audioShutdownComplete()
        && (!autonomousMediaPreparation_ || engine_.mediaShutdownComplete())
        && !preferenceResourceWorkPending())
    {
        shutdownEngine();
        closeState_ = CloseState::Ready;
        if (platform_) platform_->requestSessionFinalClose();
    }
}

void SessionApplication::retireBgmEditAuthority(bool closingDocument)
{
    view(SessionViewChange::DiscardSessionDrafts);
    documentIncarnation_ = closingDocument ? QUuid{} : QUuid::createUuid();
    bgmEditPermits_.clear();
}

void SessionApplication::requestNewProject()
{ requireExecutor(); if (!busy() && !closing()) applyNewProjectDecision(maybeSaveCurrentDocument()); }

void SessionApplication::requestOpenProject()
{
    requireExecutor();
    if (busy() || closing()) return;
    if (const auto path = pickDocumentPath(false)) (void)openProjectFromPath(*path);
}

SessionApplication::UnsavedChangesChoice SessionApplication::promptForUnsavedChanges()
{
    requireExecutor();
    return platform_ ? platform_->promptSessionUnsavedChanges(currentDocumentDisplayName()) : UnsavedChangesChoice::Cancel;
}

std::optional<std::filesystem::path> SessionApplication::pickDocumentPath(bool save)
{
    requireExecutor();
    if (documentPickerForTesting_) return documentPickerForTesting_(save);
    return platform_ ? platform_->pickSessionDocumentPath(save, documentState_.currentPath) : std::nullopt;
}

SessionApplication::SaveResult SessionApplication::saveCurrentProjectAs()
{
    const auto path = pickDocumentPath(true);
    return path ? saveProjectToPath(*path, true) : SaveResult::Cancelled;
}

void SessionApplication::executeMediaImport(const QUuid& token)
{
    requireExecutor();
    if (!pendingMediaImport_ || pendingMediaImport_->token != token) return;
    if (mediaImportStarted_ || (!platform_ && !mediaPickerForTesting_)) return;
    mediaImportStarted_ = true;
    if (mediaPickerForTesting_)
    {
        completeMediaImportPicker(token, mediaPickerForTesting_());
        return;
    }
    startPlatformRequest(token, PlatformRequestKind::Media);
}

void SessionApplication::completeMediaImportPicker(const QUuid& token, QStringList sources)
{
    requireExecutor();
    if (!mediaImportStarted_ || !pendingMediaImport_ || pendingMediaImport_->token != token) return;
    const auto admitted = *pendingMediaImport_;
    const auto* visual = std::get_if<VisualMediaImportTarget>(&admitted.target);
    std::optional<BgmTarget> bgm;
    if (const auto* playlist = std::get_if<PlaylistBgmTarget>(&admitted.target)) bgm = *playlist;
    if (const auto* item = std::get_if<ItemBgmTarget>(&admitted.target)) bgm = *item;
    AuthoredResult result;
    if (admitted.documentIncarnation != documentIncarnation_ || documentIncarnation_.isNull()
        || !engineInitialized_ || closing() || documentClosePermitted_)
    {
        result.status = AuthoredStatus::WrongDocument;
        result.playlistId = std::visit([](const auto& target) { return target.playlistId; }, admitted.target);
        result.authoredRevision = workspaceModel_.authoredRevision();
        result.dirty = documentState_.isDirty(result.authoredRevision);
        if (bgm) { result.bgmTarget = bgm; result.itemId = bgmItemId(*bgm); result.bgmImport = BgmImportOutcome::RejectedAtCompletion; }
    }
    else if (bgm) result = addBgmSources(*bgm, sources);
    else
    {
        runAuthoredWithDeferredRebuild([this, visual, &sources] {
            return addMediaSources(visual->playlistId, sources);
        }, [this, token](AuthoredResult completed) { finishMediaImportResult(token, std::move(completed)); });
        return;
    }
    finishMediaImportResult(token, std::move(result));
}

void SessionApplication::finishMediaImportResult(const QUuid& token, AuthoredResult result)
{
    if (!pendingMediaImport_ || pendingMediaImport_->token != token) return;
    mediaImportStarted_ = false;
    result.token = token;
    lastMediaImport_ = std::move(result);
    pendingMediaImport_.reset();
    publishReadOnlyInvalidation();
    if (platform_) platform_->completeSessionMediaImport(token);
}

AuthoredResult SessionApplication::beginNativeMediaImport(PlaylistId playlist,
    std::optional<ItemId> item, bool backgroundMusic)
{
    requireExecutor();
    AuthoredResult result;
    result.playlistId = playlist;
    result.itemId = item;
    result.authoredRevision = workspaceModel_.authoredRevision();
    result.dirty = documentState_.isDirty(result.authoredRevision);
    if (closing() || closePending()) return result;
    if (pendingDocumentOperation_ || pendingMediaImport_ || authoredOperationExecuting_ || pendingRuntimeIntent_)
    { result.status = AuthoredStatus::Busy; return result; }
    if (!workspaceModel_.playlist(playlist)) { result.status = AuthoredStatus::NotFound; return result; }
    MediaImportTarget target{VisualMediaImportTarget{playlist}};
    if (backgroundMusic)
    {
        const BgmTarget bgm = item ? BgmTarget{ItemBgmTarget{playlist, *item}} : BgmTarget{PlaylistBgmTarget{playlist}};
        result.bgmTarget = bgm;
        result.status = validateBgmTarget(bgm);
        if (result.status != AuthoredStatus::Accepted) return result;
        target = std::visit([](const auto& value) -> MediaImportTarget { return value; }, bgm);
    }
    result.token = QUuid::createUuid();
    result.status = AuthoredStatus::Accepted;
    result.authored = AuthoredOutcome::NotApplicable;
    pendingMediaImport_ = PendingMediaImport{result.token, documentIncarnation_, std::move(target)};
    lastMediaImport_.reset();
    view(SessionViewChange::DiscardAudioDrafts);
    publishReadOnlyInvalidation();
    QTimer::singleShot(0, this, [this, token = result.token] { executeMediaImport(token); });
    return result;
}

void SessionApplication::abortClose(const std::optional<QString>& externalSaveFailure)
{
    if (closing()) return;
    closeState_ = CloseState::Open;
    documentClosePermitted_ = false;
    if (platform_)
    {
        if (externalSaveFailure) platform_->sessionExternalCloseSaveFailed(*externalSaveFailure);
        else platform_->sessionCloseCompleted(false);
    }
}

void SessionApplication::requestClose(bool external)
{
    requireExecutor();
    if (closing()) return;
    view(SessionViewChange::DiscardDocumentDrafts);
    if (pendingMediaImport_ || authoredOperationExecuting_) { abortClose(); return; }
    if (documentCommandsPrepared_ && !documentClosePermitted_)
    {
        if (external) { closeExternalDocumentOperation(); return; }
        (void)beginDocumentOperation(DocumentAction::Close);
        return;
    }
    closeState_ = CloseState::Deciding;
    if (!documentClosePermitted_ && !platform_ && documentState_.isDirty(workspaceModel_.authoredRevision()))
    {
        const auto token = QUuid::createUuid();
        pendingDocumentOperation_ = PendingDocumentOperation{token, DocumentAction::Close, DocumentStage::Decision};
        lastDocumentOperation_ = {};
        publishReadOnlyInvalidation();
        return;
    }
    beginCloseRetirement(documentClosePermitted_ ? DestructiveDecision::ProceedAfterDiscard : maybeSaveCurrentDocument());
}

void SessionApplication::closeExternalDocumentOperation()
{
    // Repeated system Close cannot settle an already admitted tray operation.
    if (pendingDocumentOperation_ && pendingDocumentOperation_->externalClose) return;
    if (pendingMediaImport_ || authoredOperationExecuting_ || pendingDocumentOperation_)
    { if (platform_) platform_->sessionCloseCompleted(false); return; }
    closeState_ = CloseState::Deciding;
    const auto token = QUuid::createUuid();
    pendingDocumentOperation_ = PendingDocumentOperation{token, DocumentAction::Close, DocumentStage::Executing};
    pendingDocumentOperation_->externalClose = true;
    lastDocumentOperation_ = {};
    publishReadOnlyInvalidation();
    if (documentState_.isDirty(workspaceModel_.authoredRevision()))
    {
        pendingDocumentOperation_->stage = DocumentStage::Decision;
        publishReadOnlyInvalidation();
        if (platform_) platform_->surfaceSessionCloseDecision();
        return;
    }
    documentClosePermitted_ = true;
    beginCloseRetirement(DestructiveDecision::ProceedAfterDiscard);
}

void SessionApplication::closeAfterDecision(DestructiveDecision decision)
{ requireExecutor(); beginCloseRetirement(decision); }

void SessionApplication::beginCloseRetirement(DestructiveDecision decision)
{
    if (decision == DestructiveDecision::Abort) { abortClose(); return; }
    if (closing()) return;
    closeState_ = CloseState::Retiring;
    tickTimer_.stop();
    cancelPreparedRuntimeIntent();
    retireBgmEditAuthority(true);
    if (platform_) platform_->beginSessionPresentationTeardown();
    closeAllBackgroundMusicWindows();
    engine_.beginAudioShutdown();
    if (autonomousMediaPreparation_) engine_.beginMediaShutdown();
    audioEffectsPollTimer_.start();
}

void SessionApplication::completePlatformClose(bool accepted)
{
    requireExecutor();
    if (closeState_ != CloseState::Ready) return;
    if (!accepted) { if (platform_) platform_->sessionCloseCompleted(false); return; }
    closeState_ = CloseState::Accepted;
    if (pendingDocumentOperation_ && pendingDocumentOperation_->action == DocumentAction::Close)
        finishDocumentOperation(pendingDocumentOperation_->token, "completed");
    audioEffectsPollTimer_.stop();
    if (platform_) platform_->sessionCloseCompleted(true);
}

bool SessionApplication::selectPlaylist(PlaylistId id)
{
    AuthoredRequest request; request.action = AuthoredAction::SelectPlaylist; request.playlistId = id;
    const auto result = executeAuthoredOperation(request);
    return result.status == AuthoredStatus::Applied || result.status == AuthoredStatus::NoChange;
}

bool SessionApplication::selectItem(ItemId id)
{
    for (const auto& playlist : workspaceModel_.playlists())
        if (std::any_of(playlist.items.cbegin(), playlist.items.cend(), [id](const auto& item) { return item.id == id; }))
        {
            AuthoredRequest request; request.action = AuthoredAction::SelectItem; request.playlistId = playlist.id; request.itemId = id;
            const auto result = executeAuthoredOperation(request);
            return result.status == AuthoredStatus::Applied || result.status == AuthoredStatus::NoChange;
        }
    return false;
}

std::optional<SessionApplication::AudioEffectToken> SessionApplication::toggleOutputMute()
{
    requireExecutor();
    if (!engineInitialized_ || closing()) return std::nullopt;
    const auto effect = beginNativeAudioEffect(AudioEffectScope::OutputMute);
    if (!effect) return std::nullopt;
    engine_.toggleMute();
    (void)finishNativeAudioEffect(*effect);
    refreshStatus();
    return effect;
}

bool SessionApplication::seekPlayback(float seconds)
{
    requireExecutor();
    if (!engineInitialized_ || busy() || closing()) return false;
    const auto effect = beginNativeAudioEffect(AudioEffectScope::Structural,
        activePlaybackProjection_ ? activePlaybackProjection_->sourcePlaylistId : PlaylistId{}, activeTimelineItemId());
    if (!effect) return false;
    const bool accepted = engine_.seek(seconds);
    (void)finishNativeAudioEffect(*effect, accepted);
    refreshStatus();
    publishRuntimePlayback(true);
    return accepted;
}

void SessionApplication::refreshDisplaySelection()
{
    applyDisplayEnumerationResult(displayEnumeratorForTesting_
        ? displayEnumeratorForTesting_() : Platform::enumerateActiveDisplays());
}

DesktopDeploymentProjection SessionApplication::desktopDeploymentProjection() const
{
    const bool retained = desktopDeploymentDesired_ || platformFacts().desktopRetained;
    const bool ready = engineInitialized_ && !desktopReplacementInProgress_ && !closePending() && !closing();
    const bool canCreateTarget = initialCanvasOutsideModeReady() && displaySelection_.boundDisplay().has_value();
    return {retained, ready && (retained || canCreateTarget), ready,
        detachedDesktopCommandPending_};
}

DesktopDeploymentResult SessionApplication::requestDesktopDeployment(DesktopDeploymentIntent intent)
{
    requireExecutor();
    const auto projection = desktopDeploymentProjection();
    if (!engineInitialized_ || !platform_ || desktopReplacementInProgress_ || closePending() || closing())
        return DesktopDeploymentResult::Rejected;
    if ((intent == DesktopDeploymentIntent::Deploy) == projection.retained) return DesktopDeploymentResult::AlreadySatisfied;
    bool completed = false;
    if (intent == DesktopDeploymentIntent::Undeploy)
    {
        desktopDeploymentDesired_ = false;
        cancelDesktopPresentationReconciliation();
        QScopedValueRollback<bool> removing(desktopReplacementInProgress_, true);
        completed = platform_->removeSessionDesktop(engine_);
    }
    else
    {
        refreshDisplaySelection();
        if (!displaySelection_.boundDisplay()) return DesktopDeploymentResult::Rejected;
        desktopDeploymentDesired_ = true;
        completed = reconcileDesktopPresentation(false);
        if (!completed) desktopDeploymentDesired_ = false;
    }
    updateDesktopDeploymentButton();
    return completed ? DesktopDeploymentResult::Completed : DesktopDeploymentResult::Failure;
}

bool SessionApplication::reconcileDesktopPresentation(bool allowContinuation)
{
    if (!engineInitialized_ || !platform_ || desktopReplacementInProgress_) return false;
    bool deployed = false, cleanupSucceeded = true;
    auto attempt = SessionDesktopAttempt::Failed;
    {
        QScopedValueRollback<bool> replacing(desktopReplacementInProgress_, true);
        refreshDisplaySelection();
        auto facts = platformFacts();
        if (!desktopDeploymentDesired_) cleanupSucceeded = platform_->removeSessionDesktop(engine_);
        else if (facts.desktopTargetRegistered && facts.desktopHealthy)
        {
            deployed = platform_->reconcileSessionDesktop(engine_, displaySelection_.boundDisplayId());
            if (deployed) attempt = SessionDesktopAttempt::Ready;
            else cleanupSucceeded = platform_->removeSessionDesktop(engine_);
        }
        else if (facts.desktopRetained || facts.desktopHostOwned)
            cleanupSucceeded = platform_->removeSessionDesktop(engine_);
        if (desktopDeploymentDesired_ && cleanupSucceeded && !deployed)
        {
            attempt = platform_->deploySessionDesktop(engine_, displaySelection_.boundDisplayId());
            deployed = attempt == SessionDesktopAttempt::Ready;
        }
    }
    if (!deployed && attempt == SessionDesktopAttempt::TransientlyUnavailable && allowContinuation
        && desktopDeploymentDesired_ && !desktopEndpointReconciliationQueued_)
        desktopReconciliationContinuationTimer_.start();
    updateDesktopDeploymentButton();
    return desktopDeploymentDesired_ ? deployed : cleanupSucceeded;
}

SessionApplication::ViewPreferenceResult SessionApplication::selectCanvasOutsideMode(CanvasOutsideMode mode)
{
    requireExecutor();
    if (closePending() || closing()
        || (mode != CanvasOutsideMode::Opaque && mode != CanvasOutsideMode::Transparent))
        return ViewPreferenceResult::Unavailable;
    if (mode == currentCanvasOutsideMode_ && (!autonomousPreferences_
        || autonomousPreferences_->initialCanvasReadComplete || autonomousPreferences_->explicitCanvasMode))
        return ViewPreferenceResult::AlreadySatisfied;
    currentCanvasOutsideMode_ = mode;
    if (autonomousPreferences_)
    {
        autonomousPreferences_->explicitCanvasMode = true;
        autonomousPreferences_->canvasToSave = mode;
        schedulePreferenceResourceWork();
    }
    else if (preferencesStore_ && preferencesStore_->saveCanvasOutsideMode(displaySelection_.boundDisplayId(), mode) != QSettings::NoError)
        notify(SessionNotice::Warning, tr("Desktop Background"),
            tr("The desktop background setting could not be saved. This session keeps the selected mode."));
    publishReadOnlyInvalidation();
    return ViewPreferenceResult::Applied;
}

SessionApplication::ViewPreferenceResult SessionApplication::applyHighlightCanvasBoundary(bool highlighted)
{
    requireExecutor();
    if (closePending() || closing()) return ViewPreferenceResult::Unavailable;
    if (applicationPreferences_.highlightCanvasBoundary == highlighted)
        return ViewPreferenceResult::AlreadySatisfied;
    applicationPreferences_.highlightCanvasBoundary = highlighted;
    if (autonomousPreferences_)
    {
        autonomousPreferences_->highlightToSave = highlighted;
        schedulePreferenceResourceWork();
    }
    else if (preferencesStore_) preferencesStore_->saveHighlightCanvasBoundary(highlighted);
    publishReadOnlyInvalidation();
    return ViewPreferenceResult::Applied;
}

void SessionApplication::applyCommittedLanguage(UiLanguage language)
{
    requireExecutor();
    if (applicationPreferences_.language == language
        && (effectiveLanguage() == language || !platformLanguageRequest_.isNull())) return;
    applicationPreferences_.language = language;
    if (translationState_) (void)translationState_->setLanguage(language);
    else requestPlatformLanguage();
    refreshLibrary();
    publishReadOnlyInvalidation();
}

UiLanguage SessionApplication::effectiveLanguage() const noexcept
{
    return translationState_ ? translationState_->activeLanguage() : effectivePlatformLanguage_;
}

void SessionApplication::requestPlatformLanguage()
{
    if (!platform_ || translationState_ || closing()) return;
    const auto dispatch = platformLanguageRequest_ = QUuid::createUuid();
    platform_->applySessionLanguage(applicationPreferences_.language,
        [owner = QPointer<SessionApplication>{this}, dispatch](std::optional<UiLanguage> applied) {
            if (!owner) return;
            owner->requireExecutor();
            if (owner->platformLanguageRequest_ != dispatch) return;
            owner->platformLanguageRequest_ = {};
            if (!applied || owner->closing() || owner->effectivePlatformLanguage_ == *applied) return;
            owner->effectivePlatformLanguage_ = *applied;
            owner->updateDocumentTitle();
            owner->refreshLibrary();
            owner->publishReadOnlyInvalidation();
        });
}

void SessionApplication::reloadCommittedLanguage()
{
    requireExecutor();
    if (autonomousPreferences_)
    {
        if (closing()) return;
        autonomousPreferences_->reloadLanguage = true;
        schedulePreferenceResourceWork();
    }
    else if (preferencesStore_)
        if (const auto language = preferencesStore_->reloadLanguage()) applyCommittedLanguage(*language);
}

std::optional<UiLanguage> SessionApplication::persistedLanguage() const
{
    if (autonomousPreferences_) return autonomousPreferences_->persistedLanguage;
    return preferencesStore_ ? preferencesStore_->reloadLanguage() : std::nullopt;
}

CanvasOutsidePreferenceRead SessionApplication::persistedCanvasOutsideMode() const
{
    if (autonomousPreferences_) return autonomousPreferences_->persistedCanvas;
    return preferencesStore_ ? preferencesStore_->loadCanvasOutsideMode(displaySelection_.boundDisplayId())
        : CanvasOutsidePreferenceRead{std::nullopt, QSettings::AccessError};
}

bool SessionApplication::preferenceResourceWorkPending() const noexcept
{
    return autonomousPreferences_ && (autonomousPreferences_->working || autonomousPreferences_->reloadLanguage
        || autonomousPreferences_->initialCanvasRead || autonomousPreferences_->canvasToSave
        || autonomousPreferences_->highlightToSave);
}

bool SessionApplication::initialCanvasOutsideModeReady() const noexcept
{
    return !autonomousPreferences_ || autonomousPreferences_->initialCanvasReadComplete;
}

void SessionApplication::schedulePreferenceResourceWork()
{
    requireExecutor();
    if (!autonomousRuntime_ || !preferenceResourceWorkPending() || autonomousPreferences_->working) return;
    auto& state = *autonomousPreferences_;
    const bool initial = std::exchange(state.initialCanvasRead, false);
    const bool readLanguage = std::exchange(state.reloadLanguage, false);
    const auto canvas = std::exchange(state.canvasToSave, std::nullopt);
    const auto highlight = std::exchange(state.highlightToSave, std::nullopt);
    const bool readCanvas = initial || readLanguage || canvas.has_value();
    struct PreferenceResult
    {
        std::optional<UiLanguage> language;
        CanvasOutsidePreferenceRead canvas{std::nullopt, QSettings::AccessError};
        QSettings::Status canvasWrite = QSettings::AccessError;
    };
    state.working = true;
    const auto complete = [this, initial, readLanguage, readCanvas, canvas](PreferenceResult result) {
        auto& preferences = *autonomousPreferences_;
        preferences.working = false;
        if (readLanguage)
        {
            preferences.persistedLanguage = result.language;
            if (result.language && !closing() && !preferences.reloadLanguage)
                applyCommittedLanguage(*result.language);
        }
        if (readCanvas) preferences.persistedCanvas = result.canvas;
        if (initial)
        {
            preferences.initialCanvasReadComplete = true;
            if (!preferences.explicitCanvasMode && result.canvas.value)
                currentCanvasOutsideMode_ = *result.canvas.value;
        }
        if (canvas && currentCanvasOutsideMode_ == *canvas && !preferences.canvasToSave
            && result.canvasWrite != QSettings::NoError && !closing())
            notify(SessionNotice::Warning, tr("Desktop Background"),
                tr("The desktop background setting could not be saved. This session keeps the selected mode."));
        publishReadOnlyInvalidation();
        schedulePreferenceResourceWork();
    };
    try
    {
        if (!fileResources_) fileResources_ = std::make_unique<FileResources>();
        if (fileResources_->submit(*this, [location = state.location, display = displaySelection_.boundDisplayId(),
            readCanvas, readLanguage, canvas, highlight] {
            PreferenceResult result;
            try
            {
                auto settings = location.open();
                ApplicationPreferencesStore store{*settings};
                if (highlight) store.saveHighlightCanvasBoundary(*highlight);
                if (canvas) result.canvasWrite = store.saveCanvasOutsideMode(display, *canvas);
                if (readCanvas) result.canvas = store.loadCanvasOutsideMode(display);
                if (readLanguage) result.language = store.reloadLanguage();
            }
            catch (const std::exception& error) { qWarning("Session preference file operation failed: %s", error.what()); }
            return result;
        }, complete)) return;
    }
    catch (const std::exception& error) { qWarning("Unable to schedule Session preference file work: %s", error.what()); }
    complete({});
}
QJsonObject SessionApplication::nativeAudioEffect(const NativeAudioEffect& operation) const
{
    const auto result = engine_.audioEffect(operation.token);
    static constexpr const char* scopes[]{"structural", "embedded-gain", "playlist-bgm-gain", "item-bgm-gain", "output-mute"};
    static constexpr const char* phases[]{"rejected", "preparing", "pending", "applied", "deferred-unavailable", "failed", "cancelled", "expired"};
    const auto id = [](const QUuid& value) -> QJsonValue {
        return value.isNull() ? QJsonValue{QJsonValue::Null} : QJsonValue{value.toString(QUuid::WithoutBraces)};
    };
    return {{"operation", id(operation.operation)}, {"documentIncarnation", id(operation.document)},
        {"bindingToken", operation.binding}, {"execution", QString::number(operation.token.execution)},
        {"sequence", QString::number(operation.token.sequence)}, {"scope", scopes[static_cast<std::size_t>(operation.scope)]},
        {"phase", phases[static_cast<std::size_t>(result.phase)]}, {"error", QString::fromStdString(result.error)},
        {"effectTimeNs", QString::number(result.effectTimeNs)}, {"playlistId", id(operation.playlist.value)},
        {"itemId", operation.item ? id(operation.item->value) : QJsonValue{QJsonValue::Null}}};
}

QJsonObject SessionApplication::finishNativeAudioEffect(AudioEffectToken token, bool accepted)
{
    const auto result = engine_.finishAudioEffect(token, accepted);
    if (activeAudioCapture_ && *activeAudioCapture_ == token) activeAudioCapture_.reset();
    auto& operation = nativeAudioEffects_[static_cast<std::size_t>(result.scope)];
    if (!operation || operation->token != token) return {};
    operation->binding = runtimeBindingToken_;
    return nativeAudioEffect(*operation);
}

QJsonObject SessionApplication::audioEffectsSnapshot() const
{
    if (QThread::currentThread() != thread()) throw std::logic_error("Audio effect snapshots require the native control thread");
    const_cast<DesktopSceneEngine::Engine&>(engine_).pollAudioExecution();
    return audioEffectsSnapshotAfterDrain();
}

QJsonObject SessionApplication::audioEffectsSnapshotAfterDrain() const
{
    const auto state = engine_.audioExecutionState();
    QJsonArray effects;
    for (const auto& operation : nativeAudioEffects_) if (operation) effects.append(nativeAudioEffect(*operation));
    static constexpr const char* health[]{"healthy", "device-unavailable", "contract-faulted"};
    return {{"version", 1}, {"documentIncarnation", documentIncarnation_.toString(QUuid::WithoutBraces)},
        {"execution", QString::number(state.incarnation)}, {"capacityRevision", QString::number(state.capacityRevision)},
        {"closing", (closeState_ == CloseState::Retiring) || state.closing},
        {"stopped", state.stopped}, {"desiredMuted", state.desiredMuted}, {"appliedMuted", state.appliedMuted},
        {"muteKnown", state.muteKnown}, {"deviceHealth", health[static_cast<std::size_t>(state.deviceHealth)]},
        {"effects", effects}};
}

QString SessionApplication::currentDocumentDisplayName() const
{
    if (documentState_.currentPath.has_value())
    {
        const std::wstring filename = documentState_.currentPath->filename().wstring();
        if (!filename.empty())
        {
            return QString::fromStdWString(filename);
        }
    }

    return tr("Untitled");
}

SessionApplication::DestructiveDecision SessionApplication::resolveUnsavedChanges(
    UnsavedChangesChoice choice)
{
    switch (choice)
    {
    case UnsavedChangesChoice::Save:
        return decisionForSaveResult(saveCurrentProject());
    case UnsavedChangesChoice::Discard:
        return DestructiveDecision::ProceedAfterDiscard;
    default:
        return DestructiveDecision::Abort;
    }
}

SessionApplication::DestructiveDecision SessionApplication::maybeSaveCurrentDocument()
{
    if (!documentState_.isDirty(workspaceModel_.authoredRevision()))
    {
        return DestructiveDecision::ProceedAfterDiscard;
    }

    return resolveUnsavedChanges(promptForUnsavedChanges());
}

SessionApplication::DestructiveDecision SessionApplication::decisionForSaveResult(
    SaveResult result) noexcept
{
    return result == SaveResult::Saved
        ? DestructiveDecision::ProceedAfterSave
        : DestructiveDecision::Abort;
}

void SessionApplication::applyNewProjectDecision(DestructiveDecision decision)
{
    if (decision != DestructiveDecision::Abort)
    {
        createNewDocument();
    }
}

void SessionApplication::createNewDocument()
{
    retireBgmEditAuthority();
    stopActivePlayback();
    closeAllBackgroundMusicWindows();
    resetWorkspaceForNewDocument();
    documentState_.currentPath.reset();
    documentState_.markClean(workspaceModel_.authoredRevision());

    updateCanvasAspectUi();

    refreshLibrary();
    refreshStatus();
    updateDocumentTitle();
}

std::optional<SessionApplication::OpenProjectCandidate> SessionApplication::loadProjectCandidate(
    const std::filesystem::path& path, QString* failure)
{
    try
    {
        auto loaded = DesktopSceneEngine::Infrastructure::Persistence::
            loadAuthoredProjectFile(path);
        if (loaded.displayOwner.displayId()
            != displaySelection_.boundDisplayId())
        {
            const auto message = tr("The project belongs to a different display session.");
            if (failure) *failure = message;
            else notify(SessionNotice::Error, tr("Open Project Failed"), message);
            return std::nullopt;
        }

        return OpenProjectCandidate{
            std::move(loaded),
            path};
    }
    catch (const std::exception& error)
    {
        const auto message = tr("Unable to open project:\n%1\n\n%2")
            .arg(QString::fromStdWString(path.wstring()), QString::fromUtf8(error.what()));
        if (failure) *failure = message;
        else notify(SessionNotice::Error, tr("Open Project Failed"), message);
        return std::nullopt;
    }
}

bool SessionApplication::openProjectFromPath(const std::filesystem::path& path)
{
    auto candidate = loadProjectCandidate(path);
    if (!candidate.has_value())
    {
        return false;
    }

    return completeOpenProject(
        std::move(*candidate),
        maybeSaveCurrentDocument());
}

bool SessionApplication::completeOpenProject(
    OpenProjectCandidate candidate,
    DestructiveDecision decision, QString* failure)
{
    if (decision == DestructiveDecision::Abort)
    {
        return false;
    }

    if (decision == DestructiveDecision::ProceedAfterSave)
    {
        auto reloadedCandidate = loadProjectCandidate(candidate.path, failure);
        if (!reloadedCandidate.has_value())
        {
            return false;
        }
        candidate = std::move(*reloadedCandidate);
    }

    commitOpenProject(std::move(candidate));
    return true;
}

void SessionApplication::commitOpenProject(OpenProjectCandidate candidate)
{
    retireBgmEditAuthority();
    stopActivePlayback();
    closeAllBackgroundMusicWindows();
    workspaceModel_.replaceAuthoredProject(std::move(candidate.project));
    documentState_.currentPath = std::move(candidate.path);
    documentState_.markClean(workspaceModel_.authoredRevision());

    updateCanvasAspectUi();

    refreshLibrary();
    refreshStatus();
    updateDocumentTitle();
}

SessionApplication::SaveResult SessionApplication::saveCurrentProject()
{
    if (!documentState_.currentPath.has_value())
    {
        return saveCurrentProjectAs();
    }

    return saveProjectToPath(*documentState_.currentPath, false);
}

SessionApplication::SaveResult SessionApplication::saveProjectToPath(
    const std::filesystem::path& path,
    bool adoptPath, QString* failure)
{
    try
    {
        persistDocument(workspaceModel_.authoredProject(), path, adoptPath);
    }
    catch (const std::exception& error)
    {
        const auto message = tr("Unable to save project:\n%1\n\n%2")
            .arg(QString::fromStdWString(path.wstring()), QString::fromUtf8(error.what()));
        if (failure) *failure = message;
        else notify(SessionNotice::Error, tr("Save Project Failed"), message);
        return SaveResult::Failed;
    }

    if (adoptPath)
    {
        documentState_.currentPath = path;
    }
    documentState_.markClean(workspaceModel_.authoredRevision());
    updateDocumentTitle();
    return SaveResult::Saved;
}

void SessionApplication::resetWorkspaceForNewDocument()
{
    refreshDisplaySelection();
    DesktopSceneEngine::Domain::AuthoredProject project{
        requireDisplayOwner(displaySelection_.boundDisplayId())};
    if (const auto display = displaySelection_.boundDisplay())
    {
        if (const auto aspect = DesktopSceneEngine::Domain::AspectRatio::from(
                display->pixelWidth, display->pixelHeight))
            project.desktopCanvas.aspectRatio = *aspect;
    }
    // Display geometry seeds only a new document. Loaded/authored Canvas remains
    // authoritative, and unresolved identity keeps the existing safe default.
    workspaceModel_.replaceAuthoredProject(std::move(project));
}

void SessionApplication::applyDisplayEnumerationResult(
    const DesktopSceneEngine::Platform::DisplayEnumerationResult& result)
{
    displaySelection_.refresh(result);
    updateBoundDisplayUi();
}

AuthoredResult SessionApplication::executeAuthoredOperation(const AuthoredRequest& request)
{
    requireExecutor();
    AuthoredResult result;
    if (closing() || closePending() || pendingDocumentOperation_ || pendingMediaImport_
        || authoredOperationExecuting_ || pendingRuntimeIntent_)
    { result.status = closing() ? AuthoredStatus::Unavailable : AuthoredStatus::Busy; return result; }
    QScopedValueRollback<bool> executing{authoredOperationExecuting_, true};
    std::optional<AudioEffectToken> effect;
    bool rateRuntimeRequested = false;
    result.playlistId = request.playlistId;
    result.itemId = request.itemId;
    const auto before = workspaceModel_.authoredRevision();
    const auto finish = [&]()
    {
        QString phase;
        if (effect)
        {
            result.audioEffect = finishNativeAudioEffect(*effect, result.authored != AuthoredOutcome::Rejected);
            phase = result.authored == AuthoredOutcome::Rejected ? QString{} : result.audioEffect->value("phase").toString();
        }
        if (rateRuntimeRequested && result.authored != AuthoredOutcome::Rejected)
        {
            if (phase == "pending" || phase == "preparing")
                result.runtime = RuntimeOutcome::Synchronized; // Expected outcome only; the receipt below remains Pending.
            else if (!effect || phase == "applied")
            {
                const auto clock = engine_.playbackClockSample();
                const auto sceneCount = engine_.sceneCount();
                const bool bound = engineInitialized_ && sceneCount > 0U && engine_.activeSceneIndex() < sceneCount;
                result.runtime = bound && clock.rate > 0.0 && std::abs(clock.rate - request.value) < 0.00001
                    ? RuntimeOutcome::Synchronized : RuntimeOutcome::Warning;
                if (result.runtime == RuntimeOutcome::Warning)
                    result.warnings.push_back({QStringLiteral("runtime-rate-warning"),
                        tr("The configured playback rate was saved, but the runtime rate could not be applied.")});
            }
        }
        if (effect)
        {
            if (phase == "pending" || phase == "preparing")
            {
                result.runtimeOnApplied = result.runtime;
                result.runtime = RuntimeOutcome::Pending;
            }
            else if (phase == "failed") result.runtime = RuntimeOutcome::Failed;
            else if (phase == "cancelled") result.runtime = RuntimeOutcome::Cancelled;
            else if (phase == "deferred-unavailable") result.runtime = RuntimeOutcome::DeferredUnavailable;
        }
        result.authoredRevision = workspaceModel_.authoredRevision();
        result.dirty = documentState_.isDirty(result.authoredRevision);
        if (request.action == AuthoredAction::SetPlaylistMaster)
            if (const auto* current = workspaceModel_.playlist(request.playlistId))
                result.playlistMasterVolume = current->masterVolume;
        if (request.action == AuthoredAction::SetPlaylistMaster)
        {
            // Its scalar read-back already carries Master/revision/dirty. Keep
            // unrelated invalidations, but never broadcast the synchronous
            // operation's transient runtime-busy eligibility to the page.
            view(SessionViewChange::ScalarReadSnapshot);
        }
        else publishReadOnlyInvalidation();
        return result;
    };
    const auto reject = [&](AuthoredStatus status)
    {
        result.status = status;
        result.authored = AuthoredOutcome::Rejected;
        return finish();
    };
    if (request.expectedRevision && *request.expectedRevision != before)
        return reject(AuthoredStatus::StaleRevision);

    const bool itemAction = request.action == AuthoredAction::SelectItem
        || request.action == AuthoredAction::RenameItem
        || request.action == AuthoredAction::RemoveItem
        || request.action == AuthoredAction::MoveItem
        || request.action == AuthoredAction::SetItemEnabled
        || request.action == AuthoredAction::SetImageDuration
        || request.action == AuthoredAction::ApplyVideoRate
        || request.action == AuthoredAction::SetItemGain
        || request.action == AuthoredAction::SetItemPresentation;
    const auto* playlist = workspaceModel_.playlist(request.playlistId);
    if (request.action != AuthoredAction::AddPlaylist
        && request.action != AuthoredAction::SetProjectPolicy && !playlist)
        return reject(AuthoredStatus::NotFound);
    const auto* item = request.itemId ? workspaceModel_.item(*request.itemId) : nullptr;
    if (itemAction)
    {
        if (!item) return reject(AuthoredStatus::NotFound);
        if (std::none_of(playlist->items.cbegin(), playlist->items.cend(),
                [&](const auto& candidate) { return candidate.id == *request.itemId; }))
            return reject(AuthoredStatus::WrongParent);
    }
    const bool checksAlias = request.action == AuthoredAction::RenamePlaylist
        || request.action == AuthoredAction::RemovePlaylist
        || request.action == AuthoredAction::RenameItem
        || request.action == AuthoredAction::RemoveItem;
    if (checksAlias && request.expectedAlias
        && *request.expectedAlias != (itemAction ? item->displayAlias : playlist->displayAlias))
        return reject(AuthoredStatus::StaleValue);

    std::optional<AudioEffectScope> scope;
    const bool activePlaylist = activePlaybackProjection_ && activePlaybackProjection_->sourcePlaylistId == request.playlistId;
    const bool activeItem = request.itemId && activeTimelineItemId() == request.itemId;
    if (request.action == AuthoredAction::SetPlaylistMaster && activePlaylist) scope = AudioEffectScope::EmbeddedGain;
    if (request.action == AuthoredAction::SetItemGain && activeItem) scope = AudioEffectScope::EmbeddedGain;
    // Removing any Item in the active Playlist rebuilds its bound projection.
    // Reserve the whole restore before authoring, including non-current Items.
    if ((activePlaylist && (request.action == AuthoredAction::RemovePlaylist || request.action == AuthoredAction::SetPlaylistEnabled
            || request.action == AuthoredAction::RemoveItem))
        || (activeItem && (request.action == AuthoredAction::RemoveItem || request.action == AuthoredAction::SetItemEnabled
            || request.action == AuthoredAction::ApplyVideoRate || request.action == AuthoredAction::SetItemPresentation)))
        scope = AudioEffectScope::Structural;
    if (scope && engineInitialized_)
    {
        effect = beginNativeAudioEffect(*scope, request.playlistId, request.itemId);
        if (!effect) return reject(AuthoredStatus::Busy);
    }

    bool accepted = false;
    bool refreshLists = false;
    bool selectionChanged = false;
    switch (request.action)
    {
    case AuthoredAction::SelectPlaylist:
        selectionChanged = workspaceModel_.selectedPlaylistId() != request.playlistId
            || workspaceModel_.selectedItemId().has_value();
        accepted = workspaceModel_.selectPlaylist(request.playlistId);
        refreshLists = selectionChanged;
        break;
    case AuthoredAction::SelectItem:
        selectionChanged = workspaceModel_.selectedItemId() != request.itemId
            || workspaceModel_.selectedPlaylistId() != request.playlistId;
        accepted = workspaceModel_.selectItem(*request.itemId);
        refreshLists = selectionChanged;
        break;
    case AuthoredAction::AddPlaylist:
        result.playlistId = workspaceModel_.addPlaylist();
        accepted = workspaceModel_.selectPlaylist(result.playlistId);
        refreshLists = true;
        break;
    case AuthoredAction::RenamePlaylist:
        accepted = workspaceModel_.renamePlaylist(request.playlistId, request.alias);
        if (accepted) refreshOpenBackgroundMusicWindow(request.playlistId);
        refreshLists = true;
        break;
    case AuthoredAction::RemovePlaylist:
        result = removePlaylistAuthored(request.playlistId);
        accepted = result.status == AuthoredStatus::Applied;
        refreshLists = true;
        break;
    case AuthoredAction::MovePlaylist:
        if (request.direction != -1 && request.direction != 1)
            return reject(AuthoredStatus::InvalidValue);
        (void)workspaceModel_.movePlaylist(request.playlistId, request.direction);
        accepted = true; // A current-order boundary is a semantic no-op.
        refreshLists = true;
        break;
    case AuthoredAction::SetPlaylistEnabled:
        if (!request.enabled && workspaceModel_.activePlaylistId() == request.playlistId
            && workspaceModel_.playlistEnabled(request.playlistId))
            result.runtime = RuntimeOutcome::Stopped;
        accepted = setPlaylistAvailability(request.playlistId, request.enabled);
        refreshLists = true;
        break;
    case AuthoredAction::SetProjectPolicy:
        accepted = workspaceModel_.setPlaylistPlaybackMode(request.policy);
        refreshLists = true;
        break;
    case AuthoredAction::SetPlaylistPolicy:
        accepted = workspaceModel_.setPlaylistItemPlaybackMode(request.playlistId, request.policy);
        refreshLists = true;
        break;
    case AuthoredAction::SetPlaylistMaster:
        accepted = workspaceModel_.setPlaylistMasterVolume(request.playlistId, request.value);
        if (accepted && workspaceModel_.authoredRevision() != before)
        {
            queueInspectorRefresh();
            synchronizeActivePlaybackVolumeCluster();
            if (activePlaybackProjection_ && activePlaybackProjection_->sourcePlaylistId == request.playlistId)
            {
                updateEffectiveOutputVolume();
                result.runtime = RuntimeOutcome::Synchronized;
            }
        }
        if (platform_) platform_->acknowledgeSessionRevision(before, true);
        break;
    case AuthoredAction::RenameItem:
        accepted = workspaceModel_.renameItem(*request.itemId, request.alias);
        if (accepted) refreshOpenBackgroundMusicWindow(request.playlistId);
        refreshLists = true;
        break;
    case AuthoredAction::RemoveItem:
        result = removeItemAuthored(request.playlistId, *request.itemId);
        accepted = result.status == AuthoredStatus::Applied;
        refreshLists = true;
        break;
    case AuthoredAction::MoveItem:
        if (request.direction != -1 && request.direction != 1)
            return reject(AuthoredStatus::InvalidValue);
        (void)workspaceModel_.moveItem(*request.itemId, request.direction);
        accepted = true; // Existing ordering changes do not rebind playback.
        refreshLists = true;
        break;
    case AuthoredAction::SetItemEnabled:
        if (!request.enabled && workspaceModel_.activeItemId() == request.itemId
            && workspaceModel_.itemEnabled(*request.itemId))
            result.runtime = RuntimeOutcome::Stopped;
        accepted = setItemAvailability(*request.itemId, request.enabled);
        refreshLists = true;
        break;
    case AuthoredAction::SetImageDuration:
        accepted = workspaceModel_.setImageDisplayDuration(*request.itemId, request.value);
        break;
    case AuthoredAction::ApplyVideoRate:
        // A valid loaded non-step value remains a no-op when applied unchanged.
        accepted = item->mediaType == WorkspaceMediaType::Video
            && (item->playbackRate == request.value
                || workspaceModel_.setVideoPlaybackRate(*request.itemId, request.value));
        if (accepted && activePlaybackProjection_ && workspaceModel_.activeItemId() == request.itemId
            && activePlaybackProjection_->sceneIndexForItem(*request.itemId))
        {
            engine_.setPlaybackSpeed(static_cast<float>(request.value));
            rateRuntimeRequested = true;
        }
        break;
    case AuthoredAction::SetItemGain:
        accepted = workspaceModel_.setItemMediaVolume(*request.itemId, request.value);
        if (accepted && workspaceModel_.authoredRevision() != before && activeTimelineItemId() == request.itemId)
        {
            updateEffectiveOutputVolume();
            result.runtime = RuntimeOutcome::Synchronized;
        }
        break;
    case AuthoredAction::SetItemPresentation:
    {
        const auto& patch = request.presentation;
        auto presentation = item->presentation;
        if (patch.mode) presentation.mediaPresentation.mode = *patch.mode;
        if (patch.customAspect) presentation.mediaPresentation.customAspect = *patch.customAspect;
        if (patch.scale)
        {
            if (!std::isfinite(*patch.scale) || *patch.scale < 0.1 || *patch.scale > 3.0)
                return reject(AuthoredStatus::InvalidValue);
            presentation.layout.scaleX = static_cast<float>(*patch.scale);
            presentation.layout.scaleY = static_cast<float>(*patch.scale);
        }
        for (const auto& coordinate : {patch.positionX, patch.positionY})
            if (coordinate && (!std::isfinite(*coordinate) || *coordinate < -1.0 || *coordinate > 1.0))
                return reject(AuthoredStatus::InvalidValue);
        if (patch.positionX) presentation.layout.positionX = static_cast<float>(*patch.positionX);
        if (patch.positionY) presentation.layout.positionY = static_cast<float>(*patch.positionY);
        accepted = workspaceModel_.setItemPresentation(*request.itemId, presentation);
        if (accepted && workspaceModel_.authoredRevision() != before
            && activePlaybackProjection_ && workspaceModel_.activeItemId() == request.itemId)
        {
            describeAuthoredRebuild(result, rebuildActiveProjection(
                *request.itemId, ActiveProjectionRebuildMode::RestoreCurrentItem));
            invalidateTimelineHoverPreview();
        }
        break;
    }
    case AuthoredAction::AddMedia:
        return reject(AuthoredStatus::Unavailable); // Native picker admission is asynchronous.
    }
    if (!accepted) return reject(AuthoredStatus::InvalidValue);
    const bool selection = request.action == AuthoredAction::SelectPlaylist || request.action == AuthoredAction::SelectItem;
    const bool changed = workspaceModel_.authoredRevision() != before;
    result.authored = selection ? AuthoredOutcome::NotApplicable
        : changed ? AuthoredOutcome::Committed : AuthoredOutcome::Unchanged;
    result.status = changed || selectionChanged ? AuthoredStatus::Applied : AuthoredStatus::NoChange;
    if (refreshLists && (changed || selectionChanged))
    {
        refreshPlaylistList();
        refreshItemList();
        queueInspectorRefresh();
    }
    if (request.action != AuthoredAction::SetPlaylistMaster) refreshStatus();
    return finish();
}

void SessionApplication::describeAuthoredRebuild(AuthoredResult& result,
    ActiveProjectionRebuildResult rebuild, bool startsTarget)
{
    switch (rebuild)
    {
    case ActiveProjectionRebuildResult::Restored:
        result.runtime = RuntimeOutcome::Synchronized;
        result.positionRestore = PositionRestoreOutcome::Restored;
        break;
    case ActiveProjectionRebuildResult::StartedTarget:
        result.runtime = RuntimeOutcome::Synchronized;
        break;
    case ActiveProjectionRebuildResult::Deferred:
        result.runtime = RuntimeOutcome::Pending;
        break;
    case ActiveProjectionRebuildResult::ReboundAtStart:
        result.runtime = RuntimeOutcome::Warning;
        result.positionRestore = PositionRestoreOutcome::ReboundAtStart;
        result.warnings.push_back({QStringLiteral("position-restore-warning"),
            tr("Playlist updated, but the previous playback position could not be restored.")});
        break;
    case ActiveProjectionRebuildResult::Unchanged:
        result.runtime = RuntimeOutcome::Unchanged;
        result.warnings.push_back({QStringLiteral("runtime-sync-warning"),
            tr("Playlist changes could not be synchronized with active playback. "
               "They will be applied the next time the playlist is played.")});
        break;
    case ActiveProjectionRebuildResult::Stopped:
        result.runtime = RuntimeOutcome::Failed;
        if (!startsTarget) result.positionRestore = PositionRestoreOutcome::Failed;
        result.warnings.push_back({QStringLiteral("runtime-sync-failed"),
            tr("Playback was stopped because the updated playlist could not be synchronized.")});
        break;
    }
}

AuthoredResult SessionApplication::addMediaSources(PlaylistId playlistId, const QStringList& sources)
{
    AuthoredResult result;
    result.playlistId = playlistId;
    const auto before = workspaceModel_.authoredRevision();
    if (!workspaceModel_.playlist(playlistId))
    {
        result.status = AuthoredStatus::NotFound;
        result.authoredRevision = before;
        result.dirty = documentState_.isDirty(before);
        return result;
    }
    const bool synchronizesActivePlaylist = activePlaybackProjection_
        && playlistId == activePlaybackProjection_->sourcePlaylistId;
    const auto activeItemId = synchronizesActivePlaylist
        ? activePlaybackProjection_->itemForSceneIndex(engine_.activeSceneIndex()) : std::nullopt;
    const auto effect = synchronizesActivePlaylist && !sources.isEmpty()
        ? beginNativeAudioEffect(AudioEffectScope::Structural, playlistId, activeItemId) : std::nullopt;
    if (synchronizesActivePlaylist && !sources.isEmpty() && !effect)
    {
        result.status = AuthoredStatus::Busy;
        result.authoredRevision = before;
        result.dirty = documentState_.isDirty(before);
        return result;
    }
    for (const QString& source : sources)
    {
        MediaSourceResult entry;
        entry.source = source;
        // .video remains an internal Engine-test source convention, outside
        // the nine public formats accepted by authored media import.
        const auto mediaType = QFileInfo{source}.suffix().compare(QStringLiteral("video"), Qt::CaseInsensitive) == 0
            ? std::optional<WorkspaceMediaType>{} : mediaTypeForSource(source);
        if (!mediaType)
        {
            entry.outcome = MediaSourceOutcome::Unsupported;
            entry.warnings.push_back({QStringLiteral("unsupported-media"),
                tr("Unsupported media file: %1").arg(source)});
        }
        else
        {
            entry.itemId = workspaceModel_.addItem(playlistId, *mediaType,
                source, QFileInfo{source}.completeBaseName());
            entry.outcome = entry.itemId ? MediaSourceOutcome::Committed : MediaSourceOutcome::Rejected;
            if (!entry.itemId)
                entry.warnings.push_back({QStringLiteral("media-rejected"),
                    tr("Media could not be added: %1").arg(source)});
        }
        for (const auto& warning : entry.warnings) result.warnings.push_back(warning);
        result.sources.push_back(std::move(entry));
    }
    const bool added = workspaceModel_.authoredRevision() != before;
    if (added && synchronizesActivePlaylist)
    {
        const auto rebuild = activeItemId
            ? rebuildActiveProjection(*activeItemId, ActiveProjectionRebuildMode::RestoreCurrentItem)
            : ActiveProjectionRebuildResult::Unchanged;
        describeAuthoredRebuild(result, rebuild);
        for (auto& entry : result.sources)
        {
            if (entry.outcome != MediaSourceOutcome::Committed) continue;
            entry.runtime = result.runtime;
            for (const auto& warning : result.warnings)
                if (warning.code.startsWith(QStringLiteral("runtime-"))
                    || warning.code.startsWith(QStringLiteral("position-")))
                    entry.warnings.push_back(warning);
        }
    }
    if (added) refreshOpenBackgroundMusicWindow(playlistId);
    result.authoredRevision = workspaceModel_.authoredRevision();
    result.dirty = documentState_.isDirty(result.authoredRevision);
    result.status = added ? AuthoredStatus::Applied : sources.isEmpty()
        ? AuthoredStatus::PickerCancelled : AuthoredStatus::InvalidValue;
    result.authored = added ? AuthoredOutcome::Committed : sources.isEmpty()
        ? AuthoredOutcome::NotApplicable : AuthoredOutcome::Rejected;
    refreshPlaylistList();
    refreshItemList();
    queueInspectorRefresh();
    refreshStatus();
    if (effect)
    {
        result.audioEffect = finishNativeAudioEffect(*effect, added);
        const auto phase = added ? result.audioEffect->value("phase").toString() : QString{};
        if (phase == "pending" || phase == "preparing")
        {
            result.runtimeOnApplied = result.runtime;
            result.runtime = RuntimeOutcome::Pending;
            for (auto& source : result.sources) if (source.outcome == MediaSourceOutcome::Committed) source.runtime = RuntimeOutcome::Pending;
        }
        else if (phase == "failed") result.runtime = RuntimeOutcome::Failed;
        else if (phase == "cancelled") result.runtime = RuntimeOutcome::Cancelled;
        else if (phase == "deferred-unavailable") result.runtime = RuntimeOutcome::DeferredUnavailable;
    }
    return result;
}

AuthoredResult SessionApplication::removePlaylistAuthored(PlaylistId id)
{
    AuthoredResult result;
    result.playlistId = id;
    if (activePlaybackProjection_ && activePlaybackProjection_->sourcePlaylistId == id)
    {
        stopActivePlayback();
        result.runtime = RuntimeOutcome::Stopped;
    }
    closeBackgroundMusicWindow(id);
    result.status = workspaceModel_.removePlaylist(id) ? AuthoredStatus::Applied : AuthoredStatus::NotFound;
    return result;
}

AuthoredResult SessionApplication::removeItemAuthored(PlaylistId playlistId, ItemId id)
{
    AuthoredResult result;
    result.playlistId = playlistId;
    result.itemId = id;
    const PlaylistId sourcePlaylistId = activePlaybackProjection_
        ? activePlaybackProjection_->sourcePlaylistId : PlaylistId{};
    const bool removesActivePlaylistItem = sourcePlaylistId == playlistId;
    const auto previousActiveItemId = activePlaybackProjection_
        ? activePlaybackProjection_->itemForSceneIndex(engine_.activeSceneIndex()) : std::nullopt;
    const bool removesCurrentItem = removesActivePlaylistItem && previousActiveItemId == id;
    std::optional<ItemId> successorItemId;
    if (removesCurrentItem)
    {
        // Preserve production's current playable authored-order successor/wrap,
        // independently of the bound scene order and sequence policy.
        const auto collections = playbackSequenceCollections();
        const auto playlist = std::find_if(collections.itemsByPlaylist.cbegin(),
            collections.itemsByPlaylist.cend(), [sourcePlaylistId](const auto& candidate)
            { return candidate.playlistId == sourcePlaylistId; });
        if (playlist != collections.itemsByPlaylist.cend() && playlist->itemIds.size() > 1U)
        {
            const auto current = std::find(playlist->itemIds.cbegin(), playlist->itemIds.cend(), id);
            if (current != playlist->itemIds.cend())
            {
                auto next = std::next(current);
                if (next == playlist->itemIds.cend()) next = playlist->itemIds.cbegin();
                successorItemId = *next;
            }
        }
    }
    if (!workspaceModel_.removeItem(id))
    {
        result.status = AuthoredStatus::NotFound;
        return result;
    }
    result.status = AuthoredStatus::Applied;
    if (removesActivePlaylistItem)
    {
        const auto* updatedPlaylist = activePlaybackProjection_
            ? workspaceModel_.playlist(activePlaybackProjection_->sourcePlaylistId) : nullptr;
        if (!updatedPlaylist || updatedPlaylist->items.isEmpty())
        {
            stopActivePlayback();
            result.runtime = RuntimeOutcome::Stopped;
        }
        else if (removesCurrentItem)
        {
            if (!successorItemId || !workspaceModel_.isItemPlayable(sourcePlaylistId, *successorItemId))
            {
                stopActivePlayback();
                result.runtime = RuntimeOutcome::Stopped;
            }
            else
            {
                const auto rebuild = rebuildActiveProjection(*successorItemId,
                    ActiveProjectionRebuildMode::StartTargetItem);
                if (rebuild == ActiveProjectionRebuildResult::Deferred)
                    describeAuthoredRebuild(result, rebuild, true);
                else if (rebuild != ActiveProjectionRebuildResult::StartedTarget)
                {
                    stopActivePlayback();
                    describeAuthoredRebuild(result, ActiveProjectionRebuildResult::Stopped, true);
                }
                else
                {
                    describeAuthoredRebuild(result, rebuild, true);
                    rebasePlaybackSequenceToActivePosition();
                }
            }
        }
        else if (previousActiveItemId)
        {
            describeAuthoredRebuild(result, rebuildActiveProjection(*previousActiveItemId,
                ActiveProjectionRebuildMode::RestoreCurrentItem));
        }
        else
        {
            stopActivePlayback();
            describeAuthoredRebuild(result, ActiveProjectionRebuildResult::Stopped);
        }
    }
    refreshOpenBackgroundMusicWindow(playlistId);
    return result;
}

bool SessionApplication::setPlaylistAvailability(PlaylistId id, bool enabled)
{
    if (workspaceModel_.playlist(id) == nullptr)
    {
        return false;
    }

    if (workspaceModel_.playlistEnabled(id) == enabled)
    {
        return true;
    }

    if (!enabled && workspaceModel_.activePlaylistId() == id)
    {
        stopActivePlayback();
    }

    return workspaceModel_.setPlaylistEnabled(id, enabled);
}

bool SessionApplication::setItemAvailability(ItemId id, bool enabled)
{
    if (workspaceModel_.item(id) == nullptr)
    {
        return false;
    }

    if (workspaceModel_.itemEnabled(id) == enabled)
    {
        return true;
    }

    if (!enabled && workspaceModel_.activeItemId() == id)
    {
        stopActivePlayback();
    }

    return workspaceModel_.setItemEnabled(id, enabled);
}

bool SessionApplication::startPlaylistPlayback(
    PlaylistId playlistId,
    std::optional<ItemId> startItemId,
    bool synchronizeSelection,
    bool showFailureDialog,
    QString* failureStatus
)
{
    if (failureStatus) *failureStatus = QStringLiteral("unavailable");
    const auto failure = [&](const QString& message, const QString& status) {
        if (failureStatus) *failureStatus = status;
        if (showFailureDialog) notify(SessionNotice::Warning, tr("Unable to play media"), message);
        else qWarning().noquote() << "[NativePreview] media-prepare-failed:" << message;
        return false;
    };
    if (!engineInitialized_)
    {
        return false;
    }

    if (!workspaceModel_.isPlaylistPlayable(playlistId))
    {
        return false;
    }

    const std::optional<ItemId> resolvedStartItemId = startItemId.has_value()
        ? startItemId
        : workspaceModel_.firstPlayableItem(playlistId);
    if (!resolvedStartItemId.has_value()
        || !workspaceModel_.isItemPlayable(playlistId, *resolvedStartItemId))
    {
        return false;
    }

    const auto* playlist = workspaceModel_.authoredPlaylist(playlistId);
    if (playlist == nullptr)
    {
        return false;
    }

    cancelTimelineDrag();

    auto candidate = DesktopSceneEngine::Player::makeActivePlaybackProjection(
        *playlist);
    if (!candidate.has_value())
    {
        return failure(tr("Unable to create a playback sequence."), QStringLiteral("prepare-failed"));
    }

    const std::size_t startSceneIndex = candidate->sceneIndexForItem(
        *resolvedStartItemId).value_or(candidate->project.scenes.size());
    if (startSceneIndex >= candidate->project.scenes.size())
    {
        return failure(tr("Unable to create a playback sequence."), QStringLiteral("prepare-failed"));
    }

    const DesktopSceneEngine::Domain::EntityId targetSceneId =
        candidate->project.scenes[startSceneIndex].id;
    std::optional<AudioEffectToken> ownedEffect;
    if (!activeAudioCapture_)
    {
        ownedEffect = beginNativeAudioEffect(AudioEffectScope::Structural, playlistId, resolvedStartItemId);
        if (!ownedEffect) return failure(tr("An audio transition is pending."), QStringLiteral("busy"));
    }
    bool logicalAccepted = false;
    struct FinishOwnedCapture
    {
        std::function<void()> finish;
        ~FinishOwnedCapture() { if (finish) finish(); }
    } finishOwned{[&] {
        if (ownedEffect) (void)finishNativeAudioEffect(*ownedEffect, logicalAccepted);
    }};
    const bool hadPreviousActivePlayback = activePlaybackProjection_.has_value();
    const auto previousBackgroundMusicScope = activeBackgroundMusicScopeSnapshot();
    std::optional<DesktopSceneEngine::Domain::EntityId> previousSceneId{};
    const bool previousPaused = engine_.isPaused();
    const float previousPlaybackSpeed = engine_.playbackSpeed();
    if (activePlaybackProjection_.has_value())
    {
        const std::size_t previousSceneIndex = engine_.activeSceneIndex();
        if (previousSceneIndex < activePlaybackProjection_->project.scenes.size())
        {
            previousSceneId = activePlaybackProjection_->project.scenes[
                previousSceneIndex].id;
        }
    }

    auto previousProjection = std::move(activePlaybackProjection_);
    renewRuntimeBinding();
    engine_.detachProject();
    activePlaybackProjection_ = std::move(candidate);
    if (engine_.bindProject(activePlaybackProjection_->project, targetSceneId))
    {
        synchronizeActivePlaybackVolumeCluster();
        updateEffectiveOutputVolume();
        const auto targetItem = activePlaybackProjection_->itemForSceneIndex(
            startSceneIndex);
        if (targetItem.has_value())
        {
            if (synchronizeSelection)
            {
                (void)workspaceModel_.activateItem(*targetItem);
            }
            else
            {
                (void)workspaceModel_.setActiveItem(*targetItem);
            }
        }
        forwardBackgroundMusicScopeAfterStart(
            hadPreviousActivePlayback,
            previousBackgroundMusicScope);
        if (synchronizeSelection)
        {
            rebasePlaybackSequenceToActivePosition();
        }
        logicalAccepted = true;
        refreshLibrary();
        refreshStatus();
        return true;
    }

    activePlaybackProjection_.reset();
    if (previousProjection.has_value())
    {
        activePlaybackProjection_ = std::move(previousProjection);
        const DesktopSceneEngine::Domain::EntityId restoreSceneId =
            previousSceneId.has_value()
                ? *previousSceneId
                : activePlaybackProjection_->project.scenes.front().id;
        renewRuntimeBinding();
        if (engine_.bindProject(activePlaybackProjection_->project, restoreSceneId))
        {
            engine_.setPlaybackSpeed(previousPlaybackSpeed);
            if (previousPaused)
            {
                engine_.togglePause();
            }
        }
        else
        {
            activePlaybackProjection_.reset();
        }
    }

    updateEffectiveOutputVolume();
    synchronizeActivePlaybackVolumeCluster();
    synchronizeActivePlaybackItem();
    refreshLibrary();
    refreshStatus();
    return failure(tr("Media file could not be opened."), QStringLiteral("bind-failed"));
}

SessionApplication::PlaybackSequence::Collections SessionApplication::playbackSequenceCollections() const
{
    PlaybackSequence::Collections collections{};
    collections.playlistIds.reserve(static_cast<std::size_t>(workspaceModel_.playlists().size()));
    collections.itemsByPlaylist.reserve(static_cast<std::size_t>(workspaceModel_.playlists().size()));

    for (const DesktopSceneEngine::Player::WorkspacePlaylist& playlist
         : workspaceModel_.playlists())
    {
        if (!workspaceModel_.isPlaylistPlayable(playlist.id))
        {
            continue;
        }

        PlaybackSequence::PlaylistItems items{};
        items.playlistId = playlist.id;
        items.itemMode = playlist.itemPlaybackMode;
        items.itemIds.reserve(static_cast<std::size_t>(playlist.items.size()));
        for (const DesktopSceneEngine::Player::WorkspaceItem& item : playlist.items)
        {
            if (workspaceModel_.isItemPlayable(playlist.id, item.id))
            {
                items.itemIds.push_back(item.id);
            }
        }

        collections.playlistIds.push_back(playlist.id);
        collections.itemsByPlaylist.push_back(std::move(items));
    }

    return collections;
}

void SessionApplication::synchronizePlaybackSequenceConfiguration(
    const PlaybackSequence::Collections& collections
)
{
    const auto configuredPlaylistMode = workspaceModel_.playlistPlaybackMode();
    if (playbackSequence_.playlistMode() != configuredPlaylistMode)
    {
        playbackSequence_.setPlaylistMode(configuredPlaylistMode, collections);
    }

    playbackSequence_.synchronizeItemMode(collections);
}

void SessionApplication::rebasePlaybackSequenceToActivePosition()
{
    if (!activePlaybackProjection_.has_value())
    {
        playbackSequence_.clear();
        return;
    }

    const auto activeItemId = activePlaybackProjection_->itemForSceneIndex(
        engine_.activeSceneIndex());
    if (!activeItemId.has_value())
    {
        playbackSequence_.clear();
        return;
    }

    const PlaybackSequence::Collections collections = playbackSequenceCollections();
    synchronizePlaybackSequenceConfiguration(collections);
    playbackSequence_.rebase(
        collections,
        activePlaybackProjection_->sourcePlaylistId,
        *activeItemId);
}

bool SessionApplication::transitionToSequencePosition(const PlaybackSequence::Position& position)
{
    if (!activePlaybackProjection_.has_value())
    {
        return false;
    }

    const auto* targetPlaylist = workspaceModel_.playlist(position.playlistId);
    const auto* targetItem = workspaceModel_.item(position.itemId);
    if (targetPlaylist == nullptr || targetItem == nullptr)
    {
        return false;
    }

    std::optional<AudioEffectToken> ownedEffect;
    if (!activeAudioCapture_)
    {
        ownedEffect = beginNativeAudioEffect(AudioEffectScope::Structural, position.playlistId, position.itemId);
        if (!ownedEffect) return false;
    }
    bool logicalAccepted = false;
    struct FinishOwnedTransition
    {
        std::function<void()> finish;
        ~FinishOwnedTransition() { if (finish) finish(); }
    } finishOwned{[&] {
        if (ownedEffect) (void)finishNativeAudioEffect(*ownedEffect, logicalAccepted);
    }};

    if (activePlaybackProjection_->sourcePlaylistId != position.playlistId)
    {
        logicalAccepted = startPlaylistPlayback(position.playlistId, position.itemId, false);
        return logicalAccepted;
    }

    const auto targetSceneIndex = activePlaybackProjection_->sceneIndexForItem(position.itemId);
    if (!targetSceneIndex.has_value()
        || *targetSceneIndex >= activePlaybackProjection_->project.scenes.size())
    {
        return false;
    }

    const auto previousScope = activeBackgroundMusicScopeSnapshot();
    const DesktopSceneEngine::Domain::EntityId& targetSceneId =
        activePlaybackProjection_->project.scenes[*targetSceneIndex].id;
    if (!engine_.switchScene(activePlaybackProjection_->project, targetSceneId))
    {
        return false;
    }
    renewRuntimeBinding();
    if (!workspaceModel_.setActiveItem(position.itemId)) return false;

    invalidateTimelineHoverPreview();
    updateEffectiveOutputVolume();
    const auto currentScope = activeBackgroundMusicScopeSnapshot();
    if (previousScope.has_value() && currentScope.has_value()
        && !sameBackgroundMusicScope(*previousScope, *currentScope))
    {
        forwardBackgroundMusicScopeChange();
    }
    synchronizeActivePlaybackItem();
    refreshPlaylistList();
    refreshItemList();
    logicalAccepted = true;
    publishRuntimePlayback(true);
    return true;
}

bool SessionApplication::advancePlaybackSequence()
{
    if (!activePlaybackProjection_.has_value())
    {
        return false;
    }

    const PlaybackSequence::Collections collections = playbackSequenceCollections();
    synchronizePlaybackSequenceConfiguration(collections);
    if (!playbackSequence_.current().has_value())
    {
        rebasePlaybackSequenceToActivePosition();
    }

    const auto previousPosition = playbackSequence_.current();
    if (!previousPosition.has_value())
    {
        stopActivePlayback();
        return false;
    }

    const std::size_t maximumAttempts = (std::max)(
        std::size_t{1U},
        collections.playlistIds.size() * MaximumRandomRoundResolutionAttempts);
    for (std::size_t attempt = 0U; attempt < maximumAttempts; ++attempt)
    {
        const PlaybackSequence::Result result = playbackSequence_.completeCurrentItem(collections);
        if (result.kind == DesktopSceneEngine::Player::PlaybackSequenceResultKind::Identity
            && result.identity.has_value())
        {
            if (transitionToSequencePosition(*result.identity))
            {
                return true;
            }

            playbackSequence_.rebase(
                collections,
                previousPosition->playlistId,
                previousPosition->itemId);
            return false;
        }

        if (result.kind == DesktopSceneEngine::Player::PlaybackSequenceResultKind::NoTarget)
        {
            stopActivePlayback();
            return false;
        }
    }

    // A Random outer round may contain only now-empty Playlists. Its controller
    // retry is finite; exhausting this Player-level bound stops safely rather
    // than repeatedly regenerating random rounds on the UI thread.
    stopActivePlayback();
    return false;
}

std::optional<DesktopSceneEngine::BackgroundMusicControl::ActiveScopeSnapshot>
SessionApplication::activeBackgroundMusicScopeSnapshot() const
{
    if (!activePlaybackProjection_.has_value())
    {
        return std::nullopt;
    }

    const auto* const playlist = workspaceModel_.authoredPlaylist(
        activePlaybackProjection_->sourcePlaylistId);
    if (playlist == nullptr)
    {
        return std::nullopt;
    }

    const auto activeItemId = activePlaybackProjection_->itemForSceneIndex(
        engine_.activeSceneIndex());
    const auto* const activeItem = activeItemId.has_value()
        ? workspaceModel_.authoredItem(*activeItemId)
        : nullptr;
    if (activeItemId.has_value() && activeItem == nullptr)
    {
        return std::nullopt;
    }

    return DesktopSceneEngine::Player::makeActiveBackgroundMusicScopeSnapshot(
        *playlist,
        activeItem);
}

bool SessionApplication::forwardBackgroundMusicControl(
    const DesktopSceneEngine::BackgroundMusicControl::Event& event
)
{
    const bool accepted = backgroundMusicControlForwarder_
        ? backgroundMusicControlForwarder_(event)
        : engine_.processBackgroundMusicControl(event);
    if (!accepted)
    {
        std::cerr << "[Player] Background music control was rejected by Engine.\n";
    }

    return accepted;
}

void SessionApplication::forwardBackgroundMusicScopeAfterStart(
    bool hadPreviousActivePlayback,
    const std::optional<DesktopSceneEngine::BackgroundMusicControl::ActiveScopeSnapshot>&
        previousScope
)
{
    const auto currentScope = activeBackgroundMusicScopeSnapshot();
    if (!currentScope.has_value())
    {
        std::cerr << "[Player] Active playback scope could not be converted for background music.\n";
        return;
    }

    if (!hadPreviousActivePlayback)
    {
        (void)forwardBackgroundMusicControl(
            DesktopSceneEngine::BackgroundMusicControl::PlaybackSessionStarted{*currentScope});
        return;
    }

    if (!previousScope.has_value()
        || !sameBackgroundMusicScope(*previousScope, *currentScope))
    {
        (void)forwardBackgroundMusicControl(
            DesktopSceneEngine::BackgroundMusicControl::ActivePlaybackScopeChanged{*currentScope});
    }
}

void SessionApplication::forwardBackgroundMusicScopeChange()
{
    const auto scope = activeBackgroundMusicScopeSnapshot();
    if (!scope.has_value())
    {
        std::cerr << "[Player] Active playback scope could not be converted for background music.\n";
        return;
    }

    (void)forwardBackgroundMusicControl(
        DesktopSceneEngine::BackgroundMusicControl::ActivePlaybackScopeChanged{*scope});
}

void SessionApplication::handleBackgroundMusicQueueChanged(
    PlaylistId playlistId,
    std::optional<ItemId> itemId
)
{
    (void)synchronizeBackgroundMusicConfig(playlistId, itemId);
}

RuntimeOutcome SessionApplication::synchronizeBackgroundMusicConfig(PlaylistId playlistId, std::optional<ItemId> itemId)
{
    if (!engineInitialized_ || !activePlaybackProjection_.has_value()
        || activePlaybackProjection_->sourcePlaylistId != playlistId)
    {
        return RuntimeOutcome::NotApplicable;
    }

    const auto activeScope = activeBackgroundMusicScopeSnapshot();
    if (!activeScope.has_value())
    {
        std::cerr << "[Player] Active playback scope could not be converted for background music.\n";
        return RuntimeOutcome::Failed;
    }

    // Engine admission can accept an inactive-owner event without actions.
    // Native Session identity and actual C1 owner must both match this scope.
    const auto runtime = engine_.backgroundMusicObservation();
    if (!runtime.activePlaylistId || *runtime.activePlaylistId != activeScope->playlistId || !runtime.playlist.active)
        return RuntimeOutcome::NotApplicable;

    if (!itemId.has_value())
    {
        const auto* const playlist = workspaceModel_.authoredPlaylist(playlistId);
        const auto snapshot = playlist != nullptr
            ? DesktopSceneEngine::Player::makeBackgroundMusicQueueSnapshot(
                playlist->globalMusicQueue)
            : std::nullopt;
        if (!snapshot.has_value())
        {
            std::cerr << "[Player] Playlist music queue could not be converted for background music.\n";
            return RuntimeOutcome::Failed;
        }

        const bool accepted = forwardBackgroundMusicControl(
            DesktopSceneEngine::BackgroundMusicControl::PlaylistMusicConfigChanged{
                activeScope->playlistId,
                *snapshot});
        return accepted ? RuntimeOutcome::Synchronized : RuntimeOutcome::Failed;
    }

    const auto activeItemId = activePlaybackProjection_->itemForSceneIndex(
        engine_.activeSceneIndex());
    if (!activeItemId.has_value() || *activeItemId != *itemId)
    {
        return RuntimeOutcome::NotApplicable;
    }

    const auto* const item = workspaceModel_.authoredItem(*itemId);
    const auto publicItemId = item != nullptr
        ? DesktopSceneEngine::Player::makeBackgroundMusicItemId(item->id)
        : std::nullopt;
    const auto snapshot = item != nullptr
        ? DesktopSceneEngine::Player::makeBackgroundMusicQueueSnapshot(item->musicQueue)
        : std::nullopt;
    if (!publicItemId.has_value() || !snapshot.has_value())
    {
        std::cerr << "[Player] Item music queue could not be converted for background music.\n";
        return RuntimeOutcome::Failed;
    }

    if (!runtime.activeItemId || *runtime.activeItemId != *publicItemId || !runtime.item.active)
        return RuntimeOutcome::NotApplicable;

    const bool accepted = forwardBackgroundMusicControl(
        DesktopSceneEngine::BackgroundMusicControl::ItemMusicConfigChanged{
            activeScope->playlistId,
            *publicItemId,
            *snapshot});
    return accepted ? RuntimeOutcome::Synchronized : RuntimeOutcome::Failed;
}

void SessionApplication::handlePlaylistMusicEnabledChanged(PlaylistId playlistId, bool enabled)
{
    const auto* const queue = workspaceModel_.playlistMusicQueue(playlistId);
    if (queue == nullptr || queue->enabled == enabled)
    {
        refreshOpenBackgroundMusicWindow(playlistId);
        return;
    }

    const bool active = activePlaybackProjection_ && activePlaybackProjection_->sourcePlaylistId == playlistId;
    const auto effect = active ? beginNativeAudioEffect(AudioEffectScope::Structural, playlistId) : std::nullopt;
    if (active && !effect) { refreshOpenBackgroundMusicWindow(playlistId); return; }
    if (workspaceModel_.setPlaylistMusicEnabled(playlistId, enabled))
    {
        handleBackgroundMusicQueueChanged(playlistId, std::nullopt);
    }
    if (effect) (void)finishNativeAudioEffect(*effect);
    refreshOpenBackgroundMusicWindow(playlistId);
}

void SessionApplication::handleItemMusicEnabledChanged(
    PlaylistId playlistId,
    ItemId itemId,
    bool enabled
)
{
    const auto* const playlist = workspaceModel_.playlist(playlistId);
    const bool itemBelongsToPlaylist = playlist != nullptr
        && std::any_of(
            playlist->items.cbegin(),
            playlist->items.cend(),
            [itemId](const auto& item)
            {
                return item.id == itemId;
            });
    const auto* const queue = itemBelongsToPlaylist
        ? workspaceModel_.itemMusicQueue(itemId)
        : nullptr;
    if (queue == nullptr || queue->enabled == enabled)
    {
        refreshOpenBackgroundMusicWindow(playlistId);
        return;
    }

    const bool active = activePlaybackProjection_ && activePlaybackProjection_->sourcePlaylistId == playlistId && activeTimelineItemId() == itemId;
    const auto effect = active ? beginNativeAudioEffect(AudioEffectScope::Structural, playlistId, itemId) : std::nullopt;
    if (active && !effect) { refreshOpenBackgroundMusicWindow(playlistId); return; }
    if (workspaceModel_.setItemMusicEnabled(itemId, enabled))
    {
        handleBackgroundMusicQueueChanged(playlistId, itemId);
    }
    if (effect) (void)finishNativeAudioEffect(*effect);
    refreshOpenBackgroundMusicWindow(playlistId);
}

void SessionApplication::handlePlaylistTrackSwitchRequested(PlaylistId playlistId, TrackId trackId)
{
    if (!activePlaybackProjection_.has_value()
        || activePlaybackProjection_->sourcePlaylistId != playlistId)
    {
        return;
    }

    const auto* const playlist = workspaceModel_.authoredPlaylist(playlistId);
    if (playlist == nullptr || !playlist->globalMusicQueue.enabled)
    {
        return;
    }
    const auto publicPlaylistId = playlist != nullptr
        ? DesktopSceneEngine::Player::makeBackgroundMusicPlaylistId(playlist->id)
        : std::nullopt;
    const auto publicTrackId = DesktopSceneEngine::Player::makeBackgroundMusicTrackId(trackId);
    if (!publicPlaylistId.has_value() || !publicTrackId.has_value())
    {
        return;
    }

    if (forwardBackgroundMusicControl(
            DesktopSceneEngine::BackgroundMusicControl::PlaylistMusicTrackSwitchRequested{
                *publicPlaylistId,
                *publicTrackId}))
    {
        synchronizeBackgroundMusicPlaybackDisplay();
    }
}

void SessionApplication::handleItemTrackSwitchRequested(
    PlaylistId playlistId,
    ItemId itemId,
    TrackId trackId
)
{
    if (!activePlaybackProjection_.has_value()
        || activePlaybackProjection_->sourcePlaylistId != playlistId
        || activeTimelineItemId() != itemId)
    {
        return;
    }

    const auto* const playlist = workspaceModel_.authoredPlaylist(playlistId);
    const auto* const item = workspaceModel_.authoredItem(itemId);
    if (item == nullptr || !item->musicQueue.enabled)
    {
        return;
    }
    const auto publicPlaylistId = playlist != nullptr
        ? DesktopSceneEngine::Player::makeBackgroundMusicPlaylistId(playlist->id)
        : std::nullopt;
    const auto publicItemId = item != nullptr
        ? DesktopSceneEngine::Player::makeBackgroundMusicItemId(item->id)
        : std::nullopt;
    const auto publicTrackId = DesktopSceneEngine::Player::makeBackgroundMusicTrackId(trackId);
    if (!publicPlaylistId.has_value() || !publicItemId.has_value() || !publicTrackId.has_value())
    {
        return;
    }

    if (forwardBackgroundMusicControl(
            DesktopSceneEngine::BackgroundMusicControl::ItemMusicTrackSwitchRequested{
                *publicPlaylistId,
                *publicItemId,
                *publicTrackId}))
    {
        synchronizeBackgroundMusicPlaybackDisplay();
    }
}

void SessionApplication::togglePlaybackPause()
{
    engine_.togglePause();
    // Engine applies BGM policy at the matching transport ACK boundary.
    // Confirmed isPaused() deliberately need not change on this stack.
    refreshStatus();
}

SessionApplication::ActiveProjectionRebuildResult SessionApplication::rebuildActiveProjection(
    ItemId targetItemId,
    ActiveProjectionRebuildMode mode
)
{
    if (!engineInitialized_ || !activePlaybackProjection_.has_value()
        || !targetItemId.isValid())
    {
        return ActiveProjectionRebuildResult::Unchanged;
    }

    const PlaylistId sourcePlaylistId = activePlaybackProjection_->sourcePlaylistId;
    const auto* playlist = workspaceModel_.authoredPlaylist(sourcePlaylistId);
    const auto* targetItem = workspaceModel_.authoredItem(targetItemId);
    const auto activeItemId = activePlaybackProjection_->itemForSceneIndex(
        engine_.activeSceneIndex());
    if (playlist == nullptr || targetItem == nullptr
        || (mode == ActiveProjectionRebuildMode::RestoreCurrentItem
            && activeItemId != targetItemId))
    {
        return ActiveProjectionRebuildResult::Unchanged;
    }

    auto preparedCandidate = DesktopSceneEngine::Player::prepareActivePlaybackProjection(
        *playlist,
        targetItem->id);
    if (!preparedCandidate.has_value())
    {
        if (mode == ActiveProjectionRebuildMode::StartTargetItem)
        {
            stopActivePlayback();
            return ActiveProjectionRebuildResult::Stopped;
        }

        return ActiveProjectionRebuildResult::Unchanged;
    }

    float playbackTime = 0.0F;
    float playbackSpeed = 1.0F;
    bool wasPaused = false;
    if (mode == ActiveProjectionRebuildMode::RestoreCurrentItem)
    {
        playbackTime = engine_.playbackTime();
        playbackSpeed = engine_.playbackSpeed();
        wasPaused = engine_.isPaused();
        const std::size_t previousSceneIndex = engine_.activeSceneIndex();
        if (previousSceneIndex >= activePlaybackProjection_->project.scenes.size())
        {
            return ActiveProjectionRebuildResult::Unchanged;
        }
    }

    if (autonomousMediaPreparation_)
    {
        if (!authoredRebuildCapture_) return ActiveProjectionRebuildResult::Unchanged;
        authoredRebuildCapture_->emplace(AuthoredRebuildPlan{std::move(preparedCandidate->projection),
            targetItemId, mode, playbackTime, playbackSpeed, wasPaused});
        return ActiveProjectionRebuildResult::Deferred;
    }

    cancelTimelineDrag();

    auto oldProjection = std::move(activePlaybackProjection_);
    renewRuntimeBinding();
    engine_.detachProject();
    activePlaybackProjection_ = std::move(preparedCandidate->projection);

    const auto restoreBoundPlayback = [this, playbackSpeed, playbackTime, wasPaused]()
    {
        if (wasPaused)
        {
            // bindProject starts a configured scene playing. Pause before speed
            // or seek restoration so their audio submissions remain paused.
            engine_.togglePause();
        }
        engine_.setPlaybackSpeed(playbackSpeed);
        const bool restoredTime = engine_.seek(playbackTime);
        updateEffectiveOutputVolume();
        synchronizeActivePlaybackItem();
        refreshStatus();
        return restoredTime;
    };

    const DesktopSceneEngine::Domain::EntityId targetSceneId =
        activePlaybackProjection_->project.scenes[
            preparedCandidate->targetSceneIndex].id;
    if (engine_.bindProject(activePlaybackProjection_->project, targetSceneId))
    {
        if (mode == ActiveProjectionRebuildMode::StartTargetItem)
        {
            updateEffectiveOutputVolume();
            synchronizeActivePlaybackItem();
            refreshStatus();
            return ActiveProjectionRebuildResult::StartedTarget;
        }

        return restoreBoundPlayback()
            ? ActiveProjectionRebuildResult::Restored
            : ActiveProjectionRebuildResult::ReboundAtStart;
    }

    engine_.detachProject();
    activePlaybackProjection_.reset();

    if (mode == ActiveProjectionRebuildMode::StartTargetItem)
    {
        updateEffectiveOutputVolume();
        refreshPlaylistList();
        refreshItemList();
        refreshStatus();
        return ActiveProjectionRebuildResult::Stopped;
    }

    activePlaybackProjection_ = std::move(oldProjection);

    const auto rollbackSceneIndex = activePlaybackProjection_->sceneIndexForItem(targetItemId);
    if (!rollbackSceneIndex.has_value())
    {
        stopActivePlayback();
        refreshPlaylistList();
        refreshItemList();
        refreshStatus();
        return ActiveProjectionRebuildResult::Stopped;
    }

    const DesktopSceneEngine::Domain::EntityId rollbackSceneId =
        activePlaybackProjection_->project.scenes[*rollbackSceneIndex].id;
    renewRuntimeBinding();
    if (!engine_.bindProject(activePlaybackProjection_->project, rollbackSceneId))
    {
        stopActivePlayback();
        refreshPlaylistList();
        refreshItemList();
        refreshStatus();
        return ActiveProjectionRebuildResult::Stopped;
    }

    (void)restoreBoundPlayback();
    return ActiveProjectionRebuildResult::Unchanged;
}

void SessionApplication::renewRuntimeBinding()
{
    runtimeBindingToken_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    cancelTimelineDrag();
    invalidateTimelineHoverPreview();
    cancelRuntimeHover();
}

void SessionApplication::stopActivePlayback() noexcept
{
    rollbackPresentation_.reset();
    if (activePlaybackProjection_.has_value() || engine_.sceneCount() != 0U)
        renewRuntimeBinding();
    cancelTimelineDrag();
    invalidateTimelineHoverPreview();
    invalidateTimelinePreviewPreloadTarget();
    if (activePlaybackProjection_.has_value()
        || workspaceModel_.activePlaylistId().isValid())
    {
        (void)forwardBackgroundMusicControl(
            DesktopSceneEngine::BackgroundMusicControl::PlaybackStopped{});
    }
    engine_.detachProject();
    activePlaybackProjection_.reset();
    playbackSequence_.clear();
    workspaceModel_.clearActivePlayback();
    synchronizeBackgroundMusicPlaybackDisplay();
    synchronizeActivePlaybackVolumeCluster();
    updateEffectiveOutputVolume();
    publishRuntimePlayback(true);
}

void SessionApplication::synchronizeActivePlaybackItem()
{
    if (!activePlaybackProjection_.has_value())
    {
        invalidateTimelineHoverPreview();
        invalidateTimelinePreviewPreloadTarget();
        return;
    }

    view(SessionViewChange::ReconcileTimelineSource);

    reconcileTimelinePreviewPreloadTarget();

    const auto itemId = activePlaybackProjection_->itemForSceneIndex(
        engine_.activeSceneIndex());
    if (!itemId.has_value() || workspaceModel_.activeItemId() == itemId)
    {
        return;
    }

    if (workspaceModel_.setActiveItem(*itemId))
    {
        updateEffectiveOutputVolume();
        forwardBackgroundMusicScopeChange();
        refreshPlaylistList();
        refreshItemList();
    }
}

bool SessionApplication::navigateScene(bool previous)
{
    if (!activePlaybackProjection_.has_value())
    {
        refreshStatus();
        return false;
    }

    std::optional<AudioEffectToken> ownedEffect;
    if (!activeAudioCapture_)
    {
        ownedEffect = beginNativeAudioEffect(AudioEffectScope::Structural, activePlaybackProjection_->sourcePlaylistId, activeTimelineItemId());
        if (!ownedEffect) return false;
    }
    bool logicalAccepted = false;
    struct FinishOwnedNavigation
    {
        std::function<void()> finish;
        ~FinishOwnedNavigation() { if (finish) finish(); }
    } finishOwned{[&] {
        if (ownedEffect) (void)finishNativeAudioEffect(*ownedEffect, logicalAccepted);
    }};

    cancelTimelineDrag();
    if (!previous)
    {
        const bool advanced = advancePlaybackSequence();
        logicalAccepted = advanced || !activePlaybackProjection_;
        refreshStatus();
        return advanced;
    }

    const PlaybackSequence::Collections collections = playbackSequenceCollections();
    synchronizePlaybackSequenceConfiguration(collections);
    if (!playbackSequence_.current().has_value())
    {
        rebasePlaybackSequenceToActivePosition();
    }

    const auto previousPosition = playbackSequence_.current();
    bool applied = false;
    if (previousPosition.has_value())
    {
        const PlaybackSequence::Result result = playbackSequence_.previous(collections);
        if (result.kind == DesktopSceneEngine::Player::PlaybackSequenceResultKind::Identity
            && result.identity.has_value())
        {
            applied = transitionToSequencePosition(*result.identity);
            if (!applied)
                playbackSequence_.rebase(collections, previousPosition->playlistId, previousPosition->itemId);
        }
    }

    refreshStatus();
    logicalAccepted = applied;
    return applied;
}

DesktopSceneEngine::Player::DocumentCommandResult SessionApplication::beginDocumentOperation(
    DesktopSceneEngine::Player::DocumentAction action)
{
    using DesktopSceneEngine::Player::DocumentCommandStatus;
    if (!documentCommandsPrepared_ || documentClosePermitted_ || closing()
        || closePending() || QThread::currentThread() != thread())
        return {DocumentCommandStatus::Unavailable, {}};
    if (pendingDocumentOperation_) return {DocumentCommandStatus::Busy, pendingDocumentOperation_->token};
    if (pendingMediaImport_) return {DocumentCommandStatus::Busy, pendingMediaImport_->token};
    if (authoredOperationExecuting_ || (pendingRuntimeIntent_ && action != DocumentAction::Close))
        return {DocumentCommandStatus::Busy, {}};
    const auto token = QUuid::createUuid();
    pendingDocumentOperation_ = PendingDocumentOperation{token, action};
    if (action == DocumentAction::Close) closeState_ = CloseState::Deciding;
    view(SessionViewChange::DiscardDocumentDrafts);
    lastDocumentOperation_ = {};
    publishReadOnlyInvalidation();
    // The root owns this continuation. No WebView callback remains on a native
    // picker's nested event loop, so Host recreation cannot strand the work.
    QTimer::singleShot(0, this, [this, token] { executeDocumentOperation(token); });
    return {DocumentCommandStatus::Accepted, token};
}

DesktopSceneEngine::Player::DocumentCommandResult SessionApplication::continueDocumentOperation(
    const QUuid& token, DesktopSceneEngine::Player::DocumentChoice choice)
{
    using DesktopSceneEngine::Player::DocumentCommandStatus;
    if (!documentCommandsPrepared_ || documentClosePermitted_ || closing() || QThread::currentThread() != thread())
        return {DocumentCommandStatus::Unavailable, {}};
    if (token.isNull() || !pendingDocumentOperation_ || pendingDocumentOperation_->token != token
        || pendingDocumentOperation_->stage != DocumentStage::Decision)
        return {DocumentCommandStatus::StaleOperation, {}};
    pendingDocumentOperation_->stage = DocumentStage::Executing;
    publishReadOnlyInvalidation();
    QTimer::singleShot(0, this, [this, token, choice] { executeDocumentChoice(token, choice); });
    return {DocumentCommandStatus::Accepted, token};
}

QJsonObject SessionApplication::documentTransactionSnapshot() const
{
    if (!documentCommandsPrepared_) return {};
    QJsonValue pending{QJsonValue::Null};
    if (pendingDocumentOperation_)
    {
        const auto& operation = *pendingDocumentOperation_;
        QString stage;
        switch (operation.stage)
        {
        case DocumentStage::Queued: stage = QStringLiteral("queued"); break;
        case DocumentStage::OpenPicker: stage = QStringLiteral("open-picker"); break;
        case DocumentStage::SavePicker: stage = QStringLiteral("save-picker"); break;
        case DocumentStage::Decision: stage = QStringLiteral("decision"); break;
        case DocumentStage::Executing: stage = QStringLiteral("executing"); break;
        }
        pending = QJsonObject{{"token", operation.token.toString(QUuid::WithoutBraces)},
            {"action", documentActionName(operation.action)}, {"stage", stage}};
    }
    return {{"pending", pending}, {"last", lastDocumentOperation_.isEmpty()
        ? QJsonValue{QJsonValue::Null} : QJsonValue{lastDocumentOperation_}}};
}

bool SessionApplication::prepareInternalDocumentProof(const QString& absoluteProjectPath)
{
    if (!documentCommandsPrepared_ || !engineInitialized_
        || !workspaceModel_.playlists().isEmpty() || documentState_.currentPath) return false;
    const QFileInfo file{absoluteProjectPath};
    if (!file.isAbsolute() || !file.isFile()) return false;
    QString error;
    auto candidate = loadProjectCandidate(std::filesystem::path{file.absoluteFilePath().toStdWString()}, &error);
    if (!candidate || candidate->project.playlists.empty())
    {
        qWarning().noquote() << "[DocumentProof] startup candidate rejected" << error;
        return false;
    }
    commitOpenProject(std::move(*candidate));
    const auto playlist = workspaceModel_.playlists().constFirst();
    if (!workspaceModel_.renamePlaylist(playlist.id, playlist.displayAlias + QStringLiteral(" \u2014 unsaved proof")))
    {
        // This unpublished startup seed must never fall into a Save prompt on
        // failed launch; no user command or file write has occurred.
        documentState_.markClean(workspaceModel_.authoredRevision());
        updateDocumentTitle();
        return false;
    }
    updateDocumentTitle();
    qInfo().noquote() << "[DocumentProof] native dirty startup proof ready" << file.absoluteFilePath();
    return true;
}

bool SessionApplication::prepareInternalAuthoredProof(const QString& absoluteProjectPath)
{
    if (!authoredCommandsPrepared_ || !prepareInternalDocumentProof(absoluteProjectPath)) return false;
    // This startup-only proof uses the existing workflow, without exposing a
    // playback capability through B3's Bridge or replaying it on page reload.
    PlaylistId activePlaylist;
    std::optional<ItemId> activeItem;
    for (const auto& playlist : workspaceModel_.playlists())
    {
        activeItem = workspaceModel_.firstPlayableItem(playlist.id);
        if (activeItem) { activePlaylist = playlist.id; break; }
    }
    if (!activeItem) return false;
    const auto effect = beginNativeAudioEffect(AudioEffectScope::Structural, activePlaylist, activeItem);
    if (!effect) return false;
    if (!startPlaylistPlayback(activePlaylist, activeItem, false, false))
    {
        (void)finishNativeAudioEffect(*effect, false);
        return false;
    }
    if (!engine_.isPaused()) engine_.togglePause();
    (void)finishNativeAudioEffect(*effect);
    for (const auto& playlist : workspaceModel_.playlists())
    {
        if (playlist.id != activePlaylist)
        {
            if (!playlist.items.isEmpty()) (void)workspaceModel_.selectItem(playlist.items.constFirst().id);
            else (void)workspaceModel_.selectPlaylist(playlist.id);
            refreshLibrary();
            return true;
        }
    }
    const auto* playlist = workspaceModel_.playlist(activePlaylist);
    for (const auto& item : playlist->items)
        if (item.id != *activeItem) { (void)workspaceModel_.selectItem(item.id); break; }
    refreshLibrary();
    return true;
}

void SessionApplication::finishDocumentOperation(const QUuid& token, const QString& outcome, const QString& message)
{
    if (!pendingDocumentOperation_ || pendingDocumentOperation_->token != token) return;
    const bool closeAborted = pendingDocumentOperation_->action == DocumentAction::Close
        && outcome != QStringLiteral("completed") && closeState_ == CloseState::Deciding;
    const bool externalSaveFailed = closeAborted && pendingDocumentOperation_->externalClose
        && outcome == QStringLiteral("save-failed") && platform_;
    lastDocumentOperation_ = {{"token", token.toString(QUuid::WithoutBraces)},
        {"action", documentActionName(pendingDocumentOperation_->action)},
        {"outcome", outcome}, {"message", externalSaveFailed ? QString{} : message}};
    pendingDocumentOperation_.reset(); // Retire the token and any native candidate immediately.
    documentResourceDispatch_ = {};
    deferredDocumentContinuation_ = {};
    publishReadOnlyInvalidation();
    if (closeAborted) abortClose(externalSaveFailed ? std::optional{message} : std::nullopt);
}

void SessionApplication::saveDocumentForOperation(const QUuid& token, bool saveAs, bool replacing)
{
    if (!pendingDocumentOperation_ || pendingDocumentOperation_->token != token) return;
    if (!saveAs && documentState_.currentPath)
    {
        completeDocumentSave(token, documentState_.currentPath, false, replacing);
        return;
    }
    if (!platform_ && !documentPickerForTesting_)
    {
        deferredDocumentContinuation_ = [this, token, saveAs, replacing] {
            saveDocumentForOperation(token, saveAs, replacing);
        };
        return;
    }
    pendingDocumentOperation_->stage = DocumentStage::SavePicker;
    publishReadOnlyInvalidation();
    if (documentPickerForTesting_)
    {
        completeDocumentSave(token, documentPickerForTesting_(true), true, replacing);
        return;
    }
    startPlatformRequest(token, replacing ? PlatformRequestKind::SaveThenReplace : PlatformRequestKind::Save);
}

void SessionApplication::completeDocumentSave(const QUuid& token,
    std::optional<std::filesystem::path> path, bool adoptPath, bool replacing)
{
    requireExecutor();
    if (!pendingDocumentOperation_ || pendingDocumentOperation_->token != token
        || (adoptPath && pendingDocumentOperation_->stage != DocumentStage::SavePicker)) return;
    if (!path)
    {
        finishDocumentOperation(token, replacing ? QStringLiteral("save-cancelled")
            : QStringLiteral("cancelled"));
        return;
    }
    pendingDocumentOperation_->stage = DocumentStage::Executing;
    publishReadOnlyInvalidation();
    if (autonomousRuntime_)
    {
        saveDocumentOnResourceThread(token, std::move(*path), adoptPath, replacing);
        return;
    }
    QString error;
    const auto result = saveProjectToPath(*path, adoptPath, &error);
    completeDocumentSaveResult(token, result, replacing, error);
}

void SessionApplication::completeDocumentSaveResult(const QUuid& token, SaveResult result,
    bool replacing, const QString& error)
{
    if (replacing && result == SaveResult::Saved)
        completeDocumentReplacement(token, DestructiveDecision::ProceedAfterSave);
    else if (replacing)
        finishDocumentOperation(token, QStringLiteral("save-failed"), error);
    else
        finishDocumentOperation(token, result == SaveResult::Saved ? QStringLiteral("saved") : QStringLiteral("failed"), error);
}

bool SessionApplication::consumeDocumentResourceResult(const QUuid& token, const QUuid& incarnation,
    const QUuid& dispatch)
{
    requireExecutor();
    if (!pendingDocumentOperation_ || pendingDocumentOperation_->token != token
        || pendingDocumentOperation_->stage != DocumentStage::Executing
        || documentIncarnation_ != incarnation || documentResourceDispatch_ != dispatch) return false;
    documentResourceDispatch_ = {};
    return true;
}

void SessionApplication::saveDocumentOnResourceThread(const QUuid& token, std::filesystem::path path,
    bool adoptPath, bool replacing)
{
    requireExecutor();
    Q_ASSERT(documentResourceDispatch_.isNull());
    const auto dispatch = documentResourceDispatch_ = QUuid::createUuid();
    const auto incarnation = documentIncarnation_;
    const auto revision = workspaceModel_.authoredRevision();
    const auto complete = [this, token, incarnation, dispatch, revision, path, adoptPath, replacing](
        std::optional<QString> failure) {
        if (!consumeDocumentResourceResult(token, incarnation, dispatch)) return;
        if (failure)
        {
            const auto message = tr("Unable to save project:\n%1\n\n%2")
                .arg(QString::fromStdWString(path.wstring()), *failure);
            completeDocumentSaveResult(token, SaveResult::Failed, replacing, message);
            return;
        }
        if (adoptPath) documentState_.currentPath = path;
        documentState_.markClean(revision);
        updateDocumentTitle();
        // Admission normally forbids concurrent authored changes. A
        // destructive continuation must never discard a newer revision.
        if (replacing && documentState_.isDirty(workspaceModel_.authoredRevision()))
        {
            completeDocumentSaveResult(token, SaveResult::Failed, true,
                tr("The project changed while saving. The current document was kept."));
            return;
        }
        completeDocumentSaveResult(token, SaveResult::Saved, replacing, {});
    };
    try
    {
        if (!fileResources_) fileResources_ = std::make_unique<FileResources>();
        auto work = [project = workspaceModel_.authoredProject(), path, adoptPath]() -> std::optional<QString> {
            try { persistDocument(project, path, adoptPath); }
            catch (const std::exception& error) { return QString::fromUtf8(error.what()); }
            return std::nullopt;
        };
        if (fileResources_->submit(*this, std::move(work), complete)) return;
    }
    catch (const std::exception& error)
    {
        complete(QString::fromUtf8(error.what()));
        return;
    }
    complete(tr("The project file operation could not be scheduled."));
}

void SessionApplication::readDocumentOnResourceThread(const QUuid& token, std::filesystem::path path,
    bool replacementRead)
{
    requireExecutor();
    Q_ASSERT(documentResourceDispatch_.isNull());
    struct ReadResult
    {
        std::optional<Domain::AuthoredProject> project;
        QString failure;
    };
    const auto dispatch = documentResourceDispatch_ = QUuid::createUuid();
    const auto incarnation = documentIncarnation_;
    const auto complete = [this, token, incarnation, dispatch, path, replacementRead](ReadResult result) {
        if (!consumeDocumentResourceResult(token, incarnation, dispatch)) return;
        if (!result.project)
        {
            finishDocumentOperation(token, QStringLiteral("candidate-invalid"),
                tr("Unable to open project:\n%1\n\n%2")
                    .arg(QString::fromStdWString(path.wstring()), result.failure));
            return;
        }
        if (result.project->displayOwner.displayId() != displaySelection_.boundDisplayId())
        {
            finishDocumentOperation(token, QStringLiteral("candidate-invalid"),
                tr("The project belongs to a different display session."));
            return;
        }
        OpenProjectCandidate candidate{std::move(*result.project), path};
        if (replacementRead)
        {
            commitOpenProject(std::move(candidate));
            finishDocumentOperation(token, QStringLiteral("completed"));
            return;
        }
        pendingDocumentOperation_->candidate = std::move(candidate);
        decideDocumentReplacement(token);
    };
    try
    {
        if (!fileResources_) fileResources_ = std::make_unique<FileResources>();
        if (fileResources_->submit(*this, [path] {
            try { return ReadResult{Infrastructure::Persistence::loadAuthoredProjectFile(path), {}}; }
            catch (const std::exception& error) { return ReadResult{std::nullopt, QString::fromUtf8(error.what())}; }
        }, complete)) return;
    }
    catch (const std::exception& error)
    {
        complete({std::nullopt, QString::fromUtf8(error.what())});
        return;
    }
    complete({std::nullopt, tr("The project file operation could not be scheduled.")});
}

void SessionApplication::executeDocumentOperation(const QUuid& token)
{
    using DesktopSceneEngine::Player::DocumentAction;
    if (!pendingDocumentOperation_ || pendingDocumentOperation_->token != token
        || pendingDocumentOperation_->stage != DocumentStage::Queued) return;
    const auto action = pendingDocumentOperation_->action;
    const bool needsPicker = action == DocumentAction::Open || action == DocumentAction::SaveAs
        || (action == DocumentAction::Save && !documentState_.currentPath);
    if (needsPicker && !platform_ && !documentPickerForTesting_) return;
    if (action == DocumentAction::Save || action == DocumentAction::SaveAs)
    {
        saveDocumentForOperation(token, action == DocumentAction::SaveAs, false);
        return;
    }
    if (action == DocumentAction::Open)
    {
        pendingDocumentOperation_->stage = DocumentStage::OpenPicker;
        publishReadOnlyInvalidation();
        if (documentPickerForTesting_)
            completeOpenDocumentPicker(token, documentPickerForTesting_(false));
        else
            startPlatformRequest(token, PlatformRequestKind::Open);
        return;
    }
    decideDocumentReplacement(token);
}

void SessionApplication::completeOpenDocumentPicker(const QUuid& token, std::optional<std::filesystem::path> path)
{
    requireExecutor();
    if (!pendingDocumentOperation_ || pendingDocumentOperation_->token != token
        || pendingDocumentOperation_->stage != DocumentStage::OpenPicker) return;
    if (!path) { finishDocumentOperation(token, QStringLiteral("cancelled")); return; }
    if (autonomousRuntime_)
    {
        pendingDocumentOperation_->stage = DocumentStage::Executing;
        publishReadOnlyInvalidation();
        readDocumentOnResourceThread(token, std::move(*path), false);
        return;
    }
    QString error;
    auto candidate = loadProjectCandidate(*path, &error);
    if (!candidate) { finishDocumentOperation(token, QStringLiteral("candidate-invalid"), error); return; }
    pendingDocumentOperation_->candidate = std::move(candidate);
    decideDocumentReplacement(token);
}

void SessionApplication::decideDocumentReplacement(const QUuid& token)
{
    if (documentState_.isDirty(workspaceModel_.authoredRevision()))
    {
        pendingDocumentOperation_->stage = DocumentStage::Decision;
        publishReadOnlyInvalidation();
        return;
    }
    completeDocumentReplacement(token, DestructiveDecision::ProceedAfterDiscard);
}

void SessionApplication::executeDocumentChoice(const QUuid& token, DesktopSceneEngine::Player::DocumentChoice choice)
{
    using DesktopSceneEngine::Player::DocumentChoice;
    if (!pendingDocumentOperation_ || pendingDocumentOperation_->token != token
        || pendingDocumentOperation_->stage != DocumentStage::Executing) return;
    if (choice == DocumentChoice::Cancel)
    {
        finishDocumentOperation(token, QStringLiteral("cancelled"));
        return;
    }
    if (choice == DocumentChoice::Discard)
    {
        completeDocumentReplacement(token, DestructiveDecision::ProceedAfterDiscard);
        return;
    }
    saveDocumentForOperation(token, false, true);
}

void SessionApplication::completeDocumentReplacement(const QUuid& token, DestructiveDecision decision)
{
    using DesktopSceneEngine::Player::DocumentAction;
    if (!pendingDocumentOperation_ || pendingDocumentOperation_->token != token) return;
    pendingDocumentOperation_->stage = DocumentStage::Executing;
    publishReadOnlyInvalidation();
    const auto action = pendingDocumentOperation_->action;
    if (action == DocumentAction::New) createNewDocument();
    else if (action == DocumentAction::Open)
    {
        if (autonomousRuntime_ && decision == DestructiveDecision::ProceedAfterSave
            && pendingDocumentOperation_->candidate)
        {
            // The Save may have replaced the candidate's own path. Re-read
            // the bytes off control before mutating the current document.
            readDocumentOnResourceThread(token, pendingDocumentOperation_->candidate->path, true);
            return;
        }
        QString error;
        // Preserve the production post-Save reread even when Save overwrote
        // the candidate's own path. A read candidate is never a committed Open.
        if (!pendingDocumentOperation_->candidate || !completeOpenProject(
                std::move(*pendingDocumentOperation_->candidate), decision, &error))
        {
            finishDocumentOperation(token, QStringLiteral("candidate-invalid"), error);
            return;
        }
    }
    if (action == DocumentAction::Close)
    {
        documentClosePermitted_ = true;
        beginCloseRetirement(decision); // The same token remains Pending through real retirement.
        return;
    }
    finishDocumentOperation(token, QStringLiteral("completed"));
}

AuthoredResult SessionApplication::applyAuthoredOperation(const AuthoredRequest& request)
{
    AuthoredResult result;
    if (QThread::currentThread() != thread()) return result;
    result.playlistId = request.playlistId;
    result.itemId = request.itemId;
    result.authoredRevision = workspaceModel_.authoredRevision();
    result.dirty = documentState_.isDirty(result.authoredRevision);
    if (!authoredCommandsPrepared_ || !engineInitialized_ || documentClosePermitted_
        || closing() || closePending()
        || QThread::currentThread() != thread()) return result;
    if (pendingDocumentOperation_ || pendingMediaImport_ || authoredOperationExecuting_ || pendingRuntimeIntent_)
    {
        result.status = AuthoredStatus::Busy;
        return result;
    }
    if (request.action != AuthoredAction::AddMedia)
    {
        return executeAuthoredOperation(request);
    }
    if (request.expectedRevision && *request.expectedRevision != workspaceModel_.authoredRevision())
    {
        result.status = AuthoredStatus::StaleRevision;
        return result;
    }
    if (!workspaceModel_.playlist(request.playlistId))
    {
        result.status = AuthoredStatus::NotFound;
        return result;
    }
    result.token = QUuid::createUuid();
    result.status = AuthoredStatus::Accepted;
    result.authored = AuthoredOutcome::NotApplicable;
    pendingMediaImport_ = PendingMediaImport{result.token, documentIncarnation_, VisualMediaImportTarget{request.playlistId}};
    lastMediaImport_.reset();
    publishReadOnlyInvalidation();
    QTimer::singleShot(0, this, [this, token = result.token] { executeMediaImport(token); });
    return result;
}

QUuid SessionApplication::issueBgmEditPermit(PlaylistId playlistId)
{
    if (QThread::currentThread() != thread() || !authoredCommandsPrepared_ || !engineInitialized_
        || documentIncarnation_.isNull() || documentClosePermitted_ || closing()
        || closePending() || !workspaceModel_.playlist(playlistId)) return {};
    const auto permit = QUuid::createUuid();
    bgmEditPermits_.insert(permit, BgmEditPermit{documentIncarnation_, playlistId});
    return permit;
}

void SessionApplication::revokeBgmEditPermit(const QUuid& permit)
{
    if (QThread::currentThread() == thread()) bgmEditPermits_.remove(permit);
}

AuthoredStatus SessionApplication::validateBgmTarget(const DesktopSceneEngine::Player::BgmTarget& target) const
{
    using namespace DesktopSceneEngine::Player;
    const auto* playlist = workspaceModel_.playlist(bgmPlaylistId(target));
    if (!playlist) return AuthoredStatus::NotFound;
    if (const auto item = bgmItemId(target))
    {
        if (!workspaceModel_.item(*item)) return AuthoredStatus::NotFound;
        if (std::none_of(playlist->items.cbegin(), playlist->items.cend(),
            [&](const auto& candidate) { return candidate.id == *item; })) return AuthoredStatus::WrongParent;
    }
    return AuthoredStatus::Accepted;
}

AuthoredResult SessionApplication::applyBgmAuthoredOperation(const DesktopSceneEngine::Player::BgmAuthoredRequest& request)
{
    using namespace DesktopSceneEngine::Player;
    AuthoredResult result;
    if (QThread::currentThread() != thread()) return result;
    result.playlistId = bgmPlaylistId(request.target);
    result.itemId = bgmItemId(request.target);
    result.bgmTarget = request.target;
    result.bgmTrackId = request.trackId;
    result.authoredRevision = workspaceModel_.authoredRevision();
    result.dirty = documentState_.isDirty(result.authoredRevision);
    if (request.action == BgmAuthoredAction::AddTracks) result.bgmImport = BgmImportOutcome::RejectedBeforePicker;
    if (!authoredCommandsPrepared_ || !engineInitialized_ || documentIncarnation_.isNull()
        || documentClosePermitted_ || closing() || closePending()) return result;
    const auto permit = bgmEditPermits_.constFind(request.permit);
    if (request.permit.isNull() || permit == bgmEditPermits_.cend()
        || permit->documentIncarnation != documentIncarnation_ || permit->playlistId != result.playlistId)
    {
        result.status = AuthoredStatus::RevokedPermit;
        return result;
    }
    if (pendingDocumentOperation_ || pendingMediaImport_ || authoredOperationExecuting_ || pendingRuntimeIntent_)
    {
        result.status = AuthoredStatus::Busy;
        return result;
    }
    if (request.expectedRevision && *request.expectedRevision != result.authoredRevision)
    {
        result.status = AuthoredStatus::StaleRevision;
        return result;
    }
    result.status = validateBgmTarget(request.target);
    if (result.status != AuthoredStatus::Accepted) return result;
    if (request.action != BgmAuthoredAction::AddTracks)
    {
        return executeBgmMutation(request);
    }
    result.token = QUuid::createUuid();
    result.authored = AuthoredOutcome::NotApplicable;
    result.bgmImport.reset(); // Admission is not completion or authored success.
    const auto target = std::visit([](const auto& value) -> MediaImportTarget { return value; }, request.target);
    pendingMediaImport_ = PendingMediaImport{result.token, documentIncarnation_, target};
    lastMediaImport_.reset();
    publishReadOnlyInvalidation();
    QTimer::singleShot(0, this, [this, token = result.token] { executeMediaImport(token); });
    return result;
}

AuthoredResult SessionApplication::executeBgmMutation(const DesktopSceneEngine::Player::BgmAuthoredRequest& request)
{
    requireExecutor();
    using namespace DesktopSceneEngine::Player;
    namespace Domain = DesktopSceneEngine::Domain;
    AuthoredResult result;
    if (closing() || closePending() || pendingDocumentOperation_ || pendingMediaImport_
        || authoredOperationExecuting_ || pendingRuntimeIntent_)
    { result.status = closing() ? AuthoredStatus::Unavailable : AuthoredStatus::Busy; return result; }
    QScopedValueRollback<bool> executing{authoredOperationExecuting_, true};
    std::optional<AudioEffectToken> effect;
    result.playlistId = bgmPlaylistId(request.target);
    result.itemId = bgmItemId(request.target);
    result.bgmTarget = request.target;
    result.bgmTrackId = request.trackId;
    const auto before = workspaceModel_.authoredRevision();
    const auto finish = [&]() {
        if (effect)
        {
            result.audioEffect = finishNativeAudioEffect(*effect, result.authored != AuthoredOutcome::Rejected);
            const auto phase = result.authored == AuthoredOutcome::Rejected ? QString{} : result.audioEffect->value("phase").toString();
            if (phase == "pending" || phase == "preparing")
            {
                result.runtimeOnApplied = result.runtime;
                result.runtime = RuntimeOutcome::Pending;
            }
            else if (phase == "failed") result.runtime = RuntimeOutcome::Failed;
            else if (phase == "cancelled") result.runtime = RuntimeOutcome::Cancelled;
            else if (phase == "deferred-unavailable") result.runtime = RuntimeOutcome::DeferredUnavailable;
        }
        result.authoredRevision = workspaceModel_.authoredRevision();
        result.dirty = documentState_.isDirty(result.authoredRevision);
        return result;
    };
    const auto reject = [&](AuthoredStatus reason) { result.status = reason; return finish(); };
    const auto validation = validateBgmTarget(request.target);
    if (validation != AuthoredStatus::Accepted) return reject(validation);
    const auto* queue = result.itemId ? &workspaceModel_.authoredItem(*result.itemId)->musicQueue
        : &workspaceModel_.authoredPlaylist(result.playlistId)->globalMusicQueue;
    const bool trackAction = request.action == BgmAuthoredAction::SetTrackVolume
        || request.action == BgmAuthoredAction::RemoveTrack || request.action == BgmAuthoredAction::MoveTrack;
    auto track = queue->tracks.cend();
    if (trackAction)
    {
        if (!request.trackId || !request.trackId->isValid()) return reject(AuthoredStatus::TrackNotFound);
        track = std::find_if(queue->tracks.cbegin(), queue->tracks.cend(),
            [&](const auto& value) { return value.id == *request.trackId; });
        if (track == queue->tracks.cend()) return reject(AuthoredStatus::TrackNotFound);
    }
    const auto playerTrack = request.trackId ? TrackId{QUuid{QString::fromStdString(request.trackId->value())}} : TrackId{};
    bool accepted = false;
    bool boundary = false;
    MusicPlaybackMode mode = MusicPlaybackMode::ListLoop;
    const bool active = activePlaybackProjection_ && activePlaybackProjection_->sourcePlaylistId == result.playlistId
        && (!result.itemId || activeTimelineItemId() == result.itemId);
    if (active)
    {
        const bool scalar = request.action == BgmAuthoredAction::SetQueueVolume || request.action == BgmAuthoredAction::SetTrackVolume;
        const auto scope = scalar ? (result.itemId ? AudioEffectScope::ItemBgmGain : AudioEffectScope::PlaylistBgmGain)
            : AudioEffectScope::Structural;
        effect = beginNativeAudioEffect(scope, result.playlistId, result.itemId);
        if (!effect) return reject(AuthoredStatus::Busy);
    }
    switch (request.action)
    {
    case BgmAuthoredAction::SetEnabled:
        accepted = result.itemId ? workspaceModel_.setItemMusicEnabled(*result.itemId, request.enabled)
            : workspaceModel_.setPlaylistMusicEnabled(result.playlistId, request.enabled);
        break;
    case BgmAuthoredAction::SetQueueMode:
        switch (request.mode)
        {
        case Domain::MusicPlaybackMode::CurrentSingleLoop: mode = MusicPlaybackMode::CurrentSingleLoop; break;
        case Domain::MusicPlaybackMode::ListLoop: mode = MusicPlaybackMode::ListLoop; break;
        case Domain::MusicPlaybackMode::Shuffle: mode = MusicPlaybackMode::Shuffle; break;
        default: return reject(AuthoredStatus::InvalidValue);
        }
        accepted = result.itemId ? workspaceModel_.setItemMusicPlaybackMode(*result.itemId, mode)
            : workspaceModel_.setPlaylistMusicPlaybackMode(result.playlistId, mode);
        break;
    case BgmAuthoredAction::SetQueueVolume:
    case BgmAuthoredAction::SetTrackVolume:
        if (!std::isfinite(request.value) || request.value < 0.0 || request.value > 1.0)
            return reject(AuthoredStatus::InvalidValue);
        if (request.action == BgmAuthoredAction::SetQueueVolume)
            accepted = result.itemId ? workspaceModel_.setItemMusicVolume(*result.itemId, request.value)
                : workspaceModel_.setPlaylistMusicVolume(result.playlistId, request.value);
        else accepted = result.itemId ? workspaceModel_.setItemMusicTrackVolume(*result.itemId, playerTrack, request.value)
            : workspaceModel_.setPlaylistMusicTrackVolume(result.playlistId, playerTrack, request.value);
        break;
    case BgmAuthoredAction::RemoveTrack:
        accepted = result.itemId ? workspaceModel_.removeItemMusicTrack(*result.itemId, playerTrack)
            : workspaceModel_.removePlaylistMusicTrack(result.playlistId, playerTrack);
        break;
    case BgmAuthoredAction::MoveTrack:
        if (request.direction != -1 && request.direction != 1) return reject(AuthoredStatus::InvalidValue);
        boundary = (request.direction == -1 && track == queue->tracks.cbegin())
            || (request.direction == 1 && std::next(track) == queue->tracks.cend());
        accepted = boundary || (result.itemId ? workspaceModel_.moveItemMusicTrack(*result.itemId, playerTrack, request.direction)
            : workspaceModel_.movePlaylistMusicTrack(result.playlistId, playerTrack, request.direction));
        break;
    default: return reject(AuthoredStatus::InvalidValue);
    }
    if (!accepted) return reject(AuthoredStatus::InvalidValue);
    // Read only the affected native field/order; never replace a queue snapshot
    // from a requester and never call Engine configuration/switch APIs in C2.
    BgmAuthoredReadback readback;
    if (request.action == BgmAuthoredAction::SetEnabled) readback.enabled = queue->enabled;
    if (request.action == BgmAuthoredAction::SetQueueMode) readback.mode = queue->playbackMode;
    if (request.action == BgmAuthoredAction::SetQueueVolume) readback.queueVolume = queue->volume;
    if (request.action == BgmAuthoredAction::SetTrackVolume)
        readback.trackVolume = std::find_if(queue->tracks.cbegin(), queue->tracks.cend(),
            [&](const auto& value) { return value.id == *request.trackId; })->volume;
    if (request.action == BgmAuthoredAction::RemoveTrack || request.action == BgmAuthoredAction::MoveTrack)
    {
        readback.order.emplace();
        for (const auto& value : queue->tracks) readback.order->push_back(value.id);
    }
    result.bgmReadback = std::move(readback);
    const bool changed = workspaceModel_.authoredRevision() != before;
    result.status = changed ? AuthoredStatus::Applied : AuthoredStatus::NoChange;
    result.authored = changed ? AuthoredOutcome::Committed : AuthoredOutcome::Unchanged;
    if (changed)
    {
        result.runtime = synchronizeBackgroundMusicConfig(result.playlistId, result.itemId);
        if (result.runtime == RuntimeOutcome::Failed)
            result.warnings.push_back({QStringLiteral("bgm-config-sync-failed"),
                QStringLiteral("Authored BGM configuration was committed; native runtime synchronization failed.")});
    }
    const bool scalar = request.action == BgmAuthoredAction::SetQueueVolume || request.action == BgmAuthoredAction::SetTrackVolume;
    if (scalar)
    {
        if (platform_) platform_->acknowledgeSessionRevision(before, false);
        view(SessionViewChange::ScalarReadSnapshot);
    }
    else publishReadOnlyInvalidation();
    return finish();
}

AuthoredResult SessionApplication::addBgmSources(const DesktopSceneEngine::Player::BgmTarget& target, const QStringList& sources)
{
    using namespace DesktopSceneEngine::Player;
    AuthoredResult result;
    result.playlistId = bgmPlaylistId(target);
    result.itemId = bgmItemId(target);
    result.bgmTarget = target;
    result.bgmImport = BgmImportOutcome::RejectedAtCompletion;
    result.authoredRevision = workspaceModel_.authoredRevision();
    result.dirty = documentState_.isDirty(result.authoredRevision);
    result.status = validateBgmTarget(target);
    if (result.status != AuthoredStatus::Accepted) return result;
    if (sources.isEmpty())
    {
        result.status = AuthoredStatus::PickerCancelled;
        result.authored = AuthoredOutcome::NotApplicable;
        result.bgmImport = BgmImportOutcome::Cancelled;
        return result;
    }
    const auto before = result.authoredRevision;
    const bool active = activePlaybackProjection_ && activePlaybackProjection_->sourcePlaylistId == result.playlistId
        && (!result.itemId || activeTimelineItemId() == result.itemId);
    const auto effect = active ? beginNativeAudioEffect(AudioEffectScope::Structural, result.playlistId, result.itemId) : std::nullopt;
    if (active && !effect) { result.status = AuthoredStatus::Busy; return result; }
    for (const auto& source : sources)
    {
        const auto id = result.itemId ? workspaceModel_.addItemMusicTrack(*result.itemId, source)
            : workspaceModel_.addPlaylistMusicTrack(result.playlistId, source);
        BgmSourceResult file{source};
        if (id)
        {
            file.trackId = DesktopSceneEngine::Domain::TrackId{id->value.toString(QUuid::WithoutBraces).toStdString()};
            file.outcome = MediaSourceOutcome::Committed;
        }
        else file.outcome = isSupportedBackgroundMusicSource(source) ? MediaSourceOutcome::Rejected : MediaSourceOutcome::Unsupported;
        result.bgmSources.push_back(std::move(file));
    }
    result.authoredRevision = workspaceModel_.authoredRevision();
    result.dirty = documentState_.isDirty(result.authoredRevision);
    const bool changed = before != result.authoredRevision;
    result.status = changed ? AuthoredStatus::Applied : AuthoredStatus::NoChange;
    result.authored = changed ? AuthoredOutcome::Committed : AuthoredOutcome::Unchanged;
    result.bgmImport = BgmImportOutcome::Completed;
    if (changed)
    {
        // Multi-file import commits references individually, then reconciles
        // the final canonical queue once. Cancel/rejected/no-op never sync.
        result.runtime = synchronizeBackgroundMusicConfig(result.playlistId, result.itemId);
        if (result.runtime == RuntimeOutcome::Failed)
            result.warnings.push_back({QStringLiteral("bgm-config-sync-failed"),
                QStringLiteral("Authored BGM references were committed; native runtime synchronization failed.")});
    }
    if (effect)
    {
        result.audioEffect = finishNativeAudioEffect(*effect, changed);
        const auto phase = changed ? result.audioEffect->value("phase").toString() : QString{};
        if (phase == "pending" || phase == "preparing") { result.runtimeOnApplied = result.runtime; result.runtime = RuntimeOutcome::Pending; }
        else if (phase == "failed") result.runtime = RuntimeOutcome::Failed;
        else if (phase == "cancelled") result.runtime = RuntimeOutcome::Cancelled;
        else if (phase == "deferred-unavailable") result.runtime = RuntimeOutcome::DeferredUnavailable;
    }
    return result;
}

DesktopSceneEngine::Player::BgmRuntimeResult SessionApplication::bgmRuntimeState(
    const QUuid& permitId, const DesktopSceneEngine::Player::BgmTarget& target) const
{
    using namespace DesktopSceneEngine::Player;
    namespace Observation = DesktopSceneEngine::BackgroundMusicObservation;
    BgmRuntimeResult result;
    if (QThread::currentThread() != thread()) return result;
    const_cast<DesktopSceneEngine::Engine&>(engine_).pollAudioExecution();
    if (!runtimeCommandsPrepared_ || !authoredCommandsPrepared_ || !engineInitialized_ || documentIncarnation_.isNull()
        || documentClosePermitted_ || closing() || closePending()) return result;
    const auto permit = bgmEditPermits_.constFind(permitId);
    if (permitId.isNull() || permit == bgmEditPermits_.cend() || permit->documentIncarnation != documentIncarnation_
        || permit->playlistId != bgmPlaylistId(target))
    {
        result.reason = BgmRuntimeRejectReason::RevokedPermit;
        return result;
    }
    const auto valid = validateBgmTarget(target);
    if (valid != AuthoredStatus::Accepted)
    {
        result.reason = valid == AuthoredStatus::WrongParent ? BgmRuntimeRejectReason::WrongParent : BgmRuntimeRejectReason::TargetMissing;
        return result;
    }
    const auto observed = engine_.backgroundMusicObservation();
    BgmRuntimeState state;
    state.target = target;
    state.playbackState = observed.playbackState;
    state.activePlaylistId = observed.activePlaylistId;
    state.activeItemId = observed.activeItemId;
    const auto* playlist = workspaceModel_.authoredPlaylist(bgmPlaylistId(target));
    const bool activePlaylist = observed.activePlaylistId && observed.activePlaylistId->value == playlist->id.value();
    if (const auto itemId = bgmItemId(target))
    {
        state.scope = Observation::ScopeSnapshot{Observation::Scope::Item};
        const auto* item = workspaceModel_.authoredItem(*itemId);
        if (activePlaylist && observed.activeItemId && observed.activeItemId->value == item->id.value()) state.scope = observed.item;
    }
    else if (activePlaylist) state.scope = observed.playlist;
    // Inactive editor hydration has no other owner's Current/failure attached.
    result.status = BgmRuntimeCommandStatus::Ready;
    result.reason = BgmRuntimeRejectReason::None;
    result.state = std::move(state);
    return result;
}

DesktopSceneEngine::Player::BgmRuntimeResult SessionApplication::useBgmTrack(
    const QUuid& permit, const DesktopSceneEngine::Player::BgmTarget& target,
    const DesktopSceneEngine::Domain::TrackId& trackId)
{
    using namespace DesktopSceneEngine::Player;
    auto result = bgmRuntimeState(permit, target);
    if (result.status != BgmRuntimeCommandStatus::Ready) return result;
    const auto reject = [&](BgmRuntimeRejectReason reason) {
        result.status = BgmRuntimeCommandStatus::Rejected;
        result.reason = reason;
        return result;
    };
    if (pendingDocumentOperation_ || pendingMediaImport_ || authoredOperationExecuting_)
        return reject(BgmRuntimeRejectReason::Busy);
    const auto playlistId = bgmPlaylistId(target);
    const auto itemId = bgmItemId(target);
    const auto* playlist = workspaceModel_.authoredPlaylist(playlistId);
    const auto* item = itemId ? workspaceModel_.authoredItem(*itemId) : nullptr;
    const auto& queue = item ? item->musicQueue : playlist->globalMusicQueue;
    if (!trackId.isValid() || std::none_of(queue.tracks.cbegin(), queue.tracks.cend(),
        [&](const auto& track) { return track.id == trackId; })) return reject(BgmRuntimeRejectReason::TrackMissing);
    if (!queue.enabled) return reject(BgmRuntimeRejectReason::Disabled);
    if (!result.state->scope.active || !activePlaybackProjection_
        || activePlaybackProjection_->sourcePlaylistId != playlistId || (itemId && activeTimelineItemId() != itemId))
        return reject(BgmRuntimeRejectReason::Inactive);
    if (!result.state->scope.exists || !result.state->scope.switchEligible)
        return reject(BgmRuntimeRejectReason::NotSwitchable);
    if (result.state->scope.currentTrackId && result.state->scope.currentTrackId->value == trackId.value())
    {
        result.status = BgmRuntimeCommandStatus::NoChange;
        return result;
    }
    const auto publicPlaylist = makeBackgroundMusicPlaylistId(playlist->id);
    const auto publicTrack = makeBackgroundMusicTrackId(trackId);
    const auto publicItem = item ? makeBackgroundMusicItemId(item->id) : std::nullopt;
    if (!publicPlaylist || !publicTrack || (item && !publicItem)) return reject(BgmRuntimeRejectReason::TargetMissing);
    const auto effect = beginNativeAudioEffect(AudioEffectScope::Structural, playlistId, itemId);
    if (!effect) return reject(BgmRuntimeRejectReason::Busy);
    QScopedValueRollback<bool> executing{authoredOperationExecuting_, true};
    const bool accepted = item
        ? forwardBackgroundMusicControl(DesktopSceneEngine::BackgroundMusicControl::ItemMusicTrackSwitchRequested{
            *publicPlaylist, *publicItem, *publicTrack})
        : forwardBackgroundMusicControl(DesktopSceneEngine::BackgroundMusicControl::PlaylistMusicTrackSwitchRequested{
            *publicPlaylist, *publicTrack});
    const auto receipt = finishNativeAudioEffect(*effect, accepted);
    // Control admission is not a playback ACK. Failure traversal may have
    // selected a different track; only C1 post-control facts supply Current.
    auto after = bgmRuntimeState(permit, target);
    if (after.status != BgmRuntimeCommandStatus::Ready) return after;
    after.status = accepted ? BgmRuntimeCommandStatus::Admitted : BgmRuntimeCommandStatus::Rejected;
    after.reason = accepted ? BgmRuntimeRejectReason::None : BgmRuntimeRejectReason::ControlRejected;
    after.audioEffect = receipt;
    if (accepted) synchronizeBackgroundMusicPlaybackDisplay();
    return after;
}

QJsonObject SessionApplication::runtimePlaybackSnapshot() const
{
    if (QThread::currentThread() != thread())
        throw std::logic_error("Runtime playback must be observed on the native control thread");
    const auto clock = engine_.playbackClockSample();
    const auto index = engine_.activeSceneIndex();
    const auto count = engine_.sceneCount();
    const auto item = activeTimelineItemId();
    const bool hasMedia = engineInitialized_ && activePlaybackProjection_.has_value()
        && item.has_value() && count > 0U && index < count;
    const bool available = engineInitialized_ && !closing()
        && !closePending() && !pendingDocumentOperation_
        && !pendingMediaImport_ && !authoredOperationExecuting_;
    const double duration = hasMedia && std::isfinite(clock.duration) ? (std::max)(0.0, clock.duration) : 0.0;
    const double position = hasMedia && std::isfinite(clock.position) ? (std::max)(0.0, clock.position) : 0.0;
    QJsonValue playlistId{QJsonValue::Null}, itemId{QJsonValue::Null}, media{QJsonValue::Null};
    if (hasMedia)
    {
        playlistId = activePlaybackProjection_->sourcePlaylistId.value.toString(QUuid::WithoutBraces);
        itemId = item->value.toString(QUuid::WithoutBraces);
        if (engine_.mediaKind() == DesktopSceneEngine::Engine::MediaKind::Image) media = QStringLiteral("image");
        else if (engine_.mediaKind() == DesktopSceneEngine::Engine::MediaKind::Video) media = QStringLiteral("video");
    }
    const bool transport = available && hasMedia;
    bool canPrevious = false;
    if (transport)
    {
        // Ask the same native controller using a value copy so querying
        // eligibility cannot consume Previous history or random state.
        auto sequence = playbackSequence_;
        const auto collections = playbackSequenceCollections();
        if (sequence.playlistMode() != workspaceModel_.playlistPlaybackMode())
            sequence.setPlaylistMode(workspaceModel_.playlistPlaybackMode(), collections);
        sequence.synchronizeItemMode(collections);
        if (!sequence.current()) sequence.rebase(collections, activePlaybackProjection_->sourcePlaylistId, *item);
        const auto previous = sequence.previous(collections);
        canPrevious = previous.kind == DesktopSceneEngine::Player::PlaybackSequenceResultKind::Identity
            && previous.identity.has_value();
    }
    return {{"bindingToken", runtimeBindingToken_},
        {"asOfEffectSequence", QString::number(clock.asOfEffectSequence)}, {"asOfTimeNs", QString::number(clock.asOfTimeNs)},
        {"active", QJsonObject{{"playlistId", playlistId}, {"itemId", itemId}}},
        {"hasMedia", hasMedia}, {"state", hasMedia && clock.rate > 0.0 ? (clock.paused ? "paused" : "playing") : "unavailable"},
        {"position", position}, {"duration", duration},
        {"actualRate", std::isfinite(clock.rate) ? clock.rate : 0.0},
        {"outputMuted", clock.outputMuted}, {"canPlayPause", transport}, {"canPrevious", canPrevious},
        {"canNext", transport}, {"canSeek", transport && duration > 0.0}, {"canMute", available},
        {"sceneIndex", hasMedia ? static_cast<qint64>(index) : -1}, {"sceneCount", static_cast<qint64>(count)},
        {"mediaKind", media}};
}

DesktopSceneEngine::Player::RuntimeResult SessionApplication::applyRuntimeOperation(
    const DesktopSceneEngine::Player::RuntimeRequest& request)
{
    using DesktopSceneEngine::Player::RuntimeAction;
    std::optional<AudioEffectToken> effect;
    const auto reserve = [&](AudioEffectScope scope) {
        effect = beginNativeAudioEffect(scope, PlaylistId{request.playlistId},
            request.itemId.isNull() ? std::nullopt : std::optional<ItemId>{ItemId{request.itemId}});
        return effect.has_value();
    };
    const auto finish = [this, &effect](QString status, const QString& message = QString{}) {
        std::optional<QJsonObject> receipt;
        if (effect)
        {
            const bool committed = status == "applied";
            receipt = finishNativeAudioEffect(*effect, committed);
            const auto phase = receipt->value("phase").toString();
            if (committed && (phase == "pending" || phase == "preparing")) status = QStringLiteral("pending");
            else if (committed && (phase == "failed" || phase == "cancelled" || phase == "deferred-unavailable")) status = phase;
        }
        publishRuntimePlayback(true);
        return DesktopSceneEngine::Player::RuntimeResult{status, message, runtimePlaybackSnapshot(), receipt};
    };
    if (QThread::currentThread() != thread())
        throw std::logic_error("Runtime playback commands require the native control thread");
    if (!runtimeCommandsPrepared_ || !engineInitialized_ || closing()
        || closePending())
        return finish(QStringLiteral("unavailable"), tr("Playback is unavailable."));
    if (pendingDocumentOperation_ || pendingMediaImport_ || authoredOperationExecuting_)
        return finish(QStringLiteral("unavailable"), tr("A document or media operation is active."));

    if (request.action == RuntimeAction::PlayPlaylist || request.action == RuntimeAction::PlayItem)
    {
        const PlaylistId playlistId{request.playlistId};
        const auto* playlist = workspaceModel_.playlist(playlistId);
        if (!playlist) return finish(QStringLiteral("rejected"), tr("The Playlist no longer exists."));
        std::optional<ItemId> itemId;
        if (request.action == RuntimeAction::PlayItem)
        {
            itemId = ItemId{request.itemId};
            const auto member = std::find_if(playlist->items.cbegin(), playlist->items.cend(),
                [&](const auto& item) { return item.id == *itemId; });
            if (member == playlist->items.cend())
                return finish(QStringLiteral("rejected"), tr("The Item does not belong to this Playlist."));
        }
        if (!playlist->enabled) return finish(QStringLiteral("unplayable"), tr("The Playlist is disabled."));
        if (playlist->items.isEmpty()) return finish(QStringLiteral("empty"), tr("The Playlist is empty."));
        if ((itemId && !workspaceModel_.isItemPlayable(playlistId, *itemId))
            || !workspaceModel_.isPlaylistPlayable(playlistId))
            return finish(QStringLiteral("unplayable"), tr("There is no playable Item at this target."));
        QString failure;
        if (!reserve(AudioEffectScope::Structural)) return finish("busy", tr("An audio effect is pending."));
        const bool started = startPlaylistPlayback(playlistId, itemId, true, false, &failure);
        return finish(started ? QStringLiteral("applied") : failure,
            started ? QString{} : failure == QStringLiteral("prepare-failed")
                ? tr("Unable to prepare the playback sequence.") : tr("Media could not be bound for playback."));
    }

    if (request.action == RuntimeAction::ToggleOutputMute)
    {
        if (!reserve(AudioEffectScope::OutputMute)) return finish("busy", tr("An audio effect is pending."));
        engine_.toggleMute();
        refreshStatus();
        return finish(QStringLiteral("applied"));
    }

    const auto sample = runtimePlaybackSnapshot();
    if (request.action == RuntimeAction::Seek)
    {
        // A same-Item restart is a new binding even when IDs and duration match.
        if (request.bindingToken != runtimeBindingToken_
            || request.itemId != QUuid{sample.value("active").toObject().value("itemId").toString()})
            return finish(QStringLiteral("stale"), tr("The active playback binding changed."));
        if (request.value < 0 || request.value > DesktopSceneEngine::Player::TimelineSliderMaximum
            || !std::isfinite(request.duration) || request.duration <= 0.0)
            return finish(QStringLiteral("rejected"), tr("The timeline target is invalid."));
        if (!sample.value("canSeek").toBool())
            return finish(QStringLiteral("unavailable"), tr("The active media has no seekable timeline."));
        if (request.duration != sample.value("duration").toDouble())
            return finish(QStringLiteral("stale"), tr("The active playback duration changed."));
        if (!reserve(AudioEffectScope::Structural)) return finish("busy", tr("An audio effect is pending."));
        if (DesktopSceneEngine::Player::isTimelineEndIntent(request.value))
        {
            const bool applied = navigateScene(false);
            return finish(applied ? QStringLiteral("applied") : QStringLiteral("failed"),
                applied ? QString{} : tr("The native sequence could not advance."));
        }
        const bool applied = engine_.seek(DesktopSceneEngine::Player::timelineTimeForValue(
            request.value, engine_.effectiveDuration()));
        refreshStatus();
        return finish(applied ? QStringLiteral("applied") : QStringLiteral("failed"),
            applied ? QString{} : tr("The Engine could not seek this media."));
    }
    if (!sample.value("hasMedia").toBool())
        return finish(QStringLiteral("unavailable"), tr("No active media is loaded."));
    if (!reserve(AudioEffectScope::Structural)) return finish("busy", tr("An audio effect is pending."));
    switch (request.action)
    {
    case RuntimeAction::PlayPause: togglePlaybackPause(); break;
    case RuntimeAction::Previous:
        if (!sample.value("canPrevious").toBool())
            return finish(QStringLiteral("unavailable"), tr("There is no previous playback target."));
        if (!navigateScene(true)) return finish(QStringLiteral("failed"), tr("The previous media could not be bound."));
        break;
    case RuntimeAction::Next:
        if (!navigateScene(false)) return finish(QStringLiteral("failed"), tr("The native sequence could not advance."));
        break;
    default: return finish(QStringLiteral("rejected"), tr("The playback operation is invalid."));
    }
    return finish(QStringLiteral("applied"));
}

bool SessionApplication::prepareInternalPreviewMedia(const QString& absolutePath)
{
    if (!engineInitialized_
        || !workspaceModel_.playlists().isEmpty()) return false;
    const QFileInfo file(absolutePath);
    if (!file.isAbsolute() || !file.isFile()) return false;
    const auto media = mediaTypeForSource(file.absoluteFilePath());
    if (!media) return false;
    const bool image = *media == DesktopSceneEngine::Player::WorkspaceMediaType::Image;
    const auto failedSeed = [this] {
        // This explicit startup seed has never been a user-editable document.
        // Failed setup must not enter a Save transaction when startup closes.
        documentState_.markClean(workspaceModel_.authoredRevision());
        updateDocumentTitle();
        return false;
    };
    const auto playlist = workspaceModel_.addPlaylist();
    if (!workspaceModel_.renamePlaylist(playlist, QStringLiteral("Preview feasibility"))) return failedSeed();
    const auto item = workspaceModel_.addItem(playlist, *media, file.absoluteFilePath(), file.fileName());
    if (!item || (image && !workspaceModel_.setImageDisplayDuration(*item, 3600.0))) return failedSeed();
    if (!startPlaylistPlayback(playlist, *item, true, false))
    {
        qWarning() << "[NativePreview] media-prepare-failed" << file.absoluteFilePath();
        return failedSeed();
    }
    documentState_.markClean(workspaceModel_.authoredRevision());
    updateDocumentTitle();
    qInfo() << "[NativePreview] internal real media prepared" << file.absoluteFilePath();
    return true;
}

void SessionApplication::requestDesktopPresentationReconciliation()
{
    if (!engineInitialized_) return;
    desktopReconciliationContinuationTimer_.stop();
    if (desktopEndpointReconciliationQueued_) return;
    desktopEndpointReconciliationQueued_ = true;
    QMetaObject::invokeMethod(this, [this]
    {
        if (!desktopEndpointReconciliationQueued_) return;
        desktopEndpointReconciliationQueued_ = false;
        if (!engineInitialized_) return;
        (void)reconcileDesktopPresentation(true);
    }, Qt::QueuedConnection);
}

void SessionApplication::runDesktopPresentationContinuation()
{
    desktopReconciliationContinuationTimer_.stop();
    if (!engineInitialized_ || !desktopDeploymentDesired_
        || desktopEndpointReconciliationQueued_ || desktopReplacementInProgress_)
        return;
    (void)reconcileDesktopPresentation(false);
}

void SessionApplication::cancelDesktopPresentationReconciliation() noexcept
{
    desktopEndpointReconciliationQueued_ = false;
    desktopReconciliationContinuationTimer_.stop();
}

std::optional<ItemId> SessionApplication::activeTimelineItemId() const noexcept
{
    if (!activePlaybackProjection_.has_value())
    {
        return std::nullopt;
    }

    return activePlaybackProjection_->itemForSceneIndex(engine_.activeSceneIndex());
}

std::optional<DesktopSceneEngine::Player::TimelinePreviewSource>
SessionApplication::activeTimelinePreviewSource() const
{
    if (!activePlaybackProjection_.has_value())
    {
        return std::nullopt;
    }

    return DesktopSceneEngine::Player::timelinePreviewSourceForActiveScene(
        workspaceModel_,
        *activePlaybackProjection_,
        engine_.activeSceneIndex());
}

const DesktopSceneEngine::Domain::Scene* SessionApplication::activeTimelinePreviewScene() const noexcept
{
    if (!activePlaybackProjection_.has_value())
    {
        return nullptr;
    }

    const std::size_t sceneIndex = engine_.activeSceneIndex();
    return sceneIndex < activePlaybackProjection_->project.scenes.size()
        ? &activePlaybackProjection_->project.scenes[sceneIndex]
        : nullptr;
}

void SessionApplication::updateEffectiveOutputVolume()
{
    if (!engineInitialized_)
    {
        return;
    }

    engine_.setVolume(static_cast<float>(
        DesktopSceneEngine::Player::calculateEffectiveVolume(
            static_cast<double>(activePlaylistMasterVolume()),
            activeItemMediaVolume())));
}

PlaylistId SessionApplication::volumeControlPlaylistId() const noexcept
{
    return activePlaybackProjection_.has_value()
        ? activePlaybackProjection_->sourcePlaylistId
        : PlaylistId{};
}

float SessionApplication::currentPlaylistMasterVolume() const noexcept
{
    const auto* playlist = workspaceModel_.playlist(volumeControlPlaylistId());
    if (playlist == nullptr)
    {
        return 1.0F;
    }

    return std::clamp(static_cast<float>(playlist->masterVolume), 0.0F, 1.0F);
}

float SessionApplication::activePlaylistMasterVolume() const noexcept
{
    if (!activePlaybackProjection_.has_value())
    {
        return 1.0F;
    }

    const auto* playlist = workspaceModel_.playlist(
        activePlaybackProjection_->sourcePlaylistId);
    if (playlist == nullptr)
    {
        return 1.0F;
    }

    return std::clamp(static_cast<float>(playlist->masterVolume), 0.0F, 1.0F);
}

double SessionApplication::activeItemMediaVolume() const noexcept
{
    const auto itemId = activeTimelineItemId();
    if (!itemId.has_value())
    {
        return 1.0;
    }

    const auto* item = workspaceModel_.item(*itemId);
    if (item == nullptr || item->mediaType != WorkspaceMediaType::Video
        || !std::isfinite(item->itemMediaVolume))
    {
        return 1.0;
    }

    return std::clamp(item->itemMediaVolume, 0.0, 1.0);
}

}
