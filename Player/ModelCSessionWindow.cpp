#include "ModelCSessionWindow.h"

#include "BgmPeerWindow.h"
#include <QWindow>
#include "GlobalSettingsNotification.h"
#include "NativeTimelineHoverWindow.h"
#include "SessionApplication.h"
#include "SessionBridgeContract.h"
#include "SessionReadModel.h"
#include "SessionWebViewComposition.h"
#include "TimelinePreviewPresentation.h"
#include "TimelineRepresentativeTimestamp.h"
#include "TimelineUtils.h"

#include <QtCore/qt_windows.h>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QTimer>
#include <cmath>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace DesktopSceneEngine::Player
{
namespace
{
QString text(const char* source)
{
    return QCoreApplication::translate("MainWindow", source);
}

std::optional<std::int64_t> timelineDurationMilliseconds(double seconds)
{
    const double milliseconds = seconds * 1000.0;
    if (!std::isfinite(milliseconds) || milliseconds <= 0.0
        || milliseconds >= static_cast<double>((std::numeric_limits<std::int64_t>::max)()))
        return std::nullopt;
    const auto rounded = static_cast<std::int64_t>(std::llround(milliseconds));
    return rounded > 0 ? std::optional{rounded} : std::nullopt;
}

template<class Payload>
class LatestUiValue
{
public:
    bool publish(Payload payload)
    {
        const std::unique_lock lock(mutex_, std::try_to_lock);
        if (!lock) return false;
        const bool needsDelivery = !value_;
        value_ = std::move(payload);
        return needsDelivery;
    }

    std::optional<Payload> consume()
    {
        const std::lock_guard lock(mutex_);
        return std::exchange(value_, std::nullopt);
    }

private:
    std::mutex mutex_;
    std::optional<Payload> value_;
};
}

class ModelCControlPlatform final : public SessionPlatform
{
public:
    ModelCControlPlatform(SessionApplication& application, SessionControlExecutor& control,
        SessionGuiRelay& guiRelay, QPointer<ModelCSessionWindow> window, const QUuid& incarnation)
        : application_(application), control_(control), relay_(guiRelay), window_(std::move(window)),
          model_(application, incarnation)
    {}

    SessionPlatformFacts sessionPlatformFacts() const override { return observed_; }
    void observePresentationFacts(SessionPlatformFacts facts) override { observed_ = facts; }

    std::optional<std::filesystem::path> pickSessionDocumentPath(
        bool, const std::optional<std::filesystem::path>&) override
    { throw std::logic_error("The autonomous Session requires a document picker continuation"); }

    QStringList pickSessionMediaSources(bool) override
    { throw std::logic_error("The autonomous Session requires a media picker continuation"); }

    SessionUnsavedChoice promptSessionUnsavedChanges(const QString&) override
    { throw std::logic_error("The autonomous Session requires an unsaved decision continuation"); }

    void requestSessionDocumentPath(const SessionPlatformRequest& request, bool save,
        const std::optional<std::filesystem::path>& current, DocumentPathResult complete) override
    {
        send([request, save, current, callback = returnToControl(std::move(complete))](auto& window) mutable {
            window.showDocumentPicker(request, save, current, std::move(callback));
        });
    }

    void requestSessionMediaSources(const SessionPlatformRequest& request, bool music, MediaSourcesResult complete) override
    {
        send([request, music, callback = returnToControl(std::move(complete))](auto& window) mutable {
            window.showMediaPicker(request, music, std::move(callback));
        });
    }

    void surfaceSessionCloseDecision() override
    {
        send([](auto& window) { window.tray_.surfaceCloseInteraction(); });
    }

    SessionPlatformRetirement retireSessionPlatformRequest(const SessionPlatformRequest& request,
        RequestRetired retired) noexcept override
    {
        send([request, retired = returnToControl(std::move(retired))](auto& window) mutable {
            window.retireDialog(request, std::move(retired));
        });
        return SessionPlatformRetirement::AwaitAcknowledgment;
    }

    void showSessionNotice(SessionNotice kind, const QString& title, const QString& message) override
    { send([kind, title, message](auto& window) { window.showNotice(kind, title, message); }); }

    void applySessionLanguage(UiLanguage language, LanguageApplied complete) override
    {
        send([language, callback = returnToControl(std::move(complete))](auto& window) mutable {
            callback(window.translation_.setLanguage(language));
        });
    }

    void acknowledgeSessionRevision(std::uint64_t before, bool playlist) override
    {
        if (playlist) model_.acknowledgePlaylistMasterRevision(before);
        else model_.acknowledgeScalarAuthoredRevision(before);
    }

    void sessionViewChanged(SessionViewChange change, bool) override
    {
        if (viewQueued_) return;
        if (change == SessionViewChange::RuntimeSample
            && lastSample_.isValid() && lastSample_.elapsed() < 100) return;
        viewQueued_ = true;
        QTimer::singleShot(0, &application_, [this] {
            lastSample_.start();
            invalidationPending_ = model_.pollChanged() || invalidationPending_;
            auto snapshot = captureSessionWebViewFacts(application_);
            QHash<QUuid, QJsonObject> peerHeaders;
            for (auto i = application_.bgmEditPermits().cbegin(); i != application_.bgmEditPermits().cend(); ++i)
            {
                if (!application_.workspace().playlist(i->playlistId)) continue;
                peerHeaders.insert(i.key(), captureBgmPeerHeader(model_, {i.key(), i->playlistId, i->documentIncarnation}));
            }
            {
                const std::unique_lock lock(facts_->mutex, std::try_to_lock);
                if (!lock)
                {
                    viewQueued_ = false;
                    return;
                }
                if (facts_->value)
                {
                    const bool changed = std::exchange(invalidationPending_, false);
                    facts_->value = std::move(snapshot);
                    facts_->peerHeaders = std::move(peerHeaders);
                    facts_->invalidated = facts_->invalidated || changed;
                    viewQueued_ = false;
                    return;
                }
                facts_->value = std::move(snapshot);
                facts_->peerHeaders = std::move(peerHeaders);
                facts_->invalidated = std::exchange(invalidationPending_, false);
            }
            send([facts = facts_](auto& window) {
                std::optional<SessionWebViewFacts> snapshot;
                QHash<QUuid, QJsonObject> peerHeaders;
                bool invalidated = false;
                {
                    std::lock_guard lock(facts->mutex);
                    snapshot = std::exchange(facts->value, std::nullopt);
                    peerHeaders = std::move(facts->peerHeaders);
                    invalidated = std::exchange(facts->invalidated, false);
                }
                if (snapshot) window.receiveFacts(std::move(*snapshot), std::move(peerHeaders), invalidated);
            });
            viewQueued_ = false;
        });
    }

    void publishDetachedFrame(PresentationInput input, const QString& binding, CanvasOutsideMode outside,
        std::optional<Engine::MediaPreparationToken> ticket) override
    {
        if (!frames_->publish({std::move(input), binding, outside, ticket})) return;
        send([frames = frames_](auto& window) {
            if (auto frame = frames->consume()) window.receiveFrame(std::move(*frame));
        });
    }

    void refreshSessionBgmWindow(PlaylistId) override
    { sessionViewChanged(SessionViewChange::BgmPlayback, false); }
    void closeSessionBgmWindow(PlaylistId id) override
    { send([id](auto& window) { window.closePeer(id); }); }
    void closeAllSessionBgmWindows() override
    { send([](auto& window) { window.closePeers(); }); }
    void completeSessionMediaImport(const QUuid& token) override
    { send([token](auto& window) { window.restoreImportFocus(token); }); }

    bool initializeSessionPresentation(Engine&) override { return false; }
    void detachSessionPresentation(Engine&) noexcept override {}
    void prepareSessionEngineShutdown(Engine&) noexcept override {}
    void finishSessionEngineShutdown() noexcept override {}
    bool sessionRuntimePresentationReady() const override { return observed_.visible && !observed_.minimized; }
    void renderSession(Engine&, const Domain::AspectRatio&, CanvasOutsideMode) override {}
    SessionDesktopAttempt deploySessionDesktop(Engine&, const DisplayId&) override { return SessionDesktopAttempt::Failed; }
    bool removeSessionDesktop(Engine&) noexcept override { return false; }
    bool reconcileSessionDesktop(Engine&, const DisplayId&) override { return false; }
    bool requestDetachedDesktop(DisplayId display, std::optional<SessionDesktopFrame> frame,
        DesktopResult complete) override
    {
        return relay_.post([window = window_, control = &control_,
            display = std::move(display), frame = std::move(frame), complete = std::move(complete)]() mutable {
            SessionDesktopResult result;
            if (window)
            {
                result.attempt = window->reconcileDesktopEndpoint(display, frame);
                result.facts = window->currentPlatformFacts();
            }
            const auto observed = result.facts;
            const bool accepted = control->submitPlatformResult(
                [complete = std::move(complete), result = std::move(result)](SessionApplication&) mutable {
                    complete(std::move(result));
                });
            if (accepted && window && observed) window->lastPlatformFacts_ = *observed;
        });
    }
    void beginSessionPresentationTeardown() override
    { send([](auto& window) { window.beginPresentationTeardown(); }); }
    void requestSessionFinalClose() override
    { send([](auto& window) { window.finalClose_ = true; window.releasePresentation(); window.close(); }); }
    void sessionCloseCompleted(bool accepted) override
    {
        send([accepted, pending = application_.closePending()](auto& window) {
            window.settleClose(accepted, pending);
        });
    }
    void sessionExternalCloseSaveFailed(const QString& message) override
    {
        send([message](auto& window) {
            window.controlClosePending_ = false;
            window.closeFailureNoticePending_ = true;
            const auto finish = [guard = QPointer<ModelCSessionWindow>{&window}] {
                if (!guard) return;
                guard->closeFailureNoticePending_ = false;
                guard->settleClose(false, false);
            };
            const auto title = text("Save Project Failed");
            if (!window.host_ || !window.host_->showSessionNotice(SessionNotice::Error, title,
                message.isEmpty() ? title : message, finish)) finish();
        });
    }

private:
    template<class Callback>
    void send(Callback callback)
    {
        (void)relay_.post([window = window_, callback = std::move(callback)]() mutable {
            if (window) callback(*window);
        });
    }

    template<class Completion>
    auto returnToControl(Completion complete)
    {
        return [control = &control_, complete = std::move(complete)](auto... values) mutable {
            (void)control->submitPlatformResult([complete = std::move(complete), ...values = std::move(values)]
                (SessionApplication&) mutable { complete(std::move(values)...); });
        };
    }

    struct FactsMailbox
    {
        std::mutex mutex;
        std::optional<SessionWebViewFacts> value;
        QHash<QUuid, QJsonObject> peerHeaders;
        bool invalidated = false;
    };

    SessionApplication& application_;
    SessionControlExecutor& control_;
    SessionGuiRelay& relay_;
    const QPointer<ModelCSessionWindow> window_;
    SessionReadModel model_;
    SessionPlatformFacts observed_;
    QElapsedTimer lastSample_;
    bool viewQueued_ = false;
    bool invalidationPending_ = false;
    std::shared_ptr<FactsMailbox> facts_ = std::make_shared<FactsMailbox>();
    std::shared_ptr<LatestUiValue<ModelCSessionWindow::DetachedFrame>> frames_ =
        std::make_shared<LatestUiValue<ModelCSessionWindow::DetachedFrame>>();
};

ModelCSessionWindow::ModelCSessionWindow(SessionControlExecutor& control, SessionGuiRelay& guiRelay,
    DisplayId display, ApplicationTranslationState& translation, QUuid incarnation)
    : ProductWindowHost(ProductWindowRole::Session), control_(control), guiRelay_(guiRelay), display_(std::move(display)),
      translation_(translation), incarnation_(incarnation)
{
    setMinimumSize(800, 650);
    resize(1280, 720);
    setWindowTitle(QStringLiteral("DesktopSceneEngine — ") + text("Untitled"));
    settingsMessage_ = globalSettingsChangedMessageId();
    taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");
    desktopHost_.setExternalDestructionCallback([this] { scheduleDetachedDesktopTopology(); });
    try
    {
        composition_ = std::make_unique<SessionWebViewComposition>(productViewport()->winId());
        previewTarget_ = presentation_.initialize(HostedCompositionTarget{{1, 1}});
        if (!previewTarget_.isValid()) throw std::runtime_error("Unable to initialize the Model C presentation target");
    }
    catch (const std::exception& error)
    {
        composition_.reset();
        presentation_.shutdown();
        qCritical("[SessionPresentation] Model C initialization failed: %s", error.what());
        setProductPresentationStage(ProductPresentationStage::Failed, QString::fromUtf8(error.what()));
    }
    try { hoverCard_ = std::make_unique<NativeTimelineHoverWindow>(winId()); }
    catch (const std::exception& error) { qWarning("[NativeHover] surface creation unavailable: %s", error.what()); }

    const auto cache = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("Cache/Media"));
    hoverWorker_ = new TimelinePreviewWorker(hoverRequests_, std::filesystem::path{cache.toStdWString()});
    hoverWorker_->moveToThread(&hoverThread_);
    connect(&hoverThread_, &QThread::finished, hoverWorker_, &QObject::deleteLater);
    connect(hoverWorker_, &TimelinePreviewWorker::previewReady, this, &ModelCSessionWindow::receiveHover);
    hoverThread_.start();
}

ModelCSessionWindow::~ModelCSessionWindow()
{
    releasePresentation();
    hoverRequests_.stop();
    hoverThread_.quit();
    hoverThread_.wait();
}

SessionControlExecutor::PlatformFactory ModelCSessionWindow::platformFactory()
{
    return [guard = QPointer<ModelCSessionWindow>{this}, control = &control_, relay = &guiRelay_,
        incarnation = incarnation_](SessionApplication& application) {
        application.setPresentationCapabilities(true, true, true);
        if (!application.enableAutonomousMediaPreparation())
            throw std::runtime_error("Unable to start detached Session media preparation");
        return std::make_unique<ModelCControlPlatform>(application, *control, *relay, guard, incarnation);
    };
}

void ModelCSessionWindow::startPresentation()
{
    if (started_ || controlStartupFailed_ || releasing_) return;
    startRequested_ = true;
    resumeStartupClose();
    if (facts_.documentIncarnation.isNull() || facts_.audioEffects.value("version") != QJsonValue{1}) return;
    started_ = true;
    if (productFrameFailed() || !composition_ || !previewTarget_.isValid())
    {
        publishPlatformFacts();
        return;
    }
    WebViewSessionHost::PreviewPresentation preview;
    preview.composition = composition_.get();
    preview.requestExtent = [this](const QSize& size) {
        return presentation_.setHostedPresentationExtent(previewTarget_, {size.width(), size.height()});
    };
    WebViewSessionHost::RuntimePresentation runtime;
    runtime.sample = [this] { return facts_.runtime; };
    runtime.audioEffects = [this] { return facts_.audioEffects; };
    runtime.hover = [this](const TimelineHoverRequest& request, auto complete) {
        requestHover(request, std::move(complete));
    };
    runtime.cancelHover = [this] { cancelHover(); };
    runtime.presentHover = [this](const NativeTimelineHoverCard& card) {
        return hoverCard_ ? hoverCard_->present(card) : QStringLiteral("native-hover-surface-unavailable");
    };
    runtime.hideHover = [this] { if (hoverCard_) hoverCard_->hide(); };
    try
    {
        host_ = std::make_unique<WebViewSessionHost>(incarnation_, productViewport(),
            std::move(preview), std::move(runtime), webControl());
        host_->bindTrayPresentation(tray_, [guard = QPointer<ModelCSessionWindow>{this}] {
            return guard && !guard->releasing_ && !guard->dialog_ && !guard->closeIntentQueued_
                && !guard->controlClosePending_ && guard->outstandingWebCloseRequests_ == 0;
        });
        host_->setProductPresentationObserver(
            [guard = QPointer<ModelCSessionWindow>{this}](ProductPresentationStage stage, const QString& detail) {
                if (!guard) return;
                guard->setProductPresentationStage(stage, detail);
                guard->publishPlatformFacts();
            });
        setProductContent(host_.get());
    }
    catch (const std::exception& error)
    {
        if (host_)
        {
            host_->setProductPresentationObserver({});
            setProductContent(nullptr);
        }
        host_.reset();
        qCritical("[SessionPresentation] WebView setup failed: %s", error.what());
        setProductPresentationStage(ProductPresentationStage::Failed, QString::fromUtf8(error.what()));
    }
    publishPlatformFacts();
}

void ModelCSessionWindow::failControlStartup(const QString& detail)
{
    controlStartupFailed_ = true;
    setProductPresentationStage(ProductPresentationStage::Failed, detail);
    resumeStartupClose();
}

SessionPlatformFacts ModelCSessionWindow::currentPlatformFacts() const
{
    SessionPlatformFacts observed;
    observed.visible = isVisible();
    observed.minimized = isMinimized();
    observed.maximized = isMaximized();
    observed.hideEligible = tray_.eligible();
    observed.hiddenToTray = tray_.hiddenToTray();
    observed.presentationEnding = tray_.ending();
    observed.closeInteractionPending = tray_.closePending();
    observed.desktopTargetRegistered = desktopTarget_.isValid();
    observed.desktopHostOwned = desktopHost_.nativeHandle() != nullptr;
    observed.desktopHealthy = desktopHost_.isValid();
    observed.desktopRetained = observed.desktopTargetRegistered || observed.desktopHostOwned;
    return observed;
}

void ModelCSessionWindow::publishPlatformFacts()
{
    if (!started_ || releasing_) return;
    const auto observed = currentPlatformFacts();
    if (observed == lastPlatformFacts_) return;
    if (control_.submit([observed](SessionApplication& owner) { owner.observePresentationFacts(observed); }))
        lastPlatformFacts_ = observed;
}

void ModelCSessionWindow::requestDesktopDeployment(DesktopDeploymentIntent intent,
    std::function<void(DesktopDeploymentResult)> complete)
{
    if (!complete) return;
    if (!releasing_ && control_.submit([intent, complete, relay = &guiRelay_,
        guard = QPointer<ModelCSessionWindow>{this}](SessionApplication& application) mutable {
        application.requestDetachedDesktopDeployment(intent,
            [owner = QPointer<SessionApplication>{&application}, relay, guard,
                complete = std::move(complete)](DesktopDeploymentResult result) {
                const auto projected = owner ? owner->desktopDeploymentProjection() : DesktopDeploymentProjection{};
                (void)relay->post([guard, complete, projected, result] {
                    if (guard) guard->facts_.desktop = projected;
                    complete(guard ? result : DesktopDeploymentResult::Unavailable);
                });
            });
    })) return;
    complete(DesktopDeploymentResult::Unavailable);
}

void ModelCSessionWindow::scheduleDetachedDesktopTopology()
{
    if (!started_ || releasing_ || desktopTopologyQueued_) return;
    desktopTopologyQueued_ = true;
    QTimer::singleShot(0, this, [this] {
        if (releasing_) return;
        publishPlatformFacts();
        if (control_.submit([](SessionApplication& application) { application.reconcileDetachedDesktopTopology(); }))
        {
            desktopTopologyQueued_ = false;
            return;
        }
        // Keep one coalesced topology fact pending until command capacity is
        // available. Losing a display/Shell notification could strand an HWND.
        QTimer::singleShot(250, this, [this] {
            desktopTopologyQueued_ = false;
            scheduleDetachedDesktopTopology();
        });
    });
}

SessionDesktopAttempt ModelCSessionWindow::reconcileDesktopEndpoint(
    const DisplayId& display, const std::optional<SessionDesktopFrame>& frame)
{
    using Platform::DesktopPresentationHostAttachmentStatus;
    using Platform::DesktopPresentationHostStatus;
    if (releasing_ || tray_.ending()) return SessionDesktopAttempt::Failed;
    if (!frame) return removeDesktopEndpoint() ? SessionDesktopAttempt::Ready : SessionDesktopAttempt::Failed;
    try
    {
        if (desktopTarget_.isValid() && desktopHost_.isValid()
            && desktopHost_.reconcileAttachment(display) == DesktopPresentationHostAttachmentStatus::Ready)
            return SessionDesktopAttempt::Ready;
        if (!removeDesktopEndpoint()) return SessionDesktopAttempt::Failed;

        const auto status = desktopHost_.create(display);
        if (status != DesktopPresentationHostStatus::Ready)
        {
            qWarning("[Player] Desktop unavailable: host status=%d", static_cast<int>(status));
            return status == DesktopPresentationHostStatus::DisplayUnavailable
                || status == DesktopPresentationHostStatus::TransientlyUnavailable
                ? SessionDesktopAttempt::TransientlyUnavailable : SessionDesktopAttempt::Failed;
        }
        desktopTarget_ = presentation_.attachPresentationTarget({
            desktopHost_.nativeHandle(), PresentationTransport::DirectCompositionSwapChain});
        if (desktopTarget_.isValid() && presentation_.setPresentationPacingTarget(desktopTarget_))
        {
            if (presentation_.presentAndConfirm(frame->input, {desktopTarget_, frame->outside}))
                return SessionDesktopAttempt::Ready;
            qWarning("[Player] Desktop target did not present its first frame");
            return removeDesktopEndpoint()
                ? SessionDesktopAttempt::TransientlyUnavailable : SessionDesktopAttempt::Failed;
        }
        qWarning("[Player] Desktop graphics target or pacing selection unavailable");
    }
    catch (const std::exception& error) { qWarning("[Player] Desktop unavailable: %s", error.what()); }
    (void)removeDesktopEndpoint();
    return SessionDesktopAttempt::Failed;
}

bool ModelCSessionWindow::removeDesktopEndpoint() noexcept
{
    if (desktopTarget_.isValid())
    {
        if (!presentation_.detachPresentationTarget(desktopTarget_))
        {
            qWarning("[Player] Desktop detach failed; host retained");
            return false;
        }
        desktopTarget_ = {};
    }
    if (desktopHost_.destroy()) return true;
    qWarning("[Player] Desktop host reset failed; ownership retained");
    return false;
}

std::optional<CanvasOutsideInstruction> ModelCSessionWindow::desktopOutside(CanvasOutsideMode mode) const noexcept
{
    if (!desktopTarget_.isValid()) return std::nullopt;
    return CanvasOutsideInstruction{desktopTarget_, mode};
}

void ModelCSessionWindow::receiveFacts(SessionWebViewFacts facts, QHash<QUuid, QJsonObject> peerHeaders, bool invalidated)
{
    const auto binding = facts.runtime.value(QStringLiteral("bindingToken")).toString();
    const bool retirePreviousHover = binding != facts_.runtime.value(QStringLiteral("bindingToken")).toString()
        || facts.documentIncarnation != facts_.documentIncarnation || facts.hoverSource != facts_.hoverSource
        || timelineDurationMilliseconds(facts.hoverDuration) != timelineDurationMilliseconds(facts_.hoverDuration);
    facts_ = std::move(facts);
    if (retirePreviousHover) retireHover();
    reconcileHoverPreload();
    peerHeaders_ = std::move(peerHeaders);
    if (!facts_.documentDisplayName.isEmpty())
        setWindowTitle(QStringLiteral("DesktopSceneEngine — ") + facts_.documentDisplayName
            + (facts_.dirty ? QStringLiteral("*") : QString{}));
    if (startRequested_ && !started_) startPresentation();
    if (host_)
    {
        if (invalidated) host_->invalidateSnapshot();
        host_->publishRuntimeSample();
        host_->publishAudioEffects();
        host_->publishBgmRuntimeState();
    }
    for (const auto& peer : peers_.values()) if (peer) peer->publish();
    publishPlatformFacts();
}

void ModelCSessionWindow::receiveFrame(DetachedFrame frame)
{
    if (releasing_ || !composition_ || !previewTarget_.isValid() || (isMinimized() && !desktopTarget_.isValid())) return;
    const auto realized = presentation_.present(frame.input, desktopOutside(frame.outside));
    if (realized == PresentationUploadResult::ResourceFailure)
    {
        qWarning().noquote() << "[SessionPresentation] media realization failed for binding" << frame.binding;
    }
    else
        composition_->syncContent(presentation_.acquireHostedCompositionContent(previewTarget_));
    if (host_) host_->publishPreviewPresentation();
    if (frame.ticket && frame.ticket != lastReportedFrame_
        && (realized == PresentationUploadResult::Updated || realized == PresentationUploadResult::ResourceFailure))
    {
        if (control_.submitPlatformResult([ticket = *frame.ticket, realized](SessionApplication& application) {
            application.acceptPresentationResult(ticket, realized);
        })) lastReportedFrame_ = frame.ticket;
    }
}

WebViewSessionHost::ControlSubmit ModelCSessionWindow::webControl(std::optional<BgmPeerIdentity> peer)
{
    return [guard = QPointer<ModelCSessionWindow>{this}, peer](SessionBridge::Request request,
        WebViewSessionHost::ControlCompletion complete) {
        return guard && guard->submitWebCommand(std::move(request), std::move(complete), peer);
    };
}

bool ModelCSessionWindow::submitWebCommand(SessionBridge::Request request,
    WebViewSessionHost::ControlCompletion complete, std::optional<BgmPeerIdentity> peer)
{
    constexpr std::size_t MaximumWebRepliesReserved = 96;
    if (releasing_ || outstandingWebCommands_ >= MaximumWebRepliesReserved) return false;
    const bool closeRequest = request.documentAction == DocumentAction::Close;
    const auto document = facts_.documentIncarnation;
    const auto origin = peer ? peers_.value(peer->permit) : QPointer<BgmPeerWindow>{};
    const bool submitted = control_.submit([request = std::move(request), document, peer, origin,
        incarnation = incarnation_, guard = QPointer<ModelCSessionWindow>{this},
        relay = &guiRelay_, control = &control_, complete = std::move(complete)](SessionApplication& application) mutable {
        auto deliver = [guard, control, origin, relay, request, complete = std::move(complete)]
            (SessionWebViewReply reply, std::optional<bool> closePending = std::nullopt) mutable {
            (void)relay->post([guard, control, origin, request = std::move(request),
                reply = std::move(reply), complete = std::move(complete), closePending]() mutable {
                if (guard)
                {
                    --guard->outstandingWebCommands_;
                    if (closePending)
                    {
                        --guard->outstandingWebCloseRequests_;
                        guard->controlClosePending_ = *closePending;
                    }
                }
                if (auto* admission = std::get_if<BgmPeerAdmission>(&reply))
                {
                    if (guard) guard->receiveBgmAdmission(std::move(*admission), request.generation, std::move(complete));
                    else (void)control->submitPlatformResult([permit = admission->identity.permit](SessionApplication& owner) {
                        owner.revokeBgmEditPermit(permit);
                    });
                    return;
                }
                if (const auto* identify = std::get_if<DisplayIdentifyRequest>(&reply))
                {
                    // The command may have been admitted just before reload or close.
                    if (!guard || guard->releasing_ || !guard->host_ || guard->dialog_ || guard->controlClosePending_
                        || guard->closeIntentQueued_ || guard->outstandingWebCloseRequests_ != 0
                        || guard->host_->pageGeneration() != request.generation
                        || !request.pageAdmission || !request.pageAdmission->load(std::memory_order_acquire))
                    {
                        complete(WebViewSessionHost::ControlError{QStringLiteral("stale-generation")});
                        return;
                    }
                    auto status = Platform::DisplayIdentifyStatus::ApiFailure;
                    try { status = Platform::identifyDisplay(identify->displayId, Platform::DisplayIdentifyDurationMs); }
                    catch (...) { /* Report the native presentation failure and refresh the display facts. */ }
                    if (status != Platform::DisplayIdentifyStatus::Shown)
                        (void)control->submitPlatformResult([](SessionApplication& application) {
                            application.refreshDisplaySelection();
                        });
                    complete(status);
                    return;
                }
                auto payload = std::get<WebViewSessionHost::ControlReply>(std::move(reply));
                if (guard && origin && request.bgmAuthored && request.bgmAuthored->action == BgmAuthoredAction::AddTracks)
                    if (const auto* result = std::get_if<AuthoredResult>(&payload);
                        result && result->status == AuthoredStatus::Accepted && !result->token.isNull())
                        guard->importFocus_.insert(result->token, origin);
                complete(std::move(payload));
            });
        };
        SessionReadModel model(application, incarnation);
        if (request.runtime)
        {
            if (const auto error = validateSessionWebViewCommand(application, model, request, document, peer))
                deliver(WebViewSessionHost::ControlReply{*error});
            else
                application.applyRuntimeOperationDeferred(*request.runtime, [deliver = std::move(deliver)](RuntimeResult result) mutable {
                    deliver(WebViewSessionHost::ControlReply{std::move(result)});
                });
        }
        else if (request.authored)
        {
            if (const auto error = validateSessionWebViewCommand(application, model, request, document, peer))
                deliver(WebViewSessionHost::ControlReply{*error});
            else
                application.applyAuthoredOperationDeferred(*request.authored, [deliver = std::move(deliver)](AuthoredResult result) mutable {
                    deliver(WebViewSessionHost::ControlReply{std::move(result)});
                });
        }
        else if (request.desktopDeployment)
        {
            if (const auto error = validateSessionWebViewCommand(application, model, request, document, peer))
                deliver(WebViewSessionHost::ControlReply{*error});
            else
                application.requestDetachedDesktopDeployment(*request.desktopDeployment,
                    [deliver = std::move(deliver)](DesktopDeploymentResult result) mutable {
                        deliver(WebViewSessionHost::ControlReply{result});
                    });
        }
        else
        {
            auto reply = dispatchSessionWebViewCommand(application, model, request, document, peer);
            deliver(std::move(reply), request.documentAction == DocumentAction::Close
                ? std::optional{application.closePending()} : std::nullopt);
        }
    });
    if (submitted)
    {
        ++outstandingWebCommands_;
        if (closeRequest) ++outstandingWebCloseRequests_;
    }
    return submitted;
}

void ModelCSessionWindow::receiveBgmAdmission(BgmPeerAdmission admission, const QString& sourceGeneration,
    WebViewSessionHost::ControlCompletion complete)
{
    using Reply = WebViewSessionHost::OpenBgmEditorResult;
    const auto identity = admission.identity;
    if (releasing_ || !host_ || host_->pageGeneration() != sourceGeneration
        || facts_.documentIncarnation != identity.documentIncarnation)
    {
        revokePeer(identity.permit);
        complete(Reply{QStringLiteral("The initiating Session page has retired.")});
        return;
    }
    for (const auto& existing : peers_.values())
    {
        if (!existing || existing->playlistId() != identity.playlist) continue;
        revokePeer(identity.permit);
        if (existing->isMinimized()) existing->showNormal();
        existing->raise();
        existing->activateWindow();
        complete(Reply{});
        return;
    }

    WebViewSessionHost::BgmPresentation bgm;
    bgm.permit = identity.permit;
    bgm.playlistId = identity.playlist;
    bgm.header = [this, permit = identity.permit] {
        if (!peerHeaders_.contains(permit)) throw std::logic_error("The BGM peer permit has retired");
        return peerHeaders_.value(permit);
    };
    WebViewSessionHost::RuntimePresentation runtime;
    runtime.audioEffects = [this] { return facts_.audioEffects; };
    peerHeaders_.insert(identity.permit, std::move(admission.header));
    try
    {
        auto* peer = new BgmPeerWindow(incarnation_, host_.get(), std::move(bgm), std::move(runtime),
            [guard = QPointer<ModelCSessionWindow>{this}, permit = identity.permit] {
                if (!guard) return;
                guard->peers_.remove(permit);
                guard->peerHeaders_.remove(permit);
                guard->revokePeer(permit);
            }, webControl(identity), [guard = QPointer<ModelCSessionWindow>{this}, permit = identity.permit] {
                if (!guard || !guard->dialog_ || !guard->dialog_->modalOwner) return false;
                return guard->peers_.value(permit).data() == guard->dialog_->modalOwner.data();
            });
        const QPointer<BgmPeerWindow> guard{peer};
        peers_.insert(identity.permit, peer);
        peer->publish();
        if (guard && guard->showForFirstPresentation())
        {
            if (guard && guard->isVisible()) guard->raise();
            if (guard && guard->isVisible()) guard->activateWindow();
            if (guard && guard->isVisible())
            {
                complete(Reply{});
                return;
            }
        }
        complete(Reply{QStringLiteral("Native Background Music editor is unavailable.")});
    }
    catch (...)
    {
        peerHeaders_.remove(identity.permit);
        revokePeer(identity.permit);
        complete(Reply{QStringLiteral("Native Background Music editor is unavailable.")});
    }
}

void ModelCSessionWindow::revokePeer(const QUuid& permit)
{
    (void)control_.submitPlatformResult([permit](SessionApplication& owner) { owner.revokeBgmEditPermit(permit); });
}

void ModelCSessionWindow::closePeer(PlaylistId playlist)
{
    for (const auto& peer : peers_.values()) if (peer && peer->playlistId() == playlist) peer->close();
}

void ModelCSessionWindow::closePeers()
{
    for (const auto& peer : peers_.values()) if (peer) peer->close();
}

void ModelCSessionWindow::restoreImportFocus(const QUuid& token)
{
    const auto peer = importFocus_.take(token);
    if (!peer || peers_.value(peer->permit()) != peer || !peer->isProductPresentationAvailable())
        return;
    if (auto* host = peer->host()) host->reconcileForegroundKeyboardFocus(peer->effectiveWinId());
}

void ModelCSessionWindow::showNotice(SessionNotice kind, const QString& title, const QString& message)
{
    if (host_ && !releasing_) host_->showSessionNotice(kind, title, message);
}

void ModelCSessionWindow::watchDialog(QDialog& dialog, SessionPlatformRequest request,
    std::function<void(int)> complete, std::unique_ptr<QDialog> owned, BgmPeerWindow* modalOwner)
{
    if (dialog_) throw std::logic_error("Only one admitted native dialog can be active");
    dialog_ = DialogState{&dialog, request, {}, std::move(owned), modalOwner};
    dialog.setAttribute(Qt::WA_DeleteOnClose, !dialog_->owned);
    dialog.setWindowModality(Qt::WindowModal);
    connect(&dialog, &QDialog::finished, this, [this, request, complete = std::move(complete)](int result) {
        if (!dialog_ || dialog_->request.dispatch != request.dispatch) return;
        auto retired = std::move(dialog_->retired);
        const auto owningPeer = dialog_->modalOwner;
        if (auto* ownedDialog = dialog_->owned.release()) ownedDialog->deleteLater();
        dialog_.reset();
        if (retired) retired();
        else complete(result);
        if (owningPeer) owningPeer->resumeModalClose();
    });
    dialog.open();
}

void ModelCSessionWindow::showDocumentPicker(const SessionPlatformRequest& request, bool save,
    const std::optional<std::filesystem::path>& current, SessionPlatform::DocumentPathResult complete)
{
    const auto initial = save && current ? QString::fromStdWString(current->wstring()) : QString{};
    auto* picker = new QFileDialog(this, save ? text("Save Project As") : text("Open Project"), initial,
        text("DesktopSceneEngine Project (*.json);;All Files (*)"));
    picker->setAcceptMode(save ? QFileDialog::AcceptSave : QFileDialog::AcceptOpen);
    picker->setFileMode(save ? QFileDialog::AnyFile : QFileDialog::ExistingFile);
    if (save) picker->setDefaultSuffix(QStringLiteral("json"));
    watchDialog(*picker, request, [picker, complete = std::move(complete)](int result) mutable {
        const auto files = picker->selectedFiles();
        const auto path = result == QDialog::Accepted && !files.isEmpty()
            ? std::optional{std::filesystem::path{files.constFirst().toStdWString()}} : std::nullopt;
        complete(path);
    });
}

void ModelCSessionWindow::showMediaPicker(const SessionPlatformRequest& request, bool music,
    SessionPlatform::MediaSourcesResult complete)
{
    const auto filter = music
        ? text("Supported Background Music (*.mp3 *.aac *.m4a *.flac *.wav *.ogg *.opus *.wma *.alac *.aiff *.aif *.ape);;Tier 1 Audio (*.mp3 *.aac *.m4a *.flac *.wav *.ogg *.opus);;Tier 2 Audio (*.wma *.alac *.aiff *.aif *.ape)")
        : text("Media Files (*.png *.jpg *.jpeg *.webp *.mp4 *.avi *.mov *.mkv *.webm)");
    const auto peer = music ? importFocus_.value(request.operation) : QPointer<BgmPeerWindow>{};
    auto* modalOwner = peer && peers_.value(peer->permit()) == peer && peer->isVisible() && !peer->isMinimized()
        ? peer.data() : nullptr;
    auto pickerLifetime = std::make_unique<QFileDialog>(modalOwner ? nullptr : this,
        music ? text("Add Music") : text("Add Media"), QString{}, filter);
    auto* picker = pickerLifetime.get();
    if (modalOwner)
    {
        // QFileDialog uses a QWidget parent first for its native owner.
        // The transient QWindow provides peer modality while Session owns the picker.
        (void)picker->winId();
        picker->windowHandle()->setTransientParent(modalOwner->windowHandle());
    }
    picker->setAcceptMode(QFileDialog::AcceptOpen);
    picker->setFileMode(QFileDialog::ExistingFiles);
    if (!modalOwner) pickerLifetime.release(); // Ordinary Qt parent and delete-on-close.
    watchDialog(*picker, request, [picker, complete = std::move(complete)](int result) mutable {
        complete(result == QDialog::Accepted ? picker->selectedFiles() : QStringList{});
    }, std::move(pickerLifetime), modalOwner);
}

void ModelCSessionWindow::retireDialog(const SessionPlatformRequest& request, std::function<void()> retired)
{
    if (!dialog_ || dialog_->request.dispatch != request.dispatch || !dialog_->dialog)
    {
        retired();
        return;
    }
    dialog_->retired = std::move(retired);
    dialog_->dialog->reject();
}

void ModelCSessionWindow::beginPresentationTeardown()
{
    tray_.beginTeardown();
    closePeers();
    retireHover();
    reconcileHoverPreload();
    if (host_) host_->closeHost();
}

void ModelCSessionWindow::releasePresentation()
{
    if (std::exchange(releasing_, true)) return;
    if (dialog_ && dialog_->owned)
    {
        auto dialog = std::move(dialog_->owned);
        dialog_.reset();
        dialog.reset();
    }
    beginPresentationTeardown();
    if (composition_ && composition_->clearPreviewContent() < 0)
        qCritical("[ModelC] retiring composition owner after content detach failure");
    composition_.reset();
    // A desktop host can be destroyed only after every graphics borrow ends.
    (void)removeDesktopEndpoint();
    presentation_.shutdown();
    desktopTarget_ = {};
    if (!desktopHost_.destroy()) qCritical("[Player] Desktop host was retained after graphics shutdown");
    previewTarget_ = {};
}

void ModelCSessionWindow::settleClose(bool accepted, bool pending)
{
    controlClosePending_ = pending;
    tray_.completeClose(accepted);
    if (accepted && acceptedClose_) acceptedClose_();
}

void ModelCSessionWindow::resumeStartupClose()
{
    if (std::exchange(closeDuringStartup_, false))
        QTimer::singleShot(0, this, [this] { close(); });
}

void ModelCSessionWindow::closeEvent(QCloseEvent* event)
{
    if (controlStartupFailed_)
    {
        releasePresentation();
        ProductWindowHost::closeEvent(event);
        if (event->isAccepted()) settleClose(true, false);
        return;
    }
    if (finalClose_)
    {
        ProductWindowHost::closeEvent(event);
        (void)control_.submitPlatformResult([accepted = event->isAccepted()](SessionApplication& application) {
            application.completePlatformClose(accepted);
        });
        return;
    }
    event->ignore();
    if (!startRequested_)
    {
        closeDuringStartup_ = true;
        return;
    }
    const bool external = tray_.closePending();
    if (closeIntentQueued_)
    {
        // A coordinator operation is independent of a queued caption Close.
        // It must get a terminal rejection rather than disappear into that
        // local operation's coalescing latch.
        if (external && !closeIntentExternal_) tray_.completeClose(false);
        return;
    }
    if (!controlClosePending_ && host_ && host_->hasSessionNotice())
    {
        if (external && !closeFailureNoticePending_) tray_.completeClose(false);
        return;
    }
    closeIntentExternal_ = external;
    closeIntentQueued_ = control_.submitPlatformResult([external, relay = &guiRelay_,
        guard = QPointer<ModelCSessionWindow>{this}](SessionApplication& application) {
        application.requestClose(external);
        (void)relay->post([guard, pending = application.closePending()] {
            if (!guard) return;
            guard->controlClosePending_ = pending;
            guard->closeIntentQueued_ = false;
            guard->closeIntentExternal_ = false;
        });
    });
    if (!closeIntentQueued_)
    {
        closeIntentExternal_ = false;
        if (external) tray_.completeClose(false);
    }
}

bool ModelCSessionWindow::event(QEvent* event)
{
    const auto type = event->type();
    const bool handled = ProductWindowHost::event(event);
    if (type == QEvent::WindowStateChange && composition_)
    {
        if (isMinimized()) composition_->retirePreview(QStringLiteral("session-minimized"));
        if (host_) host_->refreshPreviewGeometry();
    }
    if (type == QEvent::Show || type == QEvent::Hide || type == QEvent::WindowStateChange
        || type == QEvent::WindowActivate || type == QEvent::WindowDeactivate)
        publishPlatformFacts();
    return handled;
}

bool ModelCSessionWindow::nativeEvent(const QByteArray& type, void* message, qintptr* result)
{
    if (type == "windows_generic_MSG" && message)
    {
        const auto kind = static_cast<const MSG*>(message)->message;
        if (settingsMessage_ != 0 && kind == settingsMessage_)
        {
        if (!languageReloadQueued_)
            languageReloadQueued_ = control_.submitPlatformResult([relay = &guiRelay_,
                guard = QPointer<ModelCSessionWindow>{this}](SessionApplication& application) {
                application.reloadCommittedLanguage();
                (void)relay->post([guard] { if (guard) guard->languageReloadQueued_ = false; });
            });
            if (result) *result = 0;
            return true;
        }
        if (kind == WM_DISPLAYCHANGE || (taskbarCreatedMessage_ != 0 && kind == taskbarCreatedMessage_))
            scheduleDetachedDesktopTopology();
    }
    return ProductWindowHost::nativeEvent(type, message, result);
}

void ModelCSessionWindow::requestHover(const TimelineHoverRequest& request,
    std::function<void(TimelineHoverResult)> complete)
{
    cancelHover();
    if (!facts_.hoverSource || facts_.hoverSource->itemId.value != request.itemId
        || facts_.runtime.value("bindingToken").toString() != request.bindingToken)
    {
        complete({QStringLiteral("stale"), request.bindingToken, request.itemId, request.serial, request.time, {}});
        return;
    }
    const auto duration = timelineDurationMilliseconds(facts_.hoverDuration);
    if (!hoverWorker_ || !duration || !std::isfinite(request.time) || request.time < 0.0
        || request.time > facts_.hoverDuration)
    {
        complete({QStringLiteral("unavailable"), request.bindingToken, request.itemId, request.serial, request.time, {}});
        return;
    }
    TimelinePreviewRequest native;
    native.source = *facts_.hoverSource;
    native.timeSeconds = static_cast<float>(request.time);
    native.durationMilliseconds = *duration;
    native.generation = ++hoverSerial_;
    native.serial = hoverSerial_;
    if (native.source.mediaType == WorkspaceMediaType::Video)
        native.representativeTimestampMilliseconds = representativeTimestampForTimelineHover(
            native.durationMilliseconds, static_cast<std::int64_t>(request.time * 1000.0));
    hover_ = HoverState{request, native, std::move(complete)};
    if (hoverRequests_.submit(native))
        QMetaObject::invokeMethod(hoverWorker_, &TimelinePreviewWorker::processLatest, Qt::QueuedConnection);
}

void ModelCSessionWindow::cancelHover()
{
    if (!hover_) return;
    auto pending = std::exchange(hover_, std::nullopt);
    hoverRequests_.invalidatePending();
    const auto& request = pending->request;
    pending->complete({QStringLiteral("cancelled"), request.bindingToken, request.itemId, request.serial, request.time, {}});
}

void ModelCSessionWindow::retireHover()
{
    cancelHover();
    if (hoverCard_) hoverCard_->hide();
}

void ModelCSessionWindow::reconcileHoverPreload()
{
    std::optional<HoverPreloadTarget> next;
    if (!tray_.ending() && facts_.hoverSource && facts_.hoverSource->mediaType == WorkspaceMediaType::Video)
    {
        if (const auto duration = timelineDurationMilliseconds(facts_.hoverDuration))
            next = HoverPreloadTarget{facts_.documentIncarnation, *facts_.hoverSource, *duration};
    }
    if (next == hoverPreloadTarget_) return;
    hoverPreloadTarget_ = next;
    const auto generation = ++hoverPreloadGeneration_;
    hoverRequests_.fencePreloadGeneration(generation);
    if (hoverWorker_ && hoverThread_.isRunning())
    {
        QMetaObject::invokeMethod(hoverWorker_, [worker = hoverWorker_, next = std::move(next), generation] {
            if (next) worker->reconcilePreloadTarget(next->source, next->durationMilliseconds, generation);
            else worker->invalidatePreload(generation);
        }, Qt::QueuedConnection);
    }
}

void ModelCSessionWindow::receiveHover(const TimelinePreviewResult& result)
{
    if (!hover_ || hover_->preview.serial != result.request.serial
        || hover_->preview.source != result.request.source) return;
    auto pending = std::exchange(hover_, std::nullopt);
    const auto& request = pending->request;
    QImage image;
    if (facts_.hoverScene && !result.image.isNull() && facts_.hoverSource == pending->preview.source
        && request.bindingToken == facts_.runtime.value("bindingToken").toString())
        image = composeTimelineCanvasPreview(result.image, *facts_.hoverScene,
            timelinePreviewPopupCanvasSize(facts_.canvasAspect));
    pending->complete({image.isNull() ? QStringLiteral("unavailable") : QStringLiteral("ready"),
        request.bindingToken, request.itemId, request.serial, request.time, std::move(image)});
}
}
