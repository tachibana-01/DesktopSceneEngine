#include "Platform/Windows/NativeWindowClientSize.h"

#include <Windows.h>

#include <iostream>

namespace
{

class TestWindow final
{
public:

    TestWindow()
        : instance_(GetModuleHandleW(nullptr))
    {
        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = DefWindowProcW;
        windowClass.hInstance = instance_;
        windowClass.lpszClassName = ClassName;
        classRegistered_ = RegisterClassW(&windowClass) != 0
            || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
        if (classRegistered_)
        {
            window_ = CreateWindowExW(
                0,
                ClassName,
                L"DesktopSceneEngine native size test",
                WS_OVERLAPPEDWINDOW,
                CW_USEDEFAULT,
                CW_USEDEFAULT,
                320,
                240,
                nullptr,
                nullptr,
                instance_,
                nullptr);
        }
    }

    ~TestWindow()
    {
        if (window_ != nullptr)
        {
            DestroyWindow(window_);
        }

        if (classRegistered_)
        {
            UnregisterClassW(ClassName, instance_);
        }
    }

    TestWindow(const TestWindow&) = delete;
    TestWindow& operator=(const TestWindow&) = delete;

    [[nodiscard]] HWND handle() const noexcept
    {
        return window_;
    }

    [[nodiscard]] bool resizeClient(int width, int height) const noexcept
    {
        RECT rectangle{0, 0, width, height};
        if (window_ == nullptr
            || AdjustWindowRectEx(
                &rectangle,
                WS_OVERLAPPEDWINDOW,
                FALSE,
                0) == FALSE)
        {
            return false;
        }

        return SetWindowPos(
            window_,
            nullptr,
            0,
            0,
            rectangle.right - rectangle.left,
            rectangle.bottom - rectangle.top,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != FALSE;
    }

    void destroy() noexcept
    {
        if (window_ != nullptr)
        {
            DestroyWindow(window_);
            window_ = nullptr;
        }
    }

private:

    static constexpr wchar_t ClassName[] =
        L"DesktopSceneEngineNativeWindowClientSizeTestWindow";

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    bool classRegistered_ = false;
};

bool require(bool condition, const char* message)
{
    if (condition)
    {
        return true;
    }

    std::cerr << "Test failed: " << message << std::endl;
    return false;
}

bool verifyNativeClientSizeQuery()
{
    using DesktopSceneEngine::Platform::Windows::NativeClientSize;
    using DesktopSceneEngine::Platform::Windows::queryNativeWindowClientSize;

    if (!require(!queryNativeWindowClientSize(nullptr).has_value(),
            "null native handle must fail"))
    {
        return false;
    }

    TestWindow window{};
    if (!require(window.handle() != nullptr, "test HWND should be created")
        || !require(window.resizeClient(377, 211), "test HWND should resize"))
    {
        return false;
    }

    const auto initial = queryNativeWindowClientSize(
        reinterpret_cast<void*>(window.handle()));
    if (!require(initial == NativeClientSize{377, 211},
            "query must return exact GetClientRect dimensions")
        || !require(window.resizeClient(541, 307), "test HWND should resize again"))
    {
        return false;
    }

    const auto resized = queryNativeWindowClientSize(
        reinterpret_cast<void*>(window.handle()));
    if (!require(resized == NativeClientSize{541, 307},
            "query must reflect current native client dimensions"))
    {
        return false;
    }

    void* const destroyedHandle = reinterpret_cast<void*>(window.handle());
    window.destroy();
    return require(!queryNativeWindowClientSize(destroyedHandle).has_value(),
        "destroyed native handle must fail");
}

}

int main()
{
    return verifyNativeClientSizeQuery() ? 0 : 1;
}
