// Real QWidget/HWND coordinate and dispatch tests with a recording COM input
// boundary. These do not replace the real production browser/input proof.
#include "../../Player/SessionCompositionInput.cpp"
#include <QApplication>
#include <iostream>
#include <vector>

namespace DesktopSceneEngine::Player
{
namespace
{
struct MouseRecord { COREWEBVIEW2_MOUSE_EVENT_KIND kind; COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS keys; UINT32 data; POINT point; };
class RecordingComposition final : public Microsoft::WRL::RuntimeClass<
    Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, ICoreWebView2CompositionController>
{
public:
    std::vector<MouseRecord> events;
    ComPtr<ICoreWebView2CursorChangedEventHandler> cursorHandler;
    int cursorReads = 0;
    HRESULT STDMETHODCALLTYPE get_RootVisualTarget(IUnknown** value) override { *value = nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_RootVisualTarget(IUnknown*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SendMouseInput(COREWEBVIEW2_MOUSE_EVENT_KIND kind,
        COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS keys, UINT32 data, POINT point) override
    { events.push_back({kind, keys, data, point}); return S_OK; }
    HRESULT STDMETHODCALLTYPE SendPointerInput(COREWEBVIEW2_POINTER_EVENT_KIND, ICoreWebView2PointerInfo*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE get_Cursor(HCURSOR* value) override { ++cursorReads; *value = LoadCursor(nullptr, IDC_ARROW); return S_OK; }
    HRESULT STDMETHODCALLTYPE get_SystemCursorId(UINT32* value) override { *value = 32512; return S_OK; }
    HRESULT STDMETHODCALLTYPE add_CursorChanged(ICoreWebView2CursorChangedEventHandler* value, EventRegistrationToken* token) override
    { cursorHandler = value; token->value = 1; return S_OK; }
    HRESULT STDMETHODCALLTYPE remove_CursorChanged(EventRegistrationToken) override { cursorHandler.Reset(); return S_OK; }
};
class RecordingController final : public Microsoft::WRL::RuntimeClass<
    Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, ICoreWebView2Controller>
{
public:
    int focusMoves = 0;
    ComPtr<ICoreWebView2MoveFocusRequestedEventHandler> focusHandler;
    HRESULT STDMETHODCALLTYPE get_IsVisible(BOOL* value) override { *value = TRUE; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_IsVisible(BOOL) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Bounds(RECT* value) override { *value = {}; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_Bounds(RECT) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE get_ZoomFactor(double* value) override { *value = 1; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_ZoomFactor(double) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE add_ZoomFactorChanged(ICoreWebView2ZoomFactorChangedEventHandler*, EventRegistrationToken*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE remove_ZoomFactorChanged(EventRegistrationToken) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetBoundsAndZoomFactor(RECT, double) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON) override { ++focusMoves; return S_OK; }
    HRESULT STDMETHODCALLTYPE add_MoveFocusRequested(ICoreWebView2MoveFocusRequestedEventHandler* value, EventRegistrationToken* token) override
    { focusHandler = value; token->value = 1; return S_OK; }
    HRESULT STDMETHODCALLTYPE remove_MoveFocusRequested(EventRegistrationToken) override { focusHandler.Reset(); return S_OK; }
    HRESULT STDMETHODCALLTYPE add_GotFocus(ICoreWebView2FocusChangedEventHandler*, EventRegistrationToken*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE remove_GotFocus(EventRegistrationToken) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE add_LostFocus(ICoreWebView2FocusChangedEventHandler*, EventRegistrationToken*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE remove_LostFocus(EventRegistrationToken) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE add_AcceleratorKeyPressed(ICoreWebView2AcceleratorKeyPressedEventHandler*, EventRegistrationToken*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE remove_AcceleratorKeyPressed(EventRegistrationToken) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE get_ParentWindow(HWND* value) override { *value = nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_ParentWindow(HWND) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE NotifyParentWindowPositionChanged() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Close() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE get_CoreWebView2(ICoreWebView2** value) override { *value = nullptr; return E_NOTIMPL; }
};
class FocusArgs final : public Microsoft::WRL::RuntimeClass<
    Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, ICoreWebView2MoveFocusRequestedEventArgs>
{
public:
    COREWEBVIEW2_MOVE_FOCUS_REASON reason = COREWEBVIEW2_MOVE_FOCUS_REASON_NEXT;
    BOOL handled = FALSE;
    HRESULT STDMETHODCALLTYPE get_Reason(COREWEBVIEW2_MOVE_FOCUS_REASON* value) override { *value = reason; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Handled(BOOL* value) override { *value = handled; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_Handled(BOOL value) override { handled = value; return S_OK; }
};
}
int runCompositionInputTests()
{
    int checks = 0, failures = 0;
    const auto check = [&](bool value, const char* label) { ++checks; if (!value) { ++failures; std::cerr << "FAIL: " << label << '\n'; } };
    QWidget root; root.resize(640, 440); root.move(100, 100);
    QWidget host(&root); host.setAttribute(Qt::WA_NativeWindow); host.setGeometry(21, 27, 500, 340);
    root.show(); host.show(); QApplication::processEvents();
    const auto rootHwnd = reinterpret_cast<HWND>(root.winId()), hostHwnd = reinterpret_cast<HWND>(host.winId());
    const auto composition = Microsoft::WRL::Make<RecordingComposition>();
    const auto controller = Microsoft::WRL::Make<RecordingController>();
    const auto latestMove = [&]() -> const MouseRecord* {
        for (auto e = composition->events.rbegin(); e != composition->events.rend(); ++e)
            if (e->kind == COREWEBVIEW2_MOUSE_EVENT_KIND_MOVE) return &*e;
        return nullptr;
    };
    bool enabled = true;
    SessionCompositionInput input(host, composition.Get(), controller.Get(), [&] { return enabled; });
    const auto post = [&](HWND window, UINT message, WPARAM wParam, POINT point) {
        PostMessageW(window, message, wParam, MAKELPARAM(point.x, point.y));
        QApplication::processEvents();
    };
    post(hostHwnd, WM_MOUSEMOVE, 0, {81, 63});
    // Posting this fixture's mouse message does not move the desktop cursor.
    // TrackMouseEvent may append a genuine LEAVE; inspect the MOVE being tested.
    check(latestMove() && latestMove()->point.x == 81 && latestMove()->point.y == 63,
        "Host physical coordinates are forwarded without DPI rescaling");
    POINT rootPoint{91, 73}; MapWindowPoints(hostHwnd, rootHwnd, &rootPoint, 1);
    post(rootHwnd, WM_MOUSEMOVE, 0, rootPoint);
    check(latestMove() && latestMove()->point.x == 91 && latestMove()->point.y == 73,
        "Root coordinates map to actual Host origin once");
    POINT screen{101, 83}; ClientToScreen(hostHwnd, &screen);
    post(rootHwnd, WM_MOUSEWHEEL, MAKEWPARAM(MK_CONTROL, -120), screen);
    check(composition->events.back().kind == COREWEBVIEW2_MOUSE_EVENT_KIND_WHEEL
        && composition->events.back().point.x == 101 && composition->events.back().point.y == 83
        && static_cast<INT32>(composition->events.back().data) == -120,
        "Wheel screen coordinates, sign and modifier survive forwarding");
    const auto beforeOutside = composition->events.size();
    post(rootHwnd, WM_MOUSEMOVE, 0, {-30, -30});
    check(composition->events.size() == beforeOutside + 1
        && composition->events.back().kind == COREWEBVIEW2_MOUSE_EVENT_KIND_LEAVE,
        "Outside root point dismisses hover without forwarding outside motion");
    post(hostHwnd, WM_LBUTTONDOWN, MK_LBUTTON, {100, 80});
    check(GetCapture() == hostHwnd, "Pointer down captures the physical Host");
    post(hostHwnd, WM_MOUSEMOVE, MK_LBUTTON, {-7, -11});
    check(latestMove() && latestMove()->point.x == -7 && latestMove()->point.y == -11,
        "Captured drag forwards outside bounds in Host coordinates");
    const auto beforeUp = composition->events.size();
    post(hostHwnd, WM_LBUTTONUP, 0, {-7, -11});
    check(GetCapture() != hostHwnd && composition->events.size() == beforeUp + 1
        && composition->events.back().kind == COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_UP,
        "Ordinary button up releases capture without artificial leave/up");
    post(hostHwnd, WM_RBUTTONDOWN, MK_RBUTTON, {70, 60}); post(hostHwnd, WM_RBUTTONUP, 0, {70, 60});
    check(composition->events.back().kind == COREWEBVIEW2_MOUSE_EVENT_KIND_RIGHT_BUTTON_UP,
        "Right-button pair reaches composition for native DOM context invocation");
    post(hostHwnd, WM_MBUTTONDOWN, MK_MBUTTON, {70, 60}); post(hostHwnd, WM_MBUTTONUP, 0, {70, 60});
    check(composition->events.back().kind == COREWEBVIEW2_MOUSE_EVENT_KIND_MIDDLE_BUTTON_UP, "Middle buttons remain available");
    post(hostHwnd, WM_XBUTTONDOWN, MAKEWPARAM(MK_XBUTTON2, XBUTTON2), {70, 60});
    post(hostHwnd, WM_XBUTTONUP, MAKEWPARAM(0, XBUTTON2), {70, 60});
    check(composition->events.back().kind == COREWEBVIEW2_MOUSE_EVENT_KIND_X_BUTTON_UP
        && composition->events.back().data == XBUTTON2, "X-button identity remains available");
    QWidget chromiumChild(&host); chromiumChild.setAttribute(Qt::WA_NativeWindow); chromiumChild.setGeometry(0, 0, 20, 20); chromiumChild.show();
    const auto beforeChild = composition->events.size();
    post(reinterpret_cast<HWND>(chromiumChild.winId()), WM_MOUSEMOVE, 0, {4, 5});
    check(composition->events.size() == beforeChild, "Descendant HWND input is not forwarded twice");
    const auto beforeKeyboard = composition->events.size();
    post(hostHwnd, WM_KEYDOWN, VK_ESCAPE, {0, 0});
    check(composition->events.size() == beforeKeyboard, "Adapter does not turn keyboard/IME into mouse or own product commands");
    const auto focusArgs = Microsoft::WRL::Make<FocusArgs>();
    const auto focusBefore = controller->focusMoves;
    controller->focusHandler->Invoke(controller.Get(), focusArgs.Get()); QApplication::processEvents();
    check(focusArgs->handled && controller->focusMoves == focusBefore + 1, "Page-end Tab wraps within the one active editor");
    focusArgs->reason = COREWEBVIEW2_MOVE_FOCUS_REASON_PREVIOUS; focusArgs->handled = FALSE;
    controller->focusHandler->Invoke(controller.Get(), focusArgs.Get()); QApplication::processEvents();
    check(focusArgs->handled, "Page-start Shift+Tab has the corresponding focus route");
    const auto beforeInvalidationFocus = controller->focusMoves;
    controller->focusHandler->Invoke(controller.Get(), focusArgs.Get());
    input.invalidate(); QApplication::processEvents();
    check(controller->focusMoves == beforeInvalidationFocus,
        "Queued old-page focus cannot run after routing generation invalidation");
    enabled = false;
    const auto mouseBeforeBlocked = input.diagnosticFacts().value("mouseEvents").toInteger();
    post(hostHwnd, WM_MOUSEMOVE, 0, {80, 80});
    check(input.diagnosticFacts().value("mouseEvents").toInteger() == mouseBeforeBlocked,
        "Unready/retiring page guard prevents old physical input delivery");
    enabled = true;
    const auto savedFocus = controller->focusHandler;
    const auto savedCursor = composition->cursorHandler;
    input.retire(); input.retire();
    check(!controller->focusHandler && !composition->cursorHandler && input.diagnosticFacts().value("retired").toBool(),
        "Retirement is idempotent and removes callback registrations");
    const auto focusAfterRetire = controller->focusMoves, cursorAfterRetire = composition->cursorReads;
    savedFocus->Invoke(controller.Get(), focusArgs.Get()); savedCursor->Invoke(composition.Get(), nullptr); QApplication::processEvents();
    check(controller->focusMoves == focusAfterRetire && composition->cursorReads == cursorAfterRetire,
        "Late focus/cursor callback cannot reactivate retired adapter");
    const auto afterRetireEvents = composition->events.size(); post(hostHwnd, WM_MOUSEMOVE, 0, {80, 80});
    check(composition->events.size() == afterRetireEvents, "Native filter no longer forwards after retirement");

    ProductWindowHost productWindow(ProductWindowRole::Launcher);
    productWindow.resize(640, 440);
    auto* productBrowser = new QWidget(productWindow.productViewport());
    productBrowser->setAttribute(Qt::WA_NativeWindow);
    productWindow.setProductContent(productBrowser);
    productWindow.setProductPresentationStage(ProductPresentationStage::Ready);
    productWindow.show();
    QApplication::processEvents();
    const auto productRoot = reinterpret_cast<HWND>(productWindow.winId());
    const auto productHost = reinterpret_cast<HWND>(productBrowser->winId());
    RECT client{};
    GetClientRect(productRoot, &client);
    const ProductChromeGeometry chrome{
        QRect{100, 18, 90, 32}, QRect{client.right - 144, 18, 40, 32},
        QRect{client.right - 104, 18, 40, 32}, QRect{client.right - 64, 18, 40, 32}};
    check(productWindow.setProductChromeGeometry(productBrowser->winId(), chrome),
        "The product viewport admits a native chrome projection");
    const auto productComposition = Microsoft::WRL::Make<RecordingComposition>();
    const auto productController = Microsoft::WRL::Make<RecordingController>();
    SessionCompositionInput productInput(*productBrowser, productComposition.Get(), productController.Get(), [] { return true; });
    const auto productMouseEvents = [&] { return productInput.diagnosticFacts().value("mouseEvents").toInteger(); };
    auto maximize = POINT{chrome.maximize.center().x(), chrome.maximize.center().y()};
    auto captionInBrowser = maximize;
    MapWindowPoints(productRoot, productHost, &captionInBrowser, 1);
    post(productHost, WM_MOUSEMOVE, 0, captionInBrowser);
    check(productMouseEvents() == 0, "Uncaptured input at a native button does not reach the WebView");

    ClientToScreen(productRoot, &maximize);
    SendMessageW(productRoot, WM_NCLBUTTONDOWN, HTMAXBUTTON, MAKELPARAM(maximize.x, maximize.y));
    check(productWindow.hasProductCaptionCapture() && GetCapture() == productRoot,
        "A native caption press owns capture on the root");
    post(productRoot, WM_LBUTTONUP, 0, {120, 100});
    check(!productWindow.hasProductCaptionCapture() && GetCapture() != productRoot
        && productMouseEvents() == 0 && !IsZoomed(productRoot),
        "The browser leaves the caption release to Windows and a release outside cancels the action");

    post(productHost, WM_LBUTTONDOWN, MK_LBUTTON, {130, 100});
    const auto beforeCrossing = productMouseEvents();
    post(productHost, WM_MOUSEMOVE, MK_LBUTTON, captionInBrowser);
    post(productHost, WM_LBUTTONUP, 0, captionInBrowser);
    check(productMouseEvents() == beforeCrossing + 2 && GetCapture() != productHost,
        "An existing WebView drag retains move and release while crossing native chrome");

    std::cout << "Composition input: " << checks - failures << '/' << checks << " checks PASS\n";
    return failures ? 1 : 0;
}
}
int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    return DesktopSceneEngine::Player::runCompositionInputTests();
}
