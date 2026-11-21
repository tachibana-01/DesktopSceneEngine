#pragma once

#include "ApplicationPreferences.h"
#include "ProductWindowHost.h"
#include "SessionControlExecutor.h"
#include "SessionGuiRelay.h"
#include "SessionPlatform.h"
#include "SessionPresentation.h"
#include "SessionWebViewControl.h"
#include "TimelinePreviewWorker.h"
#include <DesktopSceneEngine/EnginePresentation.h>
#include <DesktopSceneEngine/Platform/DesktopPresentationHost.h>
#include <QHash>
#include <QPointer>
#include <functional>
#include <memory>
#include <optional>

class QCloseEvent;
class QDialog;

namespace DesktopSceneEngine::Player
{
class BgmPeerWindow;
class ModelCControlPlatform;
class NativeTimelineHoverWindow;
class SessionWebViewComposition;

// The product HWND hosts Model C presentation while the process owns autonomous control.
class ModelCSessionWindow final : public ProductWindowHost
{
public:
    ModelCSessionWindow(SessionControlExecutor& control, SessionGuiRelay& guiRelay,
        DisplayId display, ApplicationTranslationState& translation, QUuid incarnation);
    ~ModelCSessionWindow() override;
    void startPresentation();
    void failControlStartup(const QString& detail);
    [[nodiscard]] SessionControlExecutor::PlatformFactory platformFactory();
    [[nodiscard]] SessionPresentation& trayPresentation() noexcept { return tray_; }
    [[nodiscard]] DesktopDeploymentProjection desktopDeploymentProjection() const noexcept { return facts_.desktop; }
    void requestDesktopDeployment(DesktopDeploymentIntent intent, std::function<void(DesktopDeploymentResult)> complete);
    void setAcceptedCloseCallback(std::function<void()> callback) { acceptedClose_ = std::move(callback); }
    void releasePresentation();

protected:
    bool event(QEvent* event) override;
    bool nativeEvent(const QByteArray& type, void* message, qintptr* result) override;
    void closeEvent(QCloseEvent* event) override;

private:
    friend class ModelCControlPlatform;
    enum class DialogKind { Open, Save, Media, Music, Decision };
    struct DialogState
    {
        QPointer<QDialog> dialog;
        SessionPlatformRequest request;
        std::function<void()> retired;
        std::unique_ptr<QDialog> owned;
        QPointer<BgmPeerWindow> modalOwner;
    };
    struct HoverState
    {
        TimelineHoverRequest request;
        TimelinePreviewRequest preview;
        std::function<void(TimelineHoverResult)> complete;
    };
    struct HoverPreloadTarget
    {
        QUuid documentIncarnation;
        TimelinePreviewSource source;
        std::int64_t durationMilliseconds;
        bool operator==(const HoverPreloadTarget&) const = default;
    };
    struct DetachedFrame
    {
        PresentationInput input;
        QString binding;
        CanvasOutsideMode outside;
        std::optional<Engine::MediaPreparationToken> ticket;
    };

    [[nodiscard]] SessionPlatformFacts currentPlatformFacts() const;
    void publishPlatformFacts();
    void scheduleDetachedDesktopTopology();
    [[nodiscard]] SessionDesktopAttempt reconcileDesktopEndpoint(
        const DisplayId& display, const std::optional<SessionDesktopFrame>& frame);
    [[nodiscard]] bool removeDesktopEndpoint() noexcept;
    [[nodiscard]] std::optional<CanvasOutsideInstruction> desktopOutside(CanvasOutsideMode mode) const noexcept;
    void receiveFacts(SessionWebViewFacts facts, QHash<QUuid, QJsonObject> peerHeaders, bool invalidated);
    void receiveFrame(DetachedFrame frame);
    [[nodiscard]] WebViewSessionHost::ControlSubmit webControl(std::optional<BgmPeerIdentity> peer = {});
    [[nodiscard]] bool submitWebCommand(SessionBridge::Request request,
        WebViewSessionHost::ControlCompletion complete, std::optional<BgmPeerIdentity> peer);
    void revokePeer(const QUuid& permit);
    void receiveBgmAdmission(BgmPeerAdmission admission, const QString& sourceGeneration,
        WebViewSessionHost::ControlCompletion complete);
    void closePeer(PlaylistId playlist);
    void closePeers();
    void restoreImportFocus(const QUuid& token);
    void showNotice(SessionNotice kind, const QString& title, const QString& message);
    void showDocumentPicker(const SessionPlatformRequest& request, bool save,
        const std::optional<std::filesystem::path>& current, SessionPlatform::DocumentPathResult complete);
    void showMediaPicker(const SessionPlatformRequest& request, bool music, SessionPlatform::MediaSourcesResult complete);
    void watchDialog(QDialog& dialog, SessionPlatformRequest request, std::function<void(int)> complete,
        std::unique_ptr<QDialog> owned = {}, BgmPeerWindow* modalOwner = nullptr);
    void retireDialog(const SessionPlatformRequest& request, std::function<void()> retired);
    void beginPresentationTeardown();
    void settleClose(bool accepted, bool pending);
    void resumeStartupClose();
    void requestHover(const TimelineHoverRequest& request, std::function<void(TimelineHoverResult)> complete);
    void cancelHover();
    void retireHover();
    void reconcileHoverPreload();
    void receiveHover(const TimelinePreviewResult& result);

    SessionControlExecutor& control_;
    SessionGuiRelay& guiRelay_;
    const DisplayId display_;
    ApplicationTranslationState& translation_;
    const QUuid incarnation_;
    SessionPresentation tray_{*this};
    EnginePresentation presentation_;
    PresentationTargetId previewTarget_;
    Platform::DesktopPresentationHost desktopHost_;
    PresentationTargetId desktopTarget_;
    std::unique_ptr<SessionWebViewComposition> composition_;
    std::unique_ptr<NativeTimelineHoverWindow> hoverCard_;
    std::unique_ptr<WebViewSessionHost> host_;
    QHash<QUuid, QPointer<BgmPeerWindow>> peers_;
    QHash<QUuid, QJsonObject> peerHeaders_;
    QHash<QUuid, QPointer<BgmPeerWindow>> importFocus_;
    SessionWebViewFacts facts_;
    SessionPlatformFacts lastPlatformFacts_;
    std::optional<DialogState> dialog_;
    std::optional<HoverState> hover_;
    std::optional<HoverPreloadTarget> hoverPreloadTarget_;
    TimelinePreviewRequestState hoverRequests_;
    QThread hoverThread_;
    TimelinePreviewWorker* hoverWorker_ = nullptr;
    std::uint64_t hoverSerial_ = 0;
    std::uint64_t hoverPreloadGeneration_ = 0;
    bool started_ = false;
    bool startRequested_ = false;
    bool controlStartupFailed_ = false;
    bool closeDuringStartup_ = false;
    bool finalClose_ = false;
    bool closeIntentQueued_ = false;
    bool closeIntentExternal_ = false;
    bool controlClosePending_ = false;
    bool closeFailureNoticePending_ = false;
    bool languageReloadQueued_ = false;
    bool releasing_ = false;
    bool desktopTopologyQueued_ = false;
    unsigned settingsMessage_ = 0;
    std::size_t outstandingWebCommands_ = 0;
    std::size_t outstandingWebCloseRequests_ = 0;
    unsigned taskbarCreatedMessage_ = 0;
    std::optional<Engine::MediaPreparationToken> lastReportedFrame_;
    std::function<void()> acceptedClose_;
};
}
