#pragma once

#include <DesktopSceneEngine/Platform/DesktopPresentationHost.h>
#include <DesktopSceneEngine/Platform/Display.h>
#include <Windows.h>
#include <string>

namespace DesktopSceneEngine::Platform::Windows
{
// Private deterministic Win32 error-path control; production has no injected
// inputs. Fixtures borrow ONLY test-owned shell-shaped windows, not Explorer.
enum class DesktopHostFailurePoint
{
    None, CreateWindow, Layering, ChildStyle, Show, Parenting, Positioning, Verification
};

struct DesktopHostCreationTrace
{
    HWND createdWindow = nullptr;
    std::wstring className;
    bool parentChanged = false;
    bool shown = false;
    bool published = false;
};

struct DesktopPresentationHostTestAccess
{
    static DesktopPresentationHostStatus create(DesktopPresentationHost& host,
        const DisplayDescriptor& display, HWND borrowedRoot,
        DesktopHostFailurePoint failure, DesktopHostCreationTrace& trace);
    static DesktopPresentationHostStatus createAfterAuthoritativeRootChange(
        DesktopPresentationHost& host, const DisplayDescriptor& display,
        HWND borrowedRoot, HWND currentProgman, HWND currentShell,
        DesktopHostCreationTrace& trace);
    static DesktopPresentationHostAttachmentStatus reconcile(
        DesktopPresentationHost& host, const DisplayDescriptor& display,
        HWND borrowedRoot);
    static DesktopPresentationHostAttachmentStatus reconcileAfterAuthoritativeRootChange(
        DesktopPresentationHost& host, const DisplayDescriptor& display,
        HWND borrowedRoot, HWND currentProgman, HWND currentShell);
    static DesktopPresentationHostAttachmentStatus reconcileAfterObservedChildDestruction(
        DesktopPresentationHost& host, const DisplayDescriptor& display,
        HWND borrowedRoot, HWND testOwnedChild);
};
}
