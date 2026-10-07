#include <DesktopSceneEngine/Platform/DesktopPresentationHost.h>
#include <DesktopSceneEngine/Platform/Display.h>
#include "DesktopShellTopology.h"
#include "DesktopPresentationHostTestAccess.h"

#include <Windows.h>
#include <cwchar>
#include <exception>
#include <optional>
#include <string>
#include <utility>

namespace DesktopSceneEngine::Platform
{
namespace
{
using namespace Windows;

HWND window(std::uintptr_t value) noexcept { return reinterpret_cast<HWND>(value); }
std::uintptr_t value(HWND handle) noexcept { return reinterpret_cast<std::uintptr_t>(handle); }

struct ShellRootObservation
{
    HWND progman = nullptr;
    HWND shell = nullptr;
};

ShellRootObservation observeAuthoritativeShellRoots() noexcept
{
    return {FindWindowW(L"Progman", nullptr), GetShellWindow()};
}

enum class HostVerificationResult
{
    Ready,
    TransientlyUnavailable,
    Rejected
};

bool hasClass(HWND handle, const wchar_t* name) noexcept
{
    wchar_t actual[64]{};
    return handle && IsWindow(handle) && GetClassNameW(handle, actual, 64) > 0
        && std::wcscmp(actual, name) == 0;
}

bool above(HWND first, HWND second) noexcept
{
    if (!first || !second || first == second || GetParent(first) != GetParent(second)) return false;
    unsigned int remaining = 1024; // Fail closed if concurrent shell reordering makes traversal cycle.
    for (HWND next = GetWindow(first, GW_HWNDNEXT); next && remaining--; next = GetWindow(next, GW_HWNDNEXT))
        if (next == second) return true;
    return false;
}

// Only direct siblings in the evidenced raised shape are candidates. Multiple
// matching children are ambiguous, not a license to choose an arbitrary WorkerW.
HWND uniqueChild(HWND parent, const wchar_t* name, bool& unambiguous) noexcept
{
    HWND match = nullptr;
    unsigned int remaining = 1024;
    HWND child = GetWindow(parent, GW_CHILD);
    for (; child && remaining; child = GetWindow(child, GW_HWNDNEXT), --remaining)
    {
        if (!hasClass(child, name)) continue;
        if (match) { unambiguous = false; return nullptr; }
        match = child;
    }
    if (child) unambiguous = false;
    return match;
}

DesktopShellFacts observeShell(HWND root) noexcept
{
    DesktopShellFacts facts;
    facts.progman = value(root);
    if (!hasClass(root, L"Progman")) return facts;
    bool unambiguous = true;
    const HWND defView = uniqueChild(root, L"SHELLDLL_DefView", unambiguous);
    const HWND worker = uniqueChild(root, L"WorkerW", unambiguous);
    const HWND icons = defView ? uniqueChild(defView, L"SysListView32", unambiguous) : nullptr;
    facts.defView = value(defView); facts.worker = value(worker); facts.icons = value(icons);
    facts.defViewParent = value(defView ? GetParent(defView) : nullptr);
    facts.workerParent = value(worker ? GetParent(worker) : nullptr);
    facts.iconsParent = value(icons ? GetParent(icons) : nullptr);
    DWORD process = 0;
    GetWindowThreadProcessId(root, &process);
    facts.classesAndProcessMatch = unambiguous && process != 0;
    for (const HWND child : {defView, icons, worker})
    {
        if (!child) continue;
        DWORD childProcess = 0;
        GetWindowThreadProcessId(child, &childProcess);
        facts.classesAndProcessMatch = facts.classesAndProcessMatch && childProcess == process;
    }
    facts.defViewLayered = defView && (GetWindowLongPtrW(defView, GWL_EXSTYLE) & WS_EX_LAYERED) != 0;
    facts.defViewAboveWorker = above(defView, worker);
    return facts;
}

class ScopedDpiContext
{
public:
    explicit ScopedDpiContext(HWND parent) noexcept
    {
        const auto context = GetWindowDpiAwarenessContext(parent);
        // The physical-pixel placement contract needs an unvirtualized parent.
        // Unknown/system/unaware shells are unavailable, not guessed DPR math.
        if (GetAwarenessFromDpiAwarenessContext(context) == DPI_AWARENESS_PER_MONITOR_AWARE)
            previous_ = SetThreadDpiAwarenessContext(context);
    }
    ~ScopedDpiContext() { if (previous_) (void)SetThreadDpiAwarenessContext(previous_); }
    bool valid() const noexcept { return previous_ != nullptr; }
private:
    DPI_AWARENESS_CONTEXT previous_ = nullptr;
};

bool setChildStyle(HWND host) noexcept
{
    SetLastError(ERROR_SUCCESS);
    const LONG_PTR previous = SetWindowLongPtrW(host, GWL_STYLE, WS_CHILD | WS_CLIPSIBLINGS);
    return previous != 0 || GetLastError() == ERROR_SUCCESS;
}

HostVerificationResult verifyHost(HWND host, const DesktopShellFacts& expected,
    const DisplayDescriptor& display,
    const std::optional<ShellRootObservation>& finalShellRootsForTesting) noexcept
{
    const HWND root = window(expected.progman);
    const auto result = [&](bool verified) noexcept
    {
        // This is the final identity-bearing observation before Ready. The test
        // override supplies native facts only; production always queries Windows.
        const ShellRootObservation authoritative = finalShellRootsForTesting
            ? *finalShellRootsForTesting : observeAuthoritativeShellRoots();
        if (authoritative.progman != root || authoritative.shell != root)
            return HostVerificationResult::TransientlyUnavailable;
        return verified ? HostVerificationResult::Ready : HostVerificationResult::Rejected;
    };
    const DesktopShellFacts fresh = observeShell(root);
    if (classifyDesktopShell(fresh) != DesktopShellKind::SupportedRaised
        || fresh.defView != expected.defView || fresh.icons != expected.icons || fresh.worker != expected.worker
        || !IsWindow(host) || GetParent(host) != root || !IsWindowVisible(host)
        || !IsWindowVisible(window(fresh.defView)) || !IsWindowVisible(window(fresh.icons))
        || !above(window(fresh.defView), host) || !above(host, window(fresh.worker))
        || (GetWindowLongPtrW(host, GWL_STYLE) & (WS_CHILD | WS_POPUP)) != WS_CHILD
        || (GetWindowLongPtrW(host, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0
        || !AreDpiAwarenessContextsEqual(GetWindowDpiAwarenessContext(host), GetWindowDpiAwarenessContext(root)))
        return result(false);
    BYTE alpha = 0; DWORD flags = 0;
    if (!GetLayeredWindowAttributes(host, nullptr, &alpha, &flags) || alpha != 255 || flags != LWA_ALPHA)
        return result(false);
    RECT screen{}, client{};
    return result(GetWindowRect(host, &screen) && GetClientRect(host, &client)
        && screen.left == display.desktopX && screen.top == display.desktopY
        && std::int64_t(screen.right) == std::int64_t(display.desktopX) + display.pixelWidth
        && std::int64_t(screen.bottom) == std::int64_t(display.desktopY) + display.pixelHeight
        && client.left == 0 && client.top == 0
        && client.right == display.pixelWidth && client.bottom == display.pixelHeight
        && GetForegroundWindow() != host && GetActiveWindow() != host);
}
}

struct DesktopPresentationHost::Impl
{
    HINSTANCE instance = GetModuleHandleW(nullptr);
    DWORD thread = GetCurrentThreadId();
    HWND host = nullptr;
    std::wstring className;
    bool registered = false;
    bool externalDestructionNotificationArmed = false;
    std::function<void()>* externalDestructionCallback = nullptr;

    static LRESULT CALLBACK procedure(HWND handle, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_NCCREATE)
        {
            auto* state = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
            SetWindowLongPtrW(handle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
            state->host = handle;
        }
        if (message == WM_NCDESTROY)
        {
            auto* state = reinterpret_cast<Impl*>(GetWindowLongPtrW(handle, GWLP_USERDATA));
            const bool notify = state && state->host == handle
                && state->externalDestructionNotificationArmed;
            if (state && state->host == handle)
            {
                state->host = nullptr;
                state->externalDestructionNotificationArmed = false;
            }
            SetWindowLongPtrW(handle, GWLP_USERDATA, 0);
            if (notify && state->externalDestructionCallback
                && *state->externalDestructionCallback)
            {
                // Native callbacks must not let an exception cross WndProc.
                try { (*state->externalDestructionCallback)(); }
                catch (...) {}
            }
        }
        if (message == WM_NCHITTEST) return HTTRANSPARENT;
        if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
        if (message == WM_ERASEBKGND) return 1;
        // No PostQuitMessage: destroying a borrowed presentation window must
        // not terminate its owner's existing control-thread message loop.
        return DefWindowProcW(handle, message, wParam, lParam);
    }

    bool reset() noexcept
    {
        if (GetCurrentThreadId() != thread) return false;
        // WM_NCDESTROY cleared host even on unexpected external destruction,
        // so a recycled numeric HWND can never be destroyed by this owner.
        const bool wasArmed = externalDestructionNotificationArmed;
        externalDestructionNotificationArmed = false;
        if (host && !DestroyWindow(host))
        {
            if (host) externalDestructionNotificationArmed = wasArmed;
            return false;
        }
        if (registered && !UnregisterClassW(className.c_str(), instance)) return false;
        registered = false;
        return true;
    }
    ~Impl() { if (!reset()) std::terminate(); }
};

struct DesktopPresentationHost::CreateInputs
{
    const DisplayDescriptor& display;
    DesktopShellFacts shell;
    DesktopHostFailurePoint failure = DesktopHostFailurePoint::None;
    DesktopHostCreationTrace* trace = nullptr;
    std::optional<ShellRootObservation> finalShellRootsForTesting{};
};

struct DesktopPresentationHost::ReconcileInputs
{
    const DisplayDescriptor& display;
    DesktopShellFacts shell;
    std::optional<ShellRootObservation> finalShellRootsForTesting{};
};

DesktopPresentationHost::DesktopPresentationHost() = default;
DesktopPresentationHost::~DesktopPresentationHost() = default;

bool DesktopPresentationHost::isValid() const noexcept
{ return impl_ && impl_->thread == GetCurrentThreadId() && impl_->host != nullptr && IsWindow(impl_->host); }
void* DesktopPresentationHost::nativeHandle() const noexcept
{ return isValid() ? impl_->host : nullptr; }
bool DesktopPresentationHost::destroy() noexcept
{
    if (impl_ && !impl_->reset()) return false;
    impl_.reset();
    return true;
}

void DesktopPresentationHost::setExternalDestructionCallback(std::function<void()> callback)
{
    externalDestructionCallback_ = std::move(callback);
    if (impl_)
    {
        impl_->externalDestructionCallback = &externalDestructionCallback_;
        impl_->externalDestructionNotificationArmed = impl_->host != nullptr
            && static_cast<bool>(externalDestructionCallback_);
    }
}

DesktopPresentationHostStatus DesktopPresentationHost::create(const DisplayId& boundDisplay)
{
    if (impl_ && impl_->thread != GetCurrentThreadId()) return DesktopPresentationHostStatus::NativeFailure;
    if (isValid()) return DesktopPresentationHostStatus::HostAlreadyExists;
    if (!destroy()) return DesktopPresentationHostStatus::NativeFailure;
    // Resolve identity BEFORE any shell side effects. Neither current cursor,
    // primary display, nor display numbering can redirect this request.
    auto displays = enumerateActiveDisplays();
    if (displays.status != DisplayEnumerationStatus::Success) return DesktopPresentationHostStatus::DisplayUnavailable;
    auto display = displays.snapshot.resolve(boundDisplay);
    if (!display) return DesktopPresentationHostStatus::DisplayUnavailable;
    HWND root = FindWindowW(L"Progman", nullptr);
    if (!root || root != GetShellWindow()) return DesktopPresentationHostStatus::TransientlyUnavailable;
    auto shell = observeShell(root);
    if (canPrepareRaisedDesktop(shell))
    {
        DWORD_PTR result = 0;
        // Exactly one bounded operation, never SMTO_NOTIMEOUTIFNOTHUNG/retry.
        if (!SendMessageTimeoutW(root, 0x052C, 0xD, 0x1,
            SMTO_ABORTIFHUNG | SMTO_BLOCK | SMTO_ERRORONEXIT, 1000, &result))
            return DesktopPresentationHostStatus::TransientlyUnavailable;
        root = FindWindowW(L"Progman", nullptr);
        if (!root || root != GetShellWindow()) return DesktopPresentationHostStatus::TransientlyUnavailable;
        shell = observeShell(root); // All pre-message sibling handles discarded.
        displays = enumerateActiveDisplays();
        if (displays.status != DisplayEnumerationStatus::Success) return DesktopPresentationHostStatus::DisplayUnavailable;
        display = displays.snapshot.resolve(boundDisplay);
        if (!display) return DesktopPresentationHostStatus::DisplayUnavailable;
    }
    return createResolved(CreateInputs{*display, shell});
}

DesktopPresentationHostStatus DesktopPresentationHost::createResolved(const CreateInputs& inputs)
{
    if (impl_ && impl_->thread != GetCurrentThreadId()) return DesktopPresentationHostStatus::NativeFailure;
    if (isValid()) return DesktopPresentationHostStatus::HostAlreadyExists;
    if (!destroy()) return DesktopPresentationHostStatus::NativeFailure;
    const auto shellKind = classifyDesktopShell(inputs.shell);
    if (shellKind == DesktopShellKind::Incomplete)
        return DesktopPresentationHostStatus::TransientlyUnavailable;
    if (shellKind != DesktopShellKind::SupportedRaised)
        return DesktopPresentationHostStatus::UnsupportedShellTopology;
    const HWND root = window(inputs.shell.progman);
    ScopedDpiContext dpi(root);
    if (!dpi.valid() || (GetWindowLongPtrW(root, GWL_EXSTYLE) & WS_EX_LAYOUTRTL) != 0)
        return DesktopPresentationHostStatus::UnsupportedShellTopology;
    POINT origin{};
    if (!ClientToScreen(root, &origin)) return DesktopPresentationHostStatus::NativeFailure;
    const auto placement = deriveDesktopPlacement(inputs.display, origin.x, origin.y);
    if (!placement) return DesktopPresentationHostStatus::NativeFailure;
    // Map both real rectangle corners as well as the pure arithmetic check.
    RECT mapped{inputs.display.desktopX, inputs.display.desktopY,
        inputs.display.desktopX + inputs.display.pixelWidth, inputs.display.desktopY + inputs.display.pixelHeight};
    SetLastError(ERROR_SUCCESS);
    if (!MapWindowPoints(nullptr, root, reinterpret_cast<POINT*>(&mapped), 2) && GetLastError() != ERROR_SUCCESS)
        return DesktopPresentationHostStatus::NativeFailure;
    if (mapped.left != placement->x || mapped.top != placement->y
        || std::int64_t(mapped.right) - mapped.left != placement->width
        || std::int64_t(mapped.bottom) - mapped.top != placement->height) return DesktopPresentationHostStatus::NativeFailure;

    auto candidate = std::make_unique<Impl>();
    candidate->className = L"DesktopSceneEngineDesktopPresentation_"
        + std::to_wstring(reinterpret_cast<std::uintptr_t>(candidate.get()));
    if (inputs.trace) inputs.trace->className = candidate->className;
    WNDCLASSW type{};
    type.lpfnWndProc = Impl::procedure; type.hInstance = candidate->instance;
    type.lpszClassName = candidate->className.c_str();
    if (!candidate->instance || !RegisterClassW(&type)) return DesktopPresentationHostStatus::NativeFailure;
    candidate->registered = true;
    const auto fails = [&](DesktopHostFailurePoint stage) { return inputs.failure == stage; };
    if (fails(DesktopHostFailurePoint::CreateWindow)) return DesktopPresentationHostStatus::NativeFailure;
    const HWND host = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TRANSPARENT,
        candidate->className.c_str(), L"Desktop Scene Engine Desktop Presentation", WS_POPUP,
        inputs.display.desktopX, inputs.display.desktopY, placement->width, placement->height,
        nullptr, nullptr, candidate->instance, candidate.get());
    if (inputs.trace) inputs.trace->createdWindow = host;
    if (!host) return DesktopPresentationHostStatus::NativeFailure;
    if (fails(DesktopHostFailurePoint::Layering) || !SetLayeredWindowAttributes(host, 0, 255, LWA_ALPHA))
        return DesktopPresentationHostStatus::NativeFailure;
    if (fails(DesktopHostFailurePoint::ChildStyle) || !setChildStyle(host)) return DesktopPresentationHostStatus::NativeFailure;
    if (fails(DesktopHostFailurePoint::Show)) return DesktopPresentationHostStatus::NativeFailure;
    (void)ShowWindow(host, SW_SHOWNOACTIVATE); // Return is previous visibility, NOT success.
    if (inputs.trace) inputs.trace->shown = true;
    if (fails(DesktopHostFailurePoint::Parenting)) return DesktopPresentationHostStatus::NativeFailure;
    SetLastError(ERROR_SUCCESS);
    const HWND previousParent = SetParent(host, root);
    if (!previousParent && GetLastError() != ERROR_SUCCESS) return DesktopPresentationHostStatus::NativeFailure;
    if (inputs.trace) inputs.trace->parentChanged = GetParent(host) == root;
    if (GetParent(host) != root || fails(DesktopHostFailurePoint::Positioning)
        || !SetWindowPos(host, window(inputs.shell.defView), placement->x, placement->y,
            placement->width, placement->height, SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW))
        return DesktopPresentationHostStatus::NativeFailure;
    if (fails(DesktopHostFailurePoint::Verification))
        return DesktopPresentationHostStatus::NativeFailure;
    const auto verification = verifyHost(
        host, inputs.shell, inputs.display, inputs.finalShellRootsForTesting);
    if (verification == HostVerificationResult::TransientlyUnavailable)
        return DesktopPresentationHostStatus::TransientlyUnavailable;
    if (verification != HostVerificationResult::Ready)
        return DesktopPresentationHostStatus::NativeFailure;
    candidate->externalDestructionCallback = &externalDestructionCallback_;
    candidate->externalDestructionNotificationArmed =
        static_cast<bool>(externalDestructionCallback_);
    impl_ = std::move(candidate); // Only a complete verified host becomes public.
    if (inputs.trace) inputs.trace->published = true;
    return DesktopPresentationHostStatus::Ready;
}

DesktopPresentationHostAttachmentStatus DesktopPresentationHost::reconcileAttachment(
    const DisplayId& boundDisplay)
{
    if (impl_ && impl_->thread != GetCurrentThreadId())
        return DesktopPresentationHostAttachmentStatus::NativeFailure;
    if (!impl_ || !impl_->host || !IsWindow(impl_->host))
        return DesktopPresentationHostAttachmentStatus::ReplacementRequired;
    const auto displays = enumerateActiveDisplays();
    if (displays.status != DisplayEnumerationStatus::Success)
        return DesktopPresentationHostAttachmentStatus::TransientlyUnavailable;
    const auto display = displays.snapshot.resolve(boundDisplay);
    if (!display) return DesktopPresentationHostAttachmentStatus::TransientlyUnavailable;
    const HWND root = FindWindowW(L"Progman", nullptr);
    if (!root || root != GetShellWindow())
        return DesktopPresentationHostAttachmentStatus::TransientlyUnavailable;
    return reconcileResolved(ReconcileInputs{*display, observeShell(root)});
}

DesktopPresentationHostAttachmentStatus DesktopPresentationHost::reconcileResolved(
    const ReconcileInputs& inputs)
{
    using Status = DesktopPresentationHostAttachmentStatus;
    if (impl_ && impl_->thread != GetCurrentThreadId()) return Status::NativeFailure;
    if (!impl_ || !impl_->host || !IsWindow(impl_->host)) return Status::ReplacementRequired;
    const auto shellKind = classifyDesktopShell(inputs.shell);
    if (shellKind == DesktopShellKind::Incomplete) return Status::TransientlyUnavailable;
    if (shellKind != DesktopShellKind::SupportedRaised) return Status::StructurallyUnsupported;
    const HWND root = window(inputs.shell.progman);
    const HWND host = impl_->host;
    if (!root || GetParent(host) != root) return Status::ReplacementRequired;
    if ((GetWindowLongPtrW(host, GWL_STYLE) & (WS_CHILD | WS_POPUP)) != WS_CHILD
        || (GetWindowLongPtrW(host, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0
        || !AreDpiAwarenessContextsEqual(
            GetWindowDpiAwarenessContext(host), GetWindowDpiAwarenessContext(root))
        || GetForegroundWindow() == host || GetActiveWindow() == host)
        return Status::ReplacementRequired;
    BYTE alpha = 0;
    DWORD flags = 0;
    if (!GetLayeredWindowAttributes(host, nullptr, &alpha, &flags)
        || alpha != 255 || flags != LWA_ALPHA)
        return Status::ReplacementRequired;
    auto verification = verifyHost(
        host, inputs.shell, inputs.display, inputs.finalShellRootsForTesting);
    if (verification == HostVerificationResult::Ready) return Status::Ready;
    if (verification == HostVerificationResult::TransientlyUnavailable)
        return Status::TransientlyUnavailable;

    ScopedDpiContext dpi(root);
    if (!dpi.valid() || (GetWindowLongPtrW(root, GWL_EXSTYLE) & WS_EX_LAYOUTRTL) != 0)
        return Status::StructurallyUnsupported;
    POINT origin{};
    if (!ClientToScreen(root, &origin)) return Status::NativeFailure;
    const auto placement = deriveDesktopPlacement(inputs.display, origin.x, origin.y);
    if (!placement) return Status::NativeFailure;
    (void)ShowWindow(host, SW_SHOWNOACTIVATE);
    if (!SetWindowPos(host, window(inputs.shell.defView), placement->x, placement->y,
            placement->width, placement->height,
            SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW))
        return Status::NativeFailure;
    verification = verifyHost(
        host, inputs.shell, inputs.display, inputs.finalShellRootsForTesting);
    if (verification == HostVerificationResult::TransientlyUnavailable)
        return Status::TransientlyUnavailable;
    return verification == HostVerificationResult::Ready ? Status::Ready : Status::NativeFailure;
}

DesktopPresentationHostStatus Windows::DesktopPresentationHostTestAccess::create(
    DesktopPresentationHost& host, const DisplayDescriptor& display, HWND borrowedRoot,
    DesktopHostFailurePoint failure, DesktopHostCreationTrace& trace)
{
    return host.createResolved(DesktopPresentationHost::CreateInputs{
        display, observeShell(borrowedRoot), failure, &trace,
        ShellRootObservation{borrowedRoot, borrowedRoot}});
}

DesktopPresentationHostStatus
Windows::DesktopPresentationHostTestAccess::createAfterAuthoritativeRootChange(
    DesktopPresentationHost& host, const DisplayDescriptor& display,
    HWND borrowedRoot, HWND currentProgman, HWND currentShell,
    DesktopHostCreationTrace& trace)
{
    return host.createResolved(DesktopPresentationHost::CreateInputs{
        display, observeShell(borrowedRoot), DesktopHostFailurePoint::None, &trace,
        ShellRootObservation{currentProgman, currentShell}});
}

DesktopPresentationHostAttachmentStatus Windows::DesktopPresentationHostTestAccess::reconcile(
    DesktopPresentationHost& host, const DisplayDescriptor& display, HWND borrowedRoot)
{
    return host.reconcileResolved(
        DesktopPresentationHost::ReconcileInputs{display, observeShell(borrowedRoot),
            ShellRootObservation{borrowedRoot, borrowedRoot}});
}

DesktopPresentationHostAttachmentStatus
Windows::DesktopPresentationHostTestAccess::reconcileAfterAuthoritativeRootChange(
    DesktopPresentationHost& host, const DisplayDescriptor& display,
    HWND borrowedRoot, HWND currentProgman, HWND currentShell)
{
    return host.reconcileResolved(
        DesktopPresentationHost::ReconcileInputs{display, observeShell(borrowedRoot),
            ShellRootObservation{currentProgman, currentShell}});
}

DesktopPresentationHostAttachmentStatus
Windows::DesktopPresentationHostTestAccess::reconcileAfterObservedChildDestruction(
    DesktopPresentationHost& host, const DisplayDescriptor& display,
    HWND borrowedRoot, HWND testOwnedChild)
{
    const auto observed = observeShell(borrowedRoot);
    if (testOwnedChild) (void)DestroyWindow(testOwnedChild);
    return host.reconcileResolved(
        DesktopPresentationHost::ReconcileInputs{display, observed,
            ShellRootObservation{borrowedRoot, borrowedRoot}});
}
}
