#include "WebViewLauncherHost.h"
#include "LauncherBridgeContract.h"
#include "LauncherLanguage.h"
#include "LauncherSessionsProjection.h"
#include "ProductWebViewChrome.h"
#include "SessionCompositionInput.h"

#include <QCloseEvent>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFocusEvent>
#include <QJsonDocument>
#include <QLabel>
#include <QMoveEvent>
#include <QPalette>
#include <QPointer>
#include <QResizeEvent>
#include <QShowEvent>
#include <QStandardPaths>
#include <QTimer>
#include <chrono>
#include <stdexcept>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
// WIN32_LEAN_AND_MEAN omits COM declarations from Windows.h. WRL must establish
// interface/IUnknown before this SDK's generated WebView2 forward declarations.
#include <wrl.h>
#include <WebView2.h>
#include <type_traits>

namespace DesktopSceneEngine::Player
{
using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
namespace
{
// This is an upper bound for a stuck browser/bootstrap, never a readiness delay.
// The in-page request deadline is 10 seconds and can expose its own Retry first.
constexpr auto ProductStartupDeadline = std::chrono::seconds(20);

// Keep a successful initialization balanced for the entire GUI STA, across host recreation.
struct GuiApartment
{
    HRESULT status = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ~GuiApartment() { if (SUCCEEDED(status)) CoUninitialize(); }
};

template<class Interface>
struct CreationResult
{
    LauncherBridge::CreationOperation operation;
    HRESULT callbackStatus = E_PENDING;
    ComPtr<Interface> value;
};

QString takeString(LPWSTR value)
{
    const auto result = value ? QString::fromWCharArray(value) : QString{};
    CoTaskMemFree(value);
    return result;
}
}

struct WebViewLauncherHost::State : std::enable_shared_from_this<State>
{
    QPointer<WebViewLauncherHost> owner;
    QPointer<QLabel> statusLabel;
    std::function<QJsonObject()> snapshot;
    std::function<QJsonObject()> refresh;
    std::function<QJsonObject(const DisplayId&)> identify;
    std::function<QJsonObject(const QUuid&, const DisplayId&)> createSession;
    std::function<QJsonObject(const QUuid&, const QUuid&)> openSession;
    LauncherLanguage* language = nullptr; // Coordinator owner outlives every host.
    ComPtr<ICoreWebView2Environment> environment;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2CompositionController> composition;
    std::unique_ptr<OpaqueWebViewVisual> compositionVisual;
    std::unique_ptr<SessionCompositionInput> compositionInput;
    ProductWebViewChrome chrome;
    ComPtr<ICoreWebView2> web;
    LauncherBridge::PageFence page;
    EventRegistrationToken navigationStarting{}, navigationCompleted{}, frameNavigation{},
        newWindow{}, messageReceived{}, processFailed{}, downloadStarting{}, scaleChanged{}, zoomChanged{};
    bool closed = false;
    bool failed = false;
    bool ready = false; // Current-document bridge admission, before product presentation.
    bool navigationCompletedForPage = false;
    std::optional<ProductPresentationStage> productReport;
    bool layoutQueued = false;
    quint64 startupAttempt = 0;
    UINT64 acceptedNavigation = 0;

    bool active() const { return !closed && !failed && owner && !owner->productFrameFailed(); }
    bool presentingProduct() const
    {
        return active() && navigationCompletedForPage && productReport.has_value();
    }

    QWidget* controllerParent() const
    {
        return owner->productViewport();
    }

    void beginStartupDeadline()
    {
        const auto attempt = ++startupAttempt;
        QTimer::singleShot(ProductStartupDeadline, owner, [weak = weak_from_this(), attempt] {
            if (const auto state = weak.lock(); state && state->active() && state->startupAttempt == attempt
                && !state->presentingProduct())
                state->fail(QStringLiteral("Launcher product presentation did not become available"));
        });
    }

    void resetForNavigation()
    {
        chrome.reset();
        if (compositionInput) compositionInput->invalidate();
        ready = false;
        navigationCompletedForPage = false;
        productReport.reset();
        acceptedNavigation = 0;
        owner->setProductPresentationStage(ProductPresentationStage::Navigating);
        if (statusLabel) statusLabel->show();
        layout();
    }

    void showProductIfReady()
    {
        if (!active() || !navigationCompletedForPage) return;
        if (!productReport)
        {
            owner->setProductPresentationStage(ProductPresentationStage::AwaitingProduct);
            return;
        }
        if (productReport == ProductPresentationStage::Failed)
        {
            chrome.reset();
            owner->setRecoverableLauncherPageFailure();
        }
        else owner->setProductPresentationStage(*productReport);
        if (statusLabel) statusLabel->hide();
        layout();
        if (!active()) return;
        const auto generation = page.generation();
        const auto report = productReport;
        if (controller && controllerParent()->isVisible() && owner->hasFocus() && !owner->isMinimized()
            && GetForegroundWindow() == reinterpret_cast<HWND>(owner->internalWinId()))
            controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
        if (active() && page.generation() == generation)
        {
            projectHideAvailability();
            qInfo().noquote() << (report == ProductPresentationStage::Failed
                ? "[LauncherHost] product_failed" : "[LauncherHost] product_ready") << generation;
        }
    }

    bool productMessage(const QByteArray& json, const QString& source)
    {
        if (!active() || json.size() > 16 * 1024) return false;
        const auto object = QJsonDocument::fromJson(json).object();
        if (object.value("kind") != QJsonValue{"shell-presentation"}) return false;
        // The four permitted fields have scalar values containing no colons.
        // Counting separators also rejects duplicate keys collapsed by QJsonObject.
        if (!ready || source != page.document() || object.size() != 4 || json.count(':') != 4
            || object.value("version") != QJsonValue{1}
            || object.value("generation") != QJsonValue{page.generation()}) return true;
        const auto state = object.value("state");
        if (state != QJsonValue{"ready"} && state != QJsonValue{"failed"}) return true;
        const auto report = state == QJsonValue{"ready"} ? ProductPresentationStage::Ready
            : ProductPresentationStage::Failed;
        if (productReport != report)
        {
            productReport = report;
            showProductIfReady();
        }
        return true;
    }

    bool readyForHide() const
    {
        return ready && presentingProduct() && productReport == ProductPresentationStage::Ready;
    }

    void projectHideAvailability()
    {
        if (!readyForHide() || !web) return;
        const QJsonObject packet{{"kind", "launcher-hide-state"}, {"version", 1},
            {"generation", page.generation()}, {"eligible", owner->trayPresentation().eligible()}};
        const auto json = QString::fromUtf8(QJsonDocument(packet).toJson(QJsonDocument::Compact));
        const auto target = web;
        target->PostWebMessageAsJson(reinterpret_cast<LPCWSTR>(json.utf16()));
    }

    bool hideMessage(const QByteArray& json, const QString& source)
    {
        if (json.size() > 512) return false;
        const auto packet = QJsonDocument::fromJson(json).object();
        if (packet.value("kind") != QJsonValue{"launcher-hide-request"}) return false;
        if (!readyForHide() || source != page.document() || page.generation().isEmpty()
            || packet.size() != 3 || json.count(':') != 3 || packet.value("version") != QJsonValue{1}
            || packet.value("generation") != QJsonValue{page.generation()}) return true;
        if (chrome.isModalSuspended(page.generation())) return true;

        const auto show = owner->trayPresentation().showGeneration();
        // Leave the WebView callback before changing the native window's visibility.
        QMetaObject::invokeMethod(QCoreApplication::instance(), [weak = weak_from_this(), generation = page.generation(), show] {
            const auto state = weak.lock();
            if (!state || !state->readyForHide() || state->page.generation() != generation
                || state->chrome.isModalSuspended(generation)
                || !state->owner->isVisible() || !state->owner->isEnabled() || state->owner->isMinimized()) return;
            auto& tray = state->owner->trayPresentation();
            if (tray.showGeneration() == show) tray.requestHide();
            state->projectHideAvailability();
        }, Qt::QueuedConnection);
        return true;
    }

    void retranslate()
    {
        if (closed || !owner || !language) return;
        const bool chinese = language->current().effective == UiLanguage::SimplifiedChinese;
        owner->setWindowTitle(chinese ? QStringLiteral("DesktopSceneEngine — 启动器")
            : QStringLiteral("DesktopSceneEngine — Launcher"));
        if (statusLabel)
            statusLabel->setText(failed
                ? (chinese ? QStringLiteral("启动器加载失败。请关闭并重新打开启动器。")
                    : QStringLiteral("Launcher failed to load. Close and reopen the Launcher."))
                : (chinese ? QStringLiteral("正在启动启动器…") : QStringLiteral("Starting Launcher…")));
    }

    void projectLanguage(const LauncherLanguageProjection& projection)
    {
        retranslate();
        if (!active() || !ready || !web) return;
        const auto generation = page.generation();
        const QJsonObject event{{"kind", "event"}, {"version", LauncherBridge::Version},
            {"type", "LauncherLanguageChanged"}, {"generation", generation},
            {"payload", serializeLauncherLanguage(projection)}};
        const auto serialized = QString::fromUtf8(QJsonDocument(event).toJson(QJsonDocument::Compact));
        require(web->PostWebMessageAsJson(reinterpret_cast<LPCWSTR>(serialized.utf16())), "Post Launcher language projection");
    }

    void projectSessions(const LauncherSessionsSnapshot& sessions)
    {
        if (!active() || !ready || !web) return;
        try
        {
            const auto generation = page.generation();
            const QJsonObject event{{"kind", "event"}, {"version", LauncherBridge::Version},
                {"type", "LauncherSessionsChanged"}, {"generation", generation},
                {"payload", launcherSessionsProjection(sessions)}};
            const auto serialized = QString::fromUtf8(QJsonDocument(event).toJson(QJsonDocument::Compact));
            if (!active() || !ready || page.generation() != generation) return;
            require(web->PostWebMessageAsJson(reinterpret_cast<LPCWSTR>(serialized.utf16())), "Post Launcher Session projection");
        }
        catch (...) { fail(QStringLiteral("Invalid native Session projection")); }
    }

    void fail(const QString& message)
    {
        if (closed || failed) return;
        failed = true;
        ready = false;
        chrome.reset();
        if (compositionInput) compositionInput->retire();
        page.close();
        if (controller) controller->put_IsVisible(FALSE);
        if (composition) composition->put_RootVisualTarget(nullptr);
        compositionVisual.reset();
        if (owner) owner->setProductPresentationStage(ProductPresentationStage::Failed, message);
        if (statusLabel)
        {
            retranslate();
            statusLabel->show();
            statusLabel->raise();
        }
        qInfo().noquote() << "[LauncherHost] failed:" << message;
    }

    bool require(HRESULT status, const char* operation)
    {
        if (SUCCEEDED(status)) return true;
        fail(QStringLiteral("%1 (0x%2)").arg(QString::fromLatin1(operation),
            QString::number(static_cast<quint32>(status), 16)));
        return false;
    }

    // Queueing alone is insufficient: a factory can dispatch a nested Qt loop before return.
    // The operation gate is checked on every completion attempt, and retried after factory return.
    template<class Interface, class Adopt>
    static void settle(const std::weak_ptr<State>& weak,
        const std::shared_ptr<CreationResult<Interface>>& pending, Adopt adopt)
    {
        const auto state = weak.lock();
        const auto disposition = pending->operation.consume(!state || !state->active());
        if (!disposition) return;
        if (*disposition != LauncherBridge::CreationDisposition::Adopt)
        {
            if constexpr (std::is_same_v<Interface, ICoreWebView2CompositionController>)
                closeProductCompositionController(pending->value.Get());
            pending->value.Reset();
            if (*disposition == LauncherBridge::CreationDisposition::FactoryFailed && state && state->active())
                state->fail(QStringLiteral("WebView2 creation factory failed"));
            return;
        }
        if (FAILED(pending->callbackStatus) || !pending->value)
        {
            if constexpr (std::is_same_v<Interface, ICoreWebView2CompositionController>)
                closeProductCompositionController(pending->value.Get());
            pending->value.Reset();
            state->fail(QStringLiteral("WebView2 asynchronous creation failed"));
            return;
        }
        adopt(*state, pending->value);
        pending->value.Reset();
    }

    void start()
    {
        if (closed || failed || !owner) return;
        (void)owner->winId();
        if (closed || failed || !owner) return;
        if (owner->productFrameFailed())
        {
            fail(QStringLiteral("native-product-frame-unavailable"));
            return;
        }
        owner->setProductPresentationStage(ProductPresentationStage::OpeningWebView);
        beginStartupDeadline();
        static thread_local GuiApartment apartment;
        if (!require(apartment.status, "GUI STA initialization")) return;
        const auto assets = QDir(QCoreApplication::applicationDirPath()).filePath("launcher");
        if (!QFileInfo::exists(QDir(assets).filePath("index.html")))
        {
            fail(QStringLiteral("Packaged Launcher assets are missing: %1").arg(assets));
            return;
        }
        const auto userData = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
            .filePath("WebView2-Launcher");
        if (!QDir{}.mkpath(userData))
        {
            fail(QStringLiteral("WebView2 user-data directory is unavailable"));
            return;
        }
        const auto weak = weak_from_this();
        const auto pending = std::make_shared<CreationResult<ICoreWebView2Environment>>();
        const auto complete = [weak, pending] {
            settle(weak, pending, [](State& state, const ComPtr<ICoreWebView2Environment>& value) {
                state.environment = value;
                state.createController();
            });
        };
        const auto status = CreateCoreWebView2EnvironmentWithOptions(nullptr,
            reinterpret_cast<LPCWSTR>(userData.utf16()), nullptr,
            Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [weak, pending, complete](HRESULT error, ICoreWebView2Environment* value) -> HRESULT {
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

    void createController()
    {
        if (!active() || !environment) return;
        ComPtr<ICoreWebView2Environment3> environment3;
        if (!require(environment.As(&environment3), "Launcher composition environment")) return;
        const auto weak = weak_from_this();
        const auto pending = std::make_shared<CreationResult<ICoreWebView2CompositionController>>();
        const auto complete = [weak, pending] {
            settle(weak, pending, [weak](State& state, const ComPtr<ICoreWebView2CompositionController>& value) {
                state.composition = value;
                if (!state.require(value.As(&state.controller), "Launcher composition controller interface") || !state.active()) return;
                if (!state.require(state.controller->put_IsVisible(FALSE), "Hide pending Launcher WebView") || !state.active()) return;
                try {
                    const auto input = QPointer<QWidget>{state.controllerParent()};
                    state.compositionVisual = std::make_unique<OpaqueWebViewVisual>(input->winId());
                    if (!state.active() || !input || !state.require(value->put_RootVisualTarget(state.compositionVisual->visual()),
                        "Launcher composition visual")) return;
                    if (!state.active() || !state.require(state.compositionVisual->commit(), "Launcher composition commit")) return;
                    if (!state.active() || !input) return;
                    state.compositionInput = std::make_unique<SessionCompositionInput>(*input.data(), value.Get(), state.controller.Get(), [weak] {
                        const auto current = weak.lock();
                        // A trusted recoverable page still owns its Retry button.
                        return current && current->presentingProduct();
                    });
                    state.configure();
                } catch (const std::exception& error) { state.fail(QString::fromUtf8(error.what())); }
            });
        };
        const auto hwnd = reinterpret_cast<HWND>(controllerParent()->winId());
        const auto status = environment3->CreateCoreWebView2CompositionController(hwnd,
            Callback<ICoreWebView2CreateCoreWebView2CompositionControllerCompletedHandler>(
                [weak, pending, complete](HRESULT error, ICoreWebView2CompositionController* value) -> HRESULT {
                    if (!pending->operation.callbackArrived())
                    {
                        closeProductCompositionController(value);
                        return S_OK;
                    }
                    pending->callbackStatus = error;
                    const auto state = weak.lock();
                    if (!state || !state->active()) closeProductCompositionController(value);
                    else pending->value = value;
                    if (QCoreApplication::instance())
                        QTimer::singleShot(0, QCoreApplication::instance(), complete);
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
        ComPtr<ICoreWebView2Controller3> controller3;
        ComPtr<ICoreWebView2Settings> settings;
        ComPtr<ICoreWebView2Settings5> settings5;
        ComPtr<ICoreWebView2_3> web3;
        ComPtr<ICoreWebView2_4> web4;
        const auto color = controllerParent()->palette().color(QPalette::Window);
        if (!require(controller.As(&controller2), "Launcher composition background interface")
            || !require(controller2->put_DefaultBackgroundColor({255, static_cast<BYTE>(color.red()),
                static_cast<BYTE>(color.green()), static_cast<BYTE>(color.blue())}), "Launcher opaque background")
            || !require(controller.As(&controller3), "WebView2 raw-pixel bounds")
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
        const auto assets = QDir::toNativeSeparators(QDir(QCoreApplication::applicationDirPath()).filePath("launcher"));
        if (!require(web3->SetVirtualHostNameToFolderMapping(L"launcher.dse.local",
            reinterpret_cast<LPCWSTR>(assets.utf16()), COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS), "Map Launcher assets")) return;
        const auto weak = weak_from_this();
        if (!require(controller3->add_RasterizationScaleChanged(Callback<ICoreWebView2RasterizationScaleChangedEventHandler>(
                [weak](ICoreWebView2Controller*, IUnknown*) -> HRESULT {
                    if (const auto state = weak.lock(); state && state->active()) state->refreshChrome();
                    return S_OK;
                }).Get(), &scaleChanged), "Launcher chrome rasterization scale")
            || !require(controller->add_ZoomFactorChanged(Callback<ICoreWebView2ZoomFactorChangedEventHandler>(
                [weak](ICoreWebView2Controller*, IUnknown*) -> HRESULT {
                    if (const auto state = weak.lock(); state && state->active()) state->refreshChrome();
                    return S_OK;
                }).Get(), &zoomChanged), "Launcher chrome zoom")) return;
        if (!require(web->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>(
            [weak](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                const auto state = weak.lock();
                if (!state || !state->active()) { args->put_Cancel(TRUE); return S_OK; }
                LPWSTR uri{};
                if (FAILED(args->get_Uri(&uri))) { args->put_Cancel(TRUE); return S_OK; }
                state->page.setPresentationLanguage(state->language->current().effective);
                const auto disposition = state->page.navigationStarting(takeString(uri));
                if (disposition == LauncherBridge::NavigationDisposition::Allow)
                {
                    if (!state->require(args->get_NavigationId(&state->acceptedNavigation), "Launcher navigation identity"))
                    {
                        args->put_Cancel(TRUE);
                        return S_OK;
                    }
                    state->ready = true; // The current document may bootstrap before NavigationCompleted.
                }
                else
                {
                    args->put_Cancel(TRUE);
                    if (disposition == LauncherBridge::NavigationDisposition::RestartWithFreshGeneration)
                    {
                        state->resetForNavigation();
                        state->beginStartupDeadline();
                        const auto generation = state->page.generation();
                        QTimer::singleShot(0, QCoreApplication::instance(), [weak, generation] {
                            if (const auto current = weak.lock(); current && current->active()
                                && current->page.generation() == generation) current->navigate();
                        });
                    }
                }
                return S_OK;
            }).Get(), &navigationStarting), "Navigation restriction")) return;
        if (!require(web->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [weak](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                const auto state = weak.lock();
                if (!state || !state->active()) return S_OK;
                UINT64 id{};
                args->get_NavigationId(&id);
                if (!state->ready || id != state->acceptedNavigation) return S_OK;
                BOOL success{};
                args->get_IsSuccess(&success);
                if (!success) state->fail(QStringLiteral("Launcher document navigation failed"));
                else
                {
                    state->navigationCompletedForPage = true;
                    qInfo().noquote() << "[LauncherHost] document_ready" << state->page.generation();
                    state->showProductIfReady();
                }
                return S_OK;
            }).Get(), &navigationCompleted), "Navigation completion")) return;
        if (!require(web->add_FrameNavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>(
            [](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                args->put_Cancel(TRUE); return S_OK;
            }).Get(), &frameNavigation), "Block frame navigation")
            || !require(web->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                    args->put_Handled(TRUE); return S_OK;
                }).Get(), &newWindow), "Block new windows")
            || !require(web4->add_DownloadStarting(Callback<ICoreWebView2DownloadStartingEventHandler>(
                [](ICoreWebView2*, ICoreWebView2DownloadStartingEventArgs* args) -> HRESULT {
                    args->put_Cancel(TRUE); return S_OK;
                }).Get(), &downloadStarting), "Block downloads")) return;
        if (!require(web->add_WebMessageReceived(Callback<ICoreWebView2WebMessageReceivedEventHandler>(
            [weak](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                if (const auto state = weak.lock(); state && state->active())
                    try { state->message(args); }
                    catch (...) { state->fail(QStringLiteral("Launcher bridge processing failed")); }
                return S_OK;
            }).Get(), &messageReceived), "Launcher bridge")
            || !require(web->add_ProcessFailed(Callback<ICoreWebView2ProcessFailedEventHandler>(
                [weak](ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs*) -> HRESULT {
                    if (const auto state = weak.lock(); state && state->active())
                        state->fail(QStringLiteral("WebView2 browser process failed"));
                    return S_OK;
                }).Get(), &processFailed), "Browser process failure")) return;
        require(web->AddScriptToExecuteOnDocumentCreated(
            L"Object.defineProperty(window, '__dseProductShell', {value: true});",
            Callback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>(
                [weak](HRESULT status, LPCWSTR) -> HRESULT {
                    if (const auto app = QCoreApplication::instance())
                        QTimer::singleShot(0, app, [weak, status] {
                            if (const auto state = weak.lock(); state && state->active()
                                && state->require(status, "Launcher shell script registration"))
                                state->navigateFirstDocument();
                        });
                    return S_OK;
                }).Get()), "Register Launcher shell script");
    }

    void navigateFirstDocument()
    {
        if (!active()) return;
        page.setPresentationLanguage(language->current().effective);
        page.prepare();
        resetForNavigation();
        navigate();
    }

    void navigate()
    {
        if (active() && web)
            require(web->Navigate(reinterpret_cast<LPCWSTR>(page.document().utf16())), "Navigate Launcher");
    }

    template<class Provider, class... Arguments>
    static QJsonObject callProvider(Provider provider, const Arguments&... arguments)
    {
        // A native provider may pump a nested event loop that retires this Host.
        // Keep its callable/captures alive independently of close() clearing State.
        if (!provider) throw std::runtime_error("Launcher provider unavailable");
        return provider(arguments...);
    }

    QJsonObject commandPayload(const LauncherBridge::Request& request)
    {
        switch (request.type)
        {
        case LauncherBridge::Command::Bootstrap: return callProvider(snapshot);
        case LauncherBridge::Command::RefreshDisplays: return callProvider(refresh);
        case LauncherBridge::Command::IdentifyDisplay: return callProvider(identify, *request.displayId);
        case LauncherBridge::Command::CommitLanguage: return language->commit(*request.language);
        case LauncherBridge::Command::CreateSession:
            return callProvider(createSession, *request.coordinatorEpoch, *request.displayId);
        case LauncherBridge::Command::OpenSession:
            return callProvider(openSession, *request.coordinatorEpoch, *request.incarnation);
        }
        throw std::runtime_error("Unknown Launcher command");
    }

    void message(ICoreWebView2WebMessageReceivedEventArgs* args)
    {
        if (!ready || !web || !active()) return;
        LPWSTR source{};
        if (FAILED(args->get_Source(&source))) return;
        const auto uri = takeString(source);
        // Drop untrusted/stale documents without replying into the current page.
        if (uri != page.document()) return;
        LPWSTR json{};
        if (FAILED(args->get_WebMessageAsJson(&json))) return;
        const auto packet = takeString(json).toUtf8();
        if (productMessage(packet, uri)) return;
        if (hideMessage(packet, uri)) return;
        if (chrome.receive(packet, page.generation(), controllerParent(), controller.Get(), web.Get(),
            presentingProduct() && productReport == ProductPresentationStage::Ready)) return;
        const auto parsed = LauncherBridge::parseRequest(packet, uri, page.document(), page.generation());
        const auto respondingGeneration = page.generation();
        QJsonObject response;
        if (!parsed.request)
        {
            if (parsed.requestId.isEmpty()) return;
            response = LauncherBridge::failure(parsed.requestId, page.generation(), parsed.error, parsed.type);
        }
        else
        {
            try
            {
                response = LauncherBridge::result(*parsed.request, commandPayload(*parsed.request));
            }
            catch (...)
            {
                response = LauncherBridge::failure(parsed.requestId, page.generation(),
                    parsed.type == LauncherBridge::Command::CreateSession ? "create-provider-failed" :
                        parsed.type == LauncherBridge::Command::OpenSession ? "open-provider-failed" :
                        parsed.type == LauncherBridge::Command::CommitLanguage ? "language-provider-failed" :
                        parsed.type == LauncherBridge::Command::IdentifyDisplay ? "identify-provider-failed" :
                        parsed.type == LauncherBridge::Command::RefreshDisplays ? "display-catalog-provider-failed" : "bootstrap-provider-failed",
                    parsed.type);
            }
        }
        if (!active() || !ready || page.generation() != respondingGeneration || page.document() != uri) return;
        const auto serialized = QString::fromUtf8(QJsonDocument(response).toJson(QJsonDocument::Compact));
        require(web->PostWebMessageAsJson(reinterpret_cast<LPCWSTR>(serialized.utf16())), "Post Launcher result");
    }

    void refreshChrome()
    {
        if (active()) chrome.refresh(controllerParent(), controller.Get(), web.Get(), page.generation(),
            presentingProduct() && productReport == ProductPresentationStage::Ready);
    }

    void queueDpiLayout()
    {
        if (!active() || layoutQueued) return;
        layoutQueued = true;
        // The Qt notification can precede the viewport HWND's physical resize.
        QMetaObject::invokeMethod(QCoreApplication::instance(), [weak = weak_from_this()] {
            if (const auto state = weak.lock())
            {
                state->layoutQueued = false;
                state->layout();
            }
        }, Qt::QueuedConnection);
    }

    void layout()
    {
        if (!active() || !controller) return;
        const auto current = controller;
        const auto hwnd = reinterpret_cast<HWND>(controllerParent()->winId());
        RECT bounds{};
        if (GetClientRect(hwnd, &bounds)
            && !require(current->put_Bounds(bounds), "Launcher viewport bounds")) return;
        if (!active()) return;
        current->NotifyParentWindowPositionChanged();
        if (!active()) return;
        const bool show = presentingProduct() && controllerParent()->isVisible() && !owner->isMinimized();
        if (require(current->put_IsVisible(show ? TRUE : FALSE), "Launcher viewport visibility")) refreshChrome();
    }

    void close()
    {
        if (closed) return;
        closed = true;
        ready = false;
        chrome.reset();
        if (compositionInput) compositionInput->retire();
        page.close();
        if (owner) owner->setProductPresentationStage(ProductPresentationStage::Retired);
        snapshot = {};
        refresh = {};
        identify = {};
        createSession = {};
        openSession = {};
        language = nullptr;
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
            ComPtr<ICoreWebView2Controller3> controller3;
            if (scaleChanged.value && SUCCEEDED(controller.As(&controller3))) controller3->remove_RasterizationScaleChanged(scaleChanged);
            if (zoomChanged.value) controller->remove_ZoomFactorChanged(zoomChanged);
            if (composition) composition->put_RootVisualTarget(nullptr);
            compositionVisual.reset();
            controller->Close();
        }
        compositionInput.reset();
        composition.Reset();
        web.Reset();
        controller.Reset();
        environment.Reset();
        qInfo() << "[LauncherHost] retired";
    }
};

WebViewLauncherHost::WebViewLauncherHost(std::function<QJsonObject()> snapshotProvider,
    std::function<QJsonObject()> refreshProvider,
    std::function<QJsonObject(const DisplayId&)> identifyProvider, LauncherLanguage& language, QWidget* parent)
    : ProductWindowHost(ProductWindowRole::Launcher, parent), state_(std::make_shared<State>())
{
    setAttribute(Qt::WA_NativeWindow);
    setFocusPolicy(Qt::StrongFocus);
    productViewport()->setFocusPolicy(Qt::StrongFocus);
    resize(1120, 760);
    setMinimumSize(720, 540);
    auto* status = new QLabel(this);
    status->setAlignment(Qt::AlignCenter);
    status->setWordWrap(true);
    setProductContent(status);
    installEventFilter(this);
    productViewport()->installEventFilter(this);
    state_->owner = this;
    state_->statusLabel = status;
    state_->snapshot = std::move(snapshotProvider);
    state_->refresh = std::move(refreshProvider);
    state_->identify = std::move(identifyProvider);
    state_->language = &language;
    state_->retranslate();
    const auto weak = std::weak_ptr<State>{state_};
    trayPresentation_.bindEligibilityChanged([this, weak](bool) {
        QMetaObject::invokeMethod(this, [weak] {
            if (const auto state = weak.lock()) state->projectHideAvailability();
        }, Qt::QueuedConnection);
    });
    QTimer::singleShot(0, this, [weak] { if (const auto state = weak.lock()) state->start(); });
}

WebViewLauncherHost::~WebViewLauncherHost() { state_->close(); }
bool WebViewLauncherHost::isWebViewReady() const { return state_->active() && state_->ready; }
QString WebViewLauncherHost::pageGeneration() const { return state_->page.generation(); }
void WebViewLauncherHost::languageChanged(const LauncherLanguageProjection& language) { state_->projectLanguage(language); }
void WebViewLauncherHost::sessionsChanged(const LauncherSessionsSnapshot& sessions) { state_->projectSessions(sessions); }
void WebViewLauncherHost::setSessionCommands(
    std::function<QJsonObject(const QUuid&, const DisplayId&)> create,
    std::function<QJsonObject(const QUuid&, const QUuid&)> open)
{
    state_->createSession = std::move(create);
    state_->openSession = std::move(open);
}

void WebViewLauncherHost::closeEvent(QCloseEvent* event)
{
    ProductWindowHost::closeEvent(event);
    if (!event->isAccepted() || state_->closed) return;
    trayPresentation_.beginTeardown();
    state_->close();
    trayPresentation_.completeClose(true);
    if (acceptedCloseCallback_) acceptedCloseCallback_();
}

void WebViewLauncherHost::resizeEvent(QResizeEvent* event) { ProductWindowHost::resizeEvent(event); state_->layout(); }
void WebViewLauncherHost::moveEvent(QMoveEvent* event) { ProductWindowHost::moveEvent(event); state_->layout(); }
void WebViewLauncherHost::showEvent(QShowEvent* event) { ProductWindowHost::showEvent(event); state_->layout(); }
void WebViewLauncherHost::hideEvent(QHideEvent* event) { ProductWindowHost::hideEvent(event); state_->layout(); }
void WebViewLauncherHost::changeEvent(QEvent* event)
{
    ProductWindowHost::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange) state_->layout();
}
void WebViewLauncherHost::focusInEvent(QFocusEvent* event)
{
    ProductWindowHost::focusInEvent(event);
    const auto hwnd = reinterpret_cast<HWND>(internalWinId());
    const auto focused = GetFocus();
    if (state_->presentingProduct() && state_->controller && state_->controllerParent()->isVisible() && !isMinimized()
        && hwnd && GetActiveWindow() == hwnd && (!focused || GetAncestor(focused, GA_ROOT) == hwnd))
        state_->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
}
bool WebViewLauncherHost::eventFilter(QObject* watched, QEvent* event)
{
    if ((watched == this || watched == productViewport()) && event->type() == QEvent::DevicePixelRatioChange)
        state_->queueDpiLayout();
    if (watched == productViewport()
        && (event->type() == QEvent::Resize || event->type() == QEvent::Show || event->type() == QEvent::Hide)) state_->layout();
    return ProductWindowHost::eventFilter(watched, event);
}
}
