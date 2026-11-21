#include "SessionTestContext.h"
#include "MainWindowTestDisplay.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QThread>
#include <QtCore/qt_windows.h>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

using namespace DesktopSceneEngine;
using namespace DesktopSceneEngine::Player;

namespace
{
int assertions = 0;
void require(bool value, const char* message)
{
    ++assertions;
    if (!value) throw std::runtime_error(message);
}

void until(const std::function<bool()>& predicate)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (!predicate())
    {
        QCoreApplication::processEvents();
        if (elapsed.elapsed() > 5000) throw std::runtime_error("Application continuation did not settle");
        QThread::msleep(1);
    }
}

// This adapter records presentation effects and returns picker choices. It
// creates no window, renderer, Engine, document, or admitted operation.
class HeadlessPlatform final : public SessionPlatform
{
public:
    SessionPlatformFacts facts;
    std::optional<std::filesystem::path> pickedPath;
    SessionUnsavedChoice choice = SessionUnsavedChoice::Cancel;
    int documentPicks = 0;
    int mediaPicks = 0;
    int mediaCompletions = 0;
    int prompts = 0;
    int detachments = 0;
    int teardownRequests = 0;
    int finalCloseRequests = 0;
    int acceptedCloses = 0;
    int cancelledCloses = 0;
    int engineInitializationRequests = 0;
    int surfacedCloseDecisions = 0;
    bool deferDocumentResult = false;
    bool deferMediaResult = false;
    bool deferLanguageApplied = false;
    SessionPlatformRequest requestedDocument;
    SessionPlatformRequest requestedMedia;
    SessionPlatformRequest retiringRequest;
    DocumentPathResult documentPathResult;
    MediaSourcesResult mediaSourcesResult;
    RequestRetired acknowledgeRetirement;
    DesktopResult desktopResult;
    DisplayId requestedDesktop;
    std::optional<SessionDesktopFrame> requestedDesktopFrame;
    int desktopRequests = 0;
    std::function<void()> duringDocumentPick;
    QUuid completedMedia;
    std::vector<std::pair<UiLanguage, LanguageApplied>> languageRequests;
    int preferenceWarnings = 0;
    std::optional<CanvasOutsideMode> detachedOutside;

    SessionPlatformFacts sessionPlatformFacts() const override { return facts; }
    void observePresentationFacts(SessionPlatformFacts observed) override { facts = observed; }
    std::optional<std::filesystem::path> pickSessionDocumentPath(bool,
        const std::optional<std::filesystem::path>&) override
    {
        ++documentPicks;
        if (duringDocumentPick) duringDocumentPick();
        return pickedPath;
    }
    void requestSessionDocumentPath(const SessionPlatformRequest& identity, bool save,
        const std::optional<std::filesystem::path>& current, DocumentPathResult complete) override
    {
        if (!deferDocumentResult)
        {
            SessionPlatform::requestSessionDocumentPath(identity, save, current, std::move(complete));
            return;
        }
        ++documentPicks;
        requestedDocument = identity;
        documentPathResult = std::move(complete);
    }
    QStringList pickSessionMediaSources(bool) override { return {}; }
    void requestSessionMediaSources(const SessionPlatformRequest& identity, bool backgroundMusic,
        MediaSourcesResult complete) override
    {
        if (!deferMediaResult)
        {
            SessionPlatform::requestSessionMediaSources(identity, backgroundMusic, std::move(complete));
            return;
        }
        ++mediaPicks;
        requestedMedia = identity;
        mediaSourcesResult = std::move(complete);
    }
    SessionUnsavedChoice promptSessionUnsavedChanges(const QString&) override
    {
        ++prompts;
        return choice;
    }
    void surfaceSessionCloseDecision() override { ++surfacedCloseDecisions; }
    SessionPlatformRetirement retireSessionPlatformRequest(const SessionPlatformRequest& identity,
        RequestRetired acknowledge) noexcept override
    {
        if (!deferDocumentResult && !deferMediaResult)
            return SessionPlatformRetirement::AwaitResult;
        retiringRequest = identity;
        acknowledgeRetirement = std::move(acknowledge);
        return SessionPlatformRetirement::AwaitAcknowledgment;
    }
    void applySessionLanguage(UiLanguage language, LanguageApplied complete) override
    {
        if (deferLanguageApplied) languageRequests.emplace_back(language, std::move(complete));
        else SessionPlatform::applySessionLanguage(language, std::move(complete));
    }
    void showSessionNotice(SessionNotice kind, const QString& title, const QString&) override
    {
        if (kind == SessionNotice::Warning && title == "Desktop Background") ++preferenceWarnings;
    }
    void sessionViewChanged(SessionViewChange, bool) override {}
    void acknowledgeSessionRevision(std::uint64_t, bool) override {}
    void refreshSessionBgmWindow(PlaylistId) override {}
    void closeSessionBgmWindow(PlaylistId) override {}
    void closeAllSessionBgmWindows() override {}
    void completeSessionMediaImport(const QUuid& token) override
    {
        ++mediaCompletions;
        completedMedia = token;
    }
    bool initializeSessionPresentation(Engine&) override
    {
        ++engineInitializationRequests;
        return false;
    }
    void detachSessionPresentation(Engine&) noexcept override { ++detachments; }
    void prepareSessionEngineShutdown(Engine&) noexcept override {}
    void finishSessionEngineShutdown() noexcept override {}
    bool sessionRuntimePresentationReady() const override { return false; }
    void renderSession(Engine&, const Domain::AspectRatio&, CanvasOutsideMode) override {}
    void publishDetachedFrame(PresentationInput, const QString&, CanvasOutsideMode outside,
        std::optional<Engine::MediaPreparationToken>) override { detachedOutside = outside; }
    SessionDesktopAttempt deploySessionDesktop(Engine&, const DisplayId&) override
    {
        return SessionDesktopAttempt::Failed;
    }
    bool removeSessionDesktop(Engine&) noexcept override { return true; }
    bool reconcileSessionDesktop(Engine&, const DisplayId&) override { return false; }
    bool requestDetachedDesktop(DisplayId display, std::optional<SessionDesktopFrame> frame,
        DesktopResult complete) override
    {
        ++desktopRequests;
        requestedDesktop = std::move(display);
        requestedDesktopFrame = std::move(frame);
        desktopResult = std::move(complete);
        return true;
    }
    void beginSessionPresentationTeardown() override { ++teardownRequests; }
    void requestSessionFinalClose() override { ++finalCloseRequests; }
    void sessionCloseCompleted(bool accepted) override
    {
        accepted ? ++acceptedCloses : ++cancelledCloses;
    }
};

void verifyIdentityAndAuthority()
{
    Engine firstEngine, secondEngine;
    ApplicationTranslationState language;
    SessionApplication first(firstEngine, TestSupport::mainWindowTestDisplayId(), language);
    SessionApplication second(secondEngine, DisplayId::fromTargetDevicePath("CP2_OTHER_SESSION"), language);
    require(first.thread() == QThread::currentThread(), "Application stays on the existing control executor");
    require(&first.presentationEngine() == &firstEngine && &second.presentationEngine() == &secondEngine,
        "Each explicit composition borrows exactly its external Engine");
    require(&first.workspace() != &second.workspace() && &first.document() != &second.document(),
        "Each application owns separate canonical model and document storage");
    require(first.documentIncarnation() != second.documentIncarnation()
        && first.bindingToken() != second.bindingToken(), "Application identities are independent");
    require(first.workspace().playlists().isEmpty() && second.workspace().playlists().isEmpty()
        && !first.document().currentPath && !first.document().isDirty(first.workspace().authoredRevision()),
        "Headless application construction establishes the same clean empty Session");

    auto& model = SessionApplicationTestAccess::model(first);
    const auto playlist = model.addPlaylist();
    const auto other = model.addPlaylist();
    require(first.selectPlaylist(playlist) && first.workspace().selectedPlaylistId() == playlist,
        "Canonical selection runs directly through the application command");
    require(first.selectPlaylist(other) && second.workspace().playlists().isEmpty(),
        "Application commands cannot mutate another Session");
    require(!first.initializeEngine() && !firstEngine.isInitialized(),
        "Headless construction never invents a native rendering endpoint");

    first.setPresentationCapabilities(true, true, true);
    DocumentCommandResult foreign;
    std::thread otherExecutor([&] { foreign = first.beginDocumentOperation(DocumentAction::Save); });
    otherExecutor.join();
    require(foreign.status == DocumentCommandStatus::Unavailable && !first.pendingDocumentOperation(),
        "Foreign executor cannot admit canonical document work");

    bool rejected = false;
    try
    {
        SessionApplication invalid(firstEngine, DisplayId::fromSourceDeviceName("\\\\.\\DISPLAY-WEAK"), language);
    }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Persistent display ownership is enforced by the application constructor");
}

void verifyContinuationSurvivesAdapterReplacement()
{
    QTemporaryDir directory;
    require(directory.isValid(), "Headless workflow has an owned temporary directory");
    Engine engine;
    ApplicationTranslationState language;
    HeadlessPlatform original, replacement;
    SessionApplication application(engine, TestSupport::mainWindowTestDisplayId(), language);
    application.setPresentationCapabilities(true, true, true);
    application.attachPlatform(original);
    bool duplicateRejected = false;
    try { application.attachPlatform(replacement); }
    catch (const std::logic_error&) { duplicateRejected = true; }
    require(duplicateRejected, "One application admits only one native adapter at a time");

    auto& model = SessionApplicationTestAccess::model(application);
    const auto playlist = model.addPlaylist();
    require(model.renamePlaylist(playlist, "Retained application document"), "Fixture authors a real document");
    const auto* workspace = &application.workspace();
    const auto* document = &application.document();
    const auto* activeProjection = &application.activeProjection();
    const auto documentIncarnation = application.documentIncarnation();
    const auto binding = application.bindingToken();
    const auto revision = model.authoredRevision();
    const auto admitted = application.beginDocumentOperation(DocumentAction::New);
    require(admitted.status == DocumentCommandStatus::Accepted && !admitted.token.isNull(),
        "Application admits and owns the document operation before queue delivery");
    application.detachPlatform(original);
    require(original.detachments == 1 && application.pendingDocumentOperation()
        && application.pendingDocumentOperation()->token == admitted.token,
        "Adapter detach preserves the admitted application token");
    application.attachPlatform(replacement);
    until([&] { return application.pendingDocumentOperation()
        && application.pendingDocumentOperation()->stage == SessionApplication::DocumentStage::Decision; });
    require(&application.workspace() == workspace && &application.document() == document
        && &application.activeProjection() == activeProjection && application.documentIncarnation() == documentIncarnation
        && application.bindingToken() == binding && application.workspace().authoredRevision() == revision,
        "Adapter replacement cannot reset canonical storage, identities, revision, or the operation");
    const auto wrong = application.continueDocumentOperation(QUuid::createUuid(), DocumentChoice::Discard);
    require(wrong.status == DocumentCommandStatus::StaleOperation
        && application.pendingDocumentOperation()->token == admitted.token,
        "Replacement adapter cannot replace an operation with an invented token");
    require(application.continueDocumentOperation(admitted.token, DocumentChoice::Cancel).status
        == DocumentCommandStatus::Accepted, "Existing operation remains continuable after adapter replacement");
    until([&] { return !application.pendingDocumentOperation(); });
    require(application.document().isDirty(revision) && application.workspace().authoredRevision() == revision,
        "Cancel preserves canonical authored content and dirty truth");

    application.requestClose(true);
    require(application.pendingDocumentOperation() && replacement.surfacedCloseDecisions == 1,
        "An admitted tray Close surfaces a decision without a native prompt");
    (void)application.continueDocumentOperation(application.pendingDocumentOperation()->token, DocumentChoice::Cancel);
    until([&] { return !application.pendingDocumentOperation(); });
    require(application.closeState() == SessionApplication::CloseState::Open && replacement.prompts == 0
        && replacement.cancelledCloses == 1 && replacement.teardownRequests == 0,
        "Cancelled Close returns canonical authority to Open before any presentation teardown");

    const auto savedPath = std::filesystem::path{directory.filePath("owned-session.dse.json").toStdWString()};
    replacement.pickedPath = savedPath;
    const auto save = application.beginDocumentOperation(DocumentAction::SaveAs);
    require(save.status == DocumentCommandStatus::Accepted,
        "Application admits Save As using the replacement adapter only for picker input");
    application.detachPlatform(replacement);
    QCoreApplication::processEvents();
    require(application.pendingDocumentOperation() && application.pendingDocumentOperation()->token == save.token
        && application.pendingDocumentOperation()->stage == SessionApplication::DocumentStage::Queued
        && replacement.documentPicks == 0 && application.workspace().authoredRevision() == revision,
        "Picker-dependent work remains admitted while its native adapter is absent");
    application.attachPlatform(replacement);
    until([&] { return !application.pendingDocumentOperation(); });
    const auto loaded = application.loadProjectCandidate(savedPath);
    require(replacement.documentPicks == 1 && loaded && loaded->project.playlists.size() == 1
        && loaded->project.playlists.front().displayAlias == "Retained application document"
        && application.document().currentPath == savedPath
        && !application.document().isDirty(application.workspace().authoredRevision()),
        "Application owns actual atomic persistence, loaded content, path adoption, and clean revision");

    application.requestClose(true);
    require(application.closeState() == SessionApplication::CloseState::Retiring && replacement.teardownRequests == 1,
        "Accepted Close first enters canonical retirement");
    until([&] { application.pollNativeAudioEffects(); return application.finalClosePermitted(); });
    require(engine.audioShutdownComplete() && replacement.finalCloseRequests == 1
        && replacement.acceptedCloses == 0, "Real Engine retirement completes before the platform can accept Close");
    application.completePlatformClose(true);
    application.completePlatformClose(true);
    require(application.closeState() == SessionApplication::CloseState::Accepted
        && replacement.acceptedCloses == 1 && !application.pendingDocumentOperation(),
        "Platform acceptance completes the same canonical Close operation exactly once");
    require(original.engineInitializationRequests == 0 && replacement.engineInitializationRequests == 0,
        "Headless ownership and workflow proof constructs no rendering endpoint");
    application.detachPlatform(replacement);
}

void verifyNativeCloseDecisionSurvivesAdapterAbsence()
{
    Engine engine;
    ApplicationTranslationState language;
    HeadlessPlatform original, replacement;
    SessionApplication application(engine, TestSupport::mainWindowTestDisplayId(), language);
    application.setPresentationCapabilities(true, true, true);
    application.attachPlatform(original);
    auto& model = SessionApplicationTestAccess::model(application);
    (void)model.addPlaylist();
    const auto revision = model.authoredRevision();
    application.requestClose(false);
    require(application.pendingDocumentOperation().has_value(), "Window Close admits one application token");
    const auto token = application.pendingDocumentOperation()->token;
    application.detachPlatform(original);
    QCoreApplication::processEvents();
    require(application.pendingDocumentOperation() && application.pendingDocumentOperation()->token == token
        && application.closeState() == SessionApplication::CloseState::Deciding
        && application.workspace().authoredRevision() == revision && original.prompts == 0,
        "Missing adapter cannot synthesize Cancel or retire an admitted native Close decision");
    application.attachPlatform(replacement);
    require(application.pendingDocumentOperation()->stage == SessionApplication::DocumentStage::Decision
        && application.continueDocumentOperation(token, DocumentChoice::Cancel).status == DocumentCommandStatus::Accepted,
        "A reattached presentation can answer the original Close token");
    until([&] { return !application.pendingDocumentOperation(); });
    require(replacement.prompts == 0 && application.closeState() == SessionApplication::CloseState::Open
        && application.document().isDirty(revision),
        "Replacement presentation cancels explicitly without changing authored truth");

    application.detachPlatform(replacement);
    application.requestClose(true);
    require(application.pendingDocumentOperation().has_value()
        && application.closeState() == SessionApplication::CloseState::Deciding,
        "External Close without adapter retains an explicit application-owned decision");
    application.attachPlatform(replacement);
    const auto externalToken = application.pendingDocumentOperation()->token;
    const auto cancelledBefore = replacement.cancelledCloses;
    application.requestClose(true);
    require(application.pendingDocumentOperation()->token == externalToken && replacement.cancelledCloses == cancelledBefore,
        "A system Close during a tray-owned decision cannot settle or replace it");
    require(application.continueDocumentOperation(token, DocumentChoice::Discard).status == DocumentCommandStatus::StaleOperation
        && application.continueDocumentOperation(externalToken, DocumentChoice::Cancel).status == DocumentCommandStatus::Accepted,
        "Only the current decision can continue tray Close");
    until([&] { return !application.pendingDocumentOperation(); });
    require(replacement.prompts == 0 && replacement.surfacedCloseDecisions == 1
        && application.closeState() == SessionApplication::CloseState::Open
        && application.workspace().authoredRevision() == revision,
        "External Close surfaces the same decision and has no implicit fallback");
    application.detachPlatform(replacement);
    const auto close = application.beginDocumentOperation(DocumentAction::Close);
    QCoreApplication::processEvents();
    require(application.continueDocumentOperation(close.token, DocumentChoice::Discard).status
        == DocumentCommandStatus::Accepted, "Explicit headless decision can authorize retirement");
    until([&] { application.pollNativeAudioEffects(); return application.finalClosePermitted(); });
    application.attachPlatform(replacement);
    require(replacement.finalCloseRequests == 0,
        "Attaching a Ready application cannot synchronously close a partially constructed adapter");
    until([&] { return replacement.finalCloseRequests == 1; });
    application.completePlatformClose(true);
    require(replacement.acceptedCloses == 1, "Queued final Close still completes the same application authority");
    application.detachPlatform(replacement);
}

void verifyDeferredPlatformDocumentResult()
{
    Engine engine;
    ApplicationTranslationState language;
    HeadlessPlatform presentation;
    presentation.deferDocumentResult = true;
    SessionApplication application(engine, TestSupport::mainWindowTestDisplayId(), language);
    application.setPresentationCapabilities(true, true, true);
    application.attachPlatform(presentation);
    auto& model = SessionApplicationTestAccess::model(application);
    (void)model.addPlaylist();
    const auto revision = model.authoredRevision();

    const auto first = application.beginDocumentOperation(DocumentAction::Open);
    until([&] { return static_cast<bool>(presentation.documentPathResult); });
    require(presentation.requestedDocument.operation == first.token && application.pendingDocumentOperation()
        && application.pendingDocumentOperation()->stage == SessionApplication::DocumentStage::OpenPicker
        && application.document().isDirty(revision), "A pending picker retains the dirty document and the native token");
    require(application.beginDocumentOperation(DocumentAction::Save).status == DocumentCommandStatus::Busy,
        "A second operation cannot execute while the platform result is pending");

    auto firstResult = std::move(presentation.documentPathResult);
    firstResult(std::nullopt);
    require(!application.pendingDocumentOperation() && model.authoredRevision() == revision
        && application.document().isDirty(revision), "Returning Cancel retires only the pending operation");

    const auto second = application.beginDocumentOperation(DocumentAction::SaveAs);
    until([&] { return presentation.requestedDocument.operation == second.token; });
    firstResult(std::nullopt);
    require(application.pendingDocumentOperation() && application.pendingDocumentOperation()->token == second.token
        && application.pendingDocumentOperation()->stage == SessionApplication::DocumentStage::SavePicker,
        "A duplicate or late Open picker result cannot complete a newer Save As");
    presentation.documentPathResult(std::nullopt);
    require(!application.pendingDocumentOperation() && application.document().isDirty(revision),
        "Save As Cancel preserves the dirty document");
    application.detachPlatform(presentation);
}

void verifyDocumentPickerDispatchRetirement()
{
    Engine engine;
    ApplicationTranslationState language;
    HeadlessPlatform old, replacement;
    old.deferDocumentResult = true;
    replacement.deferDocumentResult = true;
    SessionApplication application(engine, TestSupport::mainWindowTestDisplayId(), language);
    application.setPresentationCapabilities(true, true, true);
    application.attachPlatform(old);
    auto& model = SessionApplicationTestAccess::model(application);
    (void)model.addPlaylist();
    const auto revision = model.authoredRevision();
    const auto operation = application.beginDocumentOperation(DocumentAction::Open);
    until([&] { return static_cast<bool>(old.documentPathResult); });
    const auto before = old.requestedDocument;

    application.detachPlatform(old);
    application.attachPlatform(replacement);
    require(old.retiringRequest == before && old.acknowledgeRetirement,
        "Adapter retirement names the original native dispatch");
    old.documentPathResult(std::filesystem::path{"retired-open-result.json"});
    QCoreApplication::processEvents();
    require(replacement.documentPicks == 0 && application.pendingDocumentOperation()
        && application.pendingDocumentOperation()->token == operation.token
        && application.pendingDocumentOperation()->stage == SessionApplication::DocumentStage::OpenPicker,
        "Retired result is ignored and replacement cannot invoke a picker before retirement acknowledgement");

    old.acknowledgeRetirement();
    until([&] { return static_cast<bool>(replacement.documentPathResult); });
    const auto after = replacement.requestedDocument;
    require(after.operation == before.operation && after.dispatch != before.dispatch,
        "Replacement resumes with the same operation identity and a fresh dispatch");
    old.documentPathResult(std::nullopt);
    old.acknowledgeRetirement();
    QCoreApplication::processEvents();
    require(replacement.documentPicks == 1 && application.pendingDocumentOperation()
        && application.pendingDocumentOperation()->token == operation.token,
        "Duplicate retired result or acknowledgement cannot finish or repeat the live picker");
    replacement.documentPathResult(std::nullopt);
    require(!application.pendingDocumentOperation() && model.authoredRevision() == revision
        && application.document().isDirty(revision), "Replacement Cancel leaves the dirty document intact");
    application.detachPlatform(replacement);
}

void verifySaveThenReplaceDispatchRetirement()
{
    QTemporaryDir assets;
    require(assets.isValid(), "Save retirement has an owned empty fixture directory");
    const auto retiredPath = std::filesystem::path{assets.filePath("retired-save.dse.json").toStdWString()};
    Engine engine;
    ApplicationTranslationState language;
    HeadlessPlatform old, replacement;
    old.deferDocumentResult = true;
    replacement.deferDocumentResult = true;
    SessionApplication application(engine, TestSupport::mainWindowTestDisplayId(), language);
    application.setPresentationCapabilities(true, true, true);
    application.attachPlatform(old);
    auto& model = SessionApplicationTestAccess::model(application);
    const auto playlist = model.addPlaylist();
    const auto revision = model.authoredRevision();

    const auto operation = application.beginDocumentOperation(DocumentAction::New);
    until([&] {
        return application.pendingDocumentOperation()
            && application.pendingDocumentOperation()->stage == SessionApplication::DocumentStage::Decision;
    });
    require(application.continueDocumentOperation(operation.token, DocumentChoice::Save).status
        == DocumentCommandStatus::Accepted, "Dirty New admitted Save before replacement");
    until([&] { return static_cast<bool>(old.documentPathResult); });
    application.detachPlatform(old);
    application.attachPlatform(replacement);
    old.documentPathResult(retiredPath);
    QCoreApplication::processEvents();
    require(replacement.documentPicks == 0 && !std::filesystem::exists(retiredPath) && model.playlist(playlist)
        && application.pendingDocumentOperation()
        && application.pendingDocumentOperation()->stage == SessionApplication::DocumentStage::SavePicker,
        "Retired Save result cannot write or replace the document");
    old.acknowledgeRetirement();
    until([&] { return static_cast<bool>(replacement.documentPathResult); });
    require(replacement.requestedDocument.operation == operation.token
        && replacement.requestedDocument.dispatch != old.requestedDocument.dispatch,
        "Replacement picks a path for the original Save-before-replacement operation");
    replacement.documentPathResult(std::nullopt);
    require(!application.pendingDocumentOperation() && model.playlist(playlist)
        && model.authoredRevision() == revision && application.document().isDirty(revision)
        && application.documentTransactionSnapshot().value("last").toObject().value("outcome").toString() == "save-cancelled",
        "Fresh picker Cancel settles Save-before-replacement without discarding the document");
    application.detachPlatform(replacement);
}

void verifyMediaDispatchRetirement()
{
    Engine engine;
    ApplicationTranslationState language;
    HeadlessPlatform old, replacement;
    old.deferMediaResult = replacement.deferMediaResult = true;
    SessionApplication application(engine, TestSupport::mainWindowTestDisplayId(), language);
    application.setPresentationCapabilities(true, true, true);
    application.attachPlatform(old);
    auto& model = SessionApplicationTestAccess::model(application);
    const auto playlist = model.addPlaylist();
    const auto revision = model.authoredRevision();

    const auto operation = application.beginNativeMediaImport(playlist, std::nullopt, true);
    require(operation.status == AuthoredStatus::Accepted, "The Session admitted a BGM import");
    until([&] { return static_cast<bool>(old.mediaSourcesResult); });
    application.detachPlatform(old);
    application.attachPlatform(replacement);
    old.mediaSourcesResult({QStringLiteral("retired-track.wav")});
    QCoreApplication::processEvents();
    require(replacement.mediaPicks == 0 && old.mediaCompletions == 0 && replacement.mediaCompletions == 0
        && application.pendingMediaImport() && application.pendingMediaImport()->token == operation.token,
        "Retired BGM picker cannot complete the import or tell any adapter to restore its focus");
    old.acknowledgeRetirement();
    until([&] { return static_cast<bool>(replacement.mediaSourcesResult); });
    require(replacement.requestedMedia.operation == operation.token
        && replacement.requestedMedia.dispatch != old.requestedMedia.dispatch,
        "Replacement receives the same BGM import operation with a fresh dispatch");
    replacement.mediaSourcesResult({});
    old.mediaSourcesResult({});
    require(!application.pendingMediaImport() && application.lastMediaImport()
        && application.lastMediaImport()->token == operation.token && model.authoredRevision() == revision
        && old.mediaCompletions == 0 && replacement.mediaCompletions == 1 && replacement.completedMedia == operation.token,
        "Only the live native media result can settle the operation and signal completion");
    application.detachPlatform(replacement);
}

void verifyInlinePickerRetiresAfterNestedLoop()
{
    Engine engine;
    ApplicationTranslationState language;
    HeadlessPlatform old, replacement;
    replacement.deferDocumentResult = true;
    SessionApplication application(engine, TestSupport::mainWindowTestDisplayId(), language);
    application.setPresentationCapabilities(true, true, true);
    application.attachPlatform(old);
    old.duringDocumentPick = [&] {
        application.detachPlatform(old);
        application.attachPlatform(replacement);
        QCoreApplication::processEvents();
        require(replacement.documentPicks == 0,
            "An inline picker which is still inside its modal loop keeps the replacement fenced");
    };
    const auto operation = application.beginDocumentOperation(DocumentAction::Open);
    until([&] { return static_cast<bool>(replacement.documentPathResult); });
    require(old.documentPicks == 1 && replacement.requestedDocument.operation == operation.token
        && application.pendingDocumentOperation() && application.pendingDocumentOperation()->token == operation.token,
        "Return from the inline call acknowledges quiescence without a synthetic Cancel");
    replacement.documentPathResult(std::nullopt);
    application.detachPlatform(replacement);
}

void verifyAutonomousSaveAndOpenReread()
{
    QTemporaryDir assets;
    require(assets.isValid(), "Autonomous document operation uses an owned fixture directory");
    const auto destination = std::filesystem::path{assets.filePath("same-path.dse.json").toStdWString()};
    const auto closeEvent = [](void* event) { CloseHandle(static_cast<HANDLE>(event)); };
    const auto wake = std::unique_ptr<void, decltype(closeEvent)>{CreateEventW(nullptr, FALSE, FALSE, nullptr), closeEvent};
    require(static_cast<bool>(wake), "Autonomous document fixture has an independent fact event");
    Engine engine(Engine::PresentationMode::Detached);
    HeadlessPlatform presentation;
    SessionApplication application(engine, TestSupport::mainWindowTestDisplayId(), nullptr, {});
    require(application.initializeAutonomousRuntime(wake.get()), "Autonomous document fixture initialized real detached Engine");
    application.setPresentationCapabilities(true, true, true);
    application.attachPlatform(presentation);
    presentation.pickedPath = destination;
    auto& model = SessionApplicationTestAccess::model(application);
    const auto playlist = model.addPlaylist();
    require(model.renamePlaylist(playlist, "Original disk candidate"), "Original authored fixture named");
    const auto save = application.beginDocumentOperation(DocumentAction::SaveAs);
    require(save.status == DocumentCommandStatus::Accepted, "Autonomous Save As is admitted");
    until([&] { return !application.pendingDocumentOperation(); });
    require(application.document().currentPath == destination && !application.document().isDirty(model.authoredRevision())
        && application.documentTransactionSnapshot().value("last").toObject().value("outcome").toString() == "saved",
        "Successful resource write adopts the file path and exact saved revision on control");

    require(model.renamePlaylist(playlist, "Saved while opening the same path"), "Current authored fixture edits after initial save");
    const auto open = application.beginDocumentOperation(DocumentAction::Open);
    until([&] {
        return application.pendingDocumentOperation()
            && application.pendingDocumentOperation()->stage == SessionApplication::DocumentStage::Decision;
    });
    const auto& candidate = application.pendingDocumentOperation()->candidate;
    require(application.pendingDocumentOperation()->token == open.token && candidate
        && !candidate->project.playlists.empty() && candidate->project.playlists.front().displayAlias == "Original disk candidate",
        "The first resource read returns the on-disk candidate without altering the dirty document");
    require(application.continueDocumentOperation(open.token, DocumentChoice::Save).status
        == DocumentCommandStatus::Accepted, "Existing Open token accepts Save and keeps the replacement pending");
    until([&] { return !application.pendingDocumentOperation(); });
    require(!model.authoredProject().playlists.empty()
        && model.authoredProject().playlists.front().displayAlias == "Saved while opening the same path"
        && application.document().currentPath == destination && !application.document().isDirty(model.authoredRevision())
        && application.documentTransactionSnapshot().value("last").toObject().value("outcome").toString() == "completed",
        "The second resource read uses the newly committed bytes instead of the stale original candidate");

    presentation.pickedPath = std::filesystem::path{assets.filePath("missing.dse.json").toStdWString()};
    const auto failed = application.beginDocumentOperation(DocumentAction::Open);
    require(failed.status == DocumentCommandStatus::Accepted, "Missing path still admits the asynchronous Open attempt");
    until([&] { return !application.pendingDocumentOperation(); });
    require(application.document().currentPath == destination && !application.document().isDirty(model.authoredRevision())
        && application.documentTransactionSnapshot().value("last").toObject().value("outcome").toString() == "candidate-invalid",
        "Failed resource read preserves the previous document and clean revision");
    application.detachPlatform(presentation);
}

void verifyAutonomousPreferences()
{
    QTemporaryDir assets;
    require(assets.isValid(), "Preferences fixture has isolated storage");
    QSettings settings(assets.filePath("preferences.ini"), QSettings::IniFormat);
    ApplicationPreferencesStore storage(settings);
    const auto display = TestSupport::mainWindowTestDisplayId();
    require(storage.saveLanguage(UiLanguage::SimplifiedChinese) == QSettings::NoError
        && storage.saveCanvasOutsideMode(display, CanvasOutsideMode::Transparent) == QSettings::NoError,
        "Preferences fixture seeded persisted language and display-specific canvas mode");
    const auto closeEvent = [](void* event) { CloseHandle(static_cast<HANDLE>(event)); };
    const auto wake = std::unique_ptr<void, decltype(closeEvent)>{CreateEventW(nullptr, FALSE, FALSE, nullptr), closeEvent};
    require(static_cast<bool>(wake), "Preferences fixture has an independent fact event");
    Engine engine(Engine::PresentationMode::Detached);
    HeadlessPlatform presentation;
    presentation.deferLanguageApplied = true;
    SessionApplication application(engine, display, &storage, ApplicationPreferences{UiLanguage::SimplifiedChinese, false});
    SessionApplicationTestAccess::displayEnumerator(application, [display] {
        Platform::DisplayEnumerationResult result{Platform::DisplayEnumerationStatus::Success, {}};
        result.snapshot.displays.push_back(Platform::DisplayDescriptor{.id = display, .pixelWidth = 1024, .pixelHeight = 768});
        return result;
    });
    application.refreshDisplaySelection();
    require(application.initializeAutonomousRuntime(wake.get()), "Preferences fixture initialized real detached Engine");
    application.setPresentationCapabilities(true, true, true);
    application.attachPlatform(presentation);
    std::optional<DesktopDeploymentResult> deployment;
    const auto recordDeploy = [&deployment](DesktopDeploymentResult result) { deployment = result; };
    application.requestDetachedDesktopDeployment(DesktopDeploymentIntent::Deploy, recordDeploy);
    require(deployment == DesktopDeploymentResult::Rejected && presentation.desktopRequests == 0
        && !application.desktopDeploymentProjection().canDeploy && application.desktopDeploymentProjection().canUndeploy,
        "Even with the bound display present, fresh Deploy waits for the background read; cleanup remains available");
    until([&] { return application.canvasOutsideMode() == CanvasOutsideMode::Transparent; });
    require(application.desktopDeploymentProjection().canDeploy, "The resolved background enables fresh Deploy");
    deployment.reset();
    application.requestDetachedDesktopDeployment(DesktopDeploymentIntent::Deploy, recordDeploy);
    require(!deployment && presentation.desktopRequests == 1 && presentation.requestedDesktopFrame
        && presentation.requestedDesktopFrame->outside == CanvasOutsideMode::Transparent,
        "First output submission receives persisted Reveal instead of the startup Cover default");
    presentation.desktopResult({SessionDesktopAttempt::TransientlyUnavailable, SessionPlatformFacts{}});
    const auto desktopRequests = presentation.desktopRequests;
    require(presentation.languageRequests.size() == 1
        && presentation.languageRequests.front().first == UiLanguage::SimplifiedChinese
        && application.effectiveLanguage() == UiLanguage::English,
        "Persisted Canvas is applied, and language remains the last effective value until GUI acknowledgement");
    auto oldLanguageResult = presentation.languageRequests.front().second;
    oldLanguageResult(UiLanguage::English);
    require(application.applicationPreferences().language == UiLanguage::SimplifiedChinese
        && application.effectiveLanguage() == UiLanguage::English,
        "A real GUI translation fallback cannot overwrite the committed language");
    require(storage.saveLanguage(UiLanguage::English) == QSettings::NoError,
        "External settings writer changes the owned persisted language");
    application.reloadCommittedLanguage();
    until([&] { return presentation.languageRequests.size() == 2; });
    require(presentation.languageRequests.back().first == UiLanguage::English,
        "Explicit reload returns a file-worker outcome to the GUI language adapter");
    presentation.languageRequests.back().second(UiLanguage::English);
    oldLanguageResult(UiLanguage::SimplifiedChinese);
    require(application.effectiveLanguage() == UiLanguage::English
        && application.applicationPreferences().language == UiLanguage::English,
        "A stale language acknowledgement cannot override the newer effective value");

    using PreferenceResult = SessionApplication::ViewPreferenceResult;
    const auto highlighted = [&] { return application.applicationPreferences().highlightCanvasBoundary; };
    const auto document = application.documentIncarnation();
    const auto revision = application.workspace().authoredRevision();
    const auto binding = application.bindingToken();
    until([&] { return presentation.detachedOutside == CanvasOutsideMode::Transparent; });
    require(application.canvasOutsideMode() == CanvasOutsideMode::Transparent && !highlighted(),
        "Initial read applies the persisted bound-display background and the unhighlighted Preview");
    require(application.applyHighlightCanvasBoundary(true) == PreferenceResult::Applied
        && highlighted() && presentation.detachedOutside == CanvasOutsideMode::Transparent,
        "Enabling editor Highlight does not alter the detached desktop frame mode");
    require(application.applyHighlightCanvasBoundary(false) == PreferenceResult::Applied
        && !highlighted()
        && application.applyHighlightCanvasBoundary(false) == PreferenceResult::AlreadySatisfied,
        "Highlight turns off through native truth and repeating the current value is a no-op");
    require(application.selectCanvasOutsideMode(CanvasOutsideMode::Opaque) == PreferenceResult::Applied
        && application.canvasOutsideMode() == CanvasOutsideMode::Opaque,
        "Cover changes the effective display preference immediately");
    until([&] { return presentation.detachedOutside == CanvasOutsideMode::Opaque; });
    require(application.selectCanvasOutsideMode(CanvasOutsideMode::Transparent) == PreferenceResult::Applied
        && application.canvasOutsideMode() == CanvasOutsideMode::Transparent,
        "Reveal changes the effective display preference immediately");
    until([&] { return presentation.detachedOutside == CanvasOutsideMode::Transparent; });
    require(application.selectCanvasOutsideMode(CanvasOutsideMode::Opaque) == PreferenceResult::Applied
        && application.selectCanvasOutsideMode(CanvasOutsideMode::Opaque) == PreferenceResult::AlreadySatisfied
        && application.applyHighlightCanvasBoundary(true) == PreferenceResult::Applied,
        "Final effective native values can be persisted while chosen equal values do not write again");
    require(application.documentIncarnation() == document && application.workspace().authoredRevision() == revision
        && !application.document().isDirty(revision) && application.bindingToken() == binding
        && presentation.desktopRequests == desktopRequests,
        "View actions never author data, rebind playback, or create a desktop target");
    application.requestClose(true);
    require(application.applyHighlightCanvasBoundary(false) == PreferenceResult::Unavailable
        && application.selectCanvasOutsideMode(CanvasOutsideMode::Transparent) == PreferenceResult::Unavailable,
        "Close fences late preference changes");
    until([&] { return application.finalClosePermitted(); });
    auto readbackSettings = storage.location().open();
    ApplicationPreferencesStore readback(*readbackSettings);
    require(readback.loadCanvasOutsideMode(display).value == CanvasOutsideMode::Opaque
        && readback.load().highlightCanvasBoundary,
        "Normal Close waits for the last accepted Canvas and highlight writes to settle");
    application.completePlatformClose(true);
    application.detachPlatform(presentation);
}

void verifyBackgroundPersistenceFailure()
{
    QTemporaryDir directory;
    require(directory.isValid() && QDir(directory.path()).mkdir("invalid-ini"),
        "Failed preference fixture owns a directory in place of its INI file");
    QSettings settings(directory.filePath("invalid-ini"), QSettings::IniFormat);
    ApplicationPreferencesStore storage(settings);
    const auto display = TestSupport::mainWindowTestDisplayId();
    const auto closeEvent = [](void* event) { CloseHandle(static_cast<HANDLE>(event)); };
    const auto wake = std::unique_ptr<void, decltype(closeEvent)>{CreateEventW(nullptr, FALSE, FALSE, nullptr), closeEvent};
    require(static_cast<bool>(wake), "Failed preference fixture has a native fact event");
    Engine engine(Engine::PresentationMode::Detached);
    HeadlessPlatform presentation;
    SessionApplication application(engine, display, &storage, {});
    require(application.initializeAutonomousRuntime(wake.get()), "Failed preference fixture uses the autonomous Engine");
    application.attachPlatform(presentation);
    until([&] { return presentation.detachedOutside == CanvasOutsideMode::Opaque; });
    using PreferenceResult = SessionApplication::ViewPreferenceResult;
    require(application.selectCanvasOutsideMode(CanvasOutsideMode::Transparent) == PreferenceResult::Applied,
        "A writable live Reveal choice is accepted independently of disk");
    until([&] { return presentation.preferenceWarnings == 1 && presentation.detachedOutside == CanvasOutsideMode::Transparent; });
    const auto persisted = SessionApplicationTestAccess::persistedBackground(application);
    require(application.canvasOutsideMode() == CanvasOutsideMode::Transparent && persisted.status != QSettings::NoError
        && !persisted.value,
        "Failed persistence retains live Reveal and does not fabricate a successful stored value");
    require(application.selectCanvasOutsideMode(CanvasOutsideMode::Transparent) == PreferenceResult::AlreadySatisfied,
        "A duplicate live choice does not retry failed persistence");
    application.requestClose(true);
    until([&] { return application.finalClosePermitted(); });
    require(presentation.preferenceWarnings == 1, "A failed live preference emits its native warning once");
    application.completePlatformClose(true);
    application.detachPlatform(presentation);
}

void verifyDetachedDesktopAcknowledgment()
{
    const auto closeEvent = [](void* event) { CloseHandle(static_cast<HANDLE>(event)); };
    const auto wake = std::unique_ptr<void, decltype(closeEvent)>{CreateEventW(nullptr, FALSE, FALSE, nullptr), closeEvent};
    require(static_cast<bool>(wake), "Detached Desktop qualification created a native fact event");
    Engine engine(Engine::PresentationMode::Detached);
    HeadlessPlatform presentation;
    const auto bound = TestSupport::mainWindowTestDisplayId();
    SessionApplication application(engine, bound, nullptr, {});
    bool displayAvailable = true;
    SessionApplicationTestAccess::displayEnumerator(application, [bound, &displayAvailable] {
        Platform::DisplayEnumerationResult result{Platform::DisplayEnumerationStatus::Success, {}};
        if (displayAvailable)
            result.snapshot.displays.push_back(Platform::DisplayDescriptor{.id = bound, .pixelWidth = 1024, .pixelHeight = 768});
        return result;
    });
    require(application.initializeAutonomousRuntime(wake.get()), "Detached Desktop uses the real autonomous runtime");
    application.attachPlatform(presentation);
    std::vector<DesktopDeploymentResult> outcomes;
    const auto record = [&](DesktopDeploymentResult result) { outcomes.push_back(result); };

    application.requestDetachedDesktopDeployment(DesktopDeploymentIntent::Deploy, record);
    const auto pending = application.desktopDeploymentProjection();
    require(outcomes.empty() && presentation.desktopRequests == 1 && presentation.requestedDesktop == bound
        && presentation.requestedDesktopFrame && !presentation.requestedDesktopFrame->input.frame
        && pending.retained && pending.commandPending && !pending.canDeploy && !pending.canUndeploy,
        "A bound native Deploy carries the empty presentation input and remains pending until its GUI result");
    application.requestDetachedDesktopDeployment(DesktopDeploymentIntent::Undeploy, record);
    require(presentation.desktopRequests == 1 && outcomes == std::vector{DesktopDeploymentResult::Rejected},
        "Overlapping command cannot replace an outstanding desktop operation");
    const auto failedDeploy = presentation.desktopResult;
    SessionPlatformFacts stillOwned;
    stillOwned.desktopHostOwned = stillOwned.desktopRetained = true;
    failedDeploy({SessionDesktopAttempt::Failed, stillOwned});
    require(outcomes.back() == DesktopDeploymentResult::Failure && !application.desktopDeploymentDesired()
        && application.desktopDeploymentProjection().retained && application.desktopDeploymentProjection().canUndeploy
        && !application.desktopDeploymentProjection().commandPending,
        "Native attachment failure retains residual platform ownership for Undeploy");
    application.requestDetachedDesktopDeployment(DesktopDeploymentIntent::Deploy, record);
    require(outcomes.back() == DesktopDeploymentResult::Rejected && presentation.desktopRequests == 1,
        "Repeated Deploy cannot report success or replace the retained host after failed attachment");

    const auto recorded = outcomes.size();
    application.requestDetachedDesktopDeployment(DesktopDeploymentIntent::Undeploy, record);
    require(presentation.desktopRequests == 2 && !presentation.requestedDesktopFrame && outcomes.size() == recorded,
        "Retry removal awaits its distinct GUI result");
    failedDeploy({SessionDesktopAttempt::Ready, {}});
    require(outcomes.size() == recorded && application.platformFacts().desktopHostOwned,
        "Duplicate old result cannot complete the new removal or overwrite native facts");
    presentation.desktopResult({SessionDesktopAttempt::Ready, SessionPlatformFacts{}});
    require(outcomes.back() == DesktopDeploymentResult::Completed && !application.desktopDeploymentProjection().retained,
        "Acknowledged removal clears the exact endpoint and completes the command");

    const auto document = application.documentIncarnation();
    const auto revision = application.workspace().authoredRevision();
    const auto dirty = application.document().isDirty(revision);
    const auto before = application.runtimePlaybackSnapshot();
    const auto frameInstall = application.platformFacts();
    const auto initialRequests = presentation.desktopRequests;
    application.requestDetachedDesktopDeployment(DesktopDeploymentIntent::Undeploy, record);
    require(outcomes.back() == DesktopDeploymentResult::AlreadySatisfied && presentation.desktopRequests == initialRequests,
        "Undeploy in preview-only state does not dispatch another target operation");

    displayAvailable = false;
    application.requestDetachedDesktopDeployment(DesktopDeploymentIntent::Deploy, record);
    require(outcomes.back() == DesktopDeploymentResult::Rejected && presentation.desktopRequests == initialRequests
        && !application.desktopDeploymentProjection().retained && !application.desktopDeploymentProjection().canDeploy,
        "A disappeared exact-bound display is rejected without attaching a fallback target");
    displayAvailable = true;

    application.requestDetachedDesktopDeployment(DesktopDeploymentIntent::Deploy, record);
    presentation.desktopResult({SessionDesktopAttempt::TransientlyUnavailable, frameInstall});
    require(outcomes.back() == DesktopDeploymentResult::Failure && !application.desktopDeploymentDesired()
        && !application.desktopDeploymentProjection().retained && application.desktopDeploymentProjection().canDeploy
        && presentation.desktopRequests == initialRequests + 1,
        "A missing first Present after successful cleanup returns to preview-only and permits another Deploy");

    const auto requests = presentation.desktopRequests;
    application.requestDetachedDesktopDeployment(DesktopDeploymentIntent::Deploy, record);
    application.requestDetachedDesktopDeployment(DesktopDeploymentIntent::Deploy, record);
    require(outcomes.back() == DesktopDeploymentResult::Rejected && presentation.desktopRequests == requests + 1,
        "Repeated Deploy during the admitted attempt never queues a second target");
    SessionPlatformFacts deployed = frameInstall;
    deployed.desktopHostOwned = deployed.desktopTargetRegistered = deployed.desktopHealthy = deployed.desktopRetained = true;
    presentation.desktopResult({SessionDesktopAttempt::Ready, deployed});
    require(outcomes.back() == DesktopDeploymentResult::Completed && application.desktopDeploymentDesired()
        && application.platformFacts().desktopHealthy && application.desktopDeploymentProjection().canUndeploy,
        "Successful production-owner deployment exposes observed target health after the platform receipt");
    application.requestDetachedDesktopDeployment(DesktopDeploymentIntent::Deploy, record);
    require(outcomes.back() == DesktopDeploymentResult::AlreadySatisfied && presentation.desktopRequests == requests + 1,
        "Repeated Deploy with a healthy registered target is an idempotent no-op");
    application.reconcileDetachedDesktopTopology(false);
    const auto maintenance = application.desktopDeploymentProjection();
    require(presentation.desktopRequests == requests + 2 && !maintenance.canDeploy && !maintenance.canUndeploy
        && !maintenance.commandPending, "Background topology reconciliation is unavailable but is not a user deployment");
    presentation.desktopResult({SessionDesktopAttempt::Ready, deployed});
    application.observePresentationFacts(stillOwned);
    application.requestDetachedDesktopDeployment(DesktopDeploymentIntent::Deploy, record);
    require(outcomes.back() == DesktopDeploymentResult::Rejected && presentation.desktopRequests == requests + 2
        && application.desktopDeploymentDesired(), "A suspended retained target cannot be reported as deployed");
    application.requestDetachedDesktopDeployment(DesktopDeploymentIntent::Undeploy, record);
    presentation.desktopResult({SessionDesktopAttempt::Ready, frameInstall});
    const auto after = application.runtimePlaybackSnapshot();
    require(outcomes.back() == DesktopDeploymentResult::Completed && presentation.desktopRequests == requests + 3
        && application.documentIncarnation() == document && application.workspace().authoredRevision() == revision
        && application.document().isDirty(revision) == dirty && before.value("bindingToken") == after.value("bindingToken")
        && before.value("active") == after.value("active") && before.value("state") == after.value("state"),
        "Deploy and Undeploy affect only the platform target, preserving document revision and playback binding");

    application.requestDetachedDesktopDeployment(DesktopDeploymentIntent::Deploy, record);
    application.shutdownEngine();
    require(!application.desktopDeploymentProjection().commandPending,
        "Runtime shutdown clears explicit deployment progress before a late platform result");
    presentation.desktopResult({SessionDesktopAttempt::Ready, stillOwned});
    require(outcomes.back() == DesktopDeploymentResult::Rejected && !application.desktopDeploymentDesired()
        && !application.platformFacts().desktopHostOwned, "Late GUI completion cannot resurrect a retired Desktop");
    application.detachPlatform(presentation);
}
}

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    try
    {
        verifyIdentityAndAuthority();
        verifyContinuationSurvivesAdapterReplacement();
        verifyNativeCloseDecisionSurvivesAdapterAbsence();
        verifyDeferredPlatformDocumentResult();
        verifyDocumentPickerDispatchRetirement();
        verifySaveThenReplaceDispatchRetirement();
        verifyMediaDispatchRetirement();
        verifyInlinePickerRetiresAfterNestedLoop();
        verifyAutonomousSaveAndOpenReread();
        verifyAutonomousPreferences();
        verifyBackgroundPersistenceFailure();
        verifyDetachedDesktopAcknowledgment();
        std::cout << "SessionApplication authority and platform results PASS: " << assertions << " assertions\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "SessionApplication headless authority FAIL: " << error.what() << '\n';
        return 1;
    }
}
