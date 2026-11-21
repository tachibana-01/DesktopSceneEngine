#include "WebViewSessionHost.h"
#include "ProductWindowHost.h"
#include "ProductWebViewChrome.h"
#include "SessionWebViewComposition.h"
#include "SessionCompositionInput.h"
#include "SessionBridgeContract.h"
#include "SessionPlatform.h"
#include "SessionPresentation.h"
#include "LauncherBridgeContract.h" // Existing factory-return gate only; no Launcher dispatch/DTO.

#include <QElapsedTimer>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QFocusEvent>
#include <QHideEvent>
#include <QJsonDocument>
#include <QLabel>
#include <QMoveEvent>
#include <QPalette>
#include <QPointer>
#include <QResizeEvent>
#include <QShowEvent>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <array>
#include <chrono>
#include <deque>
#include <stdexcept>
#include <type_traits>
#include <utility>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <wrl.h>
#include <WebView2.h>

namespace DesktopSceneEngine::Player
{
using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
namespace
{
struct SessionGuiApartment
{
    HRESULT status = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ~SessionGuiApartment() { if (SUCCEEDED(status)) CoUninitialize(); }
};

template<class Interface>
struct SessionCreationResult
{
    LauncherBridge::CreationOperation operation;
    HRESULT callbackStatus = E_PENDING;
    ComPtr<Interface> value;
    QString stage;
};

QString takeSessionString(LPWSTR value)
{
    const auto result = value ? QString::fromWCharArray(value) : QString{};
    CoTaskMemFree(value);
    return result;
}
}

struct WebViewSessionHost::State : std::enable_shared_from_this<State>
{
    explicit State(const QUuid& incarnation, SessionBridge::Surface ownerSurface = SessionBridge::Surface::Main,
        PreviewPresentation previewPresentation = {}, RuntimePresentation runtimePresentation = {},
        BgmPresentation bgmPresentation = {}, ControlSubmit submit = {})
        : surface(ownerSurface), page(incarnation, surface), preview(std::move(previewPresentation)),
          runtime(std::move(runtimePresentation)), bgm(std::move(bgmPresentation)), controlSubmit(std::move(submit)) {}

    const SessionBridge::Surface surface;
    QPointer<WebViewSessionHost> owner;
    QPointer<QLabel> statusLabel;
    ComPtr<ICoreWebView2Environment> environment;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2CompositionController> compositionController;
    std::unique_ptr<SessionWebViewComposition::Binding> compositionBinding;
    std::unique_ptr<OpaqueWebViewVisual> opaqueVisual;
    std::unique_ptr<SessionCompositionInput> compositionInput;
    ProductWebViewChrome chrome;
    ComPtr<ICoreWebView2> web;
    SessionBridge::PageFence page;
    PreviewPresentation preview;
    RuntimePresentation runtime;
    BgmPresentation bgm;
    ControlSubmit controlSubmit;
    std::shared_ptr<std::atomic_bool> controlPageAdmission;
    bool borrowedEnvironment = false;
    std::optional<QJsonObject> bgmHeader;
    QElapsedTimer bgmHeaderCadence;
    bool runtimeSubscribed = false;
    bool audioEffectsNegotiated = false;
    std::optional<QJsonObject> observedAudioEffects;
    QElapsedTimer runtimeCadence;
    struct BgmBinding
    {
        QUuid permit;
        BgmTarget target;
        BgmRuntimeState last;
        quint64 subscription;
        bool operator==(const BgmBinding&) const = default;
    };
    // At most the captured Playlist and Item on this page. Page retirement
    // drops presentation bindings, never the native music sessions.
    std::array<std::optional<BgmBinding>, 2> bgmBindings;
    std::array<std::optional<quint64>, 2> bgmPolls;
    quint64 nextBgmSubscription = 0;
    QElapsedTimer bgmCadence;
    quint64 hoverEpoch = 0;
    quint32 hoverSerial = 0;
    QString observedBinding;
    QImage hoverFrame;
    // One displayed image, separate from the newer pending request. Binding/item
    // identify its source; serial/time still identify each provider completion.
    std::optional<TimelineHoverRequest> hoverReadyTarget;
    quint32 hoverFrameSerial = 0;
    QString hoverFrameStatus = QStringLiteral("pending");
    quint32 hoverPresentationSerial = 0;
    quint32 hoverCancelledSerial = 0;
    std::optional<TimelineHoverRequest> hoverTarget;
    QString hoverTargetLabel;
    std::optional<TimelineHoverPresentation::Intent> hoverView;
    std::optional<NativeTimelineHoverCard> hoverCardGeometry;
    bool hoverBlocked = false;
    EventRegistrationToken navigationStarting{}, navigationCompleted{}, frameNavigation{},
        newWindow{}, messageReceived{}, processFailed{}, downloadStarting{}, rasterizationScaleChanged{}, zoomFactorChanged{};
    bool closed = false;
    bool failed = false;
    bool ready = false;
    ProductPresentationObserver productObserver;
    bool productDocumentLoaded = false;
    bool productPageReady = false;
    bool productReady = false;
    QPointer<SessionPresentation> trayPresentation;
    std::function<bool()> canRequestHide;
    struct Notice
    {
        QUuid id;
        SessionNotice kind;
        QString title;
        QString message;
        std::function<void()> finished;
    };
    // This is a presentation queue. It owns no Session command or document
    // decision, and persists across navigation of this Main host.
    std::deque<Notice> notices;
    quint64 productDeadlineEpoch = 0;
    UINT64 acceptedNavigation = 0;
    quint64 geometryEpoch = 0;
    bool layoutQueued = false;
    struct PreviewLayout
    {
        QRect client;
        QPoint rootOrigin;
        UINT dpi;
        bool visible;
        bool operator==(const PreviewLayout&) const = default;
    };
    std::optional<PreviewLayout> previewLayout;

    bool active() const { return !closed && !failed && owner; }
    bool isBgmPeer() const { return surface == SessionBridge::Surface::BackgroundMusic; }
    bool previewEnabled() const { return !isBgmPeer() && preview.composition && preview.requestExtent; }
    bool runtimeEnabled() const { return !isBgmPeer() && runtime.sample && runtime.hover && runtime.cancelHover && controlSubmit; }

    bool readyForHide() const
    {
        return !isBgmPeer() && trayPresentation && active() && ready && productReady;
    }

    void projectHideAvailability()
    {
        if (!readyForHide() || !web) return;
        const QJsonObject packet{{"kind", "session-hide-state"}, {"version", 1},
            {"generation", page.generation()},
            {"sessionIncarnation", page.sessionIncarnation().toString(QUuid::WithoutBraces)},
            {"eligible", notices.empty() && trayPresentation->eligible()}};
        const auto json = QString::fromUtf8(QJsonDocument(packet).toJson(QJsonDocument::Compact));
        const auto target = web;
        target->PostWebMessageAsJson(reinterpret_cast<LPCWSTR>(json.utf16()));
    }

    bool mayRequestHide(const QString& generation, const QString& source) const
    {
        if (!readyForHide() || !notices.empty() || !mayDeliver(generation, source) || chrome.isModalSuspended(generation)
            || (canRequestHide && !canRequestHide())) return false;
        const auto* window = owner->window();
        const auto hwnd = reinterpret_cast<HWND>(window->internalWinId());
        return window->isVisible() && window->isEnabled() && hwnd && IsWindowVisible(hwnd)
            && IsWindowEnabled(hwnd) && !IsIconic(hwnd);
    }

    bool hideMessage(const QByteArray& message, const QString& source)
    {
        if (message.size() > 512) return false;
        const auto packet = QJsonDocument::fromJson(message).object();
        if (packet.value("kind") != QJsonValue{"session-hide-request"}) return false;
        const auto generation = page.generation();
        if (generation.isEmpty() || packet.size() != 4 || message.count(':') != 4
            || packet.value("version") != QJsonValue{1} || packet.value("generation") != QJsonValue{generation}
            || packet.value("sessionIncarnation") != QJsonValue{page.sessionIncarnation().toString(QUuid::WithoutBraces)}
            || !mayRequestHide(generation, source)) return true;

        const auto show = trayPresentation->showGeneration();
        // Leave the WebView callback before issuing the native visibility intent.
        // The coordinator retains this same page/interaction check with its one permit.
        auto stillAllowed = [weak = weak_from_this(), generation, source, show] {
            const auto state = weak.lock();
            return state && state->mayRequestHide(generation, source)
                && state->trayPresentation->showGeneration() == show;
        };
        QTimer::singleShot(0, owner.data(), [weak = weak_from_this(), stillAllowed = std::move(stillAllowed)]() mutable {
            if (const auto state = weak.lock(); state && stillAllowed())
                state->trayPresentation->requestHide(std::move(stillAllowed));
        });
        return true;
    }

    void reportProductStage(ProductPresentationStage stage, const QString& detail = {})
    {
        productReady = stage == ProductPresentationStage::Ready;
        if (productReady || stage == ProductPresentationStage::Failed || stage == ProductPresentationStage::Retired)
            ++productDeadlineEpoch;
        if (productObserver) productObserver(stage, detail);
        if (productReady)
        {
            refreshChrome();
            projectHideAvailability();
        }
    }

    void armProductDeadline()
    {
        if (!owner) return;
        const auto epoch = ++productDeadlineEpoch;
        const auto weak = weak_from_this();
        QTimer::singleShot(std::chrono::seconds(20), owner.data(), [weak, epoch] {
            if (const auto state = weak.lock(); state && state->active() && state->productDeadlineEpoch == epoch)
                state->fail(QStringLiteral("session-product-presentation-timed-out"));
        });
    }

    void beginProductNavigation()
    {
        chrome.reset();
        productDocumentLoaded = false;
        productPageReady = false;
        reportProductStage(ProductPresentationStage::Navigating);
        armProductDeadline();
    }

    void completeProductNavigation()
    {
        productDocumentLoaded = true;
        reportProductStage(productPageReady ? ProductPresentationStage::Ready : ProductPresentationStage::AwaitingProduct);
    }

    bool acceptProductMessage(const QByteArray& message, const QString& document)
    {
        if (!mayDeliver(page.generation(), document) || message.size() > 384) return false;
        const auto marker = QJsonDocument::fromJson(message).object();
        if (marker.value("kind") != QJsonValue{"shell-presentation"}) return false;
        // All four allowed keys and scalar values contain no colons. Checking
        // the source member count also rejects duplicate keys hidden by parsing.
        if (marker.size() != 4 || message.count(':') != 4 || marker.value("version") != QJsonValue{1}
            || marker.value("generation") != QJsonValue{page.generation()}) return true;
        if (marker.value("state") == QJsonValue{"failed"})
            fail(QStringLiteral("session-product-bootstrap-failed"));
        else if (marker.value("state") == QJsonValue{"ready"} && !productPageReady)
        {
            productPageReady = true;
            if (productDocumentLoaded) reportProductStage(ProductPresentationStage::Ready);
        }
        return true;
    }

    void suspendPreview(bool ordinaryLayout = false, const QString& reason = QStringLiteral("presentation-lifecycle-retired"))
    {
        ++geometryEpoch;
        if (!ordinaryLayout && preview.composition) preview.composition->retirePreview(reason);
    }

    void fail(const QString& reason)
    {
        if (closed || failed) return;
        failed = true;
        ready = false;
        chrome.reset();
        retireRuntimePage();
        suspendPreview();
        page.close();
        if (controller) controller->put_IsVisible(FALSE);
        if (compositionInput) compositionInput->retire();
        if (compositionController) compositionController->put_RootVisualTarget(nullptr);
        compositionBinding.reset();
        opaqueVisual.reset();
        if (statusLabel)
        {
            statusLabel->setText(QStringLiteral("Session presentation unavailable: %1\nThe native Session remains open.").arg(reason));
            statusLabel->show();
            statusLabel->raise();
        }
        qInfo().noquote() << "[SessionHost] failed:" << reason;
        reportProductStage(ProductPresentationStage::Failed, reason);
        retireNotices();
    }

    bool require(HRESULT status, const char* operation)
    {
        if (SUCCEEDED(status)) return true;
        fail(QStringLiteral("%1 (0x%2)").arg(QString::fromLatin1(operation),
            QString::number(static_cast<quint32>(status), 16)));
        return false;
    }

    // The reused A7 gate accounts for callbacks dispatched before the factory
    // returns, including a nested event loop that retires this child Host.
    template<class Interface, class Adopt>
    static void settle(const std::weak_ptr<State>& weak,
        const std::shared_ptr<SessionCreationResult<Interface>>& pending, Adopt adopt)
    {
        const auto state = weak.lock();
        const auto disposition = pending->operation.consume(!state || !state->active());
        if (!disposition) return;
        if (*disposition != LauncherBridge::CreationDisposition::Adopt)
        {
            closeCreatedController(pending->value.Get());
            pending->value.Reset();
            if (*disposition == LauncherBridge::CreationDisposition::FactoryFailed && state && state->active())
                state->fail(pending->stage + QStringLiteral("-factory-failed"));
            return;
        }
        if (FAILED(pending->callbackStatus) || !pending->value)
        {
            closeCreatedController(pending->value.Get());
            pending->value.Reset();
            state->fail(pending->stage + QStringLiteral("-callback-failed"));
            return;
        }
        adopt(*state, pending->value);
        pending->value.Reset();
    }

    void start()
    {
        if (!active()) return;
        reportProductStage(ProductPresentationStage::OpeningWebView);
        armProductDeadline();
        if (page.sessionIncarnation().isNull())
        {
            fail(QStringLiteral("native-session-context-unavailable"));
            return;
        }
        static thread_local SessionGuiApartment apartment;
        if (!require(apartment.status, "GUI STA initialization")) return;
        const auto assets = QDir(QCoreApplication::applicationDirPath()).filePath("launcher");
        if (!QFileInfo::exists(QDir(assets).filePath("index.html")))
        {
            fail(QStringLiteral("packaged-session-assets-missing"));
            return;
        }
        // A BGM peer borrows its Session's exact environment. Rebuilding a
        // controller does not create another browser environment authority.
        if (isBgmPeer())
        {
            if (!environment)
            {
                fail(QStringLiteral("shared-session-environment-unavailable"));
                return;
            }
            if (bgm.permit.isNull() || bgm.playlistId.value.isNull() || !bgm.header || !bgm.close
                || !runtime.audioEffects || !controlSubmit)
            {
                fail(QStringLiteral("bgm-editor-unavailable"));
                return;
            }
        }
        else if (!previewEnabled() || !runtimeEnabled() || !runtime.audioEffects)
        {
            fail(QStringLiteral("native-session-presentation-unavailable"));
            return;
        }
        if (environment)
        {
            createController();
            return;
        }
        const auto userData = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
            .filePath("WebView2-Session");
        if (!QDir{}.mkpath(userData))
        {
            fail(QStringLiteral("session-webview-profile-unavailable"));
            return;
        }
        const auto weak = weak_from_this();
        const auto pending = std::make_shared<SessionCreationResult<ICoreWebView2Environment>>();
        pending->stage = QStringLiteral("webview-environment-creation");
        const auto complete = [weak, pending] {
            settle(weak, pending, [](State& state, const ComPtr<ICoreWebView2Environment>& value) {
                state.environment = value;
                state.createController();
            });
        };
        const auto status = CreateCoreWebView2EnvironmentWithOptions(nullptr,
            reinterpret_cast<LPCWSTR>(userData.utf16()), nullptr,
            Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [pending, complete](HRESULT error, ICoreWebView2Environment* value) -> HRESULT {
                    if (!pending->operation.callbackArrived()) return S_OK;
                    pending->callbackStatus = error;
                    pending->value = value;
                    if (QCoreApplication::instance())
                        QTimer::singleShot(0, QCoreApplication::instance(), complete);
                    else complete();
                    return S_OK;
                }).Get());
        pending->operation.factoryReturned(SUCCEEDED(status));
        complete();
    }

    // Use the same factory-return gate for composition controllers. A callback
    // may arrive before factory return or after Host retirement; neither is an
    // adoption permit and a late controller is explicitly closed.
    template<class Interface> static void closeCreatedController(Interface* value)
    {
        if constexpr (std::is_same_v<Interface, ICoreWebView2CompositionController>)
            closeProductCompositionController(value);
    }

    void createController()
    {
        if (!active() || !environment) return;
        ComPtr<ICoreWebView2Environment3> env3;
        if (!require(environment.As(&env3), "Composition environment")) return;
        const auto weak = weak_from_this();
        const auto pending = std::make_shared<SessionCreationResult<ICoreWebView2CompositionController>>();
        pending->stage = QStringLiteral("webview-composition-controller-creation");
        const auto complete = [weak, pending] {
            settle(weak, pending, [weak](State& state, const ComPtr<ICoreWebView2CompositionController>& value) {
                state.compositionController = value;
                if (!state.require(value.As(&state.controller), "Composition controller interface") || !state.active()) return;
                if (state.isBgmPeer() && (!state.require(state.controller->put_IsVisible(FALSE), "Hide pending BGM WebView")
                    || !state.active())) return;
                try {
                    const auto hwnd = state.owner->winId();
                    if (state.isBgmPeer()) state.opaqueVisual = std::make_unique<OpaqueWebViewVisual>(hwnd);
                    else state.compositionBinding = state.preview.composition->attach(hwnd);
                    if (!state.active()) return;
                    const auto target = state.opaqueVisual ? state.opaqueVisual->visual() : state.compositionBinding->visual();
                    if (!state.require(value->put_RootVisualTarget(target), "Composition visual attachment") || !state.active()) return;
                    const auto committed = state.opaqueVisual ? state.opaqueVisual->commit() : state.compositionBinding->commit();
                    if (!state.require(committed, "Composition visual commit") || !state.active()) return;
                    state.compositionInput = std::make_unique<SessionCompositionInput>(*state.owner, value.Get(), state.controller.Get(), [weak] {
                        const auto current = weak.lock();
                        return current && current->active() && current->ready && current->productReady;
                    });
                    state.configure();
                } catch (const std::exception& error) { state.fail(QString::fromUtf8(error.what())); }
            });
        };
        const auto status = env3->CreateCoreWebView2CompositionController(reinterpret_cast<HWND>(owner->winId()),
            Callback<ICoreWebView2CreateCoreWebView2CompositionControllerCompletedHandler>(
                [weak, pending, complete](HRESULT error, ICoreWebView2CompositionController* value) -> HRESULT {
                    if (!pending->operation.callbackArrived())
                    {
                        closeCreatedController(value);
                        return S_OK;
                    }
                    pending->callbackStatus = error;
                    const auto state = weak.lock();
                    if (!state || !state->active()) closeCreatedController(value);
                    else pending->value = value;
                    if (QCoreApplication::instance()) QTimer::singleShot(0, QCoreApplication::instance(), complete);
                    else complete();
                    return S_OK;
                }).Get());
        pending->operation.factoryReturned(SUCCEEDED(status));
        complete();
    }

    void configure()
    {
        if (!active() || !require(controller->get_CoreWebView2(&web), "CoreWebView2")) return;
        ComPtr<ICoreWebView2Controller2> controller2;
        if (!require(controller.As(&controller2), "WebView2 background interface")) return;
        if (!isBgmPeer())
        {
            if (!require(controller2->put_DefaultBackgroundColor({0, 0, 0, 0}), "Composition transparent background")) return;
        }
        else
        {
            // The still-hidden BGM viewport uses the native shell color while
            // the blank document and first CSS paint cross the controller.
            const auto color = owner->window()->palette().color(QPalette::Window);
            if (!require(controller2->put_DefaultBackgroundColor({255, static_cast<BYTE>(color.red()),
                static_cast<BYTE>(color.green()), static_cast<BYTE>(color.blue())}), "BGM opaque background")) return;
        }
        ComPtr<ICoreWebView2Controller3> controller3;
        ComPtr<ICoreWebView2Settings> settings;
        ComPtr<ICoreWebView2Settings5> settings5;
        ComPtr<ICoreWebView2_3> web3;
        ComPtr<ICoreWebView2_4> web4;
        if (!require(controller.As(&controller3), "WebView2 raw-pixel bounds")
            || !require(controller3->put_BoundsMode(COREWEBVIEW2_BOUNDS_MODE_USE_RAW_PIXELS), "Bounds mode")
            || !require(controller3->put_ShouldDetectMonitorScaleChanges(TRUE), "Monitor scale detection")
            || !require(web->get_Settings(&settings), "WebView2 settings")
            || !require(settings.As(&settings5), "WebView2 page scale settings")
            || !require(settings5->put_IsPinchZoomEnabled(FALSE), "Disable WebView2 post-layout pinch zoom")
            || !require(web.As(&web3), "WebView2 virtual host")
            || !require(web.As(&web4), "WebView2 download restriction")) return;
        if (!require(settings->put_AreHostObjectsAllowed(FALSE), "Disable host objects")
            || !require(settings->put_IsWebMessageEnabled(TRUE), "Enable Web messages")
            || !require(settings->put_AreDefaultContextMenusEnabled(FALSE), "Disable context menu")
            || !require(settings->put_IsStatusBarEnabled(FALSE), "Disable status bar")
            || !require(settings->put_IsZoomControlEnabled(FALSE), "Disable browser zoom")
            || !require(settings->put_AreDevToolsEnabled(FALSE), "Disable DevTools UI")) return;
        if (previewEnabled() && !require(controller->put_ZoomFactor(1), "Fix Preview WebView zoom")) return;
        const auto assets = QDir::toNativeSeparators(QDir(QCoreApplication::applicationDirPath()).filePath("launcher"));
        if (!require(web3->SetVirtualHostNameToFolderMapping(L"session.dse.local",
            reinterpret_cast<LPCWSTR>(assets.utf16()), COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS), "Map Session assets")) return;
        const auto weak = weak_from_this();
        if (!require(web->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>(
            [weak](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                const auto state = weak.lock();
                if (!state || !state->active()) { args->put_Cancel(TRUE); return S_OK; }
                LPWSTR uri{};
                if (FAILED(args->get_Uri(&uri))) { args->put_Cancel(TRUE); return S_OK; }
                const auto requestedUri = takeSessionString(uri);
                if (requestedUri == state->page.document()) state->retireRuntimePage();
                const auto disposition = state->page.navigationStarting(requestedUri);
                if (disposition == SessionBridge::NavigationDisposition::Allow)
                {
                    state->suspendPreview();
                    if (state->preview.composition)
                        state->preview.composition->beginPage(state->owner->internalWinId(), state->page.generation());
                    args->get_NavigationId(&state->acceptedNavigation);
                    state->ready = true; // A trusted document may bootstrap before completion.
                    state->beginProductNavigation();
                }
                else
                {
                    args->put_Cancel(TRUE);
                    if (disposition == SessionBridge::NavigationDisposition::RestartWithFreshGeneration)
                    {
                        state->suspendPreview();
                        state->ready = false;
                        state->acceptedNavigation = 0;
                        state->beginProductNavigation();
                        QTimer::singleShot(0, QCoreApplication::instance(), [weak] {
                            if (const auto current = weak.lock(); current && current->active()) current->navigate();
                        });
                    }
                }
                return S_OK;
            }).Get(), &navigationStarting), "Session navigation restriction")) return;
        if (!require(web->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [weak](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                const auto state = weak.lock();
                if (!state || !state->active()) return S_OK;
                UINT64 id{};
                args->get_NavigationId(&id);
                if (id != state->acceptedNavigation) return S_OK;
                BOOL success{};
                args->get_IsSuccess(&success);
                if (!success) state->fail(QStringLiteral("session-page-load-failed"));
                else
                {
                    if (state->statusLabel) state->statusLabel->hide();
                    qInfo().noquote() << "[SessionHost] document_ready" << state->page.generation();
                    state->completeProductNavigation();
                }
                return S_OK;
            }).Get(), &navigationCompleted), "Session navigation completion")) return;
        if (!require(web->add_FrameNavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>(
            [](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                args->put_Cancel(TRUE); return S_OK;
            }).Get(), &frameNavigation), "Block Session frame navigation")
            || !require(web->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                    args->put_Handled(TRUE); return S_OK;
                }).Get(), &newWindow), "Block Session new windows")
            || !require(web4->add_DownloadStarting(Callback<ICoreWebView2DownloadStartingEventHandler>(
                [](ICoreWebView2*, ICoreWebView2DownloadStartingEventArgs* args) -> HRESULT {
                    args->put_Cancel(TRUE); return S_OK;
                }).Get(), &downloadStarting), "Block Session downloads")) return;
        if (!require(web->add_WebMessageReceived(Callback<ICoreWebView2WebMessageReceivedEventHandler>(
            [weak](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                if (const auto state = weak.lock(); state && state->active())
                    try { state->message(args); }
                    catch (...) { state->fail(QStringLiteral("session-bridge-processing-failed")); }
                return S_OK;
            }).Get(), &messageReceived), "Session command bridge")
            || !require(web->add_ProcessFailed(Callback<ICoreWebView2ProcessFailedEventHandler>(
                [weak](ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs*) -> HRESULT {
                    if (const auto state = weak.lock(); state && state->active())
                        state->fail(QStringLiteral("session-webview-process-failed"));
                    return S_OK;
                }).Get(), &processFailed), "Session browser process failure")) return;
        if (!require(controller3->add_RasterizationScaleChanged(
                Callback<ICoreWebView2RasterizationScaleChangedEventHandler>(
                    [weak](ICoreWebView2Controller*, IUnknown*) -> HRESULT {
                        if (const auto state = weak.lock(); state && state->active())
                        {
                            state->refreshChrome();
                            if (state->previewEnabled()) state->invalidatePreviewGeometry();
                        }
                        return S_OK;
                    }).Get(), &rasterizationScaleChanged), "Session rasterization-scale observation")
                || !require(controller->add_ZoomFactorChanged(Callback<ICoreWebView2ZoomFactorChangedEventHandler>(
                    [weak](ICoreWebView2Controller* sender, IUnknown*) -> HRESULT {
                        const auto state = weak.lock();
                        if (!state || !state->active()) return S_OK;
                        state->refreshChrome();
                        if (state->previewEnabled())
                        {
                            state->suspendPreview();
                            double zoom{};
                            if (!state->require(sender->get_ZoomFactor(&zoom), "Read Preview zoom")) return S_OK;
                            if (zoom != 1 && !state->require(sender->put_ZoomFactor(1), "Restore fixed Preview zoom")) return S_OK;
                            state->invalidatePreviewGeometry();
                        }
                        return S_OK;
                    }).Get(), &zoomFactorChanged), "Session zoom observation")) return;
        layout();
        if (!active()) return;
        page.prepare();
        require(web->AddScriptToExecuteOnDocumentCreated(
            L"Object.defineProperty(window, '__dseProductShell', {value: true});",
            Callback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>(
                [weak](HRESULT result, LPCWSTR) -> HRESULT {
                    if (const auto state = weak.lock(); state && state->active()
                        && state->require(result, "Install Session product presentation signal")) state->navigate();
                    return S_OK;
                }).Get()), "Register Session product presentation signal");
    }

    void navigate()
    {
        if (active() && web)
            require(web->Navigate(reinterpret_cast<LPCWSTR>(page.document().utf16())), "Navigate Session");
    }

    QJsonObject discloseReadySnapshot(QJsonObject payload) const
    {
        // The Host adds only this exact page's ability to receive the result.
        auto host = payload.value("host").toObject();
        host.insert("ready", true);
        payload.insert("host", host);
        if (!notices.empty())
        {
            const auto& notice = notices.front();
            const char* level = notice.kind == SessionNotice::Error ? "error"
                : notice.kind == SessionNotice::Warning ? "warning" : "information";
            payload.insert("sessionNotice", QJsonObject{{"id", notice.id.toString(QUuid::WithoutBraces)},
                {"level", level}, {"title", notice.title}, {"message", notice.message}});
        }
        return payload;
    }

    bool showSessionNotice(SessionNotice kind, const QString& title, const QString& message,
        std::function<void()> finished)
    {
        if (!active() || isBgmPeer() || (!finished && std::any_of(notices.begin(), notices.end(), [&](const Notice& notice) {
            return notice.kind == kind && notice.title == title && notice.message == message;
        }))) return false;
        constexpr std::size_t MaximumPendingNotices = 8;
        // Reserve one extra slot for a tray Close result awaiting acknowledgement.
        if (notices.size() >= MaximumPendingNotices + (finished ? 1 : 0))
        {
            qWarning().noquote() << "[SessionHost] Notice queue full:" << title;
            return false;
        }
        const bool first = notices.empty();
        notices.push_back({QUuid::createUuid(), kind, title, message, std::move(finished)});
        if (first)
        {
            projectHideAvailability();
            invalidateSnapshot();
        }
        return true;
    }

    void retireNotices()
    {
        auto retired = std::exchange(notices, {});
        for (auto& notice : retired) if (notice.finished) notice.finished();
    }

    QJsonObject acknowledgeSessionNotice(const SessionBridge::Request& request, const QString& document)
    {
        if (!productReady || !mayDeliver(request.generation, document) || notices.empty()
            || notices.front().id != request.acknowledgeSessionNotice)
            return SessionBridge::failure(request.requestId, request.generation, page.sessionIncarnation(),
                "stale-session-notice", request.type);
        auto finished = std::move(notices.front().finished);
        notices.pop_front();
        if (finished) finished();
        projectHideAvailability();
        invalidateSnapshot();
        return SessionBridge::result(request, {{"status", "applied"}});
    }

    bool mayDeliver(const QString& generation, const QString& document) const
    {
        return active() && ready && page.generation() == generation && page.document() == document;
    }

    QString publishedPresentation;
    void publishPreviewPresentation(bool force = false)
    {
        if (!preview.composition || !active() || !ready || !web) return;
        const auto generation = page.generation(), document = page.document();
        const auto payload = preview.composition->currentPresentation(owner->internalWinId(), generation);
        const auto key = generation + QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact));
        const bool changed = key != publishedPresentation;
        if (!force && !changed) return;
        // Store before posting: delivery may pump a nested page/Host retirement.
        publishedPresentation = key;
        postRuntimeEvent(QString::fromLatin1(SessionBridge::PreviewPresentationState), payload, generation, document);
        if (changed && mayDeliver(generation, document) && !payload.value("lifecycleValid").toBool())
            publishPreviewGeometryInvalidation();
    }

    QJsonObject placePreview(const SessionBridge::Request& request)
    {
        const auto generation = page.generation(), document = page.document();
        if (!mayDeliver(request.generation, document))
            return SessionBridge::failure(request.requestId, request.generation, page.sessionIncarnation(),
                QStringLiteral("stale-preview-geometry"), request.type);
        const auto host = owner->internalWinId();
        if (!preview.composition)
            return SessionBridge::failure(request.requestId, request.generation, page.sessionIncarnation(),
                QStringLiteral("preview-composition-unavailable"), request.type);
        const auto response = [&](bool accepted, const QString& outcome, const QString& reason) {
            if (!mayDeliver(generation, document) || !preview.composition)
                return SessionBridge::failure(request.requestId, request.generation, page.sessionIncarnation(),
                    QStringLiteral("stale-preview-geometry"), request.type);
            const auto payload = preview.composition->projection(
                host, generation, request.requestId, accepted, outcome, reason);
            // A new facade may have attached after a cached bootstrap event.
            // Every geometry response carries a fresh native state event;
            // render-boundary observations still publish changed facts only.
            publishPreviewPresentation(true);
            return SessionBridge::result(request, payload);
        };
        const auto reject = [&](const QString& reason, bool transient = false) {
            return response(false, transient ? QStringLiteral("transient-layout-pending") : QStringLiteral("invalid-request"), reason);
        };
        if (!previewEnabled() || !request.preview) return reject(QStringLiteral("preview-capability-unavailable"));
        const QUuid geometryRequest{request.requestId};
        if (geometryRequest.isNull() || geometryRequest.toString(QUuid::WithoutBraces).compare(request.requestId, Qt::CaseInsensitive))
            return reject(QStringLiteral("invalid-preview-request-id"));
        preview.composition->beginPage(host, generation);
        const auto& intent = *request.preview;
        hoverBlocked = intent.modalActive || !intent.floatingRects.isEmpty();
        if (hoverBlocked) cancelHoverWork();
        ComPtr<ICoreWebView2Controller3> controller3;
        double scale{}; RECT bounds{}, client{};
        const auto hwnd = reinterpret_cast<HWND>(host);
        if (!controller || FAILED(controller.As(&controller3)) || FAILED(controller3->get_RasterizationScale(&scale))
            || FAILED(controller->get_Bounds(&bounds)) || !GetClientRect(hwnd, &client))
        {
            suspendPreview(false, QStringLiteral("preview-controller-geometry-unavailable"));
            return response(false, QStringLiteral("lifecycle-retired"), QStringLiteral("preview-controller-geometry-unavailable"));
        }
        const QSize viewport{bounds.right - bounds.left, bounds.bottom - bounds.top};
        if (bounds.left || bounds.top || viewport != QSize{client.right, client.bottom})
            return reject(QStringLiteral("preview-controller-viewport-mismatch"), true);
        const auto placement = PreviewGeometry::resolve(intent, viewport, scale);
        if (!placement.error.isEmpty())
            return reject(placement.error, placement.error == QStringLiteral("preview-viewport-mismatch")
                || placement.error == QStringLiteral("preview-scale-mismatch"));
        if (!intent.visible || viewport.isEmpty() || !IsWindowVisible(hwnd) || IsIconic(GetAncestor(hwnd, GA_ROOT)))
        {
            suspendPreview(false, QStringLiteral("presentation-not-visible"));
            return response(false, QStringLiteral("lifecycle-retired"), QStringLiteral("presentation-not-visible"));
        }
        // Empty/rounded-away/outside desired geometry is admission failure,
        // not evidence that the current page, Host or content retired.
        if (placement.physicalRect.isEmpty())
            return reject(QStringLiteral("preview-desired-geometry-empty"), true);
        const auto epoch = geometryEpoch;
        const auto allocation = preview.requestExtent;
        const bool requested = allocation(placement.physicalRect.size());
        if (!mayDeliver(generation, document) || epoch != geometryEpoch)
            return SessionBridge::failure(request.requestId, request.generation, page.sessionIncarnation(),
                QStringLiteral("stale-preview-geometry"), request.type);
        if (!requested) return reject(QStringLiteral("engine-allocation-request-unavailable"));
        const auto failure = preview.composition->placePreview(placement.physicalRect, host, generation, request.requestId);
        if (!mayDeliver(generation, document) || epoch != geometryEpoch)
            return SessionBridge::failure(request.requestId, request.generation, page.sessionIncarnation(),
                QStringLiteral("stale-preview-geometry"), request.type);
        if (!failure.isEmpty()) return reject(failure, failure.contains(QStringLiteral("not-ready")));
        return response(true, QStringLiteral("accepted"), {});
    }

    static QByteArray encodeResponse(const QJsonObject& response, const QString& requestId,
        const QString& generation, const QUuid& sessionIncarnation, QString* sizeFailure = nullptr)
    {
        auto bytes = QJsonDocument(response).toJson(QJsonDocument::Compact);
        if (bytes.size() > SessionBridge::MaximumResponseBytes)
        {
            if (sizeFailure) *sizeFailure = QStringLiteral("snapshot-too-large");
            bytes = QJsonDocument(SessionBridge::failure(requestId, generation, sessionIncarnation,
                QStringLiteral("snapshot-too-large"))).toJson(QJsonDocument::Compact);
        }
        return bytes;
    }

    void finishResponseDelivery(const QString& generation, const QString& document,
        HRESULT deliveryStatus, const QString& snapshotFailure)
    {
        // Post/provider work can reenter page retirement. An old completion,
        // including a failed Post, must never retire a replacement page.
        if (!mayDeliver(generation, document)) return;
        if (!require(deliveryStatus, "Post Session snapshot result")) return;
        if (!snapshotFailure.isEmpty()) fail(snapshotFailure);
    }

    // One GUI-thread subscriber belongs to this exact ready Web document.
    // Native playback continues when this subscription retires.
    void invalidateHoverView()
    {
        ++hoverEpoch; // A hidden/retired hover cannot regain an image from late work.
        hoverFrame = {};
        hoverReadyTarget.reset();
        hoverFrameSerial = 0;
        hoverFrameStatus = QStringLiteral("pending");
        hoverView.reset();
        hoverCardGeometry.reset();
        const auto hide = runtime.hideHover;
        if (hide) hide();
    }

    void cancelHoverWork(bool retireView = true)
    {
        ++hoverEpoch;
        hoverFrameSerial = 0;
        hoverFrameStatus = QStringLiteral("pending");
        if (retireView)
        {
            hoverCancelledSerial = std::max({hoverCancelledSerial, hoverSerial, hoverPresentationSerial});
            hoverTarget.reset();
            hoverTargetLabel.clear();
            invalidateHoverView();
        }
        const auto cancel = runtime.cancelHover;
        if (cancel) cancel();
    }

    void retireRuntimePage()
    {
        if (controlPageAdmission) controlPageAdmission->store(false, std::memory_order_release);
        controlPageAdmission.reset();
        audioEffectsNegotiated = false;
        observedAudioEffects.reset();
        if (compositionInput) compositionInput->invalidate();
        runtimeSubscribed = false;
        runtimeCadence.invalidate();
        bgmBindings = {};
        bgmPolls = {};
        bgmCadence.invalidate();
        bgmHeader.reset();
        bgmHeaderCadence.invalidate();
        observedBinding.clear();
        hoverBlocked = false;
        cancelHoverWork();
        hoverSerial = 0;
        hoverPresentationSerial = 0;
        hoverCancelledSerial = 0;
    }

    void postRuntimeEvent(const QString& type, const QJsonObject& payload,
        const QString& generation, const QString& document)
    {
        if (!web || !mayDeliver(generation, document)) return;
        const QJsonObject event{{"kind", "event"}, {"version", SessionBridge::Version}, {"type", type},
            {"generation", generation}, {"sessionIncarnation", page.sessionIncarnation().toString(QUuid::WithoutBraces)}, {"payload", payload}};
        const auto bytes = QJsonDocument(event).toJson(QJsonDocument::Compact);
        if (bytes.size() > 8192) { fail(QStringLiteral("runtime-projection-too-large")); return; }
        const auto serialized = QString::fromUtf8(bytes);
        const auto target = web;
        const auto delivered = target->PostWebMessageAsJson(reinterpret_cast<LPCWSTR>(serialized.utf16()));
        if (mayDeliver(generation, document)) require(delivered, "Post Session runtime event");
    }

    void observeRuntimeBinding(const QJsonObject& sample)
    {
        const auto token = sample.value("bindingToken").toString();
        if (observedBinding != token)
        {
            cancelHoverWork();
            observedBinding = token;
        }
    }

    void publishRuntimeSample(bool immediate)
    {
        if (!runtimeSubscribed || !runtimeEnabled() || !active() || !ready || !web) return;
        if (!immediate && runtimeCadence.isValid() && runtimeCadence.elapsed() < 100) return;
        const auto generation = page.generation(), document = page.document();
        const auto provider = runtime.sample;
        const auto sample = provider();
        if (!mayDeliver(generation, document)) return;
        observeRuntimeBinding(sample);
        runtimeCadence.restart();
        postRuntimeEvent(QStringLiteral("RuntimePlaybackSample"), sample, generation, document);
    }

    void publishBgmRuntimeState()
    {
        if (!isBgmPeer() || !controlSubmit || !active() || !ready || !web
            || (!bgmBindings[0] && !bgmBindings[1])) return;
        // Check cadence before provider reads/comparisons; serialize only change.
        if (bgmCadence.isValid() && bgmCadence.elapsed() < 100) return;
        bgmCadence.restart();
        const auto generation = page.generation(), document = page.document();
        for (std::size_t i = 0; i < bgmBindings.size(); ++i)
        {
            const auto captured = bgmBindings[i];
            if (!captured) continue;
            if (bgmPolls[i]) continue;
            SessionBridge::Request request;
            request.type = QStringLiteral("GetBgmRuntimeState");
            request.generation = generation;
            request.sessionIncarnation = page.sessionIncarnation().toString(QUuid::WithoutBraces);
            request.bgmRuntime = BgmRuntimeRequest{captured->permit, captured->target, std::nullopt};
            bgmPolls[i] = captured->subscription;
            submitControl(std::move(request), document,
                [i, captured = *captured, generation, document](State& state, const ControlReply& reply) {
                    if (state.bgmPolls[i] != captured.subscription) return;
                    state.bgmPolls[i].reset();
                    if (const auto* value = std::get_if<BgmRuntimeResult>(&reply))
                        state.applyBgmRuntimePoll(i, captured, *value, generation, document);
                });
            if (!mayDeliver(generation, document)) return;
        }
    }

    void applyBgmRuntimePoll(std::size_t slot, const BgmBinding& captured,
        const BgmRuntimeResult& value, const QString& generation, const QString& document)
    {
        // One old read can finish after a newer hydration of the same scope.
        if (!mayDeliver(generation, document) || bgmBindings[slot] != captured) return;
        if (value.status != BgmRuntimeCommandStatus::Ready || !value.state)
        {
            bgmBindings[slot].reset();
            return;
        }
        if (*value.state == captured.last) return;
        bgmBindings[slot]->last = *value.state;
        postRuntimeEvent(QStringLiteral("BgmRuntimeChanged"), SessionBridge::bgmRuntimePayload(value), generation, document);
    }

    void observeBgmRuntime(const BgmRuntimeRequest& intent, const BgmRuntimeResult& result)
    {
        if (!intent.trackId && result.status == BgmRuntimeCommandStatus::Ready && result.state)
        {
            const auto slot = bgmItemId(intent.target) ? 1U : 0U;
            bgmBindings[slot] = BgmBinding{intent.permit, intent.target, *result.state, ++nextBgmSubscription};
        }
    }

    bool matchesBgmContext(const QUuid& permit, const BgmTarget& target) const
    {
        return permit == bgm.permit && bgmPlaylistId(target) == bgm.playlistId;
    }

    void publishBgmEditorState()
    {
        if (!isBgmPeer() || !active() || !ready || !web) return;
        if (bgmHeaderCadence.isValid() && bgmHeaderCadence.elapsed() < 100) return;
        bgmHeaderCadence.restart();
        const auto generation = page.generation(), document = page.document();
        const auto provider = bgm.header;
        QJsonObject value;
        try { value = provider(); }
        catch (...) { fail(QStringLiteral("bgm-editor-unavailable")); return; }
        if (!mayDeliver(generation, document)) return;
        if (bgmHeader && *bgmHeader == value) return;
        bgmHeader = value;
        postRuntimeEvent(QStringLiteral("BgmEditorInvalidated"), value, generation, document);
    }

    void acknowledgeBgmScalar(const BgmAuthoredRequest& intent, const AuthoredResult& outcome,
        const QString& generation, const QString& document)
    {
        if ((intent.action != BgmAuthoredAction::SetQueueVolume && intent.action != BgmAuthoredAction::SetTrackVolume)
            || outcome.authored != AuthoredOutcome::Committed) return;
        if (mayDeliver(generation, document) && bgmHeader && outcome.authoredRevision > 0
            && bgmHeader->value("revision").toString() == QString::number(outcome.authoredRevision - 1))
        {
            // A scalar response carries only this revision and dirty state.
            // Unseen queue edits, language, picker and busy changes still need invalidation.
            bgmHeader->insert("revision", QString::number(outcome.authoredRevision));
            bgmHeader->insert("dirty", outcome.dirty);
        }
    }

    QJsonObject closeBgmEditor(const SessionBridge::Request& request)
    {
        const auto close = bgm.close;
        try { close(); } // The peer queues ordinary close after this reply.
        catch (...)
        {
            return SessionBridge::failure(request.requestId, request.generation, page.sessionIncarnation(),
                QStringLiteral("bgm-editor-unavailable"), request.type);
        }
        return SessionBridge::result(request, {{"status", "closing"}});
    }

    bool hoverTargetCurrent(const TimelineHoverRequest& request)
    {
        const auto provider = runtime.sample;
        if (!provider) return false;
        const auto generation = page.generation(), document = page.document();
        const auto sample = provider();
        if (!mayDeliver(generation, document)) return false;
        if (!sample.value("hasMedia").toBool() || !sample.value("canSeek").toBool())
        {
            cancelHoverWork(); // Native source invalidation, not a mismatched client target.
            return false;
        }
        return std::isfinite(request.time) && request.time >= 0 && request.time <= sample.value("duration").toDouble()
            && sample.value("bindingToken").toString() == request.bindingToken
            && sample.value("active").toObject().value("itemId").toString() == request.itemId.toString(QUuid::WithoutBraces);
    }

    bool consistentHoverTarget(const TimelineHoverRequest& request, const QString& label = {}) const
    {
        if (request.serial <= hoverCancelledSerial) return false;
        if (!hoverTarget || request.serial > hoverTarget->serial) return true;
        return request.serial == hoverTarget->serial && request.bindingToken == hoverTarget->bindingToken
            && request.itemId == hoverTarget->itemId && request.time == hoverTarget->time
            && (label.isEmpty() || hoverTargetLabel.isEmpty() || label == hoverTargetLabel);
    }

    void registerHoverTarget(const TimelineHoverRequest& request, const QString& label = {})
    {
        if (hoverReadyTarget && (hoverReadyTarget->bindingToken != request.bindingToken
            || hoverReadyTarget->itemId != request.itemId))
        {
            hoverFrame = {};
            hoverReadyTarget.reset();
        }
        if (!hoverTarget || request.serial != hoverTarget->serial)
        {
            hoverTarget = request;
            hoverTargetLabel.clear();
        }
        if (!label.isEmpty()) hoverTargetLabel = label;
    }

    bool hoverOwnerAvailable() const
    {
        const auto root = owner ? reinterpret_cast<HWND>(owner->window()->winId()) : nullptr;
        return root && IsWindowVisible(root) && !IsIconic(root)
            && GetAncestor(GetForegroundWindow(), GA_ROOT) == root;
    }

    NativeTimelineHoverCard hoverCardForView() const
    {
        auto card = *hoverCardGeometry;
        card.timestamp = hoverView->timeLabel; // Footer continues to follow the requested time.
        if (hoverReadyTarget && hoverReadyTarget->bindingToken == hoverView->bindingToken
            && hoverReadyTarget->itemId == hoverView->itemId) card.image = hoverFrame;
        card.statusText = QCoreApplication::translate("MainWindow", hoverFrameSerial == hoverView->serial
            && hoverFrameStatus == QStringLiteral("unavailable") ? "Preview unavailable" : "Loading preview…");
        return card;
    }

    QString renderHover()
    {
        if (!hoverView || !hoverCardGeometry || hoverBlocked || !runtime.presentHover)
            return QStringLiteral("unavailable");
        const auto view = *hoverView;
        if (!consistentHoverTarget({view.bindingToken, view.itemId, view.serial, view.time}, view.timeLabel))
            return QStringLiteral("stale");
        const auto generation = page.generation(), document = page.document();
        const auto epoch = hoverEpoch;
        if (!hoverTargetCurrent({view.bindingToken, view.itemId, view.serial, view.time})
            || !mayDeliver(generation, document) || epoch != hoverEpoch || !hoverView
            || hoverView->serial != view.serial) return QStringLiteral("stale");
        if (!hoverOwnerAvailable())
        { invalidateHoverView(); return QStringLiteral("unavailable"); }
        const auto card = hoverCardForView();
        const auto present = runtime.presentHover; // Retain across reentrant native painting.
        const auto failure = present(card);
        if (!mayDeliver(generation, document) || epoch != hoverEpoch || !hoverView
            || hoverView->serial != view.serial) return QStringLiteral("stale");
        if (!failure.isEmpty())
        {
            invalidateHoverView();
            qWarning().noquote() << "[SessionHost] native hover presentation unavailable:" << failure;
            return QStringLiteral("unavailable");
        }
        return QStringLiteral("shown");
    }

    QJsonObject presentHoverReply(const SessionBridge::Request& request)
    {
        const auto& intent = *request.hoverPresentation;
        const auto reply = [&](const QString& status) {
            return SessionBridge::result(request, {{"status", status}, {"serial", double(intent.serial)}});
        };
        if (!runtime.presentHover || !runtime.hideHover || hoverBlocked) return reply(QStringLiteral("unavailable"));
        const auto generation = page.generation(), document = page.document();
        const auto epoch = hoverEpoch;
        const auto layoutEpoch = geometryEpoch;
        const auto current = [&] {
            return mayDeliver(generation, document) && epoch == hoverEpoch && layoutEpoch == geometryEpoch
                && intent.serial >= std::max(hoverSerial, hoverPresentationSerial)
                && consistentHoverTarget({intent.bindingToken, intent.itemId, intent.serial, intent.time}, intent.timeLabel);
        };
        if (!current() || !hoverTargetCurrent({intent.bindingToken, intent.itemId, intent.serial, intent.time}) || !current())
            return reply(QStringLiteral("stale"));
        if (!hoverOwnerAvailable())
        { invalidateHoverView(); return reply(QStringLiteral("unavailable")); }
        ComPtr<ICoreWebView2Controller3> controller3;
        double scale{};
        RECT bounds{}, client{};
        const auto hwnd = owner ? reinterpret_cast<HWND>(owner->winId()) : nullptr;
        POINT origin{};
        const bool geometryAvailable = controller && SUCCEEDED(controller.As(&controller3)) && SUCCEEDED(controller3->get_RasterizationScale(&scale))
            && SUCCEEDED(controller->get_Bounds(&bounds)) && GetClientRect(hwnd, &client) && ClientToScreen(hwnd, &origin)
            && bounds.left == 0 && bounds.top == 0 && bounds.right == client.right && bounds.bottom == client.bottom;
        if (!current()) return reply(QStringLiteral("stale"));
        if (!geometryAvailable)
        { invalidateHoverView(); return reply(QStringLiteral("unavailable")); }
        const auto placement = TimelineHoverPresentation::resolve(intent, QSize{client.right, client.bottom}, scale,
            QPoint{origin.x, origin.y});
        if (!placement.error.isEmpty() || placement.suspended)
        { invalidateHoverView(); return reply(QStringLiteral("unavailable")); }
        if (!current()) return reply(QStringLiteral("stale"));
        hoverPresentationSerial = intent.serial;
        registerHoverTarget({intent.bindingToken, intent.itemId, intent.serial, intent.time}, intent.timeLabel);
        hoverView = intent;
        NativeTimelineHoverCard card;
        card.screenRect = placement.screenRect;
        card.imageRect = placement.imageRect;
        card.timeRect = placement.timeRect;
        card.borderPixels = placement.borderPixels;
        card.fontPixels = placement.fontPixels;
        hoverCardGeometry = std::move(card);
        return reply(renderHover());
    }

    static QJsonObject runtimePayload(const RuntimeResult& result)
    {
        QJsonObject payload{{"status", result.status}, {"message", result.message}, {"sample", result.sample}};
        if (result.audioEffect) payload.insert("audioEffect", *result.audioEffect);
        return payload;
    }

    QJsonObject hoverReply(const SessionBridge::Request& request)
    {
        if (!runtimeEnabled()) return SessionBridge::failure(request.requestId, request.generation,
            page.sessionIncarnation(), QStringLiteral("runtime-capability-unavailable"), request.type);
        if (request.hoverPresentation) return presentHoverReply(request);
        if (request.cancelHoverSerial)
        {
            if (*request.cancelHoverSerial < std::max(hoverSerial, hoverPresentationSerial))
                return SessionBridge::result(request, {{"status", "stale"}, {"serial", double(*request.cancelHoverSerial)}});
            hoverSerial = *request.cancelHoverSerial;
            hoverPresentationSerial = *request.cancelHoverSerial;
            cancelHoverWork();
            return SessionBridge::result(request, {{"status", "cancelled"}, {"serial", double(hoverSerial)}});
        }
        const auto hoverRequest = *request.hover;
        if (hoverRequest.serial <= hoverSerial || hoverRequest.serial < hoverPresentationSerial
            || !consistentHoverTarget(hoverRequest) || !hoverTargetCurrent(hoverRequest))
            return SessionBridge::result(request, {{"status", "stale"}, {"serial", double(hoverRequest.serial)}});
        const auto generation = page.generation(), document = page.document();
        if (hoverRequest.serial <= hoverSerial) return SessionBridge::result(request, {{"status", "stale"}, {"serial", double(hoverRequest.serial)}});
        cancelHoverWork(false);
        if (!mayDeliver(generation, document) || !runtimeEnabled())
            return SessionBridge::failure(request.requestId, request.generation, page.sessionIncarnation(), QStringLiteral("stale-runtime-page"), request.type);
        if (hoverRequest.serial <= hoverSerial || hoverRequest.serial < hoverPresentationSerial
            || !consistentHoverTarget(hoverRequest))
            return SessionBridge::result(request, {{"status", "stale"}, {"serial", double(hoverRequest.serial)}});
        hoverSerial = hoverRequest.serial;
        registerHoverTarget(hoverRequest);
        const auto epoch = hoverEpoch;
        const auto weak = weak_from_this();
        const auto decode = runtime.hover;
        decode(hoverRequest, [weak, epoch, generation, document, hoverRequest](TimelineHoverResult result) {
            const auto state = weak.lock();
            if (!state || !state->mayDeliver(generation, document) || epoch != state->hoverEpoch
                || !state->hoverTargetCurrent(hoverRequest)
                || result.bindingToken != hoverRequest.bindingToken || result.itemId != hoverRequest.itemId
                || result.serial != hoverRequest.serial || result.time != hoverRequest.time) return;
            // Single bounded current frame, consumed directly by the native view.
            // No PNG encode, WebResourceRequested endpoint or competing DOM img.
            if (hoverRequest.serial < state->hoverPresentationSerial) return;
            if (result.status == QStringLiteral("ready")
                && (result.image.isNull() || result.image.width() > 640 || result.image.height() > 360))
                result.status = QStringLiteral("unavailable");
            state->hoverFrame = result.status == QStringLiteral("ready") ? std::move(result.image) : QImage{};
            state->hoverReadyTarget = result.status == QStringLiteral("ready")
                ? std::optional<TimelineHoverRequest>{hoverRequest} : std::nullopt;
            state->hoverFrameSerial = hoverRequest.serial;
            state->hoverFrameStatus = result.status;
            if (state->hoverView && state->hoverView->serial == hoverRequest.serial) (void)state->renderHover();
            if (!state->mayDeliver(generation, document) || epoch != state->hoverEpoch) return;
            state->postRuntimeEvent(QStringLiteral("TimelineHoverResult"), {{"status", result.status},
                {"bindingToken", hoverRequest.bindingToken}, {"itemId", hoverRequest.itemId.toString(QUuid::WithoutBraces)},
                {"serial", double(hoverRequest.serial)}, {"time", hoverRequest.time}}, generation, document);
        });
        return SessionBridge::result(request, {{"status", "pending"}, {"serial", double(hoverSerial)}});
    }

    static QJsonObject openBgmEditorPayload(const QString& error)
    {
        return {{"status", error.isEmpty() ? "opened" : "unavailable"}, {"message", error}};
    }

    QJsonObject controlResponse(const SessionBridge::Request& request, const QString& document, const ControlReply& reply)
    {
        if (const auto* error = std::get_if<ControlError>(&reply))
            return SessionBridge::failure(request.requestId, request.generation, page.sessionIncarnation(),
                error->code, request.type);
        if (request.documentAction || request.documentChoice)
            if (const auto* result = std::get_if<DocumentCommandResult>(&reply))
                return SessionBridge::documentResult(request, *result);
        if (request.authored || request.bgmAuthored)
            if (const auto* result = std::get_if<AuthoredResult>(&reply))
            {
                if (request.bgmAuthored) acknowledgeBgmScalar(*request.bgmAuthored, *result, request.generation, document);
                return SessionBridge::result(request, SessionBridge::authoredPayload(*result));
            }
        if (request.runtime)
            if (const auto* result = std::get_if<RuntimeResult>(&reply))
                return SessionBridge::result(request, runtimePayload(*result));
        if (request.canvasBoundaryHighlight || request.desktopBackground)
            if (const auto* result = std::get_if<ViewPreferenceResult>(&reply))
                return SessionBridge::result(request, {{"status", result->status}});
        if (request.identifyBoundDisplay)
            if (const auto* status = std::get_if<Platform::DisplayIdentifyStatus>(&reply))
            {
                using Status = Platform::DisplayIdentifyStatus;
                return SessionBridge::result(request, {{"status", *status == Status::Shown ? "shown"
                    : *status == Status::Unresolved ? "unavailable" : "failed"}});
            }
        if (request.desktopDeployment)
            if (const auto* result = std::get_if<DesktopDeploymentResult>(&reply))
            {
                const char* status = "unavailable";
                switch (*result)
                {
                case DesktopDeploymentResult::Completed: status = "completed"; break;
                case DesktopDeploymentResult::AlreadySatisfied: status = "already-satisfied"; break;
                case DesktopDeploymentResult::Rejected: status = "rejected"; break;
                case DesktopDeploymentResult::Failure: status = "failed"; break;
                case DesktopDeploymentResult::Unavailable:
                case DesktopDeploymentResult::Received:
                case DesktopDeploymentResult::Uncertain: break;
                }
                return SessionBridge::result(request, {{"status", status}});
            }
        if (request.bgmRuntime)
            if (const auto* result = std::get_if<BgmRuntimeResult>(&reply))
            {
                observeBgmRuntime(*request.bgmRuntime, *result);
                return SessionBridge::result(request, SessionBridge::bgmRuntimePayload(*result));
            }
        if (request.openBgmEditor)
            if (const auto* result = std::get_if<OpenBgmEditorResult>(&reply))
                return SessionBridge::result(request, openBgmEditorPayload(result->error));
        if (request.bgmEditorRead)
            if (const auto* result = std::get_if<BgmSnapshotResult>(&reply))
            {
                bgmHeader = result->header;
                return SessionBridge::result(request, result->snapshot);
            }
        if (request.type == QLatin1String(SessionBridge::SnapshotCommand))
            if (const auto* result = std::get_if<MainSnapshotResult>(&reply))
                return SessionBridge::result(request, discloseReadySnapshot(result->snapshot));
        if (request.type == QLatin1String("GetRuntimePlaybackSample"))
            if (const auto* result = std::get_if<RuntimeSampleResult>(&reply))
            {
                runtimeSubscribed = true;
                runtimeCadence.start();
                observeRuntimeBinding(result->sample);
                return SessionBridge::result(request, result->sample);
            }
        return SessionBridge::failure(request.requestId, request.generation, page.sessionIncarnation(),
            QStringLiteral("invalid-control-result"), request.type);
    }

    template<class Complete>
    void submitControl(SessionBridge::Request request, const QString& document, Complete complete)
    {
        if (!controlPageAdmission) controlPageAdmission = std::make_shared<std::atomic_bool>(true);
        request.pageAdmission = controlPageAdmission;
        const auto submit = controlSubmit;
        const auto weak = weak_from_this();
        const auto* guiThread = owner->thread();
        const auto settled = std::make_shared<bool>(false);
        ControlCompletion completion = [weak, guiThread, generation = request.generation, document, settled,
            complete = std::move(complete)](ControlReply reply) {
            // Do not even inspect State from the control executor. Its caller
            // must post completion to the GUI relay before invoking this.
            Q_ASSERT(QThread::currentThread() == guiThread);
            if (QThread::currentThread() != guiThread || std::exchange(*settled, true)) return;
            const auto state = weak.lock();
            if (state && state->mayDeliver(generation, document)) complete(*state, reply);
        };
        try
        {
            if (!submit(std::move(request), completion)) completion(ControlError{QStringLiteral("control-command-busy")});
        }
        catch (...) { completion(ControlError{QStringLiteral("control-command-unavailable")}); }
    }

    template<class Deliver>
    bool dispatchControl(const SessionBridge::Request& request, const QString& document, Deliver deliver)
    {
        if (!(request.documentAction || request.documentChoice || request.authored || request.runtime
            || request.canvasBoundaryHighlight || request.desktopBackground || request.desktopDeployment || request.identifyBoundDisplay
            || request.openBgmEditor || request.bgmAuthored || request.bgmEditorRead || request.bgmRuntime
            || request.type == QLatin1String(SessionBridge::SnapshotCommand)
            || request.type == QLatin1String("GetRuntimePlaybackSample"))) return false;
        if (!mayDeliver(request.generation, document)) return true;
        if ((request.bgmAuthored && !matchesBgmContext(request.bgmAuthored->permit, request.bgmAuthored->target))
            || (request.bgmRuntime && !matchesBgmContext(request.bgmRuntime->permit, request.bgmRuntime->target)))
        {
            deliver(SessionBridge::failure(request.requestId, request.generation, page.sessionIncarnation(),
                QStringLiteral("wrong-bgm-peer-context"), request.type));
            return true;
        }
        submitControl(request, document, [request, document, deliver = std::move(deliver)](State& state, const ControlReply& reply) {
            QJsonObject response;
            try { response = state.controlResponse(request, document, reply); }
            catch (...)
            {
                response = SessionBridge::failure(request.requestId, request.generation, state.page.sessionIncarnation(),
                    QStringLiteral("control-result-unavailable"), request.type);
            }
            if (state.mayDeliver(request.generation, document)) deliver(std::move(response));
        });
        return true;
    }

    void postResponse(const QJsonObject& response, const QString& requestId, const QString& generation,
        const QString& document, bool authoritativeSnapshot, QString snapshotFailure = {})
    {
        if (!web || !mayDeliver(generation, document)) return;
        const auto serialized = QString::fromUtf8(encodeResponse(response, requestId,
            generation, page.sessionIncarnation(), authoritativeSnapshot ? &snapshotFailure : nullptr));
        const auto target = web; // Retain COM ownership across a reentrant Post.
        const auto delivered = target->PostWebMessageAsJson(reinterpret_cast<LPCWSTR>(serialized.utf16()));
        finishResponseDelivery(generation, document, delivered, authoritativeSnapshot ? snapshotFailure : QString{});
    }

    void message(ICoreWebView2WebMessageReceivedEventArgs* args)
    {
        if (!ready || !web || !active()) return;
        LPWSTR source{};
        if (FAILED(args->get_Source(&source))) return;
        const auto uri = takeSessionString(source);
        if (uri != page.document()) return;
        LPWSTR json{};
        if (FAILED(args->get_WebMessageAsJson(&json))) return;
        const auto message = takeSessionString(json).toUtf8();
        if (acceptProductMessage(message, uri)) return;
        if (chrome.receive(message, page.generation(), owner.data(), controller.Get(), web.Get(), productReady)) return;
        if (hideMessage(message, uri)) return;
        const auto parsed = SessionBridge::parseRequest(message, uri,
            page.document(), page.generation(), page.sessionIncarnation(), surface);
        const auto generation = page.generation();
        const bool authoritativeSnapshot = parsed.request && parsed.request->type == SessionBridge::SnapshotCommand;
        QJsonObject response;
        if (!parsed.request)
        {
            if (parsed.requestId.isEmpty()) return;
            response = SessionBridge::failure(parsed.requestId, generation, page.sessionIncarnation(), parsed.error, parsed.type);
        }
        else if (parsed.request->acknowledgeSessionNotice)
            response = acknowledgeSessionNotice(*parsed.request, uri);
        else if (!notices.empty() && parsed.request->documentAction == DocumentAction::Close)
            response = SessionBridge::failure(parsed.requestId, generation, page.sessionIncarnation(), "session-notice-pending", parsed.type);
        else if (parsed.request->negotiateAudioEffects || parsed.request->readAudioEffects)
        {
            if (!runtime.audioEffects || (parsed.request->readAudioEffects && !audioEffectsNegotiated))
                response = SessionBridge::failure(parsed.requestId, generation, page.sessionIncarnation(), "audio-effects-contract-unavailable", parsed.type);
            else
            {
                try
                {
                    const auto value = runtime.audioEffects();
                    if (value.value("version") != QJsonValue{1})
                        response = SessionBridge::failure(parsed.requestId, generation, page.sessionIncarnation(), "unsupported-audio-effects-contract", parsed.type);
                    else
                    {
                        audioEffectsNegotiated = true;
                        observedAudioEffects = value;
                        response = SessionBridge::result(*parsed.request, value);
                    }
                }
                catch (...) { response = SessionBridge::failure(parsed.requestId, generation, page.sessionIncarnation(), "audio-effects-unavailable", parsed.type); }
            }
        }
        else if (runtime.audioEffects && !audioEffectsNegotiated && (parsed.request->authored || parsed.request->runtime
            || parsed.request->bgmAuthored || parsed.request->documentAction || parsed.request->documentChoice
            || (parsed.request->bgmRuntime && parsed.request->bgmRuntime->trackId)))
            response = SessionBridge::failure(parsed.requestId, generation, page.sessionIncarnation(), "audio-effects-negotiation-required", parsed.type);
        else if (dispatchControl(*parsed.request, uri,
            [weak = weak_from_this(), requestId = parsed.requestId, generation, uri, authoritativeSnapshot](QJsonObject result) {
                if (const auto state = weak.lock())
                {
                    auto error = authoritativeSnapshot ? result.value("error").toString() : QString{};
                    if (error == QLatin1String("control-command-busy")) error.clear();
                    state->postResponse(result, requestId, generation, uri, authoritativeSnapshot, std::move(error));
                }
            })) return;
        else if (parsed.request->preview) response = placePreview(*parsed.request);
        else if (parsed.request->closeBgmEditor) response = closeBgmEditor(*parsed.request);
        else if (parsed.request->hover || parsed.request->hoverPresentation || parsed.request->cancelHoverSerial)
            response = hoverReply(*parsed.request);
        else response = SessionBridge::failure(parsed.requestId, generation, page.sessionIncarnation(),
            QStringLiteral("control-command-unavailable"), parsed.type);
        postResponse(response, parsed.requestId, generation, uri, authoritativeSnapshot);
    }

    void invalidateSnapshot()
    {
        if (!active() || !ready || !web) return;
        const auto generation = page.generation();
        const auto document = page.document();
        const auto event = SessionBridge::invalidated(generation, page.sessionIncarnation());
        const auto serialized = QString::fromUtf8(QJsonDocument(event).toJson(QJsonDocument::Compact));
        if (!mayDeliver(generation, document)) return;
        require(web->PostWebMessageAsJson(reinterpret_cast<LPCWSTR>(serialized.utf16())), "Post Session snapshot invalidation");
    }

    void publishAudioEffects()
    {
        if (!active() || !ready || !web || !audioEffectsNegotiated || !runtime.audioEffects) return;
        const auto generation = page.generation(), document = page.document();
        QJsonObject value;
        try { value = runtime.audioEffects(); }
        catch (...) { fail(QStringLiteral("audio-effects-unavailable")); return; }
        if (!mayDeliver(generation, document) || (observedAudioEffects && *observedAudioEffects == value)) return;
        observedAudioEffects = value;
        postRuntimeEvent(QStringLiteral("AudioEffectsChanged"), value, generation, document);
    }

    void publishPreviewGeometryInvalidation()
    {
        if (!previewEnabled() || !active() || !ready || !web) return;
        const auto generation = page.generation();
        const auto document = page.document();
        const auto serialized = QString::fromUtf8(QJsonDocument(
            SessionBridge::previewGeometryInvalidated(generation, page.sessionIncarnation())).toJson(QJsonDocument::Compact));
        if (!mayDeliver(generation, document)) return;
        require(web->PostWebMessageAsJson(reinterpret_cast<LPCWSTR>(serialized.utf16())), "Post Preview geometry invalidation");
    }

    void invalidatePreviewGeometry()
    {
        invalidateHoverView();
        suspendPreview(true, QStringLiteral("layout-invalidated"));
        publishPreviewGeometryInvalidation();
    }

    void refreshChrome()
    {
        if (active()) chrome.refresh(owner.data(), controller.Get(), web.Get(), page.generation(), productReady);
    }

    void queueTopLevelLayout()
    {
        if (!active() || layoutQueued) return;
        layoutQueued = true;
        // Qt dispatches the monitor transition before the native child has
        // acquired its new physical extent. Its logical size can stay equal.
        QMetaObject::invokeMethod(QCoreApplication::instance(), [weak = weak_from_this()] {
            if (const auto state = weak.lock())
            {
                state->layoutQueued = false;
                state->layout(true);
            }
        }, Qt::QueuedConnection);
    }

    void layout(bool refreshGeometry = false)
    {
        if (!active() || !controller) return;
        const auto currentController = controller;
        const auto hwnd = reinterpret_cast<HWND>(owner->winId());
        const auto root = GetAncestor(hwnd, GA_ROOT);
        RECT bounds{};
        POINT origin{};
        SetLastError(ERROR_SUCCESS);
        const bool mapped = MapWindowPoints(hwnd, root, &origin, 1) || GetLastError() == ERROR_SUCCESS;
        if (!GetClientRect(hwnd, &bounds) || !mapped)
        {
            previewLayout.reset();
            if (!isBgmPeer()) suspendPreview(false, QStringLiteral("native-layout-unavailable"));
            invalidatePreviewGeometry();
            return;
        }
        const bool visible = owner->isVisible() && IsWindowVisible(hwnd) && !IsIconic(root)
            && bounds.right > bounds.left && bounds.bottom > bounds.top;
        const PreviewLayout observed{{bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top},
            {origin.x, origin.y}, GetDpiForWindow(hwnd), visible};
        const bool changed = refreshGeometry && (!previewLayout || *previewLayout != observed);
        const auto previousEpoch = geometryEpoch;
        if (changed)
        {
            // Move and Resize can describe the same already-current HWND state.
            // Record before callbacks: a nested layout must see the observation,
            // but any genuinely different nested transition still revokes it.
            previewLayout = observed;
            invalidateHoverView();
            suspendPreview(visible, QStringLiteral("native-layout-changed"));
        }
        const auto epoch = previousEpoch + (changed ? 1 : 0);
        if (!active() || geometryEpoch != epoch) return;
        const auto boundsResult = currentController->put_Bounds(bounds);
        if (!require(boundsResult, "Session WebView controller bounds")) return;
        if (compositionBinding && !require(compositionBinding->layout(reinterpret_cast<WId>(hwnd)), "Composition bounds")) return;
        if (!active() || geometryEpoch != epoch) return;
        const auto positionResult = currentController->NotifyParentWindowPositionChanged();
        if (!require(positionResult, "Session WebView controller position")) return;
        if (!active() || geometryEpoch != epoch) return;
        const auto visibleResult = currentController->put_IsVisible(visible ? TRUE : FALSE);
        if (!require(visibleResult, "Session WebView controller visibility")) return;
        refreshChrome();
        // Publish only after controller bounds/visibility are coherent; posting
        // is not another permit revocation. Never replay a cached Preview rect.
        if (changed && active() && geometryEpoch == epoch) publishPreviewGeometryInvalidation();
        publishPreviewPresentation();
    }

    void close()
    {
        if (closed) return;
        closed = true;
        ready = false;
        retireNotices();
        if (trayPresentation) trayPresentation->bindEligibilityChanged({});
        trayPresentation.clear();
        canRequestHide = {};
        chrome.reset();
        if (compositionInput) compositionInput->retire();
        retireRuntimePage();
        page.close();
        suspendPreview();
        preview = {};
        runtime = {};
        bgm = {};
        controlSubmit = {};
        owner.clear();
        if (web)
        {
            web->remove_NavigationStarting(navigationStarting);
            web->remove_NavigationCompleted(navigationCompleted);
            web->remove_FrameNavigationStarting(frameNavigation);
            web->remove_NewWindowRequested(newWindow);
            web->remove_WebMessageReceived(messageReceived);
            web->remove_ProcessFailed(processFailed);
            ComPtr<ICoreWebView2_4> web4;
            if (SUCCEEDED(web.As(&web4))) web4->remove_DownloadStarting(downloadStarting);
        }
        if (controller)
        {
            if (rasterizationScaleChanged.value)
            {
                ComPtr<ICoreWebView2Controller3> controller3;
                if (SUCCEEDED(controller.As(&controller3))) controller3->remove_RasterizationScaleChanged(rasterizationScaleChanged);
            }
            if (zoomFactorChanged.value) controller->remove_ZoomFactorChanged(zoomFactorChanged);
            if (compositionController) compositionController->put_RootVisualTarget(nullptr);
            compositionBinding.reset();
            opaqueVisual.reset();
            controller->Close();
        }
        compositionInput.reset();
        compositionController.Reset();
        web.Reset();
        controller.Reset();
        environment.Reset();
        qInfo() << "[SessionHost] retired";
        reportProductStage(ProductPresentationStage::Retired);
        productObserver = {};
    }
};

WebViewSessionHost::WebViewSessionHost(const QUuid& sessionIncarnation, QWidget* parent, PreviewPresentation preview,
    RuntimePresentation runtime, ControlSubmit submit)
    : WebViewSessionHost(parent, std::make_shared<State>(sessionIncarnation, SessionBridge::Surface::Main,
        std::move(preview), std::move(runtime), BgmPresentation{}, std::move(submit)), nullptr) {}

WebViewSessionHost::WebViewSessionHost(const QUuid& sessionIncarnation, QWidget* parent, BgmPresentation bgm,
    RuntimePresentation runtime, ControlSubmit submit, WebViewSessionHost* environmentOwner)
    : WebViewSessionHost(parent, std::make_shared<State>(sessionIncarnation, SessionBridge::Surface::BackgroundMusic,
        PreviewPresentation{}, std::move(runtime), std::move(bgm), std::move(submit)), environmentOwner) {}

WebViewSessionHost::WebViewSessionHost(QWidget* parent, std::shared_ptr<State> state, WebViewSessionHost* environmentOwner)
    : QWidget(parent), state_(std::move(state))
{
    setAttribute(Qt::WA_NativeWindow);
    setFocusPolicy(Qt::StrongFocus);
    auto* layout = new QVBoxLayout(this);
    auto* status = new QLabel(QStringLiteral("Starting native-backed Session presentation…"), this);
    status->setAlignment(Qt::AlignCenter);
    status->setWordWrap(true);
    layout->addWidget(status);
    state_->owner = this;
    state_->statusLabel = status;
    // A failed page/controller can retain a valid Session environment. Closed
    // Hosts release it; presentation health grants no environment authority.
    if (environmentOwner && environmentOwner->state_->environment)
    {
        state_->environment = environmentOwner->state_->environment;
        state_->borrowedEnvironment = bool(state_->environment);
    }
    if (parent) parent->window()->installEventFilter(this);
    const auto weak = std::weak_ptr<State>{state_};
    QTimer::singleShot(0, this, [weak] { if (const auto state = weak.lock()) state->start(); });
}

WebViewSessionHost::~WebViewSessionHost() { closeHost(); }
void WebViewSessionHost::setProductPresentationObserver(ProductPresentationObserver observer)
{
    state_->productObserver = std::move(observer);
}
void WebViewSessionHost::bindTrayPresentation(SessionPresentation& presentation, std::function<bool()> canRequestHide)
{
    if (!state_->active() || state_->isBgmPeer() || state_->trayPresentation) return;
    state_->trayPresentation = &presentation;
    state_->canRequestHide = std::move(canRequestHide);
    presentation.bindEligibilityChanged([weak = state_->weak_from_this()](bool) {
        if (const auto state = weak.lock()) state->projectHideAvailability();
    });
}
void WebViewSessionHost::closeHost() { state_->close(); }
void WebViewSessionHost::publishAudioEffects() { state_->publishAudioEffects(); }
void WebViewSessionHost::invalidateSnapshot() { state_->invalidateSnapshot(); }
bool WebViewSessionHost::showSessionNotice(SessionNotice kind, const QString& title, const QString& message,
    std::function<void()> finished)
{ return state_->showSessionNotice(kind, title, message, std::move(finished)); }
bool WebViewSessionHost::hasSessionNotice() const { return state_->active() && !state_->notices.empty(); }
void WebViewSessionHost::publishRuntimeSample(bool immediate) { state_->publishRuntimeSample(immediate); }
void WebViewSessionHost::publishBgmRuntimeState() { state_->publishBgmRuntimeState(); }
void WebViewSessionHost::publishBgmEditorState() { state_->publishBgmEditorState(); }
bool WebViewSessionHost::sharesEnvironmentWith(const WebViewSessionHost& other) const
{
    return state_->environment && other.state_->environment
        && state_->environment.Get() == other.state_->environment.Get();
}
void WebViewSessionHost::publishPreviewPresentation() { state_->publishPreviewPresentation(); }
QJsonObject WebViewSessionHost::presentationFacts() const
{
    const bool peer = state_->isBgmPeer();
    const char* actual = "initializing";
    if (state_->failed) actual = "unavailable";
    else if (state_->controller) actual = peer ? "opaque-webview" : "model-c-production";
    return {{"requested", peer ? "opaque-bgm-peer" : "model-c-production"}, {"actual", actual},
        {"controllerAlive",bool(state_->controller)}, {"compositionBound",bool(state_->compositionBinding || state_->opaqueVisual)},
        {"bgmPeer",peer}, {"sharedEnvironment",state_->borrowedEnvironment},
        {"ready",isWebViewReady()}, {"input",state_->compositionInput?state_->compositionInput->diagnosticFacts():QJsonObject{}}};
}
bool WebViewSessionHost::isWebViewReady() const { return state_->active() && state_->ready; }
bool WebViewSessionHost::isProductPresentationReady() const { return state_->active() && state_->productReady; }
void WebViewSessionHost::reconcileForegroundKeyboardFocus(WId owningWindow)
{
    if (!isProductPresentationReady() || !state_->ready || !state_->controller) return;
    const auto host = reinterpret_cast<HWND>(internalWinId());
    const auto root = reinterpret_cast<HWND>(owningWindow);
    if (!host || !root || GetAncestor(host, GA_ROOT) != root || GetForegroundWindow() != root
        || !IsWindowVisible(root) || !IsWindowEnabled(root) || IsIconic(root) || !IsWindowVisible(host))
        return;
    // Qt can already consider this widget focused before its controller is
    // ready or while the native picker has moved keyboard focus away.
    const auto controller = state_->controller;
    const auto result = controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
    if (FAILED(result)) qWarning("[BgmPeer] foreground keyboard focus handoff failed (0x%08lx)", static_cast<unsigned long>(result));
}
QString WebViewSessionHost::pageGeneration() const { return state_->page.generation(); }
void WebViewSessionHost::refreshPreviewGeometry()
{
    // Explicit owner invalidation (e.g. WindowStateChange) is stronger than a
    // duplicate Move/Resize observation and must request fresh page geometry.
    state_->previewLayout.reset();
    state_->layout(true);
}
void WebViewSessionHost::closeEvent(QCloseEvent* event)
{
    QWidget::closeEvent(event);
    if (event->isAccepted()) closeHost();
}
void WebViewSessionHost::resizeEvent(QResizeEvent* event) { QWidget::resizeEvent(event); state_->layout(true); }
void WebViewSessionHost::moveEvent(QMoveEvent* event) { QWidget::moveEvent(event); state_->layout(true); }
void WebViewSessionHost::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    state_->layout(true);
    // Qt delivers Show before the native child/ancestor is mapped. Re-evaluate
    // visibility after that mapping; otherwise the controller can stay hidden
    // and the page cannot publish fresh Preview/hover geometry after restore.
    const auto weak = std::weak_ptr<State>{state_};
    QMetaObject::invokeMethod(this, [weak] {
        if (const auto state = weak.lock()) state->layout(true);
    }, Qt::QueuedConnection);
}
void WebViewSessionHost::hideEvent(QHideEvent* event) { QWidget::hideEvent(event); state_->layout(true); }
bool WebViewSessionHost::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == window() && event)
    {
        switch (event->type())
        {
        case QEvent::Hide: case QEvent::WindowDeactivate: case QEvent::WindowStateChange: case QEvent::Close:
            state_->cancelHoverWork(); break;
        case QEvent::Move: case QEvent::Resize: case QEvent::DevicePixelRatioChange:
            state_->invalidateHoverView();
            state_->queueTopLevelLayout();
            break;
        default: break;
        }
    }
    return QWidget::eventFilter(watched, event);
}
void WebViewSessionHost::focusInEvent(QFocusEvent* event)
{
    QWidget::focusInEvent(event);
    if (state_->active() && state_->controller)
    {
        // Qt activation notifications can arrive after native focus has moved
        // elsewhere. Such a notification must not reactivate this window.
        if (event->reason() == Qt::ActiveWindowFocusReason)
        {
            const auto native = reinterpret_cast<HWND>(effectiveWinId());
            const auto root = native ? GetAncestor(native, GA_ROOT) : nullptr;
            const auto focus = GetFocus();
            if (!root || GetActiveWindow() != root
                || (focus && GetAncestor(focus, GA_ROOT) != root)) return;
        }
        state_->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
    }
}
}
