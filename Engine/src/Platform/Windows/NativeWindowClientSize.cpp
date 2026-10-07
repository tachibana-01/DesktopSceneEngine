#include "NativeWindowClientSize.h"

#include <Windows.h>

namespace DesktopSceneEngine::Platform::Windows
{

std::optional<NativeClientSize> queryNativeWindowClientSize(
    void* nativeHandle
) noexcept
{
    const HWND window = reinterpret_cast<HWND>(nativeHandle);
    if (window == nullptr || IsWindow(window) == FALSE)
    {
        return std::nullopt;
    }

    RECT clientRectangle{};
    if (GetClientRect(window, &clientRectangle) == FALSE)
    {
        return std::nullopt;
    }

    const int width = clientRectangle.right - clientRectangle.left;
    const int height = clientRectangle.bottom - clientRectangle.top;
    if (width < 0 || height < 0)
    {
        return std::nullopt;
    }

    return NativeClientSize{width, height};
}

}
