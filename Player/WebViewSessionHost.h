#pragma once

#include <DesktopSceneEngine/Platform/Display.h>

#include <QJsonObject>
#include <QRect>
#include <QUuid>
#include <QWidget>
#include <functional>
#include <memory>
#include <variant>
#include "DesktopDeployment.h"
#include "SessionDocumentTransaction.h"
#include "SessionAuthoredOperation.h"
#include "SessionRuntimeOperation.h"
#include "NativeTimelineHoverWindow.h"

namespace DesktopSceneEngine::Player
{
class SessionWebViewComposition;
class SessionPresentation;
enum class ProductPresentationStage;
enum class SessionNotice;
namespace SessionBridge { struct Request; }
// Child presentation only. The native Session root, Engine and application
// owner remain independent of this widget and every WebView document/controller.
class WebViewSessionHost final : public QWidget
{
public:
    struct PreviewPresentation
    {
        // Borrowed from Session. Host is retired before the Session target.
        SessionWebViewComposition* composition = nullptr;
        // Model C allocation input. It grants no Engine rendering authority to the Host.
        std::function<bool(const QSize&)> requestExtent;
    };
    struct RuntimePresentation
    {
        std::function<QJsonObject()> sample;
        std::function<void(const TimelineHoverRequest&, std::function<void(TimelineHoverResult)>)> hover;
        std::function<void()> cancelHover;
        std::function<QString(const NativeTimelineHoverCard&)> presentHover;
        std::function<void()> hideHover;
        std::function<QJsonObject()> audioEffects;
    };
    struct BgmPresentation
    {
        // Issued by native peer admission, immutable for this peer context.
        QUuid permit;
        PlaylistId playlistId;
        std::function<QJsonObject()> header;
        std::function<void()> close;
    };
    struct OpenBgmEditorResult { QString error; };
    struct ControlError { QString code; };
    struct MainSnapshotResult { QJsonObject snapshot; };
    struct BgmSnapshotResult { QJsonObject snapshot; QJsonObject header; };
    struct RuntimeSampleResult { QJsonObject sample; };
    struct ViewPreferenceResult { QString status; };
    using ControlReply = std::variant<DocumentCommandResult, AuthoredResult,
        RuntimeResult, BgmRuntimeResult, OpenBgmEditorResult, ControlError,
        MainSnapshotResult, BgmSnapshotResult, RuntimeSampleResult, ViewPreferenceResult,
        DesktopDeploymentResult, Platform::DisplayIdentifyStatus>;
    using ControlCompletion = std::function<void(ControlReply)>;
    // The command is already parsed and fenced to this page. The owner validates
    // audioDocument against current control truth and completes on the GUI relay.
    using ControlSubmit = std::function<bool(SessionBridge::Request, ControlCompletion)>;

    WebViewSessionHost(const QUuid& sessionIncarnation, QWidget* parent, PreviewPresentation preview,
        RuntimePresentation runtime, ControlSubmit submit);
    WebViewSessionHost(const QUuid& sessionIncarnation, QWidget* parent, BgmPresentation bgm,
        RuntimePresentation runtime, ControlSubmit submit, WebViewSessionHost* environmentOwner);
    ~WebViewSessionHost() override;
    using ProductPresentationObserver = std::function<void(ProductPresentationStage, const QString&)>;
    void setProductPresentationObserver(ProductPresentationObserver observer);
    void bindTrayPresentation(SessionPresentation& presentation, std::function<bool()> canRequestHide);
    void invalidateSnapshot();
    bool showSessionNotice(SessionNotice kind, const QString& title, const QString& message,
        std::function<void()> finished = {});
    [[nodiscard]] bool hasSessionNotice() const;
    void closeHost();
    [[nodiscard]] bool isWebViewReady() const;
    [[nodiscard]] bool isProductPresentationReady() const;
    void reconcileForegroundKeyboardFocus(WId owningWindow);
    [[nodiscard]] QString pageGeneration() const;
    void refreshPreviewGeometry();
    void publishRuntimeSample(bool immediate = false);
    void publishBgmRuntimeState();
    void publishBgmEditorState();
    void publishAudioEffects();
    [[nodiscard]] bool sharesEnvironmentWith(const WebViewSessionHost& other) const;
    void publishPreviewPresentation();
    [[nodiscard]] QJsonObject presentationFacts() const;

protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    friend struct SessionHostLifetimeTestAccess;
    struct State;
    WebViewSessionHost(QWidget* parent, std::shared_ptr<State> state, WebViewSessionHost* environmentOwner);
    std::shared_ptr<State> state_;
};
}
