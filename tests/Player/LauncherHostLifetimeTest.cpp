// White-box lifetime proof of the production State implementation. This single
// test translation unit includes Host.cpp; its target must not compile it again.
// The friend below adds no production metrics or behavior switch.
#include "../../Player/WebViewLauncherHost.cpp"

#include <QApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <iostream>
#include <string>
#include <vector>

namespace DesktopSceneEngine::Player
{
namespace
{
class TrackedUnknown final : public IUnknown
{
public:
    explicit TrackedUnknown(int& live) : live_(live) { ++live_; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** value) override
    {
        if (value) *value = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const auto remaining = --references_;
        if (!remaining) delete this;
        return remaining;
    }
private:
    ~TrackedUnknown() { --live_; }
    ULONG references_ = 1;
    int& live_;
};

// Control-side recorder only. The real Launcher HWND and its body layout
// determine whether the production Host makes this controller visible.
class VisibilityController final : public Microsoft::WRL::RuntimeClass<
    Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, ICoreWebView2Controller>
{
public:
    bool visible = false;
    HRESULT STDMETHODCALLTYPE get_IsVisible(BOOL* value) override { *value = visible; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_IsVisible(BOOL value) override { visible = value != FALSE; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Bounds(RECT*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE put_Bounds(RECT) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE get_ZoomFactor(double*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE put_ZoomFactor(double) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE add_ZoomFactorChanged(ICoreWebView2ZoomFactorChangedEventHandler*, EventRegistrationToken*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE remove_ZoomFactorChanged(EventRegistrationToken) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetBoundsAndZoomFactor(RECT, double) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE add_MoveFocusRequested(ICoreWebView2MoveFocusRequestedEventHandler*, EventRegistrationToken*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE remove_MoveFocusRequested(EventRegistrationToken) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE add_GotFocus(ICoreWebView2FocusChangedEventHandler*, EventRegistrationToken*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE remove_GotFocus(EventRegistrationToken) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE add_LostFocus(ICoreWebView2FocusChangedEventHandler*, EventRegistrationToken*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE remove_LostFocus(EventRegistrationToken) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE add_AcceleratorKeyPressed(ICoreWebView2AcceleratorKeyPressedEventHandler*, EventRegistrationToken*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE remove_AcceleratorKeyPressed(EventRegistrationToken) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE get_ParentWindow(HWND*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE put_ParentWindow(HWND) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE NotifyParentWindowPositionChanged() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Close() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE get_CoreWebView2(ICoreWebView2**) override { return E_NOTIMPL; }
};
}

struct LauncherHostLifetimeTestAccess
{
    static int run()
    {
        int checks = 0;
        int failures = 0;
        const auto check = [&](bool condition, const std::string& label) {
            ++checks;
            if (!condition) { ++failures; std::cerr << "FAIL: " << label << '\n'; }
        };
        using State = WebViewLauncherHost::State;
        using LauncherBridge::Command;

        // Exercise the real native Host closeEvent before its queued COM startup.
        // The existing State tests below cover provider and creation-result retirement.
        {
            QTemporaryDir directory;
            check(directory.isValid(), "native Host close receipt has an owned settings/profile directory");
            const auto previousProfile = qgetenv("WEBVIEW2_USER_DATA_FOLDER");
            qputenv("WEBVIEW2_USER_DATA_FOLDER", directory.filePath("profile").toUtf8());
            QSettings settings(directory.filePath("preferences.ini"), QSettings::IniFormat);
            ApplicationPreferencesStore store(settings);
            ApplicationTranslationState translation;
            LauncherLanguage language(store, translation, [] { return true; });
            WebViewLauncherHost host([] { return QJsonObject{}; }, [] { return QJsonObject{}; },
                [](const DisplayId&) { return QJsonObject{}; }, language);
            host.setAttribute(Qt::WA_ShowWithoutActivating);
            check(host.winId() != 0 && !host.state_->closed, "real native Launcher Host exists before Close");
            auto& state = *host.state_;
            const auto controller = Microsoft::WRL::Make<VisibilityController>();
            state.controller = controller;
            host.show();
            const auto* nativeClose = host.findChild<QWidget*>(QStringLiteral("dseProductFallbackClose"));
            const auto stage = [&] { return host.property("dseProductPresentation").toString(); };
            const auto productReport = [](const QString& generation, const char* status) {
                return QJsonDocument(QJsonObject{{"kind", "shell-presentation"}, {"version", 1},
                    {"generation", generation}, {"state", QString::fromLatin1(status)}}).toJson(QJsonDocument::Compact);
            };
            const auto hidePacket = [](const QString& generation) {
                return QJsonDocument(QJsonObject{{"kind", "launcher-hide-request"}, {"version", 1},
                    {"generation", generation}}).toJson(QJsonDocument::Compact);
            };
            const auto deliverHide = [] {
                QCoreApplication::sendPostedEvents(QCoreApplication::instance(), QEvent::MetaCall);
            };
            auto& tray = host.trayPresentation();
            std::vector<bool> hideAvailability;
            bool trayQualified = false;
            int hideRequests = 0;
            tray.bindEligibilityChanged([&](bool available) { hideAvailability.push_back(available); });
            tray.bindRecovery([&] { return trayQualified; }, [&] {
                ++hideRequests;
                check(tray.applyHidePermit(), "qualified Launcher receives its ordinary native tray Hide permit");
            });
            state.page.prepare();
            state.resetForNavigation();
            const auto originalDocument = state.page.document();
            const auto originalGeneration = state.page.generation();
            const auto readyReport = productReport(originalGeneration, "ready");
            const auto hideRequest = hidePacket(originalGeneration);
            check(state.hideMessage(hideRequest, originalDocument), "recognize Hide before the Launcher page is ready");
            deliverHide();
            check(host.isVisible() && hideRequests == 0 && hideAvailability == std::vector<bool>{false},
                "a not-yet-ready or unqualified Launcher does not hide");
            check(state.productMessage(readyReport, originalDocument) && !state.productReport,
                "recognized readiness before bridge admission cannot show a product");
            state.ready = state.page.navigationStarting(originalDocument) == LauncherBridge::NavigationDisposition::Allow;
            check(host.isWebViewReady() && !state.presentingProduct() && stage() == "navigating",
                "current document may bootstrap before navigation and product readiness");
            auto duplicate = readyReport;
            duplicate.insert(1, "\"state\":\"failed\",");
            check(state.productMessage(readyReport, QStringLiteral("https://launcher.dse.local/other.html"))
                && state.productMessage(productReport("old-generation", "ready"), originalDocument)
                && state.productMessage(duplicate, originalDocument) && !state.productReport,
                "wrong origin, stale generation and duplicate envelope cannot admit presentation");
            check(state.productMessage(readyReport, originalDocument) && state.productReport
                && !state.presentingProduct() && !state.statusLabel->isHidden(),
                "early valid ready receipt waits for navigation while native startup remains visible");
            const auto failedReport = productReport(originalGeneration, "failed");
            check(state.productMessage(failedReport, originalDocument) && !state.presentingProduct()
                && !controller->visible && nativeClose && nativeClose->isVisible(),
                "early recoverable receipt still waits for the current navigation to complete");
            state.navigationCompletedForPage = true;
            state.showProductIfReady();
            check(state.presentingProduct() && stage() == "failed" && host.productViewport()->isVisible()
                && controller->visible && state.statusLabel->isHidden() && nativeClose && !nativeClose->isVisible()
                && !host.isProductPresentationAvailable(),
                "first recoverable product failure displays the WebView for Retry with native chrome readiness still false");
            check(state.productMessage(readyReport, originalDocument) && stage() == "ready"
                && host.isProductPresentationAvailable() && host.productViewport()->isVisible() && controller->visible,
                "a successful same-document Retry recovers presentation");
            check(state.productMessage(failedReport, originalDocument) && stage() == "failed"
                && host.productViewport()->isVisible() && controller->visible && !host.isProductPresentationAvailable(),
                "recoverable failure from a ready page also keeps the same WebView visible");
            trayQualified = true;
            tray.refreshEligibility();
            tray.refreshEligibility();
            check(hideAvailability == std::vector<bool>({false, true}),
                "the native tray availability observer reports only changed eligibility");
            check(state.hideMessage(hideRequest, originalDocument), "recognize Hide for an unavailable product surface");
            deliverHide();
            check(host.isVisible() && hideRequests == 0, "a recoverable failure screen cannot hide the Launcher");
            check(state.productMessage(readyReport, originalDocument) && stage() == "ready", "the next Retry restores readiness");

            auto duplicateHide = hideRequest;
            duplicateHide.insert(1, "\"version\":1,");
            check(state.hideMessage(hidePacket("retired-generation"), originalDocument)
                && state.hideMessage(hideRequest, QStringLiteral("https://launcher.dse.local/other.html"))
                && state.hideMessage(duplicateHide, originalDocument), "recognize malformed or stale Launcher Hide messages");
            deliverHide();
            check(host.isVisible() && hideRequests == 0,
                "wrong source, stale generation and duplicate fields cannot hide the Launcher");
            check(state.hideMessage(hideRequest, originalDocument), "enqueue current native Hide before recovery loss");
            trayQualified = false;
            tray.refreshEligibility();
            deliverHide();
            check(host.isVisible() && hideRequests == 0 && !hideAvailability.empty() && !hideAvailability.back(),
                "loss of native restore authority disarms an already queued Hide");
            trayQualified = true;
            tray.refreshEligibility();
            check(state.hideMessage(hideRequest, originalDocument), "enqueue Hide before a native owned modal blocks the owner");
            const auto launcherWindow = reinterpret_cast<HWND>(host.winId());
            EnableWindow(launcherWindow, FALSE);
            check(!IsWindowEnabled(launcherWindow), "native owner is disabled before delivering Hide");
            deliverHide();
            check(host.isVisible() && hideRequests == 0, "a disabled native owner cannot hide during a modal interaction");
            EnableWindow(launcherWindow, TRUE);
            check(state.hideMessage(hideRequest, originalDocument),
                "enqueue current Hide before Settings makes the page's chrome inert");
            const auto suspended = QJsonDocument(QJsonObject{{"kind", "shell-chrome-geometry"}, {"version", 1},
                {"generation", originalGeneration}, {"viewport", QJsonObject{{"width", 1120}, {"height", 760}, {"dpr", 1}}},
                {"regions", QJsonObject{{"drag", QJsonValue::Null}, {"minimize", QJsonValue::Null},
                    {"maximize", QJsonValue::Null}, {"close", QJsonValue::Null}}}}).toJson(QJsonDocument::Compact);
            check(state.chrome.receive(suspended, originalGeneration, nullptr, nullptr, nullptr, true)
                && state.chrome.isModalSuspended(originalGeneration), "accept the current page's caption suspension");
            deliverHide();
            check(host.isVisible() && hideRequests == 0, "a queued Hide cannot hide Launcher behind HTML Settings");
            state.chrome.reset();
            check(state.hideMessage(hideRequest, originalDocument) && tray.restore(),
                "queue Hide and supersede it with an explicit native Show");
            deliverHide();
            check(host.isVisible() && hideRequests == 0, "an older Hide cannot undo a more recent ordinary Show");
            const auto originalGeometry = host.geometry();
            check(state.hideMessage(hideRequest, originalDocument) && state.hideMessage(hideRequest, originalDocument),
                "recognize two copies of the same qualified Hide intent");
            deliverHide();
            check(!host.isVisible() && tray.hiddenToTray() && !controller->visible && hideRequests == 1,
                "qualified Hide retires only native visibility and ignores duplicate delivery while hidden");
            check(tray.restore() && host.isVisible() && !tray.hiddenToTray() && controller->visible
                && host.geometry() == originalGeometry && state.page.generation() == originalGeneration,
                "ordinary tray restore returns the same Launcher page and floating geometry");
            check(state.hideMessage(hideRequest, originalDocument),
                "enqueue a valid Hide just before a current-page replacement");
            check(state.page.navigationStarting(originalDocument) == LauncherBridge::NavigationDisposition::RestartWithFreshGeneration,
                "reload receives a fresh Launcher generation before navigation");
            state.resetForNavigation();
            deliverHide();
            check(host.isVisible() && hideRequests == 1, "the queued old-page Hide cannot affect its replacement");
            check(state.productMessage(readyReport, originalDocument) && !state.productReport
                && !state.presentingProduct() && !state.statusLabel->isHidden() && stage() == "navigating",
                "late old-document receipt cannot revive React after reload reset");
            state.ready = state.page.navigationStarting(state.page.document()) == LauncherBridge::NavigationDisposition::Allow;
            state.navigationCompletedForPage = true;
            state.showProductIfReady();
            check(host.isWebViewReady() && !state.presentingProduct() && !state.statusLabel->isHidden()
                && stage() == "awaiting-product", "navigation alone does not expose a blank WebView");
            check(state.productMessage(productReport(state.page.generation(), "failed"), state.page.document())
                && host.productViewport()->isVisible() && controller->visible,
                "current replacement page can show Retry before a terminal native failure");
            state.fail(QStringLiteral("focused transport failure"));
            check(!state.presentingProduct() && !state.statusLabel->isHidden() && stage() == "failed"
                && !host.productViewport()->isVisible() && !controller->visible && nativeClose && nativeClose->isVisible(),
                "terminal transport failure hides the WebView and exposes the native failure and Close");
            std::vector<RoleCloseResult> receipts;
            host.trayPresentation().bindCloseReport([&] {
                if (const auto& operation = host.trayPresentation().closeOperation())
                    receipts.push_back(operation->result);
            });
            int acceptedCallbacks = 0;
            bool callbackSawClosedState = false;
            host.setAcceptedCloseCallback([&] {
                ++acceptedCallbacks;
                const auto& operation = host.trayPresentation().closeOperation();
                callbackSawClosedState = operation && operation->result == RoleCloseResult::CloseAccepted
                    && host.state_->closed && host.state_->page.closed() && !host.state_->snapshot
                    && !host.state_->refresh && !host.state_->identify && !host.state_->controller;
            });
            const RoleCloseKey key{QUuid::createUuid(), 1};
            check(host.trayPresentation().requestClose(key) == RoleCloseResult::CloseAccepted,
                "real Host ordinary Close returns its accepted receipt");
            check(receipts.size() == 2 && receipts.front() == RoleCloseResult::Pending
                && receipts.back() == RoleCloseResult::CloseAccepted && callbackSawClosedState && acceptedCallbacks == 1,
                "Pending becomes Accepted after State close and before the retirement callback");
            check(host.trayPresentation().requestClose(key) == RoleCloseResult::CloseAccepted && acceptedCallbacks == 1,
                "duplicate Host Close key preserves the terminal receipt without another callback");
            check(!state.productMessage(readyReport, originalDocument) && stage() == "retired",
                "late readiness cannot revive a retired native Launcher");
            if (previousProfile.isNull()) qunsetenv("WEBVIEW2_USER_DATA_FOLDER");
            else qputenv("WEBVIEW2_USER_DATA_FOLDER", previousProfile);
        }

        // Each cycle retains State across close just as an in-flight callback can.
        // Thus a passed weak_ptr check requires close itself to release providers;
        // destroying the whole State cannot conceal a retained closure.
        for (int cycle = 0; cycle < 50; ++cycle)
        {
            const auto state = std::make_shared<State>();
            auto resource = std::make_shared<int>(cycle);
            const std::weak_ptr<int> observed = resource;
            state->snapshot = [resource] { return QJsonObject{}; };
            state->refresh = [resource] { return QJsonObject{}; };
            state->identify = [resource](const DisplayId&) { return QJsonObject{}; };
            state->createSession = [resource](const QUuid&, const DisplayId&) { return QJsonObject{}; };
            state->openSession = [resource](const QUuid&, const QUuid&) { return QJsonObject{}; };
            state->page.prepare();
            resource.reset();
            check(!observed.expired(), "provider resource retained before teardown");
            state->close();
            check(observed.expired(), "all five provider closures released at teardown cycle " + std::to_string(cycle));
            check(!state->snapshot && !state->refresh && !state->identify
                && !state->createSession && !state->openSession && !state->language,
                "native provider pointers return to baseline");
            check(state->closed && !state->ready && state->page.closed()
                && state->page.generation().isEmpty() && !state->active(), "closed State rejects page work");
            state->close();
            check(observed.expired(), "repeat teardown is idempotent");
        }

        const auto epoch = QUuid::createUuid();
        const auto incarnation = QUuid::createUuid();
        const auto display = DisplayId::fromTargetDevicePath("host-lifetime-test");
        for (const auto command : {Command::Bootstrap, Command::RefreshDisplays,
            Command::IdentifyDisplay, Command::CreateSession, Command::OpenSession})
        {
            const auto state = std::make_shared<State>();
            auto resource = std::make_shared<int>(1);
            const std::weak_ptr<int> observed = resource;
            bool retainedDuringReentrantClose = false;
            auto provider = [resource, observed, raw = state.get(), &retainedDuringReentrantClose] {
                // Do not access the lambda object after close. A provider may pump
                // a nested Qt loop that retires its Host while native work proceeds.
                auto* const result = &retainedDuringReentrantClose;
                const auto resourceProbe = observed;
                auto* const stateToClose = raw;
                stateToClose->close();
                *result = !resourceProbe.expired();
                return QJsonObject{{"completed", true}};
            };
            switch (command)
            {
            case Command::Bootstrap: state->snapshot = std::move(provider); break;
            case Command::RefreshDisplays: state->refresh = std::move(provider); break;
            case Command::IdentifyDisplay:
                state->identify = [provider = std::move(provider)](const DisplayId&) { return provider(); }; break;
            case Command::CreateSession:
                state->createSession = [provider = std::move(provider)](const QUuid&, const DisplayId&) { return provider(); }; break;
            case Command::OpenSession:
                state->openSession = [provider = std::move(provider)](const QUuid&, const QUuid&) { return provider(); }; break;
            default: break;
            }
            resource.reset();
            LauncherBridge::Request request{"lifetime-check", "generation", command, display, {}, epoch, incarnation};
            const auto result = state->commandPayload(request);
            check(result.value("completed") == true && retainedDuringReentrantClose,
                std::string{LauncherBridge::commandName(command)} + " callable capture survives reentrant teardown");
            check(observed.expired(), std::string{LauncherBridge::commandName(command)} + " capture released after completion");
        }

        // Use the actual production settle implementation with a reference-counted
        // COM object. Interface behavior is irrelevant here: no adoption is legal
        // once the owner is closed or gone. Controller Close is covered by the
        // real WebView2 repeat harness, not simulated by this IUnknown probe.
        for (int cycle = 0; cycle < 50; ++cycle)
        {
            int live = 0;
            int adoptions = 0;
            auto state = std::make_shared<State>();
            const std::weak_ptr<State> weak = state;
            const auto pending = std::make_shared<CreationResult<IUnknown>>();
            pending->callbackStatus = S_OK;
            pending->value.Attach(new TrackedUnknown(live));
            const auto adopt = [&](State&, const ComPtr<IUnknown>&) { ++adoptions; };
            check(pending->operation.callbackArrived(), "creation callback accepted once");
            State::settle(weak, pending, adopt);
            check(live == 1 && adoptions == 0, "inline callback cannot settle before factory returns");
            state->close();
            if (cycle % 2) state.reset();
            pending->operation.factoryReturned(true);
            State::settle(weak, pending, adopt);
            check(live == 0 && adoptions == 0 && !pending->value, "closed/expired owner discards late COM result");
            State::settle(weak, pending, adopt);
            check(live == 0 && adoptions == 0 && !pending->operation.callbackArrived(), "settled creation remains one-shot");

            auto lateState = std::make_shared<State>();
            const std::weak_ptr<State> lateWeak = lateState;
            const auto late = std::make_shared<CreationResult<IUnknown>>();
            late->operation.factoryReturned(true);
            State::settle(lateWeak, late, adopt);
            lateState->close();
            lateState.reset();
            late->callbackStatus = S_OK;
            late->value.Attach(new TrackedUnknown(live));
            check(late->operation.callbackArrived(), "post-retirement callback accepted for disposal");
            State::settle(lateWeak, late, adopt);
            check(live == 0 && adoptions == 0, "callback arriving after destruction releases its COM result");
        }
        std::cout << "Launcher Host lifetime: " << checks - failures << '/' << checks << " PASS\n";
        std::cout << "Provider teardown repeats: 50; weak creation-completion repeats: 50\n";
        return failures ? 1 : 0;
    }
};
}

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    application.setQuitOnLastWindowClosed(false);
    return DesktopSceneEngine::Player::LauncherHostLifetimeTestAccess::run();
}
