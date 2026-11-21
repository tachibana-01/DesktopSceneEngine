#include "SessionCompositionInput.h"
#include "ProductWindowHost.h"

#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <QPointer>
#include <QTimer>
#include <QWidget>
#include <stdexcept>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <windowsx.h>
#include <wrl.h>
#include <WebView2.h>
#include <UIAutomation.h>

namespace DesktopSceneEngine::Player
{
using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
namespace
{
struct InputState
{
    QPointer<QWidget> host;
    QPointer<ProductWindowHost> product;
    HWND hostWindow{}, rootWindow{}, trackingWindow{};
    ComPtr<ICoreWebView2CompositionController> composition;
    ComPtr<ICoreWebView2Controller> controller;
    std::function<bool()> acceptsInput;
    EventRegistrationToken cursorChanged{}, moveFocusRequested{}, acceleratorPressed{};
    bool retired = false, mouseInside = false, releasingCapture = false;
    unsigned pressedButtons = 0;
    quint64 routingEpoch = 0;
    quint64 mouseEvents = 0, wheelEvents = 0, leaveEvents = 0, focusRequests = 0,
        captureLosses = 0, rejectedEvents = 0, failures = 0, uiaRequests = 0;
    POINT lastPoint{};

    bool active() const
    {
        return !retired && host && host->internalWinId() == reinterpret_cast<WId>(hostWindow)
            && IsWindow(hostWindow) && IsWindow(rootWindow)
            && GetAncestor(hostWindow, GA_ROOT) == rootWindow && composition && controller
            && IsWindowVisible(hostWindow) && !IsIconic(rootWindow) && IsWindowEnabled(rootWindow)
            && acceptsInput && acceptsInput();
    }
    void leave() noexcept
    {
        if (trackingWindow && IsWindow(trackingWindow))
        {
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE | TME_CANCEL, trackingWindow, 0};
            TrackMouseEvent(&track);
        }
        trackingWindow = nullptr;
        const bool wasInside = mouseInside;
        mouseInside = false; // Clear before COM callout can reenter retirement.
        const auto currentComposition = composition;
        if (wasInside && currentComposition)
        {
            if (FAILED(currentComposition->SendMouseInput(COREWEBVIEW2_MOUSE_EVENT_KIND_LEAVE,
                COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_NONE, 0, {}))) ++failures;
            ++leaveEvents;
        }
    }
    void releaseOwnCapture() noexcept
    {
        if (GetCapture() != hostWindow) return;
        releasingCapture = true;
        ReleaseCapture();
        releasingCapture = false;
    }
    void cancelCapture() noexcept
    {
        // Never manufacture mouse-up: that could commit a Timeline/slider edit.
        pressedButtons = 0;
        releaseOwnCapture();
        leave();
    }
    bool pointInside(POINT point) const
    {
        RECT bounds{};
        return GetClientRect(hostWindow, &bounds) && PtInRect(&bounds, point);
    }
    bool ownsCursor() const
    {
        if (!active() || (product && product->hasProductCaptionCapture())) return false;
        if (GetCapture() == hostWindow) return true;
        POINT screen{};
        if (!GetCursorPos(&screen)) return false;
        const auto hit = WindowFromPoint(screen);
        if (hit != hostWindow && hit != rootWindow) return false;
        if (product && !product->isProductChromeClientPoint(QPoint(int(screen.x), int(screen.y)))) return false;
        return ScreenToClient(hostWindow, &screen) && pointInside(screen);
    }
    bool updateCursor()
    {
        if (!ownsCursor()) return false;
        HCURSOR cursor{};
        const auto currentComposition = composition;
        if (FAILED(currentComposition->get_Cursor(&cursor))) { ++failures; return false; }
        if (!ownsCursor()) return false;
        SetCursor(cursor); // Use immediately; never retain the borrowed HCURSOR.
        return true;
    }
    void focus()
    {
        if (!active()) return;
        const auto focused = GetFocus();
        if (focused != hostWindow && IsChild(hostWindow, focused)) return;
        const auto currentController = controller;
        const auto epoch = routingEpoch;
        host->setFocus(Qt::MouseFocusReason);
        if (!active() || routingEpoch != epoch) return;
        if (FAILED(currentController->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC))) ++failures;
        ++focusRequests;
    }
    bool message(MSG& message, qintptr* result)
    {
        if (message.hwnd != hostWindow && message.hwnd != rootWindow) return false;
        // Chromium descendants/native dialogs already own their input. Do not
        // intercept them or forward a keyboard/IME message a second time.
        if (message.message == WM_CAPTURECHANGED && message.hwnd == hostWindow)
        {
            if (!releasingCapture)
            {
                if (pressedButtons) ++captureLosses;
                pressedButtons = 0;
                leave();
            }
            return false;
        }
        if (message.message == WM_CANCELMODE && message.hwnd == hostWindow) { cancelCapture(); return false; }
        if (message.message == WM_MOUSELEAVE)
        {
            if (message.hwnd == trackingWindow) leave();
            return false;
        }
        if (!active())
        {
            if (message.message >= WM_MOUSEFIRST && message.message <= WM_MOUSELAST) ++rejectedEvents;
            cancelCapture();
            return false;
        }
        const auto epoch = routingEpoch;
        if (message.message == WM_GETOBJECT && message.hwnd == hostWindow
            && static_cast<LONG>(message.lParam) == UiaRootObjectId)
        {
            ComPtr<ICoreWebView2CompositionController2> composition2;
            ComPtr<IUnknown> unknown;
            ComPtr<IRawElementProviderSimple> provider;
            const auto currentComposition = composition;
            if (SUCCEEDED(currentComposition.As(&composition2)) && SUCCEEDED(composition2->get_AutomationProvider(&unknown))
                && SUCCEEDED(unknown.As(&provider)))
            {
                if (!active() || routingEpoch != epoch) return false;
                ++uiaRequests;
                if (result) *result = UiaReturnRawElementProvider(hostWindow, message.wParam, message.lParam, provider.Get());
                return true;
            }
            ++failures;
            return false;
        }
        if (message.message == WM_SETCURSOR && LOWORD(message.lParam) == HTCLIENT && updateCursor())
        {
            if (result) *result = TRUE;
            return true;
        }
        switch (message.message)
        {
        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
        case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK:
        case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL: break;
        default: return false;
        }
        if (product && product->hasProductCaptionCapture()) return false;
        const bool wheel = message.message == WM_MOUSEWHEEL || message.message == WM_MOUSEHWHEEL;
        POINT point{GET_X_LPARAM(message.lParam), GET_Y_LPARAM(message.lParam)};
        if (wheel)
        {
            if (!ScreenToClient(hostWindow, &point)) { ++failures; return false; }
        }
        else if (message.hwnd != hostWindow)
        {
            SetLastError(ERROR_SUCCESS);
            if (!MapWindowPoints(message.hwnd, hostWindow, &point, 1) && GetLastError() != ERROR_SUCCESS)
            { ++failures; return false; }
        }
        if (GetCapture() != hostWindow)
        {
            POINT screen = point;
            if (!pointInside(point) || (product && (!ClientToScreen(hostWindow, &screen)
                || !product->isProductChromeClientPoint(QPoint(int(screen.x), int(screen.y))))))
            {
                leave();
                return false;
            }
        }
        const auto button = [](UINT kind, WPARAM wParam) -> unsigned {
            switch (kind)
            {
            case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: case WM_LBUTTONUP: return MK_LBUTTON;
            case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: case WM_RBUTTONUP: return MK_RBUTTON;
            case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK: case WM_MBUTTONUP: return MK_MBUTTON;
            case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK: case WM_XBUTTONUP:
                return GET_XBUTTON_WPARAM(wParam) == XBUTTON1 ? MK_XBUTTON1 : MK_XBUTTON2;
            default: return 0;
            }
        };
        const bool down = message.message == WM_LBUTTONDOWN || message.message == WM_LBUTTONDBLCLK
            || message.message == WM_RBUTTONDOWN || message.message == WM_RBUTTONDBLCLK
            || message.message == WM_MBUTTONDOWN || message.message == WM_MBUTTONDBLCLK
            || message.message == WM_XBUTTONDOWN || message.message == WM_XBUTTONDBLCLK;
        const bool up = message.message == WM_LBUTTONUP || message.message == WM_RBUTTONUP
            || message.message == WM_MBUTTONUP || message.message == WM_XBUTTONUP;
        if (down)
        {
            focus();
            if (!active() || routingEpoch != epoch) { cancelCapture(); if (result) *result = 0; return true; }
            pressedButtons |= button(message.message, message.wParam);
            SetCapture(hostWindow);
            if (!active() || routingEpoch != epoch) { cancelCapture(); if (result) *result = 0; return true; }
        }
        if (message.message == WM_MOUSEMOVE && trackingWindow != message.hwnd)
        {
            if (trackingWindow)
            {
                TRACKMOUSEEVENT cancel{sizeof(cancel), TME_LEAVE | TME_CANCEL, trackingWindow, 0};
                TrackMouseEvent(&cancel);
            }
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, message.hwnd, 0};
            if (TrackMouseEvent(&track)) trackingWindow = message.hwnd;
        }
        mouseInside = true;
        UINT32 data = 0;
        if (wheel) data = static_cast<UINT32>(static_cast<INT32>(GET_WHEEL_DELTA_WPARAM(message.wParam)));
        else if (message.message == WM_XBUTTONDOWN || message.message == WM_XBUTTONUP || message.message == WM_XBUTTONDBLCLK)
            data = GET_XBUTTON_WPARAM(message.wParam);
        const auto currentComposition = composition;
        if (FAILED(currentComposition->SendMouseInput(static_cast<COREWEBVIEW2_MOUSE_EVENT_KIND>(message.message),
            static_cast<COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS>(GET_KEYSTATE_WPARAM(message.wParam)), data, point))) ++failures;
        ++mouseEvents; if (wheel) ++wheelEvents; lastPoint = point;
        if (up)
        {
            pressedButtons &= ~button(message.message, message.wParam);
            if (!pressedButtons) releaseOwnCapture();
        }
        // Consume a failed dispatch too: replay through Qt is not a safe fallback.
        if (result) *result = message.message == WM_XBUTTONDOWN || message.message == WM_XBUTTONUP
            || message.message == WM_XBUTTONDBLCLK ? TRUE : 0;
        return true;
    }
    void retire() noexcept
    {
        if (retired) return;
        retired = true; // Guard callbacks before callouts/removal.
        cancelCapture();
        if (composition && cursorChanged.value) composition->remove_CursorChanged(cursorChanged);
        if (controller && moveFocusRequested.value) controller->remove_MoveFocusRequested(moveFocusRequested);
        if (controller && acceleratorPressed.value) controller->remove_AcceleratorKeyPressed(acceleratorPressed);
        cursorChanged = {}; moveFocusRequested = {}; acceleratorPressed = {}; acceptsInput = {};
        composition.Reset(); controller.Reset(); host.clear(); product.clear();
    }
    void invalidate() noexcept
    {
        if (retired) return;
        ++routingEpoch;
        cancelCapture();
    }
};
class InputFilter final : public QAbstractNativeEventFilter
{
public:
    explicit InputFilter(const std::shared_ptr<InputState>& state) : state_(state) {}
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override
    {
        const auto state = state_.lock();
        return state && !state->retired && eventType == "windows_generic_MSG" && message
            && state->message(*static_cast<MSG*>(message), result);
    }
private:
    std::weak_ptr<InputState> state_;
};
}
struct SessionCompositionInput::Impl
{
    std::shared_ptr<InputState> state = std::make_shared<InputState>();
    InputFilter filter{state};
    bool installed = false;
    ~Impl()
    {
        state->retire();
        if (installed && QCoreApplication::instance()) QCoreApplication::instance()->removeNativeEventFilter(&filter);
    }
};
SessionCompositionInput::SessionCompositionInput(QWidget& host, ICoreWebView2CompositionController* composition,
    ICoreWebView2Controller* controller, std::function<bool()> acceptsInput) : impl_(std::make_unique<Impl>())
{
    if (!composition || !controller || !acceptsInput || !QCoreApplication::instance())
        throw std::invalid_argument("composition-input-requires-live-controller-and-host");
    const auto state = impl_->state;
    state->host = &host; state->product = dynamic_cast<ProductWindowHost*>(host.window());
    state->hostWindow = reinterpret_cast<HWND>(host.winId());
    state->rootWindow = GetAncestor(state->hostWindow, GA_ROOT);
    state->composition = composition; state->controller = controller; state->acceptsInput = std::move(acceptsInput);
    const auto weak = std::weak_ptr<InputState>{state};
    const auto cursorStatus = composition->add_CursorChanged(Callback<ICoreWebView2CursorChangedEventHandler>(
        [weak](ICoreWebView2CompositionController*, IUnknown*) -> HRESULT {
            if (const auto current = weak.lock(); current && current->active()) current->updateCursor();
            return S_OK;
        }).Get(), &state->cursorChanged);
    if (FAILED(cursorStatus)) throw std::runtime_error("composition-cursor-registration-failed");
    const auto focusStatus = controller->add_MoveFocusRequested(Callback<ICoreWebView2MoveFocusRequestedEventHandler>(
        [weak](ICoreWebView2Controller*, ICoreWebView2MoveFocusRequestedEventArgs* args) -> HRESULT {
            const auto current = weak.lock();
            if (!current || !current->active()) return S_OK;
            COREWEBVIEW2_MOVE_FOCUS_REASON reason{};
            if (FAILED(args->get_Reason(&reason))) return S_OK;
            // The sole active central editor wraps Tab at its page boundary,
            // rather than focusing parked/disabled legacy Qt controls.
            if (reason != COREWEBVIEW2_MOVE_FOCUS_REASON_NEXT && reason != COREWEBVIEW2_MOVE_FOCUS_REASON_PREVIOUS) return S_OK;
            args->put_Handled(TRUE);
            const auto epoch = current->routingEpoch;
            QTimer::singleShot(0, QCoreApplication::instance(), [weak, reason, epoch] {
                if (const auto hostState = weak.lock(); hostState && hostState->active() && hostState->routingEpoch == epoch)
                {
                    const auto currentController = hostState->controller;
                    if (FAILED(currentController->MoveFocus(reason))) ++hostState->failures;
                    ++hostState->focusRequests;
                }
            });
            return S_OK;
        }).Get(), &state->moveFocusRequested);
    if (FAILED(focusStatus)) throw std::runtime_error("composition-focus-registration-failed");
    if (state->product)
    {
        const auto status = controller->add_AcceleratorKeyPressed(
            Callback<ICoreWebView2AcceleratorKeyPressedEventHandler>(
                [weak](ICoreWebView2Controller*, ICoreWebView2AcceleratorKeyPressedEventArgs* args) -> HRESULT {
                    const auto current = weak.lock();
                    UINT key{};
                    COREWEBVIEW2_KEY_EVENT_KIND kind{};
                    COREWEBVIEW2_PHYSICAL_KEY_STATUS physical{};
                    if (!current || !current->active() || GetForegroundWindow() != current->rootWindow
                        || FAILED(args->get_VirtualKey(&key)) || key != VK_SPACE
                        || FAILED(args->get_KeyEventKind(&kind)) || kind != COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN
                        || FAILED(args->get_PhysicalKeyStatus(&physical)) || !physical.IsMenuKeyDown) return S_OK;
                    if (FAILED(args->put_Handled(TRUE)) || physical.WasKeyDown) return S_OK;
                    const auto epoch = current->routingEpoch;
                    QTimer::singleShot(0, current->host.data(), [weak, epoch] {
                        if (const auto state = weak.lock(); state && state->active() && state->routingEpoch == epoch
                            && GetForegroundWindow() == state->rootWindow)
                            SendMessageW(state->rootWindow, WM_SYSCOMMAND, SC_KEYMENU, VK_SPACE);
                    });
                    return S_OK;
                }).Get(), &state->acceleratorPressed);
        // The native root still owns the system key when it receives it. The
        // browser accelerator is the corresponding path with DOM focus.
        if (FAILED(status)) ++state->failures;
    }
    QCoreApplication::instance()->installNativeEventFilter(&impl_->filter); impl_->installed = true;
}
SessionCompositionInput::~SessionCompositionInput() = default;
void SessionCompositionInput::invalidate() noexcept { impl_->state->invalidate(); }
void SessionCompositionInput::retire() noexcept
{
    impl_->state->retire();
    if (impl_->installed && QCoreApplication::instance()) QCoreApplication::instance()->removeNativeEventFilter(&impl_->filter);
    impl_->installed = false;
}
QJsonObject SessionCompositionInput::diagnosticFacts() const
{
    const auto& s = *impl_->state;
    return {{"retired", s.retired}, {"inputEligible", s.active()}, {"mouseEvents", qint64(s.mouseEvents)},
        {"wheelEvents", qint64(s.wheelEvents)}, {"leaveEvents", qint64(s.leaveEvents)},
        {"focusRequests", qint64(s.focusRequests)}, {"captureLosses", qint64(s.captureLosses)},
        {"rejectedEvents", qint64(s.rejectedEvents)}, {"failures", qint64(s.failures)},
        {"uiaRequests", qint64(s.uiaRequests)}, {"mouseInside", s.mouseInside},
        {"ownsCapture", GetCapture() == s.hostWindow}, {"lastX", int(s.lastPoint.x)}, {"lastY", int(s.lastPoint.y)},
        {"routingEpoch", qint64(s.routingEpoch)},
        {"keyboardImeOwner", "WebView native focus child"}, {"externalDrop", "not a current product input surface"}};
}
}
