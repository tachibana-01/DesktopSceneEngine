#include "DesktopWindowHost.h"

#include "../../Rendering/WindowTarget.h"

#include <Windows.h>
#include <commctrl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

namespace
{

constexpr wchar_t WindowTitle[] = L"Desktop Scene Engine";
constexpr int ControlBarHeight = 48;

enum ControlId : int
{
    PlayPauseControl = 1001,
    NextControl = 1002,
    HalfSpeedControl = 1003,
    NormalSpeedControl = 1004,
    DoubleSpeedControl = 1005,
    MuteControl = 1006,
    VolumeControl = 1007,
    StatusControl = 1008
};

struct InputState
{
    std::array<bool, 256> pressed{};
    bool volumeChanged = false;
    float volume = 1.0F;
};

struct WindowUi
{
    InputState input{};
    int width = 1280;
    int height = 720;
    bool hasControlBar = true;
    HWND playPause = nullptr;
    HWND next = nullptr;
    HWND halfSpeed = nullptr;
    HWND normalSpeed = nullptr;
    HWND doubleSpeed = nullptr;
    HWND mute = nullptr;
    HWND volume = nullptr;
    HWND status = nullptr;
};

void pressVirtualKey(WindowUi& ui, unsigned int virtualKey) noexcept
{
    if (virtualKey < ui.input.pressed.size())
    {
        ui.input.pressed[virtualKey] = true;
    }
}

void layoutControls(WindowUi& ui, int width, int height) noexcept
{
    const int y = (std::max)(0, height - ControlBarHeight) + 8;
    constexpr int buttonHeight = 30;
    constexpr int gap = 6;
    int x = 8;

    const auto place = [&](HWND control, int controlWidth) noexcept
    {
        if (control != nullptr)
        {
            MoveWindow(control, x, y, controlWidth, buttonHeight, TRUE);
        }
        x += controlWidth + gap;
    };

    place(ui.playPause, 104);
    place(ui.next, 72);
    place(ui.halfSpeed, 52);
    place(ui.normalSpeed, 52);
    place(ui.doubleSpeed, 52);
    place(ui.mute, 72);

    if (ui.volume != nullptr)
    {
        const int sliderWidth = (std::max)(96, (width - x) / 3);
        MoveWindow(ui.volume, x, y + 2, sliderWidth, 26, TRUE);
        x += sliderWidth + gap;
    }

    if (ui.status != nullptr)
    {
        MoveWindow(
            ui.status,
            x,
            y + 2,
            (std::max)(0, width - x - 8),
            26,
            TRUE);
    }
}

HWND createButton(
    HWND parent,
    HINSTANCE instance,
    const wchar_t* text,
    int id
)
{
    return CreateWindowExW(
        0,
        L"BUTTON",
        text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0,
        0,
        0,
        0,
        parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        instance,
        nullptr);
}

void createControls(HWND parent, HINSTANCE instance, WindowUi& ui)
{
    INITCOMMONCONTROLSEX commonControls{
        sizeof(INITCOMMONCONTROLSEX),
        ICC_BAR_CLASSES};
    (void)InitCommonControlsEx(&commonControls);

    ui.playPause = createButton(parent, instance, L"Play / Pause", PlayPauseControl);
    ui.next = createButton(parent, instance, L"Next", NextControl);
    ui.halfSpeed = createButton(parent, instance, L"0.5x", HalfSpeedControl);
    ui.normalSpeed = createButton(parent, instance, L"1x", NormalSpeedControl);
    ui.doubleSpeed = createButton(parent, instance, L"2x", DoubleSpeedControl);
    ui.mute = createButton(parent, instance, L"Mute", MuteControl);

    ui.volume = CreateWindowExW(
        0,
        TRACKBAR_CLASSW,
        L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_AUTOTICKS,
        0,
        0,
        0,
        0,
        parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(VolumeControl)),
        instance,
        nullptr);
    if (ui.volume != nullptr)
    {
        SendMessageW(ui.volume, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
        SendMessageW(ui.volume, TBM_SETPOS, TRUE, 100);
    }

    ui.status = CreateWindowExW(
        0,
        L"STATIC",
        L"",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
        0,
        0,
        0,
        0,
        parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(StatusControl)),
        instance,
        nullptr);
}

LRESULT CALLBACK windowProcedure(
    HWND windowHandle,
    UINT message,
    WPARAM wParam,
    LPARAM lParam
)
{
    if (message == WM_CLOSE)
    {
        DestroyWindow(windowHandle);
        return 0;
    }

    if (message == WM_DESTROY)
    {
        PostQuitMessage(0);
        return 0;
    }

    if (message == WM_NCCREATE)
    {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(
            windowHandle,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return TRUE;
    }

    if (message == WM_KEYDOWN || message == WM_SYSKEYDOWN)
    {
        if (wParam < 256 && (lParam & (1LL << 30)) == 0)
        {
            auto* ui = reinterpret_cast<WindowUi*>(GetWindowLongPtrW(
                windowHandle,
                GWLP_USERDATA));
            if (ui != nullptr)
            {
                pressVirtualKey(*ui, static_cast<unsigned int>(wParam));
            }
        }

        return 0;
    }

    if (message == WM_COMMAND && HIWORD(wParam) == BN_CLICKED)
    {
        auto* ui = reinterpret_cast<WindowUi*>(GetWindowLongPtrW(
            windowHandle,
            GWLP_USERDATA));
        if (ui != nullptr)
        {
            switch (LOWORD(wParam))
            {
            case PlayPauseControl:
                pressVirtualKey(*ui, VK_SPACE);
                break;
            case NextControl:
                pressVirtualKey(*ui, 'N');
                break;
            case HalfSpeedControl:
                pressVirtualKey(*ui, '5');
                break;
            case NormalSpeedControl:
                pressVirtualKey(*ui, '1');
                break;
            case DoubleSpeedControl:
                pressVirtualKey(*ui, '2');
                break;
            case MuteControl:
                pressVirtualKey(*ui, 'M');
                break;
            default:
                break;
            }
            SetFocus(windowHandle);
        }
        return 0;
    }

    if (message == WM_HSCROLL)
    {
        auto* ui = reinterpret_cast<WindowUi*>(GetWindowLongPtrW(
            windowHandle,
            GWLP_USERDATA));
        if (ui != nullptr && reinterpret_cast<HWND>(lParam) == ui->volume)
        {
            const LRESULT position = SendMessageW(
                ui->volume,
                TBM_GETPOS,
                0,
                0);
            ui->input.volume = static_cast<float>(position) / 100.0F;
            ui->input.volumeChanged = true;
            SetFocus(windowHandle);
            return 0;
        }
    }

    if (message == WM_SIZE)
    {
        auto* ui = reinterpret_cast<WindowUi*>(GetWindowLongPtrW(
            windowHandle,
            GWLP_USERDATA));
        if (ui != nullptr)
        {
            ui->width = static_cast<int>(LOWORD(lParam));
            ui->height = static_cast<int>(HIWORD(lParam));
            layoutControls(
                *ui,
                ui->width,
                ui->height);
        }
    }

    if (message == WM_ERASEBKGND)
    {
        return 1;
    }

    return DefWindowProcW(windowHandle, message, wParam, lParam);
}

}

namespace DesktopSceneEngine
{
namespace Platform
{
namespace Windows
{

struct DesktopWindowHost::Impl
{
    HINSTANCE instance = nullptr;
    HWND window = nullptr;
    std::wstring className{};
    bool classRegistered = false;
    bool ownsWindow = true;
    WindowUi ui{};
};

DesktopWindowHost::DesktopWindowHost() = default;

DesktopWindowHost::~DesktopWindowHost()
{
    shutdown();
}

void DesktopWindowHost::initialize()
{
    if (isInitialized())
    {
        return;
    }

    impl_ = std::make_unique<Impl>();
    impl_->instance = GetModuleHandleW(nullptr);
    impl_->className = L"DesktopSceneEngineWindow_"
        + std::to_wstring(reinterpret_cast<std::uintptr_t>(impl_.get()));

    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = windowProcedure;
    windowClass.hInstance = impl_->instance;
    windowClass.lpszClassName = impl_->className.c_str();

    if (RegisterClassW(&windowClass) == 0)
    {
        impl_.reset();
        return;
    }

    impl_->classRegistered = true;
    impl_->window = CreateWindowExW(
        0,
        impl_->className.c_str(),
        WindowTitle,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        impl_->ui.width,
        impl_->ui.height,
        nullptr,
        nullptr,
        impl_->instance,
        &impl_->ui
    );

    if (impl_->window == nullptr)
    {
        shutdown();
        return;
    }

    ShowWindow(impl_->window, SW_SHOW);
    UpdateWindow(impl_->window);
    createControls(impl_->window, impl_->instance, impl_->ui);
    layoutControls(impl_->ui, impl_->ui.width, impl_->ui.height);
    SetForegroundWindow(impl_->window);
    SetFocus(impl_->window);
}

void DesktopWindowHost::initializeExternal(void* handle)
{
    if (isInitialized() || handle == nullptr)
    {
        return;
    }

    impl_ = std::make_unique<Impl>();
    impl_->window = reinterpret_cast<HWND>(handle);
    impl_->ownsWindow = false;
    impl_->ui.hasControlBar = false;

    if (IsWindow(impl_->window) == FALSE)
    {
        impl_.reset();
    }
}

void DesktopWindowHost::shutdown() noexcept
{
    if (!impl_)
    {
        return;
    }

    if (impl_->ownsWindow && impl_->window != nullptr)
    {
        DestroyWindow(impl_->window);
    }

    impl_->window = nullptr;

    if (impl_->classRegistered)
    {
        UnregisterClassW(impl_->className.c_str(), impl_->instance);
        impl_->classRegistered = false;
    }

    impl_.reset();
}

bool DesktopWindowHost::isInitialized() const noexcept
{
    return impl_ != nullptr && impl_->window != nullptr;
}

Rendering::WindowTarget DesktopWindowHost::getWindowTarget() const noexcept
{
    if (!isInitialized())
    {
        return {};
    }

    Rendering::WindowTarget target{};
    target.handle = reinterpret_cast<void*>(impl_->window);
    target.bottomInsetPixels = impl_->ui.hasControlBar ? ControlBarHeight : 0;
    return target;
}

bool DesktopWindowHost::processMessages() noexcept
{
    if (!isInitialized())
    {
        return false;
    }

    if (!impl_->ownsWindow)
    {
        return true;
    }

    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != 0)
    {
        if (message.message == WM_QUIT)
        {
            return false;
        }

        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    if (impl_->window != nullptr && IsWindow(impl_->window) == FALSE)
    {
        impl_->window = nullptr;
        return false;
    }

    return true;
}

bool DesktopWindowHost::consumeKeyPress(unsigned int virtualKey) noexcept
{
    if (!isInitialized() || virtualKey >= 256)
    {
        return false;
    }

    const bool pressed = impl_->ui.input.pressed[virtualKey];
    impl_->ui.input.pressed[virtualKey] = false;
    return pressed;
}

bool DesktopWindowHost::consumeVolumeSlider(float& volume) noexcept
{
    if (!isInitialized() || !impl_->ui.input.volumeChanged)
    {
        return false;
    }

    volume = impl_->ui.input.volume;
    impl_->ui.input.volumeChanged = false;
    return true;
}

void DesktopWindowHost::setVolumeControl(float volume) noexcept
{
    if (!isInitialized() || impl_->ui.volume == nullptr)
    {
        return;
    }

    const float clamped = (std::max)(0.0F, (std::min)(1.0F, volume));
    const int position = static_cast<int>(std::lround(clamped * 100.0F));
    SendMessageW(impl_->ui.volume, TBM_SETPOS, TRUE, position);
    impl_->ui.input.volume = clamped;
    impl_->ui.input.volumeChanged = false;
}

void DesktopWindowHost::setMuteControl(bool muted) noexcept
{
    if (isInitialized() && impl_->ui.mute != nullptr)
    {
        SetWindowTextW(impl_->ui.mute, muted ? L"Unmute" : L"Mute");
    }
}

void DesktopWindowHost::setStatusText(const std::string& text) noexcept
{
    if (!isInitialized() || impl_->ui.status == nullptr)
    {
        return;
    }

    const int required = MultiByteToWideChar(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0);
    if (required <= 0)
    {
        SetWindowTextW(impl_->ui.status, L"");
        return;
    }

    std::wstring wide(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        wide.data(),
        required);
    SetWindowTextW(impl_->ui.status, wide.c_str());
}

}
}
}
