#include <DesktopSceneEngine/Platform/Display.h>

#include <Windows.h>

#include <algorithm>
#include <limits>
#include <memory>
#include <string>

namespace
{

using DesktopSceneEngine::Platform::DisplayDescriptor;
using DesktopSceneEngine::Platform::DisplayEnumerationStatus;
using DesktopSceneEngine::Platform::DisplayIdentifyStatus;
using DesktopSceneEngine::DisplayId;

constexpr wchar_t OverlayClassName[] = L"DesktopSceneEngineDisplayIdentifier";
constexpr UINT_PTR OverlayTimerId = 1U;
constexpr int OverlayBorderThickness = 8;
constexpr BYTE OverlayOpacity = 144U;

struct OverlayWindowState
{
    HINSTANCE instance = nullptr;
    int displayNumber = 0;
    bool unregisterClassOnDestroy = false;
};

struct MonitorLookup
{
    const std::wstring& sourceDeviceName;
    RECT bounds{};
    bool found = false;
    bool apiFailure = false;
};

HWND currentOverlay = nullptr;

std::wstring wideFromUtf8(const std::string& value)
{
    if (value.empty()
        || value.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
    {
        return {};
    }

    const int required = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0);
    if (required <= 0)
    {
        return {};
    }

    std::wstring wide(static_cast<std::size_t>(required), L'\0');
    const int converted = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        wide.data(),
        required);
    return converted == required ? wide : std::wstring{};
}

LRESULT CALLBACK overlayWindowProcedure(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam
);

bool registerOverlayClass(HINSTANCE instance, bool& registered)
{
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = overlayWindowProcedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.lpszClassName = OverlayClassName;

    if (RegisterClassExW(&windowClass) != 0U)
    {
        registered = true;
        return true;
    }

    registered = false;
    return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

void destroyCurrentOverlay() noexcept
{
    if (currentOverlay == nullptr || IsWindow(currentOverlay) == FALSE)
    {
        currentOverlay = nullptr;
        return;
    }

    const auto* state = reinterpret_cast<const OverlayWindowState*>(
        GetWindowLongPtrW(currentOverlay, GWLP_USERDATA));
    const HINSTANCE instance = state == nullptr ? nullptr : state->instance;
    const bool unregisterClass = state != nullptr && state->unregisterClassOnDestroy;
    const HWND overlay = currentOverlay;
    currentOverlay = nullptr;
    DestroyWindow(overlay);
    if (unregisterClass && instance != nullptr)
    {
        (void)UnregisterClassW(OverlayClassName, instance);
    }
}

BOOL CALLBACK findMonitorForSourceDevice(
    HMONITOR monitor,
    HDC,
    LPRECT,
    LPARAM userData
)
{
    auto* lookup = reinterpret_cast<MonitorLookup*>(userData);
    if (lookup == nullptr)
    {
        return FALSE;
    }

    MONITORINFOEXW information{};
    information.cbSize = sizeof(information);
    if (GetMonitorInfoW(monitor, &information) == FALSE)
    {
        lookup->apiFailure = true;
        return FALSE;
    }

    if (lookup->sourceDeviceName == information.szDevice)
    {
        lookup->bounds = information.rcMonitor;
        lookup->found = true;
        return FALSE;
    }

    return TRUE;
}

DisplayIdentifyStatus findCurrentMonitorBounds(
    const DisplayDescriptor& descriptor,
    RECT& bounds
)
{
    const std::wstring sourceDeviceName = wideFromUtf8(descriptor.sourceDeviceName);
    if (sourceDeviceName.empty())
    {
        return DisplayIdentifyStatus::ApiFailure;
    }

    MonitorLookup lookup{sourceDeviceName};
    (void)EnumDisplayMonitors(
        nullptr,
        nullptr,
        findMonitorForSourceDevice,
        reinterpret_cast<LPARAM>(&lookup));
    if (lookup.apiFailure)
    {
        return DisplayIdentifyStatus::ApiFailure;
    }
    if (!lookup.found)
    {
        return DisplayIdentifyStatus::Unresolved;
    }

    bounds = lookup.bounds;
    return DisplayIdentifyStatus::Shown;
}

int fontHeightForBounds(const RECT& bounds) noexcept
{
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    const int shortestEdge = (std::max)(1, (std::min)(width, height));
    return (std::max)(96, (std::min)(640, shortestEdge / 3));
}

void paintOverlay(HWND window, const OverlayWindowState& state)
{
    PAINTSTRUCT paint{};
    HDC deviceContext = BeginPaint(window, &paint);
    RECT client{};
    GetClientRect(window, &client);

    HBRUSH background = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(deviceContext, &client, background);
    DeleteObject(background);

    HPEN border = CreatePen(PS_SOLID, OverlayBorderThickness, RGB(0, 220, 255));
    HGDIOBJ previousPen = SelectObject(deviceContext, border);
    HGDIOBJ previousBrush = SelectObject(deviceContext, GetStockObject(HOLLOW_BRUSH));
    const int borderRight = (std::max)(
        OverlayBorderThickness,
        static_cast<int>(client.right) - OverlayBorderThickness / 2);
    const int borderBottom = (std::max)(
        OverlayBorderThickness,
        static_cast<int>(client.bottom) - OverlayBorderThickness / 2);
    Rectangle(
        deviceContext,
        OverlayBorderThickness / 2,
        OverlayBorderThickness / 2,
        borderRight,
        borderBottom);
    SelectObject(deviceContext, previousBrush);
    SelectObject(deviceContext, previousPen);
    DeleteObject(border);

    const HFONT font = CreateFontW(
        -fontHeightForBounds(client),
        0,
        0,
        0,
        FW_BOLD,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS,
        L"Segoe UI");
    HGDIOBJ previousFont = SelectObject(deviceContext, font);
    SetBkMode(deviceContext, TRANSPARENT);
    SetTextColor(deviceContext, RGB(255, 255, 255));
    const std::wstring number = std::to_wstring(state.displayNumber);
    DrawTextW(
        deviceContext,
        number.c_str(),
        static_cast<int>(number.size()),
        &client,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(deviceContext, previousFont);
    DeleteObject(font);

    EndPaint(window, &paint);
}

LRESULT CALLBACK overlayWindowProcedure(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam
)
{
    if (message == WM_NCCREATE)
    {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(
            window,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return TRUE;
    }

    const auto* state = reinterpret_cast<const OverlayWindowState*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message)
    {
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_TIMER:
        if (wParam == OverlayTimerId)
        {
            const HINSTANCE instance = state == nullptr ? nullptr : state->instance;
            const bool unregisterClass = state != nullptr && state->unregisterClassOnDestroy;
            KillTimer(window, OverlayTimerId);
            DestroyWindow(window);
            if (unregisterClass && instance != nullptr)
            {
                (void)UnregisterClassW(OverlayClassName, instance);
            }
            return 0;
        }
        break;
    case WM_PAINT:
        if (state != nullptr)
        {
            paintOverlay(window, *state);
            return 0;
        }
        break;
    case WM_NCDESTROY:
        if (currentOverlay == window)
        {
            currentOverlay = nullptr;
        }
        delete state;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        break;
    default:
        break;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

DisplayIdentifyStatus showOverlay(
    const RECT& bounds,
    int displayNumber,
    std::uint32_t durationMilliseconds
)
{
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    if (width <= 0 || height <= 0)
    {
        return DisplayIdentifyStatus::ApiFailure;
    }

    destroyCurrentOverlay();

    const HINSTANCE instance = GetModuleHandleW(nullptr);
    bool registeredClass = false;
    if (instance == nullptr || !registerOverlayClass(instance, registeredClass))
    {
        return DisplayIdentifyStatus::ApiFailure;
    }

    auto state = std::make_unique<OverlayWindowState>();
    state->instance = instance;
    state->displayNumber = displayNumber;
    state->unregisterClassOnDestroy = registeredClass;
    HWND overlay = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | WS_EX_LAYERED,
        OverlayClassName,
        L"",
        WS_POPUP,
        bounds.left,
        bounds.top,
        width,
        height,
        nullptr,
        nullptr,
        instance,
        state.get());
    if (overlay == nullptr)
    {
        if (registeredClass)
        {
            (void)UnregisterClassW(OverlayClassName, instance);
        }
        return DisplayIdentifyStatus::ApiFailure;
    }

    (void)SetLayeredWindowAttributes(overlay, 0, OverlayOpacity, LWA_ALPHA);
    currentOverlay = overlay;
    (void)state.release();
    SetWindowPos(
        overlay,
        HWND_TOPMOST,
        bounds.left,
        bounds.top,
        width,
        height,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
    ShowWindow(overlay, SW_SHOWNOACTIVATE);
    UpdateWindow(overlay);
    if (SetTimer(overlay, OverlayTimerId, durationMilliseconds, nullptr) == 0U)
    {
        destroyCurrentOverlay();
        return DisplayIdentifyStatus::ApiFailure;
    }

    return DisplayIdentifyStatus::Shown;
}

}

namespace DesktopSceneEngine::Platform
{

DisplayIdentifyStatus identifyDisplay(
    const DisplayId& id,
    std::uint32_t durationMilliseconds
)
{
    if (!id.isValid() || durationMilliseconds == 0U)
    {
        return DisplayIdentifyStatus::InvalidRequest;
    }

    const DisplayEnumerationResult result = enumerateActiveDisplays();
    if (result.status != DisplayEnumerationStatus::Success)
    {
        return DisplayIdentifyStatus::ApiFailure;
    }

    const auto descriptor = result.snapshot.resolve(id);
    if (!descriptor.has_value())
    {
        return DisplayIdentifyStatus::Unresolved;
    }

    RECT bounds{};
    const DisplayIdentifyStatus boundsStatus = findCurrentMonitorBounds(*descriptor, bounds);
    if (boundsStatus != DisplayIdentifyStatus::Shown)
    {
        return boundsStatus;
    }

    if (descriptor->displayNumber <= 0)
    {
        return DisplayIdentifyStatus::ApiFailure;
    }

    return showOverlay(bounds, descriptor->displayNumber, durationMilliseconds);
}

}
