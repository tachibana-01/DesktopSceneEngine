#include <DesktopSceneEngine/Platform/DesktopPresentationHost.h>
#include "Platform/Windows/DesktopPresentationHostTestAccess.h"

#include <Windows.h>
#include <iostream>
#include <vector>

using namespace DesktopSceneEngine;
using namespace DesktopSceneEngine::Platform;
using namespace DesktopSceneEngine::Platform::Windows;

namespace
{
bool require(bool value, const char* label)
{ if (!value) std::cerr << "Test failed: " << label << '\n'; return value; }

class ShellFixture
{
public:
    ShellFixture()
    {
        for (const auto* name : {L"Progman", L"SHELLDLL_DefView", L"SysListView32", L"WorkerW"})
        {
            WNDCLASSW type{}; type.lpfnWndProc = DefWindowProcW;
            type.hInstance = GetModuleHandleW(nullptr); type.lpszClassName = name;
            const auto atom = RegisterClassW(&type);
            if (atom) registeredClasses.push_back(name);
            valid = valid && atom != 0;
        }
        if (!valid) return;
        root = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"Progman",
            L"DSE test-owned shell fixture", WS_POPUP, 100, 100, 600, 400,
            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        worker = child(L"WorkerW", root, 0);
        defView = child(L"SHELLDLL_DefView", root, WS_EX_LAYERED);
        icons = child(L"SysListView32", defView, 0);
        valid = root && worker && defView && icons
            && SetLayeredWindowAttributes(defView, 0, 255, LWA_ALPHA)
            && SetWindowPos(defView, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        ShowWindow(root, SW_SHOWNOACTIVATE);
    }
    ~ShellFixture()
    {
        for (const HWND additional : additionalRoots)
            if (IsWindow(additional)) DestroyWindow(additional);
        if (root) DestroyWindow(root); // All of these are test-created, never Explorer windows.
        for (const auto* name : registeredClasses) UnregisterClassW(name, GetModuleHandleW(nullptr));
    }
    ShellFixture(const ShellFixture&) = delete;
    ShellFixture& operator=(const ShellFixture&) = delete;
    bool intact() const { return IsWindow(root) && IsWindow(defView) && IsWindow(icons) && IsWindow(worker); }
    HWND createAdditionalRoot()
    {
        const HWND additional = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
            L"Progman", L"DSE test-owned replacement shell root", WS_POPUP,
            710, 100, 600, 400, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (additional) additionalRoots.push_back(additional);
        return additional;
    }
    void removeWorker() { if (worker) DestroyWindow(worker); worker = nullptr; }
    bool restoreWorker()
    {
        worker = child(L"WorkerW", root, 0);
        return worker && SetWindowPos(defView, HWND_TOP, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    HWND root = nullptr, defView = nullptr, icons = nullptr, worker = nullptr;
    bool valid = true;
private:
    std::vector<const wchar_t*> registeredClasses;
    std::vector<HWND> additionalRoots;
    static HWND child(const wchar_t* name, HWND parent, DWORD extended)
    { return CreateWindowExW(extended, name, L"", WS_CHILD | WS_VISIBLE,
        0, 0, 600, 400, parent, nullptr, GetModuleHandleW(nullptr), nullptr); }
};

bool verifyOwnership()
{
    DesktopPresentationHost host;
    if (!require(!host.isValid() && !host.nativeHandle() && host.destroy() && host.destroy(), "empty/idempotent lifetime")
        || !require(host.create(DisplayId::fromTargetDevicePath("DSE-missing-display-for-test"))
            == DesktopPresentationHostStatus::DisplayUnavailable && !host.nativeHandle(), "unresolved identity publishes nothing")) return false;
    ShellFixture shell;
    if (!require(shell.valid && shell.intact(), "test-owned native shell fixture")) return false;
    DisplayDescriptor display;
    display.id = DisplayId::fromTargetDevicePath("fixture-display");
    display.desktopX = 140; display.desktopY = 170;
    display.pixelWidth = 377; display.pixelHeight = 211;
    int externalDestructions = 0;
    host.setExternalDestructionCallback([&externalDestructions] { ++externalDestructions; });
    for (const auto failure : {DesktopHostFailurePoint::CreateWindow, DesktopHostFailurePoint::Layering,
            DesktopHostFailurePoint::ChildStyle, DesktopHostFailurePoint::Show,
            DesktopHostFailurePoint::Parenting, DesktopHostFailurePoint::Positioning,
            DesktopHostFailurePoint::Verification})
    {
        DesktopHostCreationTrace trace;
        const auto status = DesktopPresentationHostTestAccess::create(host, display, shell.root, failure, trace);
        WNDCLASSW classInformation{};
        if (!require(status == DesktopPresentationHostStatus::NativeFailure, "deterministic native failure is surfaced")
            || !require(!trace.className.empty()
                && bool(trace.createdWindow) == (failure != DesktopHostFailurePoint::CreateWindow)
                && trace.shown == (failure == DesktopHostFailurePoint::Parenting
                    || failure == DesktopHostFailurePoint::Positioning || failure == DesktopHostFailurePoint::Verification)
                && trace.parentChanged == (failure == DesktopHostFailurePoint::Positioning || failure == DesktopHostFailurePoint::Verification),
                "failure control executes all earlier real native steps")
            || !require(!host.isValid() && !host.nativeHandle() && !trace.published
                && (!trace.createdWindow || !IsWindow(trace.createdWindow)) && shell.intact()
                && externalDestructions == 0, "failed transaction destroys only candidate without external signal")
            || !require(!GetClassInfoW(GetModuleHandleW(nullptr), trace.className.c_str(), &classInformation), "failed transaction releases owned class")) return false;
    }
    SetWindowLongPtrW(shell.defView, GWL_EXSTYLE, 0);
    DesktopHostCreationTrace rejected;
    if (!require(DesktopPresentationHostTestAccess::create(host, display, shell.root,
            DesktopHostFailurePoint::None, rejected) == DesktopPresentationHostStatus::UnsupportedShellTopology
        && !rejected.createdWindow && shell.intact(), "classic topology fails before native creation")) return false;
    SetWindowLongPtrW(shell.defView, GWL_EXSTYLE, WS_EX_LAYERED);
    SetLayeredWindowAttributes(shell.defView, 0, 255, LWA_ALPHA);
    shell.removeWorker();
    DesktopHostCreationTrace incomplete;
    if (!require(DesktopPresentationHostTestAccess::create(host, display, shell.root,
            DesktopHostFailurePoint::None, incomplete) == DesktopPresentationHostStatus::TransientlyUnavailable
        && !incomplete.createdWindow && externalDestructions == 0,
        "incomplete Shell topology is transient and publishes nothing")
        || !require(shell.restoreWorker() && shell.intact(), "restore raised Shell fixture")) return false;
    const HWND replacementRoot = shell.createAdditionalRoot();
    if (!require(replacementRoot && replacementRoot != shell.root,
            "distinct retained replacement Shell root fixture")) return false;
    DesktopHostCreationTrace staleRootCandidate;
    if (!require(DesktopPresentationHostTestAccess::createAfterAuthoritativeRootChange(
            host, display, shell.root, replacementRoot, replacementRoot,
            staleRootCandidate) == DesktopPresentationHostStatus::TransientlyUnavailable
            && !host.isValid() && !staleRootCandidate.published
            && (!staleRootCandidate.createdWindow
                || !IsWindow(staleRootCandidate.createdWindow))
            && shell.intact() && IsWindow(replacementRoot)
            && externalDestructions == 0,
        "creation rejects retained old topology after authoritative Shell root changes"))
        return false;
    for (int cycle = 0; cycle < 3; ++cycle)
    {
        DesktopHostCreationTrace trace;
        const HWND foreground = GetForegroundWindow();
        if (!require(DesktopPresentationHostTestAccess::create(host, display, shell.root,
                DesktopHostFailurePoint::None, trace) == DesktopPresentationHostStatus::Ready
                && host.isValid() && trace.published && GetForegroundWindow() == foreground, "verified non-activating native host")
            || !require(GetParent(static_cast<HWND>(host.nativeHandle())) == shell.root
                && GetWindow(shell.defView, GW_HWNDNEXT) == host.nativeHandle()
                && GetWindow(static_cast<HWND>(host.nativeHandle()), GW_HWNDNEXT) == shell.worker,
                "host between borrowed DefView and WorkerW")) return false;
        RECT screen{}, client{};
        GetWindowRect(static_cast<HWND>(host.nativeHandle()), &screen);
        GetClientRect(static_cast<HWND>(host.nativeHandle()), &client);
        if (!require(screen.left == 140 && screen.top == 170 && screen.right == 517 && screen.bottom == 381
            && client.right == 377 && client.bottom == 211, "actual physical screen and GetClientRect geometry")) return false;
        const auto owned = host.nativeHandle();
        if (!require(host.create(display.id) == DesktopPresentationHostStatus::HostAlreadyExists
            && host.nativeHandle() == owned, "create never silently replaces a live borrowed graphics HWND")) return false;
        if (cycle == 0)
        {
            DisplayDescriptor moved = display;
            moved.desktopX = 180; moved.desktopY = 195;
            moved.pixelWidth = 421; moved.pixelHeight = 239;
            ShowWindow(static_cast<HWND>(owned), SW_HIDE);
            SetWindowPos(static_cast<HWND>(owned), HWND_TOP, 1, 2, 3, 4,
                SWP_NOACTIVATE);
            if (!require(DesktopPresentationHostTestAccess::reconcile(
                    host, moved, shell.root) == DesktopPresentationHostAttachmentStatus::Ready
                    && host.nativeHandle() == owned,
                "geometry/visibility/sibling drift repairs the same HWND")) return false;
            RECT repaired{};
            GetWindowRect(static_cast<HWND>(owned), &repaired);
            if (!require(repaired.left == moved.desktopX && repaired.top == moved.desktopY
                    && repaired.right - repaired.left == moved.pixelWidth
                    && repaired.bottom - repaired.top == moved.pixelHeight
                    && GetWindow(shell.defView, GW_HWNDNEXT) == owned,
                "in-place repair restores exact physical placement and ordering")) return false;
            if (!require(DesktopPresentationHostTestAccess::reconcileAfterAuthoritativeRootChange(
                    host, moved, shell.root, replacementRoot, shell.root)
                    == DesktopPresentationHostAttachmentStatus::TransientlyUnavailable
                    && host.nativeHandle() == owned,
                "reconciliation checks current Progman before accepting retained root")
                || !require(DesktopPresentationHostTestAccess::reconcileAfterAuthoritativeRootChange(
                    host, moved, shell.root, shell.root, replacementRoot)
                    == DesktopPresentationHostAttachmentStatus::TransientlyUnavailable
                    && host.nativeHandle() == owned,
                "reconciliation checks GetShellWindow before accepting retained root"))
                return false;
        }
        if (cycle == 1)
        {
            DestroyWindow(static_cast<HWND>(owned));
            if (!require(!host.isValid() && !host.nativeHandle() && externalDestructions == 1,
                "external WM_NCDESTROY clears ownership and signals exactly once")) return false;
            const auto unrelated = CreateWindowExW(0, L"WorkerW", L"unrelated test window",
                WS_CHILD, 0, 0, 1, 1, shell.root, nullptr, GetModuleHandleW(nullptr), nullptr);
            if (!require(unrelated && host.destroy() && IsWindow(unrelated), "external destruction cannot double-destroy/recycled HWND")) return false;
            DestroyWindow(unrelated);
        }
        if (cycle == 2)
        {
            shell.removeWorker();
            if (!require(DesktopPresentationHostTestAccess::reconcile(
                    host, display, shell.root)
                    == DesktopPresentationHostAttachmentStatus::TransientlyUnavailable,
                "published host distinguishes transient incomplete topology")
                || !require(shell.restoreWorker(), "restore WorkerW after transient observation"))
                return false;

            SetWindowLongPtrW(shell.defView, GWL_EXSTYLE, 0);
            if (!require(DesktopPresentationHostTestAccess::reconcile(
                    host, display, shell.root)
                    == DesktopPresentationHostAttachmentStatus::StructurallyUnsupported,
                "published host distinguishes complete unsupported topology")) return false;
            SetWindowLongPtrW(shell.defView, GWL_EXSTYLE, WS_EX_LAYERED);
            SetLayeredWindowAttributes(shell.defView, 0, 255, LWA_ALPHA);

            SetWindowPos(static_cast<HWND>(owned), HWND_TOP, 1, 2, 3, 4, SWP_NOACTIVATE);
            auto invalidDisplay = display;
            invalidDisplay.pixelWidth = 0;
            if (!require(DesktopPresentationHostTestAccess::reconcile(
                    host, invalidDisplay, shell.root)
                    == DesktopPresentationHostAttachmentStatus::NativeFailure,
                "published host distinguishes local native repair failure")
                || !require(DesktopPresentationHostTestAccess::reconcile(
                    host, display, shell.root) == DesktopPresentationHostAttachmentStatus::Ready,
                "valid facts repair after local native failure")) return false;

            const HWND observedWorker = shell.worker;
            const auto changedDuringObservation =
                DesktopPresentationHostTestAccess::reconcileAfterObservedChildDestruction(
                    host, display, shell.root, observedWorker);
            shell.worker = nullptr;
            if (!require(changedDuringObservation != DesktopPresentationHostAttachmentStatus::Ready,
                "topology change after observation fails closed during fresh revalidation")
                || !require(shell.restoreWorker()
                    && DesktopPresentationHostTestAccess::reconcile(
                        host, display, shell.root) == DesktopPresentationHostAttachmentStatus::Ready,
                "stable topology revalidates after the observed-generation race")) return false;

            SetLastError(ERROR_SUCCESS);
            const HWND previous = SetParent(static_cast<HWND>(owned), shell.defView);
            if (!require((previous || GetLastError() == ERROR_SUCCESS)
                    && DesktopPresentationHostTestAccess::reconcile(
                        host, display, shell.root)
                        == DesktopPresentationHostAttachmentStatus::ReplacementRequired,
                "wrong parent rejects in-place repair and requires replacement")) return false;
        }
        const int expectedSignals = cycle >= 1 ? 1 : 0;
        if (!require(host.destroy() && host.destroy() && !IsWindow(static_cast<HWND>(owned))
            && shell.intact() && externalDestructions == expectedSignals,
            "controlled reset/reuse destroys only own window without external signal")) return false;
    }
    DesktopHostCreationTrace scopedTrace;
    {
        DesktopPresentationHost scoped;
        if (!require(DesktopPresentationHostTestAccess::create(scoped, display, shell.root,
            DesktopHostFailurePoint::None, scopedTrace) == DesktopPresentationHostStatus::Ready, "RAII host")) return false;
    }
    WNDCLASSW information{};
    if (!require(!IsWindow(scopedTrace.createdWindow)
        && !GetClassInfoW(GetModuleHandleW(nullptr), scopedTrace.className.c_str(), &information)
        && shell.intact(), "destructor releases own HWND/class and leaves borrowed fixture HWNDs alive")) return false;

    DesktopPresentationHost parentLossHost;
    int parentLossSignals = 0;
    parentLossHost.setExternalDestructionCallback([&parentLossSignals] { ++parentLossSignals; });
    DesktopHostCreationTrace parentLossTrace;
    if (!require(DesktopPresentationHostTestAccess::create(parentLossHost, display,
                shell.root, DesktopHostFailurePoint::None, parentLossTrace)
                == DesktopPresentationHostStatus::Ready,
        "parent-loss published-host fixture")) return false;
    const HWND destroyedRoot = shell.root;
    if (!require(DestroyWindow(destroyedRoot) != FALSE && !parentLossHost.isValid()
            && parentLossSignals == 1,
        "test-owned parent destruction delivers one external host-loss signal")) return false;
    shell.root = nullptr;
    return require(parentLossHost.destroy() && parentLossSignals == 1,
        "cleanup after parent loss is idempotent and does not duplicate the signal");
}
}

int main() { return verifyOwnership() ? 0 : 1; }
