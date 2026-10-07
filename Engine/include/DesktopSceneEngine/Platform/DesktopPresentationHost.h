#pragma once

#include <DesktopSceneEngine/DisplayIdentity.h>
#include <functional>
#include <memory>

namespace DesktopSceneEngine::Platform::Windows
{
struct DesktopPresentationHostTestAccess;
}

namespace DesktopSceneEngine::Platform
{

enum class DesktopPresentationHostStatus
{
    Ready,
    DisplayUnavailable,
    UnsupportedShellTopology,
    NativeFailure,
    HostAlreadyExists,
    TransientlyUnavailable
};

enum class DesktopPresentationHostAttachmentStatus
{
    Ready,
    TransientlyUnavailable,
    StructurallyUnsupported,
    ReplacementRequired,
    NativeFailure
};

// Owns one non-activating native desktop HWND, never graphics resources.
// Windows 11 raised shell only; executable must declare modern Windows
// compatibility for layered child windows. Requires a per-monitor-aware shell;
// creation temporarily matches its DPI context and restores the calling thread.
// Create, query, destroy, and object destruction belong to the creating thread.
// Rendering borrows nativeHandle(): detach its presentation target BEFORE
// destroy() or object destruction. A live host is never implicitly replaced.
class DesktopPresentationHost final
{
public:
    DesktopPresentationHost();
    ~DesktopPresentationHost();
    DesktopPresentationHost(const DesktopPresentationHost&) = delete;
    DesktopPresentationHost& operator=(const DesktopPresentationHost&) = delete;
    DesktopPresentationHost(DesktopPresentationHost&&) = delete;
    DesktopPresentationHost& operator=(DesktopPresentationHost&&) = delete;

    [[nodiscard]] DesktopPresentationHostStatus create(const DisplayId& boundDisplay);
    // Re-observes current display/Shell truth. Placement and sibling-order
    // drift are repaired in place; identity/ownership drift requests a fresh
    // endpoint from the caller. No graphics resource is touched here.
    [[nodiscard]] DesktopPresentationHostAttachmentStatus reconcileAttachment(
        const DisplayId& boundDisplay);
    // Delivered once when a published HWND is destroyed outside destroy().
    // The callback runs on the HWND owner thread after native ownership clears.
    void setExternalDestructionCallback(std::function<void()> callback);
    [[nodiscard]] bool isValid() const noexcept;
    [[nodiscard]] void* nativeHandle() const noexcept;
    // False on wrong thread/native destruction failure: ownership is retained.
    // The destructor cannot safely abandon a live HWND and terminates on such
    // a caller contract violation. Empty/double destruction succeeds.
    [[nodiscard]] bool destroy() noexcept;

private:
    struct Impl;
    struct CreateInputs;
    struct ReconcileInputs;
    [[nodiscard]] DesktopPresentationHostStatus createResolved(const CreateInputs& inputs);
    [[nodiscard]] DesktopPresentationHostAttachmentStatus reconcileResolved(
        const ReconcileInputs& inputs);
    std::function<void()> externalDestructionCallback_{};
    std::unique_ptr<Impl> impl_{};
    friend struct Windows::DesktopPresentationHostTestAccess;
};

}
