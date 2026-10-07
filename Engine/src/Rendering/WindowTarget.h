#pragma once

#include <DesktopSceneEngine/PresentationTarget.h>

namespace DesktopSceneEngine::Rendering
{

// Non-owning internal presentation target. The referenced native window must outlive its use.
struct WindowTarget
{
    void* handle = nullptr;
    int bottomInsetPixels = 0;
    PresentationTransport transport = PresentationTransport::HwndSwapChain;
};

}
