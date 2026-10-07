#include <DesktopSceneEngine/Platform/DesktopPresentationHost.h>
#include <DesktopSceneEngine/Platform/Display.h>
#include <DesktopSceneEngine/Engine.h>
#include "Rendering/GraphicsBackend.h"
#include "Rendering/Renderer.h"

#include <Windows.h>
#include <array>
#include <charconv>
#include <iostream>
#include <string_view>

namespace DesktopSceneEngine::Rendering
{
// Reuse the closed Slice 2 test seam for fixture upload/readback/diagnostics.
// All lifecycle and frames enter through Engine's public API.
class CanvasTargetFitTestAccess
{
public:
    static GraphicsBackend& backend(Engine& engine) { return engine.renderer_->backend_; }
};
}

namespace
{
using namespace DesktopSceneEngine;
using namespace DesktopSceneEngine::Platform;
using Rendering::CanvasTargetFitTestAccess;

bool require(bool value, const char* label)
{ if (!value) std::cerr << "Test failed: " << label << '\n'; return value; }

void pumpMessages()
{
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
    { TranslateMessage(&message); DispatchMessageW(&message); }
}

struct PreviewWindow
{
    PreviewWindow()
    {
        WNDCLASSW type{}; type.lpfnWndProc = DefWindowProcW;
        type.hInstance = GetModuleHandleW(nullptr); type.lpszClassName = L"DSEDesktopHostIntegrationPreview";
        registered = RegisterClassW(&type) != 0;
        if (registered) handle = CreateWindowExW(WS_EX_NOACTIVATE, type.lpszClassName,
            L"L6 Slice 3 Preview control", WS_OVERLAPPEDWINDOW, 200, 200, 640, 400,
            nullptr, nullptr, type.hInstance, nullptr);
        if (handle) ShowWindow(handle, SW_SHOWNOACTIVATE);
    }
    ~PreviewWindow()
    {
        if (handle && IsWindow(handle)) DestroyWindow(handle);
        if (registered) UnregisterClassW(L"DSEDesktopHostIntegrationPreview", GetModuleHandleW(nullptr));
    }
    PreviewWindow(const PreviewWindow&) = delete;
    PreviewWindow& operator=(const PreviewWindow&) = delete;
    HWND handle = nullptr;
    bool registered = false;
};

bool nativeHostFacts(const DesktopPresentationHost& host, const DisplayDescriptor& display)
{
    const auto handle = static_cast<HWND>(host.nativeHandle());
    RECT screen{}, client{};
    const auto parent = GetParent(handle);
    wchar_t name[64]{};
    GetClassNameW(parent, name, 64);
    std::cout << "Host=" << handle << " parent=" << parent << " previous=" << GetWindow(handle, GW_HWNDPREV)
        << " next=" << GetWindow(handle, GW_HWNDNEXT) << '\n';
    return require(host.isValid() && parent == GetShellWindow() && std::wstring_view(name) == L"Progman"
        && IsWindowVisible(handle) && GetWindowRect(handle, &screen) && GetClientRect(handle, &client)
        && screen.left == display.desktopX && screen.top == display.desktopY
        && screen.right - screen.left == display.pixelWidth && screen.bottom - screen.top == display.pixelHeight
        && client.right == display.pixelWidth && client.bottom == display.pixelHeight, "real production host parent/physical geometry/visibility");
}

int run(const DisplayDescriptor& display, bool human, bool nativeOnly)
{
    std::cout << "Explicit bound display " << display.displayNumber << ": " << display.pixelWidth << 'x' << display.pixelHeight
        << " at (" << display.desktopX << ',' << display.desktopY << ") id=" << *display.id.canonicalRepresentation() << std::endl;
    if (nativeOnly)
    {
        DesktopPresentationHost host;
        const HWND foreground = GetForegroundWindow();
        const auto context = GetThreadDpiAwarenessContext();
        const auto status = host.create(display.id);
        std::cout << "Native create status=" << int(status) << std::endl;
        if (!require(status == DesktopPresentationHostStatus::Ready, "current shell supports production raised host")) return 1;
        const auto owned = static_cast<HWND>(host.nativeHandle());
        const auto parent = GetParent(owned);
        return nativeHostFacts(host, display)
            && require(GetForegroundWindow() == foreground && AreDpiAwarenessContextsEqual(context, GetThreadDpiAwarenessContext()), "focus and control-thread DPI unchanged")
            && require(host.destroy() && !IsWindow(owned) && IsWindow(parent), "real host destruction never destroys shell") ? 0 : 1;
    }
    PreviewWindow preview;
    DesktopPresentationHost host; // Engine must shut down before this owner on every early return.
    Engine engine;
    const auto previewId = engine.initialize({preview.handle});
    if (!require(previewId.isValid(), "one Engine Preview control")) return 1;
    const HWND foreground = GetForegroundWindow();
    const auto context = GetThreadDpiAwarenessContext();
    const auto status = host.create(display.id);
    std::cout << "Engine integration host status=" << int(status) << std::endl;
    if (!require(status == DesktopPresentationHostStatus::Ready, "production host ready before graphics attach")
        || !nativeHostFacts(host, display)
        || !require(GetForegroundWindow() == foreground && AreDpiAwarenessContextsEqual(context, GetThreadDpiAwarenessContext()), "deployment does not steal focus or alter thread DPI")) return 1;
    const auto owned = static_cast<HWND>(host.nativeHandle());
    const auto parent = GetParent(owned);
    const auto defView = FindWindowExW(parent, nullptr, L"SHELLDLL_DefView", nullptr);
    const auto worker = FindWindowExW(parent, nullptr, L"WorkerW", nullptr);
    const auto desktopId = engine.attachPresentationTarget({host.nativeHandle(), PresentationTransport::DirectCompositionSwapChain});
    if (!require(desktopId.isValid() && engine.setPresentationPacingTarget(desktopId), "DirectComposition attachment is intended pacing authority")) return 1;
    auto& backend = CanvasTargetFitTestAccess::backend(engine);
    const std::array<unsigned char, 4> green{13, 219, 13, 255};
    if (!require(backend.updateTexture(green.data(), 1, 1, 4, 4, green.size())
            == DesktopSceneEngine::Rendering::Detail::MediaUpdateResult::Updated
        && backend.captureNextPresentationPixelForTesting(desktopId)
        && backend.captureNextPresentationPixelForTesting(previewId), "one shared known GPU content source")) return 1;
    engine.render({16, 10});
    const auto desktop = backend.presentationTargetDiagnosticsForTesting(desktopId);
    const auto control = backend.presentationTargetDiagnosticsForTesting(previewId);
    constexpr std::uint32_t greenRgba = 0xFF0DDB0DU;
    if (!require(desktop.capturedPixelValid && desktop.capturedPixelRgba == greenRgba
        && control.capturedPixelValid && control.capturedPixelRgba == greenRgba
        && desktop.compositionTreeSubmitted && desktop.lastPresentSyncInterval == 1
        && control.lastPresentSyncInterval == 0 && desktop.lastPresentOrder > control.lastPresentOrder
        && backend.mediaUploadCountForTesting() == 1, "actual shared GPU pixels, mixed-target pacing and composition publication")) return 1;
    std::cout << "GPU readback PASS: Desktop/Preview=(13,219,13,255); one upload, Desktop pacing."
        << (human ? " Human gate running for 120 seconds: green/cyan desktop on the explicit display." : " Automated integration running.") << std::endl;
    const auto start = GetTickCount64();
    ULONGLONG uploadedPhase = 0;
    while (GetTickCount64() - start < (human ? 120000U : 300U))
    {
        pumpMessages();
        const auto phase = (GetTickCount64() - start) / 1000;
        if (phase != uploadedPhase)
        {
            const std::array<unsigned char, 4> color{13, 219, static_cast<unsigned char>(phase % 2 ? 219 : 13), 255};
            if (!require(backend.updateTexture(color.data(), 1, 1, 4, 4, color.size())
                    == DesktopSceneEngine::Rendering::Detail::MediaUpdateResult::Updated,
                "shared color update")) return 1;
            uploadedPhase = phase;
        }
        engine.render({16, 10});
        if (!require(backend.presentationTargetInfo(desktopId).ready && !backend.presentationTargetInfo(previewId).failed,
            "desktop presentation and shared device remain operational")) return 1;
    }
    if (!require(engine.detachPresentationTarget(desktopId), "graphics detach before HWND destruction")) return 1;
    if (!require(host.destroy() && !IsWindow(owned) && IsWindow(parent) && IsWindow(defView) && IsWindow(worker), "owned HWND gone; borrowed shell alive")) return 1;
    if (!require(engine.setPresentationPacingTarget(previewId), "Preview becomes control pacing after detach")) return 1;
    const auto before = backend.presentationTargetDiagnosticsForTesting(previewId).drawCount;
    for (int i = 0; i < 30; ++i) { pumpMessages(); engine.render({16, 10}); }
    if (!require(engine.isInitialized() && backend.presentationTargetInfo(previewId).ready
        && backend.presentationTargetDiagnosticsForTesting(previewId).drawCount >= before + 30, "Preview/Engine healthy after desktop cleanup")) return 1;
    engine.shutdown();
    std::cout << "Cleanup PASS: graphics detached first; owned desktop HWND destroyed; shell alive; Preview continued; Engine shutdown.\n";
    return 0;
}
}

int main(int argc, char** argv)
{
    bool human = false, nativeOnly = false;
    int displayNumber = 1; // Test binding only; production host never accepts a number/fallback.
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view argument(argv[i]);
        if (argument == "--human-gate") human = true;
        else if (argument == "--native-only") nativeOnly = true;
        else if (argument == "--display-number" && i + 1 < argc)
        {
            const std::string_view number(argv[++i]);
            const auto parsed = std::from_chars(number.data(), number.data() + number.size(), displayNumber);
            if (parsed.ec != std::errc{} || parsed.ptr != number.data() + number.size()) return 1;
        }
        else return 1;
    }
    const auto current = enumerateActiveDisplays();
    if (!require(current.status == DisplayEnumerationStatus::Success, "live canonical enumeration")) return 1;
    for (const auto& display : current.snapshot.displays)
        if (display.displayNumber == displayNumber) return run(display, human, nativeOnly);
    std::cerr << "Explicit test display not available; no fallback.\n";
    return 1;
}
