// White-box proof of the actual production State; this target must not compile
// WebViewSessionHost.cpp separately. The private friend adds no runtime API.
#include "../../Player/WebViewSessionHost.cpp"

#include <QApplication>
#include <iostream>
#include <vector>

namespace DesktopSceneEngine::Player
{
namespace
{
class SessionTrackedUnknown final : public IUnknown
{
public:
    explicit SessionTrackedUnknown(int& live) : live_(live) { ++live_; }
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
    ~SessionTrackedUnknown() { --live_; }
    ULONG references_ = 1;
    int& live_;
};

// A controller boundary for the real State::layout path, not a browser/renderer
// substitute. Actual COM/HWND behavior is checked by the dedicated runtime.
class SessionLayoutController final : public Microsoft::WRL::RuntimeClass<
    Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, ICoreWebView2Controller>
{
public:
    std::function<void()> onBounds;
    std::function<void()> onMoveFocus;
    std::function<void()> onParentPosition;
    int focusMoves = 0;
    COREWEBVIEW2_MOVE_FOCUS_REASON lastFocusReason = COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC;
    int closes = 0;
    int visibilityWrites = 0;
    HRESULT STDMETHODCALLTYPE get_IsVisible(BOOL* value) override { *value = visible_; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_IsVisible(BOOL value) override { ++visibilityWrites; visible_ = value; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Bounds(RECT* value) override { *value = bounds_; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_Bounds(RECT value) override { bounds_ = value; const auto callback = onBounds; if (callback) callback(); return S_OK; }
    HRESULT STDMETHODCALLTYPE get_ZoomFactor(double* value) override { *value = 1; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_ZoomFactor(double) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE add_ZoomFactorChanged(ICoreWebView2ZoomFactorChangedEventHandler*, EventRegistrationToken*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE remove_ZoomFactorChanged(EventRegistrationToken) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetBoundsAndZoomFactor(RECT, double) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON reason) override
    {
        ++focusMoves; lastFocusReason = reason;
        if (onMoveFocus) onMoveFocus();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE add_MoveFocusRequested(ICoreWebView2MoveFocusRequestedEventHandler*, EventRegistrationToken*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE remove_MoveFocusRequested(EventRegistrationToken) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE add_GotFocus(ICoreWebView2FocusChangedEventHandler*, EventRegistrationToken*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE remove_GotFocus(EventRegistrationToken) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE add_LostFocus(ICoreWebView2FocusChangedEventHandler*, EventRegistrationToken*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE remove_LostFocus(EventRegistrationToken) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE add_AcceleratorKeyPressed(ICoreWebView2AcceleratorKeyPressedEventHandler*, EventRegistrationToken*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE remove_AcceleratorKeyPressed(EventRegistrationToken) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE get_ParentWindow(HWND* value) override { *value = nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_ParentWindow(HWND) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE NotifyParentWindowPositionChanged() override
    {
        if (onParentPosition) onParentPosition();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Close() override { ++closes; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_CoreWebView2(ICoreWebView2** value) override { *value = nullptr; return E_NOTIMPL; }
private:
    RECT bounds_{};
    BOOL visible_ = FALSE;
};

// The real settlement policy must QI/Close a composition result even when its
// callback arrives before factory return or after the owning Host has died.
class SessionCompositionResult final : public ICoreWebView2CompositionController
{
public:
    explicit SessionCompositionResult(ICoreWebView2Controller* controller):controller_(controller){}
    std::vector<bool> rootVisualWrites;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** value) override {
        if(!value)return E_POINTER;*value=nullptr;
        if(id==__uuidof(ICoreWebView2Controller))return controller_->QueryInterface(id,value);
        if(id==__uuidof(IUnknown)||id==__uuidof(ICoreWebView2CompositionController)){*value=static_cast<ICoreWebView2CompositionController*>(this);AddRef();return S_OK;}return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override{return ++references_;}
    ULONG STDMETHODCALLTYPE Release() override{const auto count=--references_;if(!count)delete this;return count;}
    HRESULT STDMETHODCALLTYPE get_RootVisualTarget(IUnknown**) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE put_RootVisualTarget(IUnknown* target) override
    {
        rootVisualWrites.push_back(target != nullptr);
        rootVisual_ = target;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SendMouseInput(COREWEBVIEW2_MOUSE_EVENT_KIND,COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS,UINT32,POINT) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SendPointerInput(COREWEBVIEW2_POINTER_EVENT_KIND,ICoreWebView2PointerInfo*) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE get_Cursor(HCURSOR*) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE get_SystemCursorId(UINT32*) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE add_CursorChanged(ICoreWebView2CursorChangedEventHandler*,EventRegistrationToken*) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE remove_CursorChanged(EventRegistrationToken) override{return S_OK;}
private:
    ~SessionCompositionResult()=default;
    ULONG references_=1;
    ComPtr<ICoreWebView2Controller> controller_;
    ComPtr<IUnknown> rootVisual_;
};
}

struct SessionHostLifetimeTestAccess
{
    static bool rejectControl(SessionBridge::Request, WebViewSessionHost::ControlCompletion)
    {
        return false;
    }

    template<class Check>
    static void verifySessionHide(Check check)
    {
        QWidget window;
        SessionPresentation presentation(window);
        bool qualified = true;
        bool localReady = true;
        std::function<bool()> permitAdmission;
        presentation.bindPermitRecovery([&] { return qualified; }, [&](std::function<bool()> guard) {
            permitAdmission = std::move(guard);
        });

        const auto incarnation = QUuid::createUuid();
        WebViewSessionHost host(incarnation, &window, {}, {}, rejectControl);
        QCoreApplication::removePostedEvents(&host, QEvent::MetaCall); // Hold creation; exercise the live Host admission path.
        host.bindTrayPresentation(presentation, [&] { return localReady; });
        window.show();
        host.show();
        const auto state = host.state_;
        state->page.prepare();
        state->ready = state->productReady = true;
        const auto generation = state->page.generation();
        const auto source = state->page.document();
        const auto encode = [](const QJsonObject& object) { return QJsonDocument(object).toJson(QJsonDocument::Compact); };
        const QJsonObject request{{"kind", "session-hide-request"}, {"version", 1}, {"generation", generation},
            {"sessionIncarnation", incarnation.toString(QUuid::WithoutBraces)}};
        auto invalid = request;
        invalid["sessionIncarnation"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
        check(state->hideMessage(encode(invalid), source), "Main consumes a mismatched Session Hide packet");
        check(state->hideMessage(encode(request), "https://unrelated.invalid/"), "Main consumes unauthorized source Hide");
        QCoreApplication::processEvents();
        check(!permitAdmission && window.isVisible(), "foreign source and Session cannot request a permit");

        WebViewSessionHost peer(incarnation, &window, {}, {}, rejectControl, &host);
        QCoreApplication::removePostedEvents(&peer, QEvent::MetaCall);
        const auto peerState = peer.state_;
        peerState->page.prepare();
        peerState->ready = peerState->productReady = true;
        auto peerRequest = request;
        peerRequest["generation"] = peerState->page.generation();
        check(peerState->hideMessage(encode(peerRequest), peerState->page.document()), "peer rejects Main Hide packet");
        QCoreApplication::processEvents();
        check(!permitAdmission, "BGM peer cannot request a Main Hide permit");

        const auto requestPermit = [&] {
            permitAdmission = {};
            const auto current = state->hideMessage(encode(request), source);
            QCoreApplication::processEvents();
            return current && permitAdmission && permitAdmission();
        };
        check(requestPermit(), "ready Main requests the existing native Hide permit");
        localReady = false;
        check(permitAdmission && !permitAdmission() && window.isVisible(), "local Close blocks pending web Hide");
        localReady = true;
        const QJsonObject nullRegions{{"drag", QJsonValue::Null}, {"minimize", QJsonValue::Null},
            {"maximize", QJsonValue::Null}, {"close", QJsonValue::Null}};
        const QJsonObject viewport{{"width", 800}, {"height", 600}, {"dpr", 1}};
        state->chrome.receive(encode({{"kind", "shell-chrome-geometry"}, {"version", 1}, {"generation", generation},
            {"viewport", viewport}, {"regions", nullRegions}}), generation, &host, nullptr, nullptr, true);
        check(permitAdmission && !permitAdmission() && window.isVisible(), "DOM modal blocks pending web Hide");
        check(!requestPermit(), "DOM modal blocks admission of a new Hide");
        state->chrome.reset();

        check(requestPermit(), "active page can request Hide again after its modal is dismissed");
        check(permitAdmission && permitAdmission() && presentation.applyHidePermit()
            && !window.isVisible() && !window.isMinimized(), "permitted request uses native non-minimizing Hide");
        check(presentation.restore() && window.isVisible(), "native restore shows the same primary after Hide");
        check(requestPermit(), "restored Main can request another Hide");
        state->page.prepare();
        check(permitAdmission && !permitAdmission() && window.isVisible(), "the retired web page cannot commit a pending Hide");
        check(!requestPermit(), "retired generation cannot ask for another Hide");
    }

    template<class Check>
    static void verifyFocusHandoff(Check check)
    {
        // Actual production focusInEvent, existing recording controller seam,
        // and real current-thread native activation/focus. No copied guard or
        // browser substitute is used to decide whether MoveFocus is forwarded.
        QWidget root, peer;
        root.setWindowTitle(QStringLiteral("C4 owned root focus test"));
        peer.setWindowTitle(QStringLiteral("C4 owned peer focus test"));
        root.resize(240, 180); peer.resize(240, 180);
        const auto session = QUuid::createUuid();
        WebViewSessionHost rootHost(session, &root, {}, {}, rejectControl);
        WebViewSessionHost peerHost(session, &peer, {}, {}, rejectControl, &rootHost);
        rootHost.setGeometry(0, 0, 240, 180); peerHost.setGeometry(0, 0, 240, 180);
        const auto rootWindow = reinterpret_cast<HWND>(root.winId());
        const auto peerWindow = reinterpret_cast<HWND>(peer.winId());
        const auto rootChild = reinterpret_cast<HWND>(rootHost.winId());
        const auto peerChild = reinterpret_cast<HWND>(peerHost.winId());
        ShowWindow(rootWindow, SW_SHOWNOACTIVATE); ShowWindow(peerWindow, SW_SHOWNOACTIVATE);
        ShowWindow(rootChild, SW_SHOWNOACTIVATE); ShowWindow(peerChild, SW_SHOWNOACTIVATE);
        const auto rootController = Microsoft::WRL::Make<SessionLayoutController>();
        const auto peerController = Microsoft::WRL::Make<SessionLayoutController>();
        rootController->onMoveFocus = [=] { SetActiveWindow(rootWindow); SetFocus(rootChild); };
        peerController->onMoveFocus = [=] { SetActiveWindow(peerWindow); SetFocus(peerChild); };
        rootHost.state_->controller = rootController;
        peerHost.state_->controller = peerController;
        const auto activate = [](HWND window, HWND focus) { SetActiveWindow(window); SetFocus(focus); };
        const auto notify = [](WebViewSessionHost& host, Qt::FocusReason reason) {
            QFocusEvent event(QEvent::FocusIn, reason);
            host.focusInEvent(&event);
        };

        activate(peerWindow, peerChild);
        const bool peerNativeReady = GetActiveWindow() == peerWindow && GetFocus() == peerChild;
        check(peerNativeReady, "focus setup: real native peer is active/focused");
        if (!peerNativeReady) return; // An invalid environment is not a semantic RED.
        auto before = rootController->focusMoves;
        notify(rootHost, Qt::ActiveWindowFocusReason);
        check(rootController->focusMoves == before, "stale root activation does not forward MoveFocus");
        check(GetActiveWindow() == peerWindow && GetFocus() == peerChild,
            "stale root notification preserves native peer activation/focus");

        activate(rootWindow, rootChild);
        before = peerController->focusMoves;
        notify(peerHost, Qt::ActiveWindowFocusReason);
        check(peerController->focusMoves == before, "inverse stale peer activation does not forward MoveFocus");
        check(GetActiveWindow() == rootWindow && GetFocus() == rootChild,
            "inverse stale peer notification preserves native root activation/focus");

        activate(rootWindow, rootWindow);
        before = rootController->focusMoves;
        notify(rootHost, Qt::ActiveWindowFocusReason);
        check(rootController->focusMoves == before + 1 && GetFocus() == rootChild,
            "genuine root activation still hands focus to WebView controller");
        activate(peerWindow, peerWindow);
        before = peerController->focusMoves;
        notify(peerHost, Qt::ActiveWindowFocusReason);
        check(peerController->focusMoves == before + 1 && GetFocus() == peerChild,
            "genuine peer activation still hands focus to WebView controller");

        activate(rootWindow, nullptr);
        check(GetActiveWindow() == rootWindow && GetFocus() == nullptr,
            "null-focus setup preserves actual native root activation");
        before = rootController->focusMoves;
        notify(rootHost, Qt::ActiveWindowFocusReason);
        check(rootController->focusMoves == before + 1 && GetFocus() == rootChild,
            "genuine activation with temporarily null focus is not keyboard-dead");

        const auto child = CreateWindowExW(0, L"STATIC", L"C4 owned native child", WS_CHILD | WS_VISIBLE,
            0, 0, 20, 20, rootChild, nullptr, GetModuleHandleW(nullptr), nullptr);
        check(child != nullptr, "native child setup succeeds");
        if (child)
        {
            activate(rootWindow, child);
            check(GetFocus() == child && GetAncestor(child, GA_ROOT) == rootWindow,
                "native child belongs to the actual Host top-level");
            before = rootController->focusMoves;
            notify(rootHost, Qt::ActiveWindowFocusReason);
            check(rootController->focusMoves == before + 1,
                "legitimate same-root native child does not suppress activation handoff");
            DestroyWindow(child);
        }

        const auto popup = CreateWindowExW(0, L"STATIC", L"C4 owned native popup", WS_POPUP,
            0, 0, 100, 60, rootWindow, nullptr, GetModuleHandleW(nullptr), nullptr);
        check(popup != nullptr, "owned native popup setup succeeds");
        if (popup)
        {
            ShowWindow(popup, SW_SHOWNOACTIVATE); activate(popup, popup);
            check(GetActiveWindow() == popup && GetFocus() == popup
                && GetAncestor(popup, GA_ROOT) == popup && GetWindow(popup, GW_OWNER) == rootWindow,
                "owned popup has separate native activation authority despite root owner");
            before = rootController->focusMoves;
            notify(rootHost, Qt::ActiveWindowFocusReason);
            check(rootController->focusMoves == before && GetFocus() == popup,
                "stale root activation does not steal focus from an owned popup");
            DestroyWindow(popup);
        }

        for (const auto reason : {Qt::MouseFocusReason, Qt::TabFocusReason, Qt::BacktabFocusReason, Qt::OtherFocusReason})
        {
            activate(peerWindow, peerChild);
            before = rootController->focusMoves;
            notify(rootHost, reason);
            check(rootController->focusMoves == before + 1 && GetFocus() == rootChild
                && rootController->lastFocusReason == COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC,
                "intentional Mouse/Tab/Backtab/Other focus remains forwarded");
        }
        activate(rootWindow, rootChild);
        before = peerController->focusMoves;
        notify(peerHost, Qt::OtherFocusReason);
        check(peerController->focusMoves == before + 1 && GetFocus() == peerChild,
            "explicit peer reuse/programmatic focus can intentionally move from root");

        rootHost.closeHost();
        activate(peerWindow, peerChild);
        before = rootController->focusMoves;
        notify(rootHost, Qt::ActiveWindowFocusReason);
        check(rootController->focusMoves == before && GetFocus() == peerChild,
            "retired Host forwards no late activation handoff");
        peerHost.closeHost();
    }
    static int focusHandoff()
    {
        int checks = 0, failures = 0;
        verifyFocusHandoff([&](bool condition, const char* label) {
            ++checks;
            std::cout << (condition ? "PASS " : "FAIL ") << label << '\n';
            if (!condition) ++failures;
        });
        std::cout << "Focus handoff focused: " << checks - failures << '/' << checks << " PASS\n";
        return failures ? 1 : 0;
    }
    // Reference-ownership boundary only; real controller recreation is covered
    // by the actual COM fixture. No browser/runtime behavior is simulated here.
    class SharedEnvironment final : public Microsoft::WRL::RuntimeClass<
        Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, Microsoft::WRL::ChainInterfaces<
            ICoreWebView2Environment3, ICoreWebView2Environment2, ICoreWebView2Environment>>
    {
    public:
        bool supportsComposition = false;
        int windowedCreations = 0;
        int compositionCreations = 0;
        HWND compositionParent = nullptr;
        ComPtr<ICoreWebView2CreateCoreWebView2CompositionControllerCompletedHandler> completion;

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override
        {
            if (!supportsComposition && iid == __uuidof(ICoreWebView2Environment3))
            {
                if (!value) return E_POINTER;
                *value = nullptr;
                return E_NOINTERFACE;
            }
            return RuntimeClass::QueryInterface(iid, value);
        }
        HRESULT STDMETHODCALLTYPE CreateCoreWebView2Controller(HWND, ICoreWebView2CreateCoreWebView2ControllerCompletedHandler*) override
        {
            ++windowedCreations;
            return E_NOTIMPL;
        }
        HRESULT STDMETHODCALLTYPE CreateWebResourceResponse(IStream*, int, LPCWSTR, LPCWSTR, ICoreWebView2WebResourceResponse**) override { return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE get_BrowserVersionString(LPWSTR*) override { return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE add_NewBrowserVersionAvailable(ICoreWebView2NewBrowserVersionAvailableEventHandler*, EventRegistrationToken*) override { return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE remove_NewBrowserVersionAvailable(EventRegistrationToken) override { return S_OK; }
        HRESULT STDMETHODCALLTYPE CreateWebResourceRequest(LPCWSTR, LPCWSTR, IStream*, LPCWSTR, ICoreWebView2WebResourceRequest**) override { return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE CreateCoreWebView2PointerInfo(ICoreWebView2PointerInfo**) override { return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE CreateCoreWebView2CompositionController(HWND parent,
            ICoreWebView2CreateCoreWebView2CompositionControllerCompletedHandler* callback) override
        {
            ++compositionCreations;
            compositionParent = parent;
            completion = callback;
            return S_OK;
        }
        HRESULT complete(ICoreWebView2CompositionController* result)
        {
            const auto callback = std::exchange(completion, {});
            return callback ? callback->Invoke(S_OK, result) : E_UNEXPECTED;
        }
    };
    template<class Check>
    static void verifyFailedEnvironment(Check check)
    {
        QWidget root;
        const auto session = QUuid::createUuid();
        const auto environment = Microsoft::WRL::Make<SharedEnvironment>();
        WebViewSessionHost old(session, &root, {}, {}, rejectControl);
        old.state_->environment = environment;
        old.state_->fail(QStringLiteral("owned-test-presentation-failure"));
        check(old.state_->failed && !old.state_->active() && old.state_->environment,
            "failed presentation retains its live Session environment");
        WebViewSessionHost next(session, &root, {}, {}, rejectControl, &old);
        check(next.state_->environment.Get() == environment.Get() && next.sharesEnvironmentWith(old),
            "recreated Host borrows exact environment from a failed old presentation");
        old.closeHost();
        check(!old.state_->environment && next.state_->environment.Get() == environment.Get(),
            "borrowed reference survives old Host retirement independently");
        WebViewSessionHost afterClosed(session, &root, {}, {}, rejectControl, &old);
        check(!afterClosed.state_->environment && !afterClosed.state_->borrowedEnvironment,
            "closed Host grants no stale environment reference");
        next.closeHost(); afterClosed.closeHost();

        WebViewSessionHost mainRoute(session, &root, {}, {}, rejectControl);
        mainRoute.state_->environment = environment;
        mainRoute.state_->createController();
        check(mainRoute.state_->failed && environment->windowedCreations == 0
            && mainRoute.presentationFacts().value("actual") == QJsonValue{"unavailable"},
            "Main requires the composition environment and never attempts a windowed controller");
        WebViewSessionHost peerRoute(session, &root, {}, {}, rejectControl, &mainRoute);
        peerRoute.state_->createController();
        check(peerRoute.state_->failed && environment->windowedCreations == 0 && environment->compositionCreations == 0,
            "opaque BGM fails closed without Environment3, with no windowed fallback");
        peerRoute.closeHost();

        environment->supportsComposition = true;
        auto pendingPeer = std::make_unique<WebViewSessionHost>(session, &root,
            WebViewSessionHost::BgmPresentation{}, WebViewSessionHost::RuntimePresentation{}, rejectControl, &mainRoute);
        pendingPeer->state_->createController();
        check(pendingPeer->sharesEnvironmentWith(mainRoute) && environment->compositionCreations == 1
            && environment->compositionParent == reinterpret_cast<HWND>(pendingPeer->internalWinId())
            && environment->windowedCreations == 0 && !pendingPeer->state_->controller,
            "BGM requests one composition controller from the exact borrowed environment and uses its own HWND");
        pendingPeer.reset();
        const auto lateController = Microsoft::WRL::Make<SessionLayoutController>();
        ComPtr<SessionCompositionResult> lateResult;
        lateResult.Attach(new SessionCompositionResult(lateController.Get()));
        const auto lateStatus = environment->complete(lateResult.Get());
        QCoreApplication::sendPostedEvents(QCoreApplication::instance(), QEvent::MetaCall);
        check(SUCCEEDED(lateStatus) && lateController->closes == 1 && lateResult->rootVisualWrites.empty(),
            "late BGM composition callback closes once without attaching a visual");

        const SessionGuiApartment apartment;
        check(SUCCEEDED(apartment.status), "BGM visual lifetime fixture has a GUI STA");
        if (FAILED(apartment.status)) return;
        WebViewSessionHost failingPeer(session, &root, {}, {}, rejectControl, &mainRoute);
        failingPeer.state_->createController();
        const auto failingController = Microsoft::WRL::Make<SessionLayoutController>();
        ComPtr<SessionCompositionResult> failingResult;
        failingResult.Attach(new SessionCompositionResult(failingController.Get()));
        const auto failureStatus = environment->complete(failingResult.Get());
        QCoreApplication::sendPostedEvents(QCoreApplication::instance(), QEvent::MetaCall);
        check(SUCCEEDED(failureStatus) && environment->compositionCreations == 2 && environment->windowedCreations == 0
            && failingPeer.state_->failed && failingPeer.state_->environment.Get() == mainRoute.state_->environment.Get()
            && !failingPeer.state_->opaqueVisual && failingController->visibilityWrites > 0
            && failingResult->rootVisualWrites == std::vector<bool>{true, false},
            "BGM attaches an opaque composition visual and detaches it on input setup failure");
        failingPeer.closeHost();
        check(failingController->closes == 1, "failed BGM composition controller closes once on host retirement");
    }
    static int failedEnvironment()
    {
        int checks = 0, failures = 0;
        verifyFailedEnvironment([&](bool valid, const char* label) { ++checks; if (!valid) { ++failures; std::cerr << "FAIL: " << label << '\n'; } });
        std::cout << "Failed environment focused: " << checks-failures << '/' << checks << " PASS\n";
        return failures ? 1 : 0;
    }

    template<class Check>
    static void verifyTopLevelLayoutNotification(Check check)
    {
        QWidget root;
        WebViewSessionHost host(QUuid::createUuid(), &root, {}, {}, rejectControl);
        const auto child = reinterpret_cast<HWND>(host.winId());
        const auto logical = host.geometry();
        RECT native{};
        const bool nativeClient = GetClientRect(child, &native) != FALSE;
        const auto controller = Microsoft::WRL::Make<SessionLayoutController>();
        std::vector<char> calls;
        controller->onBounds = [&] { calls.push_back('B'); };
        controller->onParentPosition = [&] { calls.push_back('P'); };
        host.state_->controller = controller;

        // A DPR transition need not alter any logical QWidget size. Exercise
        // the real root notification, with the controller recording new bounds
        // and parent position writes after the Qt notification settles.
        QEvent dpr(QEvent::DevicePixelRatioChange);
        QMoveEvent moved(root.pos(), root.pos());
        QResizeEvent resized(root.size(), root.size());
        QCoreApplication::sendEvent(&root, &dpr);
        QCoreApplication::sendEvent(&root, &moved);
        QCoreApplication::sendEvent(&root, &resized);
        check(nativeClient && calls.empty() && host.state_->layoutQueued && host.geometry() == logical,
            "root DPR/geometry notifications queue one relayout without a local logical change");
        QCoreApplication::sendPostedEvents(QCoreApplication::instance(), QEvent::MetaCall);
        RECT actual{};
        controller->get_Bounds(&actual);
        check(!host.state_->layoutQueued && calls == std::vector<char>{'B', 'P'} && EqualRect(&native, &actual),
            "coalesced root notifications write raw child bounds before notifying WebView parent position");

        QCoreApplication::sendEvent(&root, &dpr);
        host.closeHost();
        QCoreApplication::sendPostedEvents(QCoreApplication::instance(), QEvent::MetaCall);
        check(calls == std::vector<char>{'B', 'P'} && controller->closes == 1,
            "queued root notification cannot touch a retired WebView controller");
    }
    static int lastReady()
    {
        int checks = 0, failures = 0;
        const auto check = [&](bool condition, const char* label) {
            ++checks;
            std::cout << (condition ? "PASS " : "FAIL ") << label << '\n';
            if (!condition) ++failures;
        };
        auto item = QUuid::createUuid();
        QString binding = QStringLiteral("source-A");
        bool valid = true;
        QWidget root;
        int hides = 0, cancels = 0;
        std::function<void(TimelineHoverResult)> completion;
        WebViewSessionHost::RuntimePresentation runtime;
        runtime.sample = [&] { return QJsonObject{{"bindingToken", binding}, {"hasMedia", valid},
            {"canSeek", true}, {"duration", 10}, {"active", QJsonObject{{"itemId", item.toString(QUuid::WithoutBraces)}}}}; };
        runtime.hover = [&](const TimelineHoverRequest&, std::function<void(TimelineHoverResult)> callback) { completion = std::move(callback); };
        runtime.cancelHover = [&] { ++cancels; };
        runtime.hideHover = [&] { ++hides; };
        WebViewSessionHost host(item, &root, {}, std::move(runtime), rejectControl);
        const auto state = host.state_;
        state->page.prepare(); state->ready = true;
        SessionBridge::Request request{"last-ready", state->page.generation(), item.toString(QUuid::WithoutBraces), "RequestTimelineHover"};
        const auto target = [&](quint32 serial, double time) {
            TimelineHoverRequest value{binding, item, serial, time};
            state->registerHoverTarget(value, QString::number(time));
            TimelineHoverPresentation::Intent view;
            view.bindingToken = binding; view.itemId = item; view.serial = serial;
            view.time = time; view.timeLabel = QString::number(time);
            state->hoverView = view;
            NativeTimelineHoverCard geometry;
            geometry.screenRect = {100, 100, 216, 153}; geometry.imageRect = {8, 8, 200, 113};
            geometry.timeRect = {8, 121, 200, 24}; state->hoverCardGeometry = geometry;
            request.hover = value;
        };
        const auto submit = [&] { return state->hoverReply(request)["payload"].toObject()["status"].toString(); };
        // No HWND/foreground substitute: inspect the exact card consumed by
        // renderHover after its existing lifecycle/foreground authority checks.
        const auto card = [&] { return state->hoverCardForView(); };
        target(1, 1);
        check(card().image.isNull() && submit() == "pending", "first request is Loading with no valid ready frame");
        int freedA = 0, freedC = 0;
        struct Storage { uchar bytes[8*8*4]{}; int* freed; };
        const auto image = [](int& freed, QColor color) {
            auto* storage = new Storage; storage->freed = &freed;
            QImage value(storage->bytes, 8, 8, 8*4, QImage::Format_RGB32, [](void* p) {
                auto* s = static_cast<Storage*>(p); ++*s->freed; delete s;
            }, storage);
            value.fill(color); return value;
        };
        completion({"ready", binding, item, 1, 1, image(freedA, Qt::red)});
        const auto readyKey = state->hoverFrame.cacheKey();
        check(card().image.cacheKey() == readyKey && !card().image.isNull(), "A ready is the actual paint card image");
        // Presentation arrives before the existing frontend decode timer.
        target(2, 2);
        check(card().image.cacheKey() == readyKey && card().timestamp == "2", "B pointer movement retains A before provider submission; footer follows pointer");
        (void)submit();
        const auto completeB = completion;
        check(!state->hoverFrame.isNull() && state->hoverFrame.cacheKey() == readyKey,
            "ready A survives same-source B pending");
        target(3, 3); (void)submit();
        const auto completeC = completion;
        check(card().image.cacheKey() == readyKey && freedA == 0, "C pending also retains one A frame");
        completeB({"ready", binding, item, 2, 2, image(freedC, Qt::blue)});
        check(card().image.cacheKey() == readyKey && freedC == 1, "late B is rejected and its image released without replacing A");
        completeC({"ready", binding, item, 3, 3, image(freedC, Qt::green)});
        check(card().image.pixelColor(0,0) == QColor(Qt::green) && freedA == 1 && freedC == 1,
            "current C replaces A and releases old A exactly once");
        target(4, 4); (void)submit();
        completion({"unavailable", binding, item, 4, 4, {}});
        check(card().image.isNull() && card().statusText.contains("unavailable") && freedC == 2,
            "terminal unavailable clears retained image and paints truthful failure");
        target(5, 5); (void)submit();
        completion({"ready", binding, item, 5, 5, QImage{8,8,QImage::Format_RGB32}});
        binding = "source-B"; // Same Item but renewed native source/binding authority.
        target(6, 6);
        check(card().image.isNull() && !state->hoverReadyTarget, "source/binding change clears old frame before new decode");
        (void)submit(); completion({"ready", binding, item, 6, 6, QImage{8,8,QImage::Format_RGB32}});
        item = QUuid::createUuid(); target(7, 7);
        check(card().image.isNull(), "different Item cannot inherit previous source frame");
        (void)submit(); completion({"ready", binding, item, 7, 7, QImage{8,8,QImage::Format_RGB32}});
        state->runtime.hover = [&](const TimelineHoverRequest& r, std::function<void(TimelineHoverResult)> done) {
            done({"ready", r.bindingToken, r.itemId, r.serial, r.time, image(freedC, Qt::blue)});
        };
        target(8, 8);
        check(submit() == "pending" && card().image.pixelColor(0,0) == QColor(Qt::blue)
            && state->hoverReadyTarget->serial == 8, "inline cache-hit completion replaces immediately; no added timer");
        SessionBridge::Request leave{"last-ready-leave", state->page.generation(), item.toString(QUuid::WithoutBraces), "CancelTimelineHover"};
        leave.cancelHoverSerial = 9;
        (void)state->hoverReply(leave);
        check(state->hoverFrame.isNull() && !state->hoverReadyTarget && !state->hoverView && !state->hoverCardGeometry && hides > 0,
            "control: pointer leave clears frame and popup view");
        check(freedA == 1 && freedC == 3 && cancels == 9, "one retained frame bound; replacements/leave release image resources; cancellations preserved");
        state->runtime.hover = runtime.hover;
        target(10, 1); (void)submit();
        const auto afterHide = completion;
        state->invalidateHoverView();
        afterHide({"ready", binding, item, 10, 1, QImage{8,8,QImage::Format_RGB32}});
        check(state->hoverFrame.isNull() && !state->hoverReadyTarget, "explicit popup hide invalidates retained image");
        target(11, 2); (void)submit();
        completion({"ready", binding, item, 11, 2, QImage{8,8,QImage::Format_RGB32}});
        valid = false;
        target(12, 3);
        check(submit() == "stale" && state->hoverFrame.isNull() && !state->hoverReadyTarget,
            "invalid native source clears/fences retained image; no misleading stale media");
        state->retireRuntimePage();
        check(state->hoverFrame.isNull() && !state->hoverReadyTarget && !state->hoverView, "page retirement clears last-ready state");
        valid = true; state->page.prepare(); state->ready = true;
        request.generation = state->page.generation();
        target(1, 1); (void)submit();
        completion({"ready", binding, item, 1, 1, QImage{8,8,QImage::Format_RGB32}});
        check(!state->hoverFrame.isNull(), "fresh page can adopt its own new ready image");
        host.closeHost();
        check(state->hoverFrame.isNull() && !state->hoverReadyTarget && !state->hoverView && state->closed,
            "ordinary Host close releases last-ready; no popup resurrection");
        std::cout << "Last-ready focused: " << checks - failures << '/' << checks << " PASS\n";
        return failures ? 1 : 0;
    }

    template<class Check>
    static void verifyNotices(Check check)
    {
        const auto incarnation = QUuid::createUuid();
        using Host = WebViewSessionHost;
        Host host(incarnation, nullptr, Host::PreviewPresentation{}, Host::RuntimePresentation{}, rejectControl);
        QCoreApplication::removePostedEvents(&host, QEvent::MetaCall);
        const auto state = host.state_;
        state->page.prepare();
        state->ready = true;
        state->productReady = true;
        const auto front = [&] { return state->discloseReadySnapshot({}).value("sessionNotice").toObject(); };
        int completed = 0;
        host.showSessionNotice(SessionNotice::Warning, "Desktop Background", "The live mode was kept.");
        const auto background = front();
        host.showSessionNotice(SessionNotice::Warning, "Desktop Background", "The live mode was kept.");
        host.showSessionNotice(SessionNotice::Error, "Save Project Failed", "The current document was kept.", [&] { ++completed; });
        check(host.hasSessionNotice() && state->notices.size() == 2 && front() == background,
            "mandatory product notices are retained, deduplicated and exposed in order");
        SessionBridge::Request acknowledgement{"notice", state->page.generation(),
            incarnation.toString(QUuid::WithoutBraces), SessionBridge::AcknowledgeSessionNoticeCommand};
        acknowledgement.acknowledgeSessionNotice = QUuid{background.value("id").toString()};
        const auto previousPage = state->page.document();
        state->retireRuntimePage();
        state->page.prepare();
        check(state->acknowledgeSessionNotice(acknowledgement, previousPage).value("ok") == false && front() == background,
            "late notice acknowledgement cannot dismiss the pending notice on a new page");
        acknowledgement.generation = state->page.generation();
        const auto currentPage = state->page.document();
        check(state->acknowledgeSessionNotice(acknowledgement, currentPage).value("ok") == true
            && front().value("title") == "Save Project Failed" && completed == 0,
            "current confirmation advances to the next native notice");
        check(state->acknowledgeSessionNotice(acknowledgement, currentPage).value("ok") == false && host.hasSessionNotice() && completed == 0,
            "duplicate confirmation has no authority over the next notice");
        acknowledgement.acknowledgeSessionNotice = QUuid{front().value("id").toString()};
        check(state->acknowledgeSessionNotice(acknowledgement, currentPage).value("ok") == true
            && !host.hasSessionNotice() && completed == 1, "explicit acknowledgement clears the notice and completes the tray feedback once");
        host.showSessionNotice(SessionNotice::Error, "Save Project Failed", "A second result.", [&] { ++completed; });
        host.closeHost();
        host.closeHost();
        check(!host.hasSessionNotice() && completed == 2, "terminal presentation retirement releases modal gating and settles pending tray feedback once");
    }

    template<class Check>
    static void verifyAsyncControl(Check check)
    {
        const auto incarnation = QUuid::createUuid();
        const auto documentIncarnation = QUuid::createUuid();
        const auto nativeToken = QUuid::createUuid();
        using Host = WebViewSessionHost;
        Host::ControlCompletion completion;
        std::shared_ptr<const std::atomic_bool> admission;
        int submissions = 0;
        int deliveries = 0;
        QJsonObject last;
        Host::ControlSubmit control = [&](SessionBridge::Request request, Host::ControlCompletion callback) {
            ++submissions;
            if (request.documentAction)
                check(request.audioDocument == documentIncarnation && request.documentAction == DocumentAction::New,
                    "control receives the same typed document command and expected document incarnation");
            admission = request.pageAdmission;
            check(admission && admission->load(std::memory_order_acquire),
                "control receives current native page admission");
            completion = std::move(callback);
            return true;
        };
        auto host = std::make_unique<Host>(incarnation, nullptr, Host::PreviewPresentation{},
            Host::RuntimePresentation{}, std::move(control));
        auto state = host->state_;
        state->page.prepare();
        state->ready = true;
        const auto deliver = [&](QJsonObject result) { ++deliveries; last = std::move(result); };
        SessionBridge::Request request{"cp3-async", state->page.generation(),
            incarnation.toString(QUuid::WithoutBraces), "NewDocument"};
        request.documentAction = DocumentAction::New;
        request.audioDocument = documentIncarnation;
        check(!state->isBgmPeer() && state->controlSubmit
            && state->page.document().endsWith("&presentation=model-c-production"),
            "the Main surface uses the modern page fence and one control port");
        check(state->dispatchControl(request, state->page.document(), deliver) && submissions == 1 && deliveries == 0,
            "async document intent submits once and does not return a synchronous reply");
        completion(DocumentCommandResult{DocumentCommandStatus::Accepted, nativeToken});
        completion(Host::ControlError{QStringLiteral("duplicate")});
        check(deliveries == 1 && last.value("requestId") == request.requestId && last.value("generation") == request.generation
            && last.value("payload").toObject().value("token") == nativeToken.toString(QUuid::WithoutBraces)
            && last.value("payload").toObject().value("status") == "accepted",
            "current typed result delivers exactly once in its original envelope");

        check(state->dispatchControl(request, state->page.document(), deliver) && submissions == 2,
            "control submits a second independent operation");
        const auto retiredAdmission = admission;
        state->retireRuntimePage();
        state->page.prepare();
        completion(DocumentCommandResult{DocumentCommandStatus::Accepted, nativeToken});
        check(deliveries == 1 && state->ready && !state->failed && retiredAdmission && !retiredAdmission->load(std::memory_order_acquire),
            "reload retires control admission and delivery for the old page");
        check(state->dispatchControl(request, state->page.document(), deliver) && submissions == 2,
            "old generation cannot submit a new command");

        request.generation = state->page.generation();
        const auto deferred = state->controlSubmit;
        state->controlSubmit = [](SessionBridge::Request, Host::ControlCompletion) { return false; };
        check(state->dispatchControl(request, state->page.document(), deliver) && deliveries == 2
            && last.value("error") == "control-command-busy", "saturated control explicitly rejects before admission");
        state->controlSubmit = deferred;
        SessionBridge::Request sample{"cp3-read", request.generation, request.sessionIncarnation};
        check(state->dispatchControl(sample, state->page.document(), deliver) && submissions == 3 && deliveries == 2,
            "authoritative project snapshot is captured on demand through control");
        completion(Host::MainSnapshotResult{QJsonObject{{"host", QJsonObject{{"ready", false}, {"sessionRoleReady", false}}},
            {"nativeFact", "retained"}}});
        const auto payload = last.value("payload").toObject();
        check(deliveries == 3 && payload.value("nativeFact") == "retained"
            && payload.value("host").toObject().value("ready") == true
            && payload.value("host").toObject().value("sessionRoleReady") == false,
            "snapshot adds page readiness without replacing native role facts");

        check(state->dispatchControl(request, state->page.document(), deliver) && submissions == 4 && admission != retiredAdmission,
            "fresh page submits with its own admission");
        const std::weak_ptr<Host::State> lifetime = state;
        host.reset();
        state.reset();
        completion(DocumentCommandResult{DocumentCommandStatus::Accepted, nativeToken});
        check(lifetime.expired() && deliveries == 3 && admission && !admission->load(std::memory_order_acquire),
            "destroyed Host retires admission and is not retained by a late result");

        Host::BgmPresentation bgm;
        bgm.permit = QUuid::createUuid();
        bgm.playlistId = PlaylistId{QUuid::createUuid()};
        bgm.header = [] { return QJsonObject{}; };
        bgm.close = [] {};
        Host::ControlSubmit peerControl = [&](SessionBridge::Request, Host::ControlCompletion callback) {
            ++submissions;
            completion = std::move(callback);
            return true;
        };
        Host peer(incarnation, nullptr, bgm, {}, std::move(peerControl), nullptr);
        const auto peerState = peer.state_;
        peerState->page.prepare();
        peerState->ready = true;
        SessionBridge::Request import{"cp3-peer", peerState->page.generation(), request.sessionIncarnation, "BgmAddTracks"};
        import.audioDocument = documentIncarnation;
        import.bgmAuthored = BgmAuthoredRequest{BgmAuthoredAction::AddTracks, QUuid::createUuid(), PlaylistBgmTarget{bgm.playlistId}};
        check(peerState->isBgmPeer() && peerState->dispatchControl(import, peerState->page.document(), deliver)
            && submissions == 4 && deliveries == 4 && last.value("error") == "wrong-bgm-peer-context",
            "async peer cannot submit an import under another peer's permit");
        SessionBridge::Request peerRead{"cp3-peer-read", import.generation, import.sessionIncarnation, "GetBgmEditorSnapshot"};
        peerRead.bgmEditorRead = true;
        check(peerState->dispatchControl(peerRead, peerState->page.document(), deliver) && submissions == 5 && deliveries == 4,
            "peer snapshot has an async-only control provider");
        const QJsonObject header{{"revision", "17"}};
        completion(Host::BgmSnapshotResult{QJsonObject{{"peerFact", "retained"}}, header});
        check(deliveries == 5 && last.value("payload").toObject().value("peerFact") == "retained" && peerState->bgmHeader == header,
            "peer hydration delivers native data and records its detached header");
        SessionBridge::Request runtimeRead{"cp3-bgm-state", import.generation, import.sessionIncarnation, "GetBgmRuntimeState"};
        const BgmTarget target = PlaylistBgmTarget{bgm.playlistId};
        runtimeRead.bgmRuntime = BgmRuntimeRequest{bgm.permit, target, std::nullopt};
        check(peerState->dispatchControl(runtimeRead, peerState->page.document(), deliver) && submissions == 6 && deliveries == 5,
            "BGM runtime hydration goes through control");
        BgmRuntimeState runtimeState;
        runtimeState.target = target;
        completion(BgmRuntimeResult{BgmRuntimeCommandStatus::Ready, BgmRuntimeRejectReason::None, runtimeState});
        check(deliveries == 6 && peerState->bgmBindings[0] && peerState->bgmBindings[0]->permit == bgm.permit
            && peerState->bgmBindings[0]->last.target == target, "BGM runtime hydration binds only its exact peer and scope");

        import.bgmAuthored->permit = bgm.permit;
        check(peerState->dispatchControl(import, peerState->page.document(), deliver) && submissions == 7 && deliveries == 6,
            "same peer may submit its own import with no synchronous result");
        peer.closeHost();
        completion(Host::ControlError{QStringLiteral("closed-peer")});
        check(deliveries == 6, "retired peer drops the transport result without replaying the control operation");
    }

    static int asyncControlOnly()
    {
        int checks = 0;
        int failures = 0;
        verifyAsyncControl([&](bool condition, const char* label) {
            ++checks;
            if (!condition) { ++failures; std::cerr << "FAIL: " << label << '\n'; }
        });
        std::cout << "CP3 async control bridge: " << checks - failures << '/' << checks << " PASS\n";
        return failures ? 1 : 0;
    }

    template<class Check>
    static void verifyProductReadiness(Check check)
    {
        using Stage = ProductPresentationStage;
        std::optional<Stage> stage;
        int stageUpdates = 0;
        QString failureDetail;
        QWidget shell;
        WebViewSessionHost host(QUuid::createUuid(), &shell, {}, {}, rejectControl);
        const auto state = host.state_;
        host.setProductPresentationObserver([&](Stage next, const QString& detail) {
            stage = next;
            ++stageUpdates;
            failureDetail = detail;
        });
        const auto receipt = [](const QString& generation, const QString& marker) {
            return QJsonDocument(QJsonObject{{"kind", "shell-presentation"}, {"version", 1},
                {"generation", generation}, {"state", marker}}).toJson(QJsonDocument::Compact);
        };
        state->page.prepare();
        const auto firstPage = state->page.document();
        const auto firstGeneration = state->page.generation();
        check(state->page.navigationStarting(firstPage) == SessionBridge::NavigationDisposition::Allow,
            "product test admits the current trusted generation");
        state->ready = true;
        state->beginProductNavigation();
        check(host.isWebViewReady() && !host.isProductPresentationReady() && stage == Stage::Navigating,
            "transport admission allows bootstrap without promising useful presentation");
        check(state->acceptProductMessage(receipt(firstGeneration, "ready"), firstPage)
            && !host.isProductPresentationReady() && stage == Stage::Navigating,
            "ready arriving before navigation completion cannot prematurely publish product readiness");
        state->completeProductNavigation();
        const auto readyUpdates = stageUpdates;
        check(host.isProductPresentationReady() && stage == Stage::Ready
            && state->acceptProductMessage(receipt(firstGeneration, "ready"), firstPage) && stageUpdates == readyUpdates,
            "navigation completion and matching early product receipt settle once");

        check(state->page.navigationStarting(firstPage) == SessionBridge::NavigationDisposition::RestartWithFreshGeneration,
            "F5 requests a replacement product generation");
        state->beginProductNavigation();
        const auto nextPage = state->page.document();
        const auto nextGeneration = state->page.generation();
        check(state->page.navigationStarting(nextPage) == SessionBridge::NavigationDisposition::Allow,
            "replacement generation can navigate");
        state->completeProductNavigation();
        check(stage == Stage::AwaitingProduct && !host.isProductPresentationReady(),
            "replacement navigation completion is distinct from useful product presentation");
        const auto updates = stageUpdates;
        check(!state->acceptProductMessage(receipt(nextGeneration, "ready"), firstPage)
            && state->acceptProductMessage(receipt(firstGeneration, "ready"), nextPage)
            && stageUpdates == updates && !host.isProductPresentationReady(),
            "wrong source and stale generation cannot promote the replacement page");
        auto duplicate = receipt(nextGeneration, "ready");
        duplicate.replace("\"state\":", "\"state\":\"failed\",\"state\":");
        check(state->acceptProductMessage(duplicate, nextPage) && stageUpdates == updates,
            "duplicate receipt members cannot mutate the product shell");
        check(state->acceptProductMessage(receipt(nextGeneration, "failed"), nextPage)
            && stage == Stage::Failed && failureDetail == QStringLiteral("session-product-bootstrap-failed")
            && !host.isWebViewReady() && !host.isProductPresentationReady(),
            "current product failure retires admission and publishes truthful shell failure");

        host.closeHost();
        const auto retiredUpdates = stageUpdates;
        check(stage == Stage::Retired && !state->acceptProductMessage(receipt(nextGeneration, "ready"), nextPage)
            && stageUpdates == retiredUpdates && !host.isProductPresentationReady(),
            "late product receipt cannot revive a retired Host");
    }

    static int productReadinessOnly()
    {
        int checks = 0;
        int failures = 0;
        verifyProductReadiness([&](bool condition, const char* label) {
            ++checks;
            std::cout << (condition ? "PASS " : "FAIL ") << label << '\n';
            if (!condition) ++failures;
        });
        std::cout << "CP4 product readiness: " << checks - failures << '/' << checks << " PASS\n";
        return failures ? 1 : 0;
    }

    static int run()
    {
        int checks = 0;
        int failures = 0;
        const auto check = [&](bool condition, const char* label) {
            ++checks;
            if (!condition) { ++failures; std::cerr << "FAIL: " << label << '\n'; }
        };
        verifyFailedEnvironment(check);
        verifyTopLevelLayoutNotification(check);
        verifyFocusHandoff(check);
        verifyAsyncControl(check);
        verifyNotices(check);
        verifyProductReadiness(check);
        verifySessionHide(check);
        using State = WebViewSessionHost::State;
        const auto incarnation = QUuid::createUuid();
        const SessionBridge::Request request{"size-check", "size-generation", incarnation.toString(QUuid::WithoutBraces)};
        const auto normal = SessionBridge::result(request, QJsonObject{{"nativeFact", "retained"}});
        check(QJsonDocument::fromJson(State::encodeResponse(normal, request.requestId, request.generation, incarnation)).object() == normal,
            "bounded response preserves normal authoritative payload");
        const auto oversized = SessionBridge::result(request,
            QJsonObject{{"source", QString(SessionBridge::MaximumResponseBytes + 1, QLatin1Char('x'))}});
        QString sizeFailure;
        const auto bounded = State::encodeResponse(oversized, request.requestId, request.generation, incarnation, &sizeFailure);
        const auto rejection = QJsonDocument::fromJson(bounded).object();
        check(bounded.size() < SessionBridge::MaximumResponseBytes && rejection["ok"] == false
            && rejection["error"] == "snapshot-too-large" && rejection["requestId"] == request.requestId
            && rejection["generation"] == request.generation && rejection["sessionIncarnation"] == request.sessionIncarnation
            && !rejection.contains("payload") && sizeFailure == "snapshot-too-large",
            "oversized native snapshot has explicit correlated transport failure and presentation failure reason");
        QWidget nativeSessionRoot;
        const auto rootId = nativeSessionRoot.winId();

        {
            auto layoutHost = std::make_unique<WebViewSessionHost>(incarnation, &nativeSessionRoot,
                WebViewSessionHost::PreviewPresentation{}, WebViewSessionHost::RuntimePresentation{}, rejectControl);
            const auto layoutState = layoutHost->state_;
            const auto layoutController = Microsoft::WRL::Make<SessionLayoutController>();
            layoutState->controller = layoutController;
            const auto epoch = layoutState->geometryEpoch;
            layoutState->layout(true); // Move observation of current client state.
            layoutState->layout(true); // Resize observation of that same state.
            check(layoutState->geometryEpoch == epoch + 1,
                "one observed native layout invalidates once across duplicate Move/Resize callbacks");
            const auto beforeEpoch = layoutState->geometryEpoch;
            const auto layoutWindow = reinterpret_cast<HWND>(layoutHost->winId());
            RECT size{}; GetClientRect(layoutWindow, &size);
            SetWindowPos(layoutWindow, nullptr, 0, 0, size.right + 17, size.bottom + 9,
                SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            layoutState->layout(true); layoutState->layout(true);
            check(layoutState->geometryEpoch == beforeEpoch + 1,
                "a changed client size creates exactly one new geometry transition");
            const auto beforeMove = layoutState->geometryEpoch;
            SetWindowPos(layoutWindow, nullptr, 13, 19, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            layoutState->layout(true); layoutState->layout(true);
            check(layoutState->geometryEpoch == beforeMove + 1,
                "a host origin change inside the Session still revokes exactly once");
            const auto beforeHard = layoutState->geometryEpoch;
            layoutState->invalidatePreviewGeometry();
            check(layoutState->geometryEpoch == beforeHard + 1,
                "explicit hard invalidation is not deduplicated by unchanged native layout");
            const auto beforeRefresh = layoutState->geometryEpoch;
            layoutHost->refreshPreviewGeometry();
            check(layoutState->geometryEpoch == beforeRefresh + 1,
                "explicit owner refresh still requests current geometry for unchanged layout");
            const auto beforeReentry = layoutController->visibilityWrites;
            layoutController->onBounds = [raw = layoutState.get()] { raw->invalidatePreviewGeometry(); };
            layoutState->layout(true);
            check(layoutController->visibilityWrites == beforeReentry,
                "controller reentry invalidation prevents the outer layout from publishing stale visibility");
            layoutController->onBounds = [raw = layoutState.get()] { raw->close(); };
            layoutState->layout(true);
            check(layoutState->closed && !layoutState->controller && layoutController->visibilityWrites == beforeReentry,
                "controller reentry close retains the executing controller and stops later layout work");
        }

        // Construct real child Hosts, but do not pump their queued WebView start.
        // The repeated real controller path is covered by the runtime fixture.
        for (int cycle = 0; cycle < 8; ++cycle)
        {
            auto resource = std::make_shared<int>(cycle);
            const std::weak_ptr<int> observed = resource;
            auto host = std::make_unique<WebViewSessionHost>(incarnation, &nativeSessionRoot,
                WebViewSessionHost::PreviewPresentation{}, WebViewSessionHost::RuntimePresentation{},
                [resource](SessionBridge::Request, WebViewSessionHost::ControlCompletion) { return false; });
            const auto state = host->state_;
            state->page.prepare();
            check(state->page.navigationStarting(state->page.document()) == SessionBridge::NavigationDisposition::Allow,
                "test enters real current-page fence");
            state->ready = true;
            const auto document = state->page.document();
            const auto generation = state->page.generation();
            resource.reset();
            check(host->parentWidget() == &nativeSessionRoot && nativeSessionRoot.winId() == rootId
                && state->page.sessionIncarnation() == incarnation, "Host is child presentation under stable native role");
            check(state->mayDeliver(generation, document), "current open page can receive a snapshot");
            check(state->page.navigationStarting(document) == SessionBridge::NavigationDisposition::RestartWithFreshGeneration
                && !state->mayDeliver(generation, document), "result captured for old page fenced after reload");
            host->closeHost();
            check(observed.expired() && !state->controlSubmit && state->closed && !state->ready
                && state->page.closed() && !state->owner, "close releases the control port and revokes page admission");
            check(!state->mayDeliver(state->page.generation(), state->page.document()), "closed Host cannot deliver result/invalidation");
            host->closeHost();
            host.reset();
            check(nativeSessionRoot.winId() == rootId && !nativeSessionRoot.isVisible(), "Host retirement preserves native Session root");
        }

        // The executing control port must retain its capture across reentrant
        // retirement. The same retirement makes its result undeliverable.
        {
            using Host = WebViewSessionHost;
            auto host = std::make_unique<Host>(incarnation, &nativeSessionRoot,
                Host::PreviewPresentation{}, Host::RuntimePresentation{}, rejectControl);
            const auto state = host->state_;
            state->page.prepare();
            state->ready = true;
            auto capture = std::make_shared<int>(42);
            const std::weak_ptr<int> observed = capture;
            bool retainedDuringClose = false;
            int deliveries = 0;
            state->controlSubmit = [capture, raw = state.get(), observed, &retainedDuringClose]
                (SessionBridge::Request, Host::ControlCompletion complete) {
                const auto probe = observed;
                auto* const retained = &retainedDuringClose;
                raw->close();
                *retained = !probe.expired();
                complete(Host::MainSnapshotResult{QJsonObject{{"nativeFact", "retired"}}});
                return true;
            };
            capture.reset();
            SessionBridge::Request snapshot{"reentrant-submit", state->page.generation(),
                incarnation.toString(QUuid::WithoutBraces)};
            check(state->dispatchControl(snapshot, state->page.document(), [&](QJsonObject) { ++deliveries; })
                && retainedDuringClose && observed.expired() && deliveries == 0 && !state->controlSubmit,
                "executing control capture survives reentrant Close and loses delivery authority");
        }

        // Snapshot and Post errors belong to the captured page. A current
        // error retires presentation but cannot make a document decision.
        for (int mode = 0; mode < 5; ++mode)
        {
            using Host = WebViewSessionHost;
            int decisions = 0;
            auto faultHost = std::make_unique<Host>(incarnation, &nativeSessionRoot,
                Host::PreviewPresentation{}, Host::RuntimePresentation{},
                [&decisions](SessionBridge::Request request, Host::ControlCompletion) {
                    if (request.documentChoice) ++decisions;
                    return false;
                });
            const auto faultState = faultHost->state_;
            faultState->page.prepare();
            faultState->ready = true;
            const auto generation = faultState->page.generation(), document = faultState->page.document();
            faultState->finishResponseDelivery(generation, document, S_OK, {});
            check(faultState->active() && faultState->ready, "successful Post keeps current presentation available");
            if (mode == 0 || mode == 1)
            {
                if (mode == 0) faultState->page.prepare();
                else faultState->close();
                faultState->finishResponseDelivery(generation, document, S_OK, "snapshot-unavailable");
                faultState->finishResponseDelivery(generation, document, E_FAIL, {});
                check(!faultState->failed && (mode == 0 ? faultState->active() && faultState->ready : faultState->closed),
                    "stale snapshot or Post failure cannot fail a replacement page or resurrect a closed Host");
            }
            else
            {
                const auto reason = mode == 2 ? QStringLiteral("snapshot-unavailable")
                    : mode == 3 ? sizeFailure : QString{};
                faultState->finishResponseDelivery(generation, document, mode == 4 ? E_FAIL : S_OK, reason);
                check(faultState->failed && !faultState->ready && faultState->page.closed()
                    && !faultState->mayDeliver(generation, document) && faultState->owner == faultHost.get()
                    && decisions == 0 && faultState->statusLabel
                    && faultState->statusLabel->text().startsWith("Session presentation unavailable:"),
                    "current snapshot/size/Post failure retires presentation without resolving the document");
            }
            faultHost->closeHost();
            check(decisions == 0 && nativeSessionRoot.winId() == rootId,
                "presentation retirement preserves the native Session role");
        }

        // Actual B4 State keeps only a page-scoped projection subscription and
        // one replaceable hover delivery; native execution survives retirement.
        int hoverCancels = 0;
        QJsonObject runtimeSample{{"bindingToken", "binding-one"}, {"hasMedia", true}, {"canSeek", true}, {"duration", 1},
            {"active", QJsonObject{{"itemId", incarnation.toString(QUuid::WithoutBraces)}}}};
        std::function<void(TimelineHoverResult)> lateHover;
        WebViewSessionHost::RuntimePresentation runtimeCallbacks;
        runtimeCallbacks.sample = [&] { return runtimeSample; };
        runtimeCallbacks.cancelHover = [&] { ++hoverCancels; };
        runtimeCallbacks.hover = [&](const TimelineHoverRequest&, std::function<void(TimelineHoverResult)> callback) { lateHover = std::move(callback); };
        WebViewSessionHost::RuntimePresentation callbackCopy = runtimeCallbacks;
        auto runtimeHost = std::make_unique<WebViewSessionHost>(incarnation, &nativeSessionRoot,
            WebViewSessionHost::PreviewPresentation{}, callbackCopy,
            [&](SessionBridge::Request, WebViewSessionHost::ControlCompletion complete) {
                complete(WebViewSessionHost::RuntimeSampleResult{runtimeSample});
                return true;
            });
        const auto runtimeState = runtimeHost->state_;
        runtimeState->page.prepare(); runtimeState->ready = true;
        SessionBridge::Request runtimeRead{"b4-read", runtimeState->page.generation(), incarnation.toString(QUuid::WithoutBraces), "GetRuntimePlaybackSample"};
        QJsonObject readReply;
        const auto deliverSample = [&](QJsonObject reply) { readReply = std::move(reply); };
        (void)runtimeState->dispatchControl(runtimeRead, runtimeState->page.document(), deliverSample);
        check(runtimeState->runtimeSubscribed && readReply["payload"].toObject() == runtimeSample,
            "B4 read subscribes one current native sample without building editor snapshot");
        (void)runtimeState->dispatchControl(runtimeRead, runtimeState->page.document(), deliverSample);
        check(runtimeState->runtimeSubscribed, "repeated read is idempotent subscription, not another timer");
        SessionBridge::Request hoverRead{"b4-hover", runtimeState->page.generation(), incarnation.toString(QUuid::WithoutBraces), "RequestTimelineHover"};
        hoverRead.hover = TimelineHoverRequest{"binding-one", incarnation, 1, .25};
        check(runtimeState->hoverReply(hoverRead)["payload"].toObject()["status"] == "pending" && bool(lateHover),
            "native hover stores only current page-associated delivery");
        const auto initialEpoch = runtimeState->hoverEpoch;
        SessionBridge::Request hoverCancel{"b4-cancel", runtimeState->page.generation(), incarnation.toString(QUuid::WithoutBraces), "CancelTimelineHover"};
        hoverCancel.cancelHoverSerial = 2;
        check(runtimeState->hoverReply(hoverCancel)["payload"].toObject()["status"] == "cancelled"
            && runtimeState->hoverEpoch > initialEpoch && runtimeState->hoverFrame.isNull(), "hover cancel retires current native frame/view and late callback epoch");
        hoverRead.hover->serial = 1;
        check(runtimeState->hoverReply(hoverRead)["payload"].toObject()["status"] == "stale", "late hover request cannot replace newer cancellation");
        runtimeState->retireRuntimePage();
        check(!runtimeState->runtimeSubscribed && !runtimeState->runtimeCadence.isValid()
            && runtimeState->hoverSerial == 0 && runtimeState->hoverCardGeometry == std::nullopt, "page retirement revokes subscription/serial/view without touching Engine");
        lateHover(TimelineHoverResult{"ready", "binding-one", incarnation, 1, .25, QImage{}});
        check(runtimeState->hoverFrame.isNull(), "late hover result after page retirement cannot restore a frame or popup");
        runtimeHost->closeHost();
        lateHover(TimelineHoverResult{"ready", "binding-one", incarnation, 1, .25, QImage{}});
        check(hoverCancels >= 3 && runtimeState->hoverFrame.isNull()
            && !runtimeState->runtime.sample && !runtimeState->runtimeSubscribed,
            "callback after Host close cannot recreate frame, delivery or runtime subscription");
        runtimeHost.reset();

        auto hoverProviderHost = std::make_unique<WebViewSessionHost>(incarnation, &nativeSessionRoot,
            WebViewSessionHost::PreviewPresentation{}, callbackCopy, rejectControl);
        const auto providerState = hoverProviderHost->state_;
        providerState->page.prepare(); providerState->ready = true;
        providerState->runtime.sample = [raw = providerState.get(), &runtimeSample] { raw->close(); return runtimeSample; };
        SessionBridge::Request providerHover{"b4-reentrant-hover", providerState->page.generation(), incarnation.toString(QUuid::WithoutBraces), "RequestTimelineHover"};
        providerHover.hover = TimelineHoverRequest{"binding-one", incarnation, 1, .25};
        bool emptyHoverCallable = false;
        try { (void)providerState->hoverReply(providerHover); } catch (const std::bad_function_call&) { emptyHoverCallable = true; }
        check(!emptyHoverCallable && providerState->closed && providerState->hoverFrame.isNull(),
            "hover provider reentrant retirement cannot invoke a cleared decoder callback");
        hoverProviderHost.reset();

        // The provider may outlive its consumer. The completion itself must
        // retain neither QWidget nor State, and must not acquire a new page.
        for (int cycle = 0; cycle < 4; ++cycle)
        {
            std::function<void(TimelineHoverResult)> delayedCompletion;
            auto callbacks = callbackCopy;
            callbacks.hover = [&](const TimelineHoverRequest&, std::function<void(TimelineHoverResult)> completion) {
                delayedCompletion = std::move(completion);
            };
            auto deferredHost = std::make_unique<WebViewSessionHost>(incarnation, &nativeSessionRoot,
                WebViewSessionHost::PreviewPresentation{}, std::move(callbacks), rejectControl);
            deferredHost->state_->page.prepare();
            deferredHost->state_->ready = true;
            SessionBridge::Request deferredRequest{"b4-delayed", deferredHost->state_->page.generation(), incarnation.toString(QUuid::WithoutBraces), "RequestTimelineHover"};
            deferredRequest.hover = TimelineHoverRequest{"binding-one", incarnation, 1, .25};
            check(deferredHost->state_->hoverReply(deferredRequest)["payload"].toObject()["status"] == "pending"
                && bool(delayedCompletion), "asynchronous provider retains an independent delayed completion");
            const std::weak_ptr<State> expiredConsumer = deferredHost->state_;
            deferredHost.reset();
            check(expiredConsumer.expired(), "pending hover completion does not artificially retain destroyed Host State");
            QImage frame{8, 8, QImage::Format_RGB32}; frame.fill(Qt::red);
            delayedCompletion(TimelineHoverResult{"ready", "binding-one", incarnation, 1, .25, frame});
            check(expiredConsumer.expired() && nativeSessionRoot.winId() == rootId,
                "provider completion after full Host destruction is discarded without recreating presentation state");
        }

        std::function<void(TimelineHoverResult)> oldPageCompletion;
        auto pageCallbacks = callbackCopy;
        pageCallbacks.hover = [&](const TimelineHoverRequest&, std::function<void(TimelineHoverResult)> completion) {
            oldPageCompletion = std::move(completion);
        };
        auto replacementHost = std::make_unique<WebViewSessionHost>(incarnation, &nativeSessionRoot,
            WebViewSessionHost::PreviewPresentation{}, std::move(pageCallbacks), rejectControl);
        const auto replacementState = replacementHost->state_;
        replacementState->page.prepare(); replacementState->ready = true;
        SessionBridge::Request oldPageRequest{"b4-old-page", replacementState->page.generation(), incarnation.toString(QUuid::WithoutBraces), "RequestTimelineHover"};
        oldPageRequest.hover = TimelineHoverRequest{"binding-one", incarnation, 1, .25};
        (void)replacementState->hoverReply(oldPageRequest);
        replacementState->retireRuntimePage(); replacementState->page.prepare(); replacementState->ready = true;
        const auto replacementGeneration = replacementState->page.generation();
        replacementState->hoverFrame = QImage{8,8,QImage::Format_RGB32};
        replacementState->hoverFrame.fill(Qt::blue);
        replacementState->hoverFrameSerial = 77;
        const auto currentFrameKey = replacementState->hoverFrame.cacheKey();
        oldPageCompletion(TimelineHoverResult{"ready", "binding-one", incarnation, 1, .25, QImage{8, 8, QImage::Format_RGB32}});
        check(replacementGeneration != oldPageRequest.generation && replacementState->hoverFrame.cacheKey() == currentFrameKey
            && replacementState->hoverFrameSerial == 77, "old page completion cannot republish or overwrite replacement page frame");
        replacementHost.reset();

        // The native view uses the same weak page/epoch lease as the retired
        // PNG delivery. Geometry can supersede decode before its debounce fires.
        std::vector<std::function<void(TimelineHoverResult)>> frameCompletions;
        int nativeHides = 0, nativeShows = 0;
        auto nativeCallbacks = callbackCopy;
        nativeCallbacks.hover = [&](const TimelineHoverRequest&, std::function<void(TimelineHoverResult)> completion) {
            frameCompletions.push_back(std::move(completion));
        };
        nativeCallbacks.presentHover = [&](const NativeTimelineHoverCard&) { ++nativeShows; return QString{}; };
        nativeCallbacks.hideHover = [&] { ++nativeHides; };
        auto nativeHost = std::make_unique<WebViewSessionHost>(incarnation, &nativeSessionRoot,
            WebViewSessionHost::PreviewPresentation{}, std::move(nativeCallbacks), rejectControl);
        const auto nativeState = nativeHost->state_;
        nativeState->page.prepare(); nativeState->ready = true;
        SessionBridge::Request nativeRead{"native-hover", nativeState->page.generation(), incarnation.toString(QUuid::WithoutBraces), "RequestTimelineHover"};
        nativeRead.hover = TimelineHoverRequest{"binding-one", incarnation, 1, .25};
        check(nativeState->hoverReply(nativeRead)["payload"].toObject()["status"] == "pending", "native view uses existing decoder request admission");
        nativeState->hoverPresentationSerial = 2;
        QImage redFrame{8,8,QImage::Format_RGB32}; redFrame.fill(Qt::red);
        frameCompletions[0]({"ready", "binding-one", incarnation, 1, .25, redFrame});
        check(nativeState->hoverFrame.isNull() && nativeShows == 0,
            "newer geometry suppresses old decode even before the newer decode request exists");
        nativeRead.hover->serial = 2;
        check(nativeState->hoverReply(nativeRead)["payload"].toObject()["status"] == "pending", "geometry serial does not block the matching decode request");
        frameCompletions[1]({"ready", "wrong-binding", incarnation, 2, .25, redFrame});
        check(nativeState->hoverFrame.isNull(), "wrong binding result cannot adopt a native frame");
        frameCompletions[1]({"ready", "binding-one", incarnation, 2, .5, redFrame});
        check(nativeState->hoverFrame.isNull(), "wrong time result cannot adopt a native frame");
        frameCompletions[1]({"ready", "binding-one", incarnation, 2, .25, redFrame});
        check(!nativeState->hoverFrame.isNull() && nativeState->hoverFrameSerial == 2 && nativeShows == 0,
            "current frame adoption alone cannot show a popup without a current geometry permit");
        SessionBridge::Request nativePresent{"native-present", nativeState->page.generation(), incarnation.toString(QUuid::WithoutBraces), "PresentTimelineHover"};
        TimelineHoverPresentation::Intent view;
        view.bindingToken = "binding-one"; view.itemId = incarnation; view.serial = 1; view.time = .25;
        nativePresent.hoverPresentation = view;
        check(nativeState->hoverReply(nativePresent)["payload"].toObject()["status"] == "stale",
            "older presentation cannot replace newer request/geometry serial");
        nativePresent.hoverPresentation->serial = 2;
        check(nativeState->hoverReply(nativePresent)["payload"].toObject()["status"] == "unavailable"
            && !nativeState->hoverView && nativeShows == 0,
            "a hidden owner cannot store a future automatic popup restoration permit");
        nativePresent.hoverPresentation->time = .5;
        check(nativeState->hoverReply(nativePresent)["payload"].toObject()["status"] == "stale",
            "same serial with a different hover time cannot relabel an already decoded frame");
        nativePresent.hoverPresentation->time = .25;
        nativeState->registerHoverTarget(*nativeRead.hover, QStringLiteral("00:00"));
        nativePresent.hoverPresentation->timeLabel = QStringLiteral("00:01");
        check(nativeState->hoverReply(nativePresent)["payload"].toObject()["status"] == "stale",
            "same serial cannot change the formatted time label after presentation identity is registered");
        nativePresent.hoverPresentation->timeLabel = QStringLiteral("00:00");
        SessionBridge::Request nativeCancel{"native-cancel", nativeState->page.generation(), incarnation.toString(QUuid::WithoutBraces), "CancelTimelineHover"};
        nativeCancel.cancelHoverSerial = 2;
        check(nativeState->hoverReply(nativeCancel)["payload"].toObject()["status"] == "cancelled", "current native presentation can be explicitly retired");
        check(nativeState->hoverReply(nativePresent)["payload"].toObject()["status"] == "stale",
            "a cancelled serial cannot create another popup permit after the owner restores");
        nativeRead.hover->serial = 3;
        (void)nativeState->hoverReply(nativeRead);
        nativeState->retireRuntimePage(); nativeState->page.prepare(); nativeState->ready = true;
        frameCompletions[2]({"ready", "binding-one", incarnation, 3, .25, redFrame});
        check(nativeState->hoverFrame.isNull() && !nativeState->hoverView && nativeShows == 0 && nativeHides > 0,
            "old page completion cannot recreate native frame/permit or show after page replacement");
        // Simulate the lease registered after validated COM geometry; the real
        // controller conversion and HWND placement are proven by runtime tests.
        nativeState->hoverPresentationSerial = 10;
        nativeState->registerHoverTarget({"binding-one", incarnation, 10, .25}, QStringLiteral("00:00"));
        nativeRead.generation = nativeState->page.generation();
        nativeRead.hover = TimelineHoverRequest{"binding-one", incarnation, 10, .5};
        check(nativeState->hoverReply(nativeRead)["payload"].toObject()["status"] == "stale",
            "presentation-first same serial cannot admit a decode at a different time");
        nativeRead.hover->time = .25;
        check(nativeState->hoverReply(nativeRead)["payload"].toObject()["status"] == "pending",
            "presentation-first matching tuple admits its debounced decode");
        check(nativeState->consistentHoverTarget(*nativeRead.hover, QStringLiteral("00:00")),
            "same-serial same-target geometry-only updates retain the current request lease");
        nativeState->runtime.sample = [raw = nativeState.get(), &runtimeSample, incarnation] {
            raw->hoverPresentationSerial = 12;
            raw->registerHoverTarget({"binding-one", incarnation, 12, .25}, QStringLiteral("00:00"));
            return runtimeSample;
        };
        nativePresent.generation = nativeState->page.generation();
        nativePresent.hoverPresentation->serial = 11;
        check(nativeState->hoverReply(nativePresent)["payload"].toObject()["status"] == "stale"
            && nativeState->hoverPresentationSerial == 12 && nativeState->hoverTarget->serial == 12,
            "a sample-provider reentrant newer presentation cannot be overwritten or hidden by the older intent");
        nativeState->runtime.sample = [raw = nativeState.get(), &runtimeSample] {
            raw->invalidatePreviewGeometry();
            return runtimeSample;
        };
        nativePresent.hoverPresentation->serial = 13;
        check(nativeState->hoverReply(nativePresent)["payload"].toObject()["status"] == "stale" && !nativeState->hoverView,
            "resize or DPI invalidation during a sample read cannot commit the old presentation geometry");
        nativeHost.reset();

        // Reference-counted objects exercise real settle() and release ordering.
        // No claim is made that this IUnknown probe simulates a real controller;
        // actual controller Close/handler counts belong to runtime verification.
        for (int cycle = 0; cycle < 8; ++cycle)
        {
            int live = 0;
            int adoptions = 0;
            auto pendingState = std::make_shared<State>(incarnation);
            const std::weak_ptr<State> weak = pendingState;
            const auto pending = std::make_shared<SessionCreationResult<IUnknown>>();
            pending->callbackStatus = S_OK;
            pending->value.Attach(new SessionTrackedUnknown(live));
            const auto adopt = [&](State&, const ComPtr<IUnknown>&) { ++adoptions; };
            check(pending->operation.callbackArrived(), "environment callback accepted once");
            State::settle(weak, pending, adopt);
            check(live == 1 && adoptions == 0, "inline creation callback cannot adopt before factory return");
            pendingState->close();
            if (cycle % 2) pendingState.reset();
            pending->operation.factoryReturned(true);
            State::settle(weak, pending, adopt);
            check(live == 0 && adoptions == 0 && !pending->value, "closed/expired Host disposes late creation result");
            State::settle(weak, pending, adopt);
            check(live == 0 && adoptions == 0 && !pending->operation.callbackArrived(), "late creation settlement remains one-shot");

            auto lateState = std::make_shared<State>(incarnation);
            const std::weak_ptr<State> lateWeak = lateState;
            const auto late = std::make_shared<SessionCreationResult<IUnknown>>();
            late->operation.factoryReturned(true);
            State::settle(lateWeak, late, adopt);
            lateState->close();
            lateState.reset();
            late->callbackStatus = S_OK;
            late->value.Attach(new SessionTrackedUnknown(live));
            check(late->operation.callbackArrived(), "post-destruction callback is accepted for disposal");
            State::settle(lateWeak, late, adopt);
            check(live == 0 && adoptions == 0, "callback after destruction releases COM result");
        }
        for(bool inlineCallback:{false,true})
        {
            auto owner=std::make_unique<WebViewSessionHost>(incarnation, &nativeSessionRoot,
                WebViewSessionHost::PreviewPresentation{}, WebViewSessionHost::RuntimePresentation{}, rejectControl);
            auto current=owner->state_;std::weak_ptr<State> weak=current;
            auto result=std::make_shared<SessionCreationResult<ICoreWebView2CompositionController>>();
            const auto controller=Microsoft::WRL::Make<SessionLayoutController>();int adopted=0;
            const auto adopt=[&](State&,const ComPtr<ICoreWebView2CompositionController>&){++adopted;};
            if(inlineCallback){result->callbackStatus=S_OK;result->value.Attach(new SessionCompositionResult(controller.Get()));check(result->operation.callbackArrived(),"composition inline callback accepted");State::settle(weak,result,adopt);check(adopted==0,"composition callback waits for factory return");}
            else result->operation.factoryReturned(true);
            owner.reset();
            current.reset();
            if(inlineCallback)result->operation.factoryReturned(true);
            else {result->callbackStatus=S_OK;result->value.Attach(new SessionCompositionResult(controller.Get()));check(result->operation.callbackArrived(),"composition callback after closing accepted for disposal");}
            State::settle(weak,result,adopt);State::settle(weak,result,adopt);
            check(adopted==0&&controller->closes==1&&!result->value,"retired composition result is closed once without adoption");
        }
        std::cout << "Session Host lifetime: " << checks - failures << '/' << checks << " PASS\n";
        std::cout << "Child Host/page teardown repeats: 8; weak creation repeats: 8\n";
        return failures ? 1 : 0;
    }
};
}

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    if (application.arguments().contains(QStringLiteral("--focus-handoff-only")))
        return DesktopSceneEngine::Player::SessionHostLifetimeTestAccess::focusHandoff();
    if (application.arguments().contains(QStringLiteral("--failed-environment-only")))
        return DesktopSceneEngine::Player::SessionHostLifetimeTestAccess::failedEnvironment();
    if (application.arguments().contains(QStringLiteral("--hover-last-ready-only")))
        return DesktopSceneEngine::Player::SessionHostLifetimeTestAccess::lastReady();
    if (application.arguments().contains(QStringLiteral("--cp3-async-control-only")))
        return DesktopSceneEngine::Player::SessionHostLifetimeTestAccess::asyncControlOnly();
    if (application.arguments().contains(QStringLiteral("--cp4-product-readiness-only")))
        return DesktopSceneEngine::Player::SessionHostLifetimeTestAccess::productReadinessOnly();
    return DesktopSceneEngine::Player::SessionHostLifetimeTestAccess::run();
}
