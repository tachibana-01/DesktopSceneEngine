#pragma once

#include <DesktopSceneEngine/PresentationTarget.h>

namespace DesktopSceneEngine::Rendering
{

// Window transports borrow a native window that must outlive their use.
// Hosted content instead uses an explicit allocation extent and no HWND.
struct WindowTarget
{
    void* handle = nullptr;
    int bottomInsetPixels = 0;
    PresentationTransport transport = PresentationTransport::HwndSwapChain;
    PresentationExtent allocationExtent{};
};

}
