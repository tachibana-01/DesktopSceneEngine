#include "GraphicsBackend.h"
#include "WindowTarget.h"
#include <DesktopSceneEngine/Engine.h>
#include <Windows.h>
#include <dcomp.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <iostream>
#include <array>
#include <string_view>

using DesktopSceneEngine::PresentationTransport;

static_assert(DesktopSceneEngine::Engine::ExternalWindowTarget{}.transport
    == PresentationTransport::HwndSwapChain);
static_assert(DesktopSceneEngine::Rendering::WindowTarget{}.transport
    == PresentationTransport::HwndSwapChain);

namespace
{
using Microsoft::WRL::ComPtr;
using DesktopSceneEngine::Rendering::GraphicsBackend;

bool require(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "Test failed: " << message << std::endl;
    return condition;
}

// Independent native layer reservation checks detect dropped transport intent
// and leaked HWND bindings, not merely the backend's own diagnostics.
class BindingProbe
{
public:
    BindingProbe()
    {
        result = DCompositionCreateDevice(nullptr, IID_PPV_ARGS(&device));
    }
    HRESULT reserve(HWND window)
    {
        ComPtr<IDCompositionTarget> target;
        return device->CreateTargetForHwnd(window, TRUE, &target);
    }
    HRESULT result = E_FAIL;
    ComPtr<IDCompositionDevice> device;
};

class TestWindow
{
public:
    TestWindow(int width = 320, int height = 200, int x = 100,
        const wchar_t* title = L"L6 Slice 1 Composition")
    {
        WNDCLASSW type{};
        type.lpfnWndProc = DefWindowProcW;
        type.hInstance = GetModuleHandleW(nullptr);
        type.lpszClassName = L"DSECompositionTargetTest";
        RegisterClassW(&type);
        window = CreateWindowExW(WS_EX_NOACTIVATE, type.lpszClassName,
            title, WS_POPUP, x, 100, width, height,
            nullptr, nullptr, type.hInstance, nullptr);
        ShowWindow(window, SW_SHOWNOACTIVATE);
        UpdateWindow(window);
    }
    ~TestWindow() { DestroyWindow(window); }
    TestWindow(const TestWindow&) = delete;
    TestWindow& operator=(const TestWindow&) = delete;
    bool resize(int width, int height)
    {
        return SetWindowPos(window, nullptr, 0, 0, width, height,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != FALSE;
    }
    void destroy() { DestroyWindow(window); window = nullptr; }
    HWND window = nullptr;
};

bool verifyNativeCompositionAttachment()
{
    TestWindow window;
    BindingProbe probe;
    if (!require(window.window && SUCCEEDED(probe.result), "native test preconditions"))
        return false;
    GraphicsBackend backend;
    const auto id = backend.initialize({window.window, 0,
        PresentationTransport::DirectCompositionSwapChain});
    return require(id.isValid(), "composition attachment succeeds")
        && require(FAILED(probe.reserve(window.window)),
            "explicit transport must reserve the HWND composition layer");
}

using DesktopSceneEngine::Rendering::Detail::FramePreparationResult;
using DesktopSceneEngine::Rendering::Detail::CompositionFailurePoint;
using DesktopSceneEngine::Rendering::WindowTarget;
using CompositionTrustForTesting = GraphicsBackend::CompositionDiagnostics::TrustState;
constexpr auto Composition = PresentationTransport::DirectCompositionSwapChain;

WindowTarget compositionFor(HWND window) { return {window, 0, Composition}; }

const char* compositionTrustName(CompositionTrustForTesting state)
{
    switch (state)
    {
    case CompositionTrustForTesting::Unavailable: return "Unavailable";
    case CompositionTrustForTesting::Trusted: return "Trusted";
    case CompositionTrustForTesting::UntrustedPending: return "UntrustedPending";
    case CompositionTrustForTesting::UntrustedSpent: return "UntrustedSpent";
    }
    return "Unknown";
}

bool uploadPixel(GraphicsBackend& backend, const std::array<unsigned char, 4>& pixel)
{
    return backend.updateTexture(pixel.data(), 1, 1, 4, 4, pixel.size())
        == DesktopSceneEngine::Rendering::Detail::MediaUpdateResult::Updated;
}

std::uint32_t packed(const std::array<unsigned char, 4>& pixel)
{
    return pixel[0] | (std::uint32_t(pixel[1]) << 8)
        | (std::uint32_t(pixel[2]) << 16) | (std::uint32_t(pixel[3]) << 24);
}

bool capture(GraphicsBackend& backend, DesktopSceneEngine::PresentationTargetId id)
{
    return require(backend.captureNextPresentationPixelForTesting(id), "request GPU readback");
}

bool pixelMatches(GraphicsBackend& backend, DesktopSceneEngine::PresentationTargetId id,
    const std::array<unsigned char, 4>& pixel)
{
    const auto diagnostics = backend.presentationTargetDiagnosticsForTesting(id);
    return require(diagnostics.capturedPixelValid && diagnostics.capturedPixelRgba == packed(pixel),
        "rendered target samples the known shared media texture");
}

bool controlPresentedResult(GraphicsBackend& backend, DesktopSceneEngine::PresentationTargetId id)
{
    return backend.forceNextDxgiPresentResultForTesting(id, static_cast<std::int32_t>(S_OK));
}

bool verifyCanvasOutsideModeSwitching()
{
    using DesktopSceneEngine::CanvasOutsideInstruction;
    using DesktopSceneEngine::CanvasOutsideMode;
    TestWindow ordinary{320, 320, 100}, addressed{320, 320, 460}, peer{320, 320, 820};
    GraphicsBackend backend;
    const auto a = backend.initialize({ordinary.window});
    const auto b = backend.attachPresentationTarget(compositionFor(addressed.window));
    const auto c = backend.attachPresentationTarget(compositionFor(peer.window));
    if (!require(a.isValid() && b.isValid() && c.isValid()
            && backend.setPresentationPacingTarget(b), "mode-switch target preconditions")) return false;
    const auto native = backend.presentationTargetDiagnosticsForTesting(b);
    const auto target = backend.presentationTargetInfo(b);
    const auto composition = backend.compositionDiagnosticsForTesting();
    DesktopSceneEngine::Rendering::RenderData frame;
    frame.canvasAspect = 16.0 / 9.0;
    frame.clearColor[0] = frame.clearColor[1] = frame.clearColor[3] = 0.0F;
    frame.clearColor[2] = 1.0F;
    std::uint64_t frames = 0U;
    const auto sample = [&](bool transparent, bool inside, std::uint32_t expectedInside,
        bool mediaPresent = false)
    {
        frame.canvasOutside = CanvasOutsideInstruction{b, transparent
            ? CanvasOutsideMode::Transparent : CanvasOutsideMode::Opaque};
        for (const auto id : {a, b, c})
            if (!backend.captureNextPresentationPixelForTesting(id, 160, inside ? 160 : 5)
                || !controlPresentedResult(backend, id)) return false;
        if (backend.beginFrame() != FramePreparationResult::ReadyTargetsAvailable) return false;
        backend.renderPrepared(frame);
        ++frames;
        const auto ad = backend.presentationTargetDiagnosticsForTesting(a);
        const auto bd = backend.presentationTargetDiagnosticsForTesting(b);
        const auto cd = backend.presentationTargetDiagnosticsForTesting(c);
        const auto expectedPeer = inside ? expectedInside : 0xFFFF0000U;
        if (!require(ad.capturedPixelValid && ad.capturedPixelRgba == expectedPeer
                && cd.capturedPixelValid && cd.capturedPixelRgba == expectedPeer
                && bd.capturedPixelValid && bd.capturedPixelRgba
                     == (transparent && (!inside || !mediaPresent) ? 0U : inside ? expectedInside : 0xFFFF0000U),
                "one complete frame changes only the addressed DComp uncovered pixels")) return false;
        const auto current = backend.presentationTargetInfo(b);
        const auto shared = backend.compositionDiagnosticsForTesting();
        return require(bd.deviceIdentity != 0U && bd.swapChainIdentity != 0U
                && bd.compositionTargetIdentity != 0U && bd.compositionVisualIdentity != 0U
                && bd.deviceIdentity == native.deviceIdentity
                && ad.deviceIdentity == bd.deviceIdentity && cd.deviceIdentity == bd.deviceIdentity
                && bd.swapChainIdentity == native.swapChainIdentity
                && bd.compositionTargetIdentity == native.compositionTargetIdentity
                && bd.compositionVisualIdentity == native.compositionVisualIdentity
                && current.targetGeneration == target.targetGeneration
                && current.deviceGeneration == target.deviceGeneration
                && bd.successfulResizeCount == native.successfulResizeCount
                && shared.capabilityIdentity == composition.capabilityIdentity
                && shared.capabilityCreationCount == composition.capabilityCreationCount
                && shared.commitCount == composition.commitCount && shared.waitCount == composition.waitCount
                && ad.presentAttemptCount == frames && bd.presentAttemptCount == frames
                && cd.presentAttemptCount == frames
                && ad.lastPresentSyncInterval == 0U && cd.lastPresentSyncInterval == 0U
                && ad.lastPresentFlags == DXGI_PRESENT_DO_NOT_WAIT
                && cd.lastPresentFlags == DXGI_PRESENT_DO_NOT_WAIT
                && bd.lastPresentSyncInterval == 1U && bd.lastPresentFlags == 0U
                && ad.lastPresentOrder < cd.lastPresentOrder && cd.lastPresentOrder < bd.lastPresentOrder,
            "toggle preserves shared/device/target identities, DComp cadence and existing pacing");
    };
    if (!sample(false, false, 0xFFFF0000U) || !sample(true, false, 0xFFFF0000U)
        || !sample(true, true, 0xFFFF0000U) || !sample(false, false, 0xFFFF0000U)) return false;
    if (!require(backend.presentationTargetDiagnosticsForTesting(b).drawCount == 0U
            && backend.mediaUploadCountForTesting() == 0U,
        "empty-media clear is not a media draw or media preparation")) return false;
    constexpr std::array<unsigned char, 4> black{0, 0, 0, 7};
    if (!uploadPixel(backend, black) || !sample(true, true, 0xFF000000U, true)
        || !sample(false, true, 0xFF000000U, true) || !sample(true, false, 0xFF000000U, true)) return false;
    for (const auto id : {a, b, c})
        if (!require(backend.presentationTargetDiagnosticsForTesting(id).drawCount == 3U,
            "existing media drawCount remains one media draw per usable target")) return false;
    return require(backend.mediaUploadCountForTesting() == 1U,
        "mode switching never duplicates the shared media upload");
}

bool opaquePixelMatches(GraphicsBackend& backend, DesktopSceneEngine::PresentationTargetId id,
    const std::array<unsigned char, 4>& source, const char* path, const char* transport)
{
    const auto diagnostics = backend.presentationTargetDiagnosticsForTesting(id);
    const auto actual = diagnostics.capturedPixelRgba;
    const std::array<unsigned char, 4> expected{source[0], source[1], source[2], 255};
    std::cout << "Alpha contract " << path << "/" << transport
        << " source=(" << unsigned(source[0]) << "," << unsigned(source[1])
        << "," << unsigned(source[2]) << "," << unsigned(source[3]) << ")"
        << " readback=(" << (actual & 255U) << "," << ((actual >> 8) & 255U)
        << "," << ((actual >> 16) & 255U) << "," << ((actual >> 24) & 255U) << ")\n";
    return require(diagnostics.capturedPixelValid && actual == packed(expected),
        "final framebuffer preserves source RGB and writes opaque alpha");
}

bool verifyOpaquePresentationFramebuffer()
{
    TestWindow ordinary, composition{320, 200, 460};
    GraphicsBackend backend;
    const auto a = backend.initialize({ordinary.window});
    const auto b = backend.attachPresentationTarget(compositionFor(composition.window));
    if (!require(a.isValid() && b.isValid(), "alpha contract HWND preconditions")) return false;
    const auto capability = backend.compositionDiagnosticsForTesting();
    const auto generation = backend.presentationTargetInfo(a).deviceGeneration;
    constexpr std::array<std::array<unsigned char, 4>, 3> sources{{
        {220, 80, 40, 0}, {120, 200, 60, 128}, {13, 219, 77, 255}}};
    bool success = true;
    std::uint64_t frames = 0;
    // Texture starts without media (allocation), then exercises UpdateSubresource.
    // Image uses its distinct allocation entry point; both reach the real shader.
    for (const bool imageUpload : {false, true})
    {
        const char* path = imageUpload ? "image" : "texture";
        const auto pacing = imageUpload ? b : a;
        if (!require(backend.setPresentationPacingTarget(pacing), "alpha contract pacing selection")) return false;
        for (const auto& source : sources)
        {
            const bool uploaded = imageUpload
                ? backend.updateImage(source.data(), 1, 1, 4, 4, source.size())
                    == DesktopSceneEngine::Rendering::Detail::MediaUpdateResult::Updated
                : uploadPixel(backend, source);
            if (!require(uploaded && backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable
                    && controlPresentedResult(backend, a) && controlPresentedResult(backend, b)
                    && capture(backend, a) && capture(backend, b),
                    "shared non-opaque source drives production rendering")) return false;
            backend.renderPrepared({});
            ++frames;
            success = opaquePixelMatches(backend, a, source, path, "ordinary") && success;
            success = opaquePixelMatches(backend, b, source, path, "composition") && success;
            const auto ad = backend.presentationTargetDiagnosticsForTesting(a);
            const auto bd = backend.presentationTargetDiagnosticsForTesting(b);
            if (!require(backend.mediaUploadCountForTesting() == frames
                    && ad.drawCount == frames && bd.drawCount == frames,
                    "alpha correction retains one upload and one draw per target")) return false;
            const auto& pacer = imageUpload ? bd : ad;
            const auto& nonPacer = imageUpload ? ad : bd;
            if (!require(pacer.lastPresentSyncInterval == 1 && pacer.lastPresentFlags == 0
                    && nonPacer.lastPresentSyncInterval == 0
                    && nonPacer.lastPresentFlags == DXGI_PRESENT_DO_NOT_WAIT
                    && nonPacer.lastPresentOrder < pacer.lastPresentOrder,
                    "alpha correction retains mixed-target pacing")) return false;
        }
    }
    // Clear is the other production RTV writer. Check it without a covering draw.
    backend.clearImage();
    for (const auto& source : sources)
    {
        DesktopSceneEngine::Rendering::RenderData clearOnly{};
        for (std::size_t channel = 0; channel < source.size(); ++channel)
            clearOnly.clearColor[channel] = float(source[channel]) / 255.0F;
        if (!require(backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable
                && controlPresentedResult(backend, a) && controlPresentedResult(backend, b)
                && capture(backend, a) && capture(backend, b),
                "clear-only alpha contract frame")) return false;
        backend.renderPrepared(clearOnly);
        success = opaquePixelMatches(backend, a, source, "clear", "ordinary") && success;
        success = opaquePixelMatches(backend, b, source, "clear", "composition") && success;
        if (!require(backend.mediaUploadCountForTesting() == frames
                && backend.presentationTargetDiagnosticsForTesting(a).drawCount == frames
                && backend.presentationTargetDiagnosticsForTesting(b).drawCount == frames,
                "clear-only frames introduce no media upload or draw")) return false;
    }
    const auto finalCapability = backend.compositionDiagnosticsForTesting();
    return require(capability.capabilityIdentity != 0 && capability.capabilityCreationCount == 1
            && finalCapability.capabilityIdentity == capability.capabilityIdentity
            && finalCapability.capabilityCreationCount == capability.capabilityCreationCount
            && finalCapability.commitCount == capability.commitCount
            && finalCapability.waitCount == capability.waitCount
            && backend.presentationTargetInfo(a).deviceGeneration == generation
            && backend.presentationTargetInfo(b).deviceGeneration == generation,
            "opaque output retains shared device/capability and no per-frame Commit/wait") && success;
}

// Public forwarding is observed through native layer occupancy. It would fail
// even if internal transport diagnostics merely echoed the caller's enum.
bool verifyEngineForwarding()
{
    TestWindow first, second{320, 200, 460}, ordinary{320, 200, 820};
    BindingProbe probe;
    if (!require(first.window && second.window && ordinary.window && SUCCEEDED(probe.result),
            "Engine forwarding HWND preconditions")) return false;
    DesktopSceneEngine::Engine engine;
    const auto firstId = engine.initialize({first.window, Composition});
    if (!require(firstId.isValid() && FAILED(probe.reserve(first.window)),
            "Engine initialize wrapper preserves explicit composition transport")) return false;
    const auto secondId = engine.attachPresentationTarget({second.window, Composition});
    if (!require(secondId.isValid() && FAILED(probe.reserve(second.window)),
            "Engine attach preserves explicit composition transport")) return false;
    const auto ordinaryId = engine.attachPresentationTarget({ordinary.window});
    if (!require(ordinaryId.isValid() && SUCCEEDED(probe.reserve(ordinary.window)),
            "public aggregate default retains ordinary HWND transport")) return false;
    if (!require(!engine.attachPresentationTarget({first.window}).isValid(),
            "duplicate HWND rejects attempted in-place transport change")) return false;
    if (!require(!engine.attachPresentationTarget({ordinary.window,
                static_cast<PresentationTransport>(255)}).isValid(), "reject invalid transport")) return false;
    if (!require(engine.detachPresentationTarget(firstId) && SUCCEEDED(probe.reserve(first.window)),
            "public detach releases native composition binding")) return false;
    const auto replacement = engine.attachPresentationTarget({first.window});
    if (!require(replacement.isValid() && replacement != firstId
            && SUCCEEDED(probe.reserve(first.window)), "transport change requires fresh attachment")) return false;
    engine.shutdown();
    if (!require(SUCCEEDED(probe.reserve(second.window)), "Engine shutdown releases composition targets")) return false;
    DesktopSceneEngine::Engine invalid;
    return require(!invalid.initialize({first.window, static_cast<PresentationTransport>(255)}).isValid()
        && !invalid.isInitialized(), "initialization must not silently drop invalid transport intent");
}

bool verifyAttachRollback(bool compositionSurvivor)
{
    constexpr CompositionFailurePoint stages[] = {
        CompositionFailurePoint::CapabilityCreation, CompositionFailurePoint::SwapChainCreation,
        CompositionFailurePoint::RenderTargetViewCreation, CompositionFailurePoint::TargetCreation,
        CompositionFailurePoint::VisualCreation, CompositionFailurePoint::SetContent,
        CompositionFailurePoint::SetRoot, CompositionFailurePoint::AttachCommit};
    for (const auto stage : stages)
    {
        if (compositionSurvivor && stage == CompositionFailurePoint::CapabilityCreation)
            continue; // A live composition peer has already created the capability.
        TestWindow ordinary, composition{320, 200, 460};
        BindingProbe probe;
        if (!require(ordinary.window && composition.window && SUCCEEDED(probe.result),
                "rollback native HWND preconditions")) return false;
        GraphicsBackend backend;
        const auto survivor = backend.initialize(compositionSurvivor
            ? compositionFor(ordinary.window) : WindowTarget{ordinary.window});
        constexpr std::array<unsigned char, 4> pixel{13, 219, 47, 255};
        if (!require(survivor.isValid() && uploadPixel(backend, pixel), "rollback survivor initialized")) return false;
        const auto nextId = backend.nextPresentationTargetValueForTesting();
        const auto deviceGeneration = backend.presentationTargetInfo(survivor).deviceGeneration;
        backend.failNextCompositionOperationForTesting(stage);
        if (!require(!backend.attachPresentationTarget(compositionFor(composition.window)).isValid(),
                "controlled composition construction stage fails")) return false;
        if (!require(backend.nextPresentationTargetValueForTesting() == nextId
                && backend.presentationTargetCountForTesting() == 1
                && backend.pacingPresentationTargetForTesting() == survivor,
                "failed attach preserves allocator, collection and pacing target")) return false;
        if (!require(SUCCEEDED(probe.reserve(composition.window)),
                "failed candidate leaves no native composition layer reservation")) return false;
        if (!require(backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable
                && controlPresentedResult(backend, survivor)
                && capture(backend, survivor),
                "construction failure is local, not D3D11 device-removal state")) return false;
        backend.renderPrepared({});
        if (!pixelMatches(backend, survivor, pixel)) return false;
        if (compositionSurvivor && !require(FAILED(probe.reserve(ordinary.window)),
                "rollback preserves the live composition peer's HWND binding")) return false;
        const auto fresh = backend.attachPresentationTarget(compositionFor(composition.window));
        if (!require(fresh.isValid() && backend.nextPresentationTargetValueForTesting() == nextId + 1
                && backend.presentationTargetInfo(fresh).deviceGeneration == deviceGeneration,
                "fresh same-HWND attach succeeds with unconsumed ID and shared device")) return false;
        if (!require(backend.compositionDiagnosticsForTesting().capabilityCreationCount == 1,
                "first successful lazy capability is reused after target rollback")) return false;
        if (!require(backend.detachPresentationTarget(fresh) && SUCCEEDED(probe.reserve(composition.window)),
                "rollback successor detaches completely")) return false;
    }
    return true;
}

bool verifyFailedInitialRollbackContainsPublishedPeer()
{
    for (const auto rollbackStage : {CompositionFailurePoint::DisconnectSetRoot,
            CompositionFailurePoint::DisconnectSetContent})
    {
        TestWindow ordinary{320, 200, 100}, peer{300, 190, 460}, candidate{240, 160, 820};
        BindingProbe probe;
        if (!require(ordinary.window && peer.window && candidate.window
                && SUCCEEDED(probe.result), "dual-failure peer native preconditions")) return false;
        GraphicsBackend backend;
        const auto ordinaryId = backend.initialize({ordinary.window});
        const auto peerId = backend.attachPresentationTarget(compositionFor(peer.window));
        if (!require(ordinaryId.isValid() && peerId.isValid()
                && backend.setPresentationPacingTarget(peerId),
                "dual-failure published peer and preferred pacing preconditions")) return false;
        const auto before = backend.compositionDiagnosticsForTesting();
        const auto nextId = backend.nextPresentationTargetIdForTesting();
        const auto generation = backend.presentationTargetInfo(ordinaryId).deviceGeneration;
        // AttachCommit is after accepted SetContent and SetRoot mutations, so
        // its failure requires a real disconnect rollback of the candidate.
        if (!require(backend.forceNextCompositionOperationResultForTesting(
                    CompositionFailurePoint::AttachCommit, static_cast<std::int32_t>(E_FAIL))
                && backend.forceNextCompositionRollbackResultForTesting(
                    rollbackStage, static_cast<std::int32_t>(E_FAIL)),
                "arm distinct construction and required rollback setter failures")) return false;
        if (!require(!backend.attachPresentationTarget(compositionFor(candidate.window)).isValid(),
                "failed dual-fault candidate never publishes")) return false;
        const auto after = backend.compositionDiagnosticsForTesting();
        if (!require(backend.nextPresentationTargetIdForTesting() == nextId
                && backend.presentationTargetCountForTesting() == 2U
                && !backend.pendingTargetLocalRecoveryForTesting(nextId)
                && after.armedFailurePoint == CompositionFailurePoint::None
                && after.armedRollbackFailurePoint == CompositionFailurePoint::None
                && after.lastDisconnectResult == static_cast<std::int32_t>(E_FAIL)
                && after.commitCount == before.commitCount + 2U
                && after.waitCount == before.waitCount + 1U
                && after.capabilityIdentity == before.capabilityIdentity
                && after.capabilityCreationCount == before.capabilityCreationCount
                && SUCCEEDED(probe.reserve(candidate.window)),
                "candidate identity and native binding are discarded after failed rollback"))
            return false;
        if (!require(backend.hasCurrentGraphicsGenerationForTesting()
                && !backend.pendingGraphicsRecoveryForTesting()
                && backend.presentationTargetInfo(ordinaryId).deviceGeneration == generation
                && after.trustState == CompositionTrustForTesting::UntrustedPending
                && !backend.presentationTargetInfo(peerId).ready
                && backend.presentationTargetInfo(ordinaryId).ready
                && backend.preferredPacingTargetForTesting() == peerId
                && backend.pacingPresentationTargetForTesting() == ordinaryId
                && !backend.attachPresentationTarget(compositionFor(candidate.window)).isValid(),
                "failed required candidate rollback quarantines the surviving DComp peer"))
            return false;
        const auto peerPresents = backend.presentationTargetDiagnosticsForTesting(peerId).presentAttemptCount;
        const auto commits = after.commitCount;
        const auto waits = after.waitCount;
        backend.failNextCompositionOperationForTesting(CompositionFailurePoint::CapabilityCreation);
        if (!require(backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable
                && controlPresentedResult(backend, ordinaryId),
                "ordinary target remains independently usable after candidate rollback failure"))
            return false;
        backend.renderPrepared({});
        if (!require(backend.presentationTargetDiagnosticsForTesting(peerId).presentAttemptCount
                    == peerPresents
                && backend.compositionDiagnosticsForTesting().commitCount == commits
                && backend.compositionDiagnosticsForTesting().waitCount == waits
                && backend.compositionDiagnosticsForTesting().trustState
                    == CompositionTrustForTesting::UntrustedSpent
                && backend.compositionDiagnosticsForTesting().armedFailurePoint
                    == CompositionFailurePoint::None,
                "contained peer neither presents nor reconciles the uncertain DComp domain"))
            return false;
    }
    return true;
}

bool verifyFailedInitialRollbackResetsEmptyCapability()
{
    for (const auto rollbackStage : {CompositionFailurePoint::DisconnectSetRoot,
            CompositionFailurePoint::DisconnectSetContent})
    {
        TestWindow ordinary{320, 200, 100}, candidate{240, 160, 460};
        BindingProbe probe;
        if (!require(ordinary.window && candidate.window && SUCCEEDED(probe.result),
                "dual-failure zero-peer native preconditions")) return false;
        GraphicsBackend backend;
        const auto ordinaryId = backend.initialize({ordinary.window});
        if (!require(ordinaryId.isValid(), "zero-peer ordinary target initialized")) return false;
        const auto before = backend.compositionDiagnosticsForTesting();
        const auto nextId = backend.nextPresentationTargetIdForTesting();
        const auto generation = backend.presentationTargetInfo(ordinaryId).deviceGeneration;
        if (!require(before.capabilityIdentity == 0U,
                "zero-peer attachment starts without a DComp capability")) return false;
        if (!require(backend.forceNextCompositionOperationResultForTesting(
                    CompositionFailurePoint::AttachCommit, static_cast<std::int32_t>(E_FAIL))
                && backend.forceNextCompositionRollbackResultForTesting(
                    rollbackStage, static_cast<std::int32_t>(E_FAIL)),
                "arm zero-peer candidate construction and rollback failures")) return false;
        if (!require(!backend.attachPresentationTarget(compositionFor(candidate.window)).isValid(),
                "zero-peer failed candidate never publishes")) return false;
        const auto empty = backend.compositionDiagnosticsForTesting();
        if (!require(backend.nextPresentationTargetIdForTesting() == nextId
                && backend.presentationTargetCountForTesting() == 1U
                && !backend.pendingTargetLocalRecoveryForTesting(nextId)
                && empty.armedFailurePoint == CompositionFailurePoint::None
                && empty.armedRollbackFailurePoint == CompositionFailurePoint::None
                && empty.lastDisconnectResult == static_cast<std::int32_t>(E_FAIL)
                && empty.commitCount == before.commitCount + 2U
                && empty.waitCount == before.waitCount + 1U
                && empty.capabilityCreationCount == before.capabilityCreationCount + 1U
                && empty.capabilityIdentity == 0U
                && empty.trustState == CompositionTrustForTesting::Trusted
                && backend.hasCurrentGraphicsGenerationForTesting()
                && !backend.pendingGraphicsRecoveryForTesting()
                && backend.presentationTargetInfo(ordinaryId).deviceGeneration == generation
                && backend.presentationTargetInfo(ordinaryId).ready
                && SUCCEEDED(probe.reserve(candidate.window)),
                "zero logical DComp peers retire uncertain capability on healthy D3D"))
            return false;
        const auto freshId = backend.attachPresentationTarget(compositionFor(candidate.window));
        const auto fresh = backend.compositionDiagnosticsForTesting();
        if (!require(freshId.isValid() && freshId == nextId
                && backend.presentationTargetInfo(freshId).ready
                && !backend.pendingTargetLocalRecoveryForTesting(freshId)
                && fresh.capabilityIdentity != 0U
                && fresh.capabilityCreationCount == empty.capabilityCreationCount + 1U
                && fresh.trustState == CompositionTrustForTesting::Trusted
                && backend.presentationTargetInfo(freshId).deviceGeneration == generation,
                "fresh attachment creates a new trusted capability without replacing D3D"))
            return false;
    }
    return true;
}

bool verifyFailedInitialRollbackDeviceLostKeepsCandidateUnpublished()
{
    using DesktopSceneEngine::Rendering::Detail::GenerationReplacementResult;
    TestWindow ordinary{320, 200, 100}, peer{300, 190, 460}, candidate{240, 160, 820};
    GraphicsBackend backend;
    const auto ordinaryId = backend.initialize({ordinary.window});
    const auto peerId = backend.attachPresentationTarget(compositionFor(peer.window));
    if (!require(ordinaryId.isValid() && peerId.isValid(),
            "candidate rollback DeviceLost peer preconditions")) return false;
    const auto nextId = backend.nextPresentationTargetIdForTesting();
    const auto generation = backend.presentationTargetInfo(ordinaryId).deviceGeneration;
    if (!require(backend.forceNextCompositionOperationResultForTesting(
                CompositionFailurePoint::AttachCommit, static_cast<std::int32_t>(E_FAIL))
            && backend.forceNextCompositionRollbackResultForTesting(
                CompositionFailurePoint::DisconnectSetContent,
                static_cast<std::int32_t>(DXGI_ERROR_DEVICE_REMOVED)),
            "arm local construction failure then DeviceLost-class rollback")) return false;
    if (!require(!backend.attachPresentationTarget(compositionFor(candidate.window)).isValid(),
            "DeviceLost rollback must not publish failed candidate")) return false;
    if (!require(backend.nextPresentationTargetIdForTesting() == nextId
            && backend.presentationTargetCountForTesting() == 2U
            && !backend.pendingTargetLocalRecoveryForTesting(nextId)
            && !backend.hasCurrentGraphicsGenerationForTesting()
            && backend.pendingGraphicsRecoveryForTesting(),
            "DeviceLost rollback takes Slice 5 without candidate ID or local authority"))
        return false;
    return require(backend.recoverGraphicsGeneration(nullptr)
                == GenerationReplacementResult::GenerationAvailable
            && backend.presentationTargetInfo(ordinaryId).ready
            && backend.presentationTargetInfo(peerId).ready
            && backend.presentationTargetInfo(ordinaryId).deviceGeneration > generation,
        "published peers recover on a fresh D3D generation without failed candidate");
}

bool verifyInvalidEndpointFailedDisconnectContainsPeer(bool explicitDetach)
{
    for (const auto stage : {CompositionFailurePoint::DisconnectSetRoot,
            CompositionFailurePoint::DisconnectSetContent})
    {
        TestWindow ordinary{320, 200, 100}, removed{300, 190, 460},
            peer{240, 160, 820}, fresh{200, 120, 1180};
        GraphicsBackend backend;
        const auto ordinaryId = backend.initialize({ordinary.window});
        const auto removedId = backend.attachPresentationTarget(compositionFor(removed.window));
        const auto peerId = backend.attachPresentationTarget(compositionFor(peer.window));
        if (!require(ordinaryId.isValid() && removedId.isValid() && peerId.isValid()
                && backend.setPresentationPacingTarget(peerId),
                "invalid-endpoint published peers and preferred pacing preconditions")) return false;
        const auto before = backend.compositionDiagnosticsForTesting();
        const auto generation = backend.presentationTargetInfo(ordinaryId).deviceGeneration;
        const HWND oldWindow = removed.window;
        removed.destroy();
        if (!require(IsWindow(oldWindow) == FALSE && IsWindow(peer.window) != FALSE,
                "only the removed DComp endpoint becomes invalid")) return false;
        backend.failNextCompositionOperationForTesting(stage);
        if (explicitDetach)
        {
            if (!require(backend.detachPresentationTarget(removedId),
                    "invalid DComp endpoint detaches logically")) return false;
        }
        else if (!require(backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable,
                "invalid DComp endpoint is reconciled with ordinary fallback")) return false;
        const auto after = backend.compositionDiagnosticsForTesting();
        if (!require(after.armedFailurePoint == CompositionFailurePoint::None
                && after.lastDisconnectResult == static_cast<std::int32_t>(E_FAIL)
                && after.commitCount == before.commitCount + 1U
                && after.waitCount == before.waitCount + 1U
                && backend.hasCurrentGraphicsGenerationForTesting()
                && !backend.pendingGraphicsRecoveryForTesting()
                && backend.presentationTargetInfo(ordinaryId).deviceGeneration == generation
                && backend.presentationTargetCountForTesting() == (explicitDetach ? 2U : 3U)
                && !backend.pendingTargetLocalRecoveryForTesting(removedId)
                && !backend.presentationTargetInfo(removedId).ready,
                "failed invalid-endpoint setter remains authoritative after Commit and Wait"))
            return false;
        if (!require(after.trustState == CompositionTrustForTesting::UntrustedPending
                && !backend.presentationTargetInfo(peerId).ready
                && backend.presentationTargetInfo(ordinaryId).ready
                && backend.preferredPacingTargetForTesting() == peerId
                && backend.pacingPresentationTargetForTesting() == ordinaryId
                && !backend.attachPresentationTarget(compositionFor(fresh.window)).isValid(),
                "invalid-endpoint failed disconnect quarantines surviving DComp peer"))
        {
            std::cerr << "Observed trust: " << compositionTrustName(after.trustState)
                << "; peer ready: " << backend.presentationTargetInfo(peerId).ready << '\n';
            return false;
        }
        const auto peerPresents = backend.presentationTargetDiagnosticsForTesting(peerId).presentAttemptCount;
        const auto commits = after.commitCount;
        const auto waits = after.waitCount;
        backend.failNextCompositionOperationForTesting(CompositionFailurePoint::CapabilityCreation);
        if (!require(backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable
                && controlPresentedResult(backend, ordinaryId),
                "ordinary HWND remains usable after invalid-endpoint containment")) return false;
        backend.renderPrepared({});
        if (!require(backend.presentationTargetDiagnosticsForTesting(peerId).presentAttemptCount
                    == peerPresents
                && backend.compositionDiagnosticsForTesting().commitCount == commits
                && backend.compositionDiagnosticsForTesting().waitCount == waits
                && backend.compositionDiagnosticsForTesting().trustState
                    == CompositionTrustForTesting::UntrustedSpent
                && backend.compositionDiagnosticsForTesting().armedFailurePoint
                    == CompositionFailurePoint::None,
                "invalid-endpoint containment has no later DComp Present or cleanup retry"))
            return false;
    }
    return true;
}

bool verifyInvalidEndpointFailedDisconnectResetsEmptyCapability(bool explicitDetach)
{
    TestWindow ordinary{320, 200, 100}, removed{300, 190, 460}, fresh{240, 160, 820};
    GraphicsBackend backend;
    const auto ordinaryId = backend.initialize({ordinary.window});
    const auto removedId = backend.attachPresentationTarget(compositionFor(removed.window));
    if (!require(ordinaryId.isValid() && removedId.isValid(),
            "invalid-endpoint zero-peer preconditions")) return false;
    const auto before = backend.compositionDiagnosticsForTesting();
    const auto generation = backend.presentationTargetInfo(ordinaryId).deviceGeneration;
    removed.destroy();
    backend.failNextCompositionOperationForTesting(CompositionFailurePoint::DisconnectSetRoot);
    if (explicitDetach)
    {
        if (!require(backend.detachPresentationTarget(removedId),
                "final invalid DComp endpoint detaches")) return false;
    }
    else if (!require(backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable,
            "final invalid DComp endpoint reconciles")) return false;
    const auto empty = backend.compositionDiagnosticsForTesting();
    if (!require(empty.lastDisconnectResult == static_cast<std::int32_t>(E_FAIL)
            && empty.commitCount == before.commitCount + 1U
            && empty.waitCount == before.waitCount + 1U
            && empty.capabilityIdentity == 0U
            && empty.trustState == CompositionTrustForTesting::Trusted
            && backend.presentationTargetCountForTesting() == (explicitDetach ? 1U : 2U)
            && !backend.pendingTargetLocalRecoveryForTesting(removedId)
            && backend.hasCurrentGraphicsGenerationForTesting()
            && backend.presentationTargetInfo(ordinaryId).deviceGeneration == generation,
            "failed final invalid-endpoint disconnect retires uncertain capability"))
        return false;
    const auto freshId = backend.attachPresentationTarget(compositionFor(fresh.window));
    const auto freshState = backend.compositionDiagnosticsForTesting();
    return require(freshId.isValid() && backend.presentationTargetInfo(freshId).ready
            && !backend.pendingTargetLocalRecoveryForTesting(freshId)
            && freshState.capabilityIdentity != 0U
            && freshState.capabilityCreationCount == empty.capabilityCreationCount + 1U
            && backend.presentationTargetInfo(freshId).deviceGeneration == generation,
        "later attachment creates fresh capability on the same healthy D3D generation");
}

bool verifyInvalidEndpointDisconnectDeviceLost(bool explicitDetach)
{
    TestWindow ordinary{320, 200, 100}, removed{300, 190, 460}, peer{240, 160, 820};
    GraphicsBackend backend;
    const auto ordinaryId = backend.initialize({ordinary.window});
    const auto removedId = backend.attachPresentationTarget(compositionFor(removed.window));
    const auto peerId = backend.attachPresentationTarget(compositionFor(peer.window));
    if (!require(ordinaryId.isValid() && removedId.isValid() && peerId.isValid(),
            "invalid-endpoint DeviceLost preconditions")) return false;
    removed.destroy();
    if (!require(backend.forceNextCompositionOperationResultForTesting(
                CompositionFailurePoint::DisconnectSetContent,
                static_cast<std::int32_t>(DXGI_ERROR_DEVICE_REMOVED)),
            "arm DeviceLost-class required disconnect")) return false;
    if (explicitDetach)
    {
        if (!require(backend.detachPresentationTarget(removedId),
                "invalid DeviceLost endpoint detaches logically")) return false;
    }
    else if (!require(backend.beginFrame() == FramePreparationResult::DeviceLost,
            "invalid endpoint reconciliation reports DeviceLost")) return false;
    return require(!backend.hasCurrentGraphicsGenerationForTesting()
            && backend.pendingGraphicsRecoveryForTesting()
            && !backend.pendingTargetLocalRecoveryForTesting(removedId)
            && backend.presentationTargetCountForTesting() == (explicitDetach ? 2U : 3U),
        "invalid-endpoint DeviceLost takes Slice 5 without local authority");
}

bool verifyMixedRenderingAndLifecycle()
{
    TestWindow ordinary{320, 200, 100}, composition{320, 200, 460}, other{240, 180, 820};
    BindingProbe probe;
    if (!require(ordinary.window && composition.window && other.window && SUCCEEDED(probe.result),
            "mixed HWND preconditions")) return false;
    GraphicsBackend backend;
    const auto a = backend.initialize({ordinary.window});
    if (!require(a.isValid() && backend.compositionDiagnosticsForTesting().capabilityIdentity == 0,
            "ordinary startup does not create DirectComposition capability")) return false;
    const auto b = backend.attachPresentationTarget(compositionFor(composition.window));
    const auto c = backend.attachPresentationTarget(compositionFor(other.window));
    const auto capability = backend.compositionDiagnosticsForTesting();
    if (!require(b.isValid() && c.isValid() && capability.capabilityIdentity != 0
            && capability.capabilityCreationCount == 1, "two composition targets reuse one capability")) return false;
    const auto deviceGeneration = backend.presentationTargetInfo(a).deviceGeneration;
    if (!require(backend.presentationTargetInfo(b).deviceGeneration == deviceGeneration
            && backend.presentationTargetInfo(c).deviceGeneration == deviceGeneration,
            "all transports share the D3D11 device generation")) return false;
    const auto bResources = backend.presentationTargetDiagnosticsForTesting(b);
    const auto aResources = backend.presentationTargetDiagnosticsForTesting(a);
    if (!require(bResources.transport == Composition && bResources.hasCompositionTarget
            && bResources.hasCompositionVisual && bResources.compositionTreeSubmitted
            && !aResources.hasCompositionTarget && !aResources.hasCompositionVisual,
            "Ready requires composition payload; ordinary owns no composition resources")) return false;
    const auto nextId = backend.nextPresentationTargetValueForTesting();
    if (!require(!backend.attachPresentationTarget({composition.window}).isValid()
            && backend.nextPresentationTargetValueForTesting() == nextId,
            "duplicate HWND cannot change immutable transport")) return false;
    if (!require(backend.setPresentationPacingTarget(b), "composition may be pacing target")) return false;
    for (unsigned char frame = 0; frame < 4; ++frame)
    {
        const std::array<unsigned char, 4> pixel{13, 219, frame, 255};
        if (!require(uploadPixel(backend, pixel)
                && backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable
                && controlPresentedResult(backend, a)
                && controlPresentedResult(backend, b)
                && controlPresentedResult(backend, c)
                && capture(backend, a) && capture(backend, b) && capture(backend, c),
                "one shared frame upload prepares all targets")) return false;
        backend.renderPrepared({});
        if (!pixelMatches(backend, a, pixel) || !pixelMatches(backend, b, pixel)
                || !pixelMatches(backend, c, pixel)) return false;
        const auto ad = backend.presentationTargetDiagnosticsForTesting(a);
        const auto bd = backend.presentationTargetDiagnosticsForTesting(b);
        const auto cd = backend.presentationTargetDiagnosticsForTesting(c);
        if (!require(ad.drawCount == frame + 1U && bd.drawCount == frame + 1U
                && cd.drawCount == frame + 1U && backend.mediaUploadCountForTesting() == frame + 1U,
                "draw once per target, media upload once per frame")) return false;
        if (!require(ad.lastPresentSyncInterval == 0 && ad.lastPresentFlags == DXGI_PRESENT_DO_NOT_WAIT
                && cd.lastPresentSyncInterval == 0 && cd.lastPresentFlags == DXGI_PRESENT_DO_NOT_WAIT
                && bd.lastPresentSyncInterval == 1 && bd.lastPresentFlags == 0
                && ad.lastPresentOrder < bd.lastPresentOrder && cd.lastPresentOrder < bd.lastPresentOrder,
                "mixed non-pacers submit without intentional waiting before composition pacer")) return false;
        if (!require(!backend.presentationTargetInfo(b).occluded && !backend.presentationTargetInfo(c).occluded,
                "composition flip chains do not manufacture ordinary HWND occlusion")) return false;
    }
    if (!require(backend.compositionDiagnosticsForTesting().commitCount == capability.commitCount
            && backend.compositionDiagnosticsForTesting().waitCount == capability.waitCount,
            "content frames perform no composition Commit or lifecycle wait")) return false;
    if (!require(backend.setPresentationPacingTarget(a)
            && backend.forceNextDxgiPresentResultForTesting(b,
                static_cast<std::int32_t>(DXGI_ERROR_WAS_STILL_DRAWING)), "control composition non-pacer busy result")) return false;
    if (!require(backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable
            && controlPresentedResult(backend, a) && controlPresentedResult(backend, c),
            "prepare ordinary pacing frame")) return false;
    backend.renderPrepared({});
    const auto ad = backend.presentationTargetDiagnosticsForTesting(a);
    const auto bd = backend.presentationTargetDiagnosticsForTesting(b);
    if (!require(backend.presentationTargetInfo(b).ready && !backend.presentationTargetInfo(b).occluded
            && bd.lastPresentSyncInterval == 0 && bd.lastPresentFlags == DXGI_PRESENT_DO_NOT_WAIT
            && ad.lastPresentSyncInterval == 1 && ad.lastPresentFlags == 0
            && bd.lastPresentOrder < ad.lastPresentOrder, "composition busy skip is local; ordinary pacing works")) return false;
    if (!require(backend.detachPresentationTarget(a) && backend.presentationTargetInfo(b).ready
            && backend.pacingPresentationTargetForTesting() == b, "ordinary detach leaves composition operational")) return false;
    if (!require(backend.detachPresentationTarget(b) && SUCCEEDED(probe.reserve(composition.window))
            && backend.presentationTargetInfo(c).ready && backend.pacingPresentationTargetForTesting() == c,
            "composition detach frees binding and preserves other composition target")) return false;
    if (!require(backend.detachPresentationTarget(c) && backend.presentationTargetCountForTesting() == 0,
            "final composition detach leaves no presentation targets")) return false;
    const auto afterFinalDetach = backend.compositionDiagnosticsForTesting();
    const bool capabilityRetired = require(afterFinalDetach.capabilityIdentity == 0,
        "final composition detach retires the shared capability");
    if (!capabilityRetired)
        std::cerr << "Observed capability present after final detach: "
            << (afterFinalDetach.capabilityIdentity != 0) << '\n';
    if (!require(backend.beginFrame() == FramePreparationResult::NoReadyTargets,
            "zero-target frame keeps the D3D generation alive")) return false;
    const auto fresh = backend.attachPresentationTarget(compositionFor(composition.window));
    if (!require(fresh.isValid() && fresh != b && fresh != c
            && backend.presentationTargetInfo(fresh).deviceGeneration == deviceGeneration
            && controlPresentedResult(backend, fresh) && capture(backend, fresh),
            "fresh target retains the D3D generation and media path")) return false;
    const auto afterReattach = backend.compositionDiagnosticsForTesting();
    const bool capabilityRecreated = require(afterReattach.capabilityCreationCount
            == capability.capabilityCreationCount + 1,
        "later composition attachment creates a fresh capability");
    if (!capabilityRecreated)
        std::cerr << "Observed capability creations after reattach: "
            << afterReattach.capabilityCreationCount << "; expected: "
            << capability.capabilityCreationCount + 1 << '\n';
    backend.renderPrepared({});
    if (!pixelMatches(backend, fresh, {13, 219, 3, 255})) return false;
    backend.shutdown();
    return require(backend.compositionDiagnosticsForTesting().capabilityIdentity == 0
        && SUCCEEDED(probe.reserve(composition.window)), "shutdown releases targets before shared capability")
        && capabilityRetired && capabilityRecreated;
}

bool verifyResizeAndUnavailableIsolation()
{
    TestWindow composition{420, 240}, ordinary{320, 200, 800};
    if (!require(composition.window && ordinary.window, "resize HWND preconditions")) return false;
    GraphicsBackend backend;
    const auto a = backend.initialize(compositionFor(composition.window));
    const auto b = backend.attachPresentationTarget({ordinary.window});
    if (!require(a.isValid() && b.isValid(), "resize targets initialized")) return false;
    const auto initialA = backend.presentationTargetInfo(a);
    const auto initialB = backend.presentationTargetInfo(b);
    const auto commits = backend.compositionDiagnosticsForTesting().commitCount;
    constexpr std::array<unsigned char, 4> pixel{41, 197, 89, 255};
    if (!require(uploadPixel(backend, pixel) && composition.resize(640, 360)
            && backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable,
            "native composition resize reconciles")) return false;
    if (!require(backend.presentationTargetInfo(a).clientWidth == 640
            && backend.presentationTargetInfo(a).clientHeight == 360
            && backend.presentationTargetInfo(a).targetGeneration > initialA.targetGeneration
            && backend.presentationTargetInfo(b).targetGeneration == initialB.targetGeneration
            && controlPresentedResult(backend, a) && capture(backend, a)
            && controlPresentedResult(backend, b), "resize is target-local")) return false;
    backend.renderPrepared({});
    if (!pixelMatches(backend, a, pixel)) return false;
    const auto beforePartial = backend.presentationTargetInfo(a);
    const auto beforeResizes = backend.presentationTargetDiagnosticsForTesting(a).successfulResizeCount;
    backend.failNextPresentationRenderTargetViewCreationForTesting();
    if (!require(composition.resize(800, 450)
            && backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable,
            "composition RTV failure leaves ordinary target ready")) return false;
    const auto failed = backend.presentationTargetInfo(a);
    if (!require(failed.failed && failed.clientWidth == 800 && failed.clientHeight == 450
            && failed.targetGeneration == beforePartial.targetGeneration
            && backend.presentationTargetDiagnosticsForTesting(a).successfulResizeCount == beforeResizes + 1
            && backend.pacingPresentationTargetForTesting() == b,
            "successful ResizeBuffers is recorded before failed RTV; failed pacer falls back")) return false;
    if (!require(backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable
            && backend.presentationTargetInfo(a).ready
            && backend.presentationTargetDiagnosticsForTesting(a).successfulResizeCount == beforeResizes + 1,
            "one authorized same-size RTV recovery does not repeat ResizeBuffers")) return false;
    if (!require(composition.resize(640, 360)
            && backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable
            && backend.presentationTargetInfo(a).ready
            && backend.presentationTargetDiagnosticsForTesting(a).successfulResizeCount == beforeResizes + 2,
            "changed native size recovers with real ResizeBuffers")) return false;
    if (!require(backend.setPresentationPacingTarget(a) && composition.resize(0, 0)
            && backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable
            && backend.presentationTargetInfo(a).suspended
            && backend.pacingPresentationTargetForTesting() == a
            && backend.presentationTargetInfo().clientWidth == initialB.clientWidth,
            "temporary suspension preserves selected ID, excludes false Ready geometry")) return false;
    if (!require(composition.resize(640, 360)
            && backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable
            && backend.presentationTargetInfo(a).ready
            && backend.presentationTargetDiagnosticsForTesting(a).successfulResizeCount == beforeResizes + 2
            && backend.compositionDiagnosticsForTesting().commitCount == commits,
            "zero size never requests composition resize or tree Commit")) return false;
    composition.destroy();
    if (!require(backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable
            && backend.presentationTargetInfo(a).failed
            && !backend.presentationTargetDiagnosticsForTesting(a).hasCompositionTarget
            && !backend.presentationTargetDiagnosticsForTesting(a).hasCompositionVisual
            && backend.presentationTargetInfo(b).ready
            && controlPresentedResult(backend, b) && capture(backend, b),
            "unavailable composition HWND releases binding without corrupting ordinary target")) return false;
    backend.renderPrepared({});
    return pixelMatches(backend, b, pixel) && require(backend.detachPresentationTarget(a), "detach unavailable target");
}

bool verifyInitiallySuspendedComposition()
{
    TestWindow ordinary, zero{0, 0, 800};
    GraphicsBackend backend;
    const auto a = backend.initialize({ordinary.window});
    const auto z = backend.attachPresentationTarget(compositionFor(zero.window));
    const auto diagnostics = backend.presentationTargetDiagnosticsForTesting(z);
    if (!require(a.isValid() && z.isValid() && backend.presentationTargetInfo(z).suspended
            && diagnostics.transport == Composition && !diagnostics.hasCompositionTarget
            && !diagnostics.hasCompositionVisual
            && backend.compositionDiagnosticsForTesting().capabilityIdentity == 0,
            "zero-size composition attaches Suspended with known transport but no native resources")) return false;
    if (!require(backend.setPresentationPacingTarget(z) && zero.resize(300, 180)
            && backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable,
            "first drawable native size realizes suspended composition transport")) return false;
    return require(backend.presentationTargetInfo(z).ready
        && backend.presentationTargetDiagnosticsForTesting(z).compositionTreeSubmitted
        && backend.compositionDiagnosticsForTesting().capabilityCreationCount == 1
        && backend.pacingPresentationTargetForTesting() == z, "lazy realization retains original target ID/pacing");
}

bool verifyPublishedTargetSharedMutationFailureHandsOffToContainment()
{
    using DesktopSceneEngine::Rendering::Detail::GenerationReplacementResult;
    for (const auto failure : {CompositionFailurePoint::SetRoot,
            CompositionFailurePoint::AttachCommit})
    {
        TestWindow ordinary{320, 200, 100}, peer{320, 200, 460}, recovering{0, 0, 820},
            retired{200, 120, 1180};
        GraphicsBackend backend;
        const auto ordinaryId = backend.initialize({ordinary.window});
        const auto peerId = backend.attachPresentationTarget(compositionFor(peer.window));
        const auto recoveringId = backend.attachPresentationTarget(compositionFor(recovering.window));
        const auto retiredId = backend.attachPresentationTarget({retired.window});
        if (!require(ordinaryId.isValid() && peerId.isValid() && recoveringId.isValid()
                && retiredId.isValid()
                && backend.presentationTargetInfo(recoveringId).suspended
                && backend.compositionDiagnosticsForTesting().trustState
                    == CompositionTrustForTesting::Trusted,
            "published suspended target and trusted DComp peer preconditions")) return false;

        // Rebuild one established logical slot as Failed with a real pending
        // local edge. The next frame, not this setup, must consume that edge.
        if (!require(backend.forceDeviceLostOnTargetDetachForTesting(retiredId)
                && backend.detachPresentationTarget(retiredId)
                && !backend.hasCurrentGraphicsGenerationForTesting()
                && backend.pendingGraphicsRecoveryForTesting()
                && recovering.resize(300, 180)
                && backend.forceNextTargetRealizationResultForTesting(
                    recoveringId, static_cast<std::int32_t>(E_FAIL))
                && backend.recoverGraphicsGeneration(nullptr)
                    == GenerationReplacementResult::GenerationAvailable
                && backend.pendingTargetLocalRecoveryForTesting(recoveringId)
                && backend.presentationTargetInfo(recoveringId).failed
                && backend.presentationTargetInfo(peerId).ready
                && backend.compositionDiagnosticsForTesting().trustState
                    == CompositionTrustForTesting::Trusted,
                "generation rebuild leaves the published DComp slot with pending local authority"))
            return false;

        // SetContent is accepted before SetRoot/Commit fails. The 7A edge is
        // spent before native work; the resulting shared ambiguity belongs to 7B.
        backend.failNextCompositionOperationForTesting(failure);
        if (!require(backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable,
                "published DComp target begins one drawable reconstruction")) return false;
        const auto after = backend.compositionDiagnosticsForTesting();
        if (!require(after.trustState == CompositionTrustForTesting::UntrustedPending
                && backend.presentationTargetInfo(recoveringId).failed
                && !backend.pendingTargetLocalRecoveryForTesting(recoveringId)
                && !backend.presentationTargetInfo(peerId).ready
                && backend.presentationTargetInfo(ordinaryId).ready,
                "shared setter/Commit failure quarantines DComp peers, not ordinary HWND"))
            return false;
        if (!require(backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable
                && backend.compositionDiagnosticsForTesting().trustState
                    == CompositionTrustForTesting::Trusted
                && backend.compositionDiagnosticsForTesting().capabilityCreationCount
                    == after.capabilityCreationCount + 1U
                && backend.compositionDiagnosticsForTesting().commitCount == after.commitCount + 1U
                && backend.compositionDiagnosticsForTesting().waitCount == after.waitCount + 1U
                && backend.presentationTargetInfo(recoveringId).ready
                && backend.presentationTargetInfo(peerId).ready
                && !backend.pendingTargetLocalRecoveryForTesting(recoveringId),
            "7A shared-mutation handoff is reconstructed by one 7B batch without minting a local edge"))
            return false;
        const auto recovered = backend.compositionDiagnosticsForTesting();
        for (int frame = 0; frame < 2; ++frame)
        {
            if (!require(backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable
                    && backend.compositionDiagnosticsForTesting().trustState
                        == CompositionTrustForTesting::Trusted
                    && !backend.pendingTargetLocalRecoveryForTesting(recoveringId)
                    && backend.compositionDiagnosticsForTesting().commitCount == recovered.commitCount
                    && backend.compositionDiagnosticsForTesting().waitCount == recovered.waitCount
                    && backend.presentationTargetInfo(ordinaryId).ready,
                    "7B publication does not trigger an extra 7A reconstruction on subsequent frames"))
                return false;
        }
    }
    return true;
}

bool verifyTrustedSameHwndReconstructionWaitsForOldBinding()
{
    TestWindow ordinary{320, 200, 100}, recovering{0, 0, 460};
    BindingProbe probe;
    GraphicsBackend backend;
    const auto ordinaryId = backend.initialize({ordinary.window});
    const auto recoveringId = backend.attachPresentationTarget(compositionFor(recovering.window));
    const auto deviceGeneration = backend.presentationTargetInfo(ordinaryId).deviceGeneration;
    if (!require(ordinaryId.isValid() && recoveringId.isValid()
            && backend.presentationTargetInfo(recoveringId).suspended
            && SUCCEEDED(probe.result),
            "trusted same-HWND reconstruction preconditions")) return false;

    backend.failNextCompositionOperationForTesting(CompositionFailurePoint::VisualCreation);
    if (!require(recovering.resize(300, 180)
            && backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable
            && backend.presentationTargetInfo(recoveringId).failed
            && backend.compositionDiagnosticsForTesting().trustState
                == CompositionTrustForTesting::Trusted,
            "local creation failure rolls back without untrusting the shared domain"))
        return false;
    const auto afterFailure = backend.compositionDiagnosticsForTesting();
    if (!require(afterFailure.commitCount >= 1 && afterFailure.waitCount >= 1
            && SUCCEEDED(probe.reserve(recovering.window)),
            "old target disconnect Commit and required completion precede same-HWND reuse"))
        return false;
    if (!require(backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable
            && backend.presentationTargetInfo(recoveringId).failed
            && backend.compositionDiagnosticsForTesting().commitCount
                == afterFailure.commitCount,
            "spent local attempt does not retry at unchanged size")) return false;
    if (!require(recovering.resize(320, 200)
            && backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable
            && backend.presentationTargetInfo(recoveringId).ready
            && backend.presentationTargetInfo(recoveringId).targetGeneration > 0
            && backend.presentationTargetInfo(recoveringId).deviceGeneration == deviceGeneration
            && FAILED(probe.reserve(recovering.window)),
            "real size transition permits fresh same-HWND target after old binding ends"))
        return false;
    return true;
}

bool verifyDisconnectFailures()
{
    for (const auto stage : {CompositionFailurePoint::DisconnectSetRoot,
            CompositionFailurePoint::DisconnectSetContent,
            CompositionFailurePoint::DisconnectCommit,
            CompositionFailurePoint::DisconnectWait})
    {
        TestWindow ordinary, composition{320, 200, 800};
        BindingProbe probe;
        GraphicsBackend backend;
        const auto a = backend.initialize({ordinary.window});
        const auto b = backend.attachPresentationTarget(compositionFor(composition.window));
        if (!require(a.isValid() && b.isValid() && SUCCEEDED(probe.result), "disconnect preconditions")) return false;
        const auto generation = backend.presentationTargetInfo(a).deviceGeneration;
        backend.failNextCompositionOperationForTesting(stage);
        if (!require(backend.detachPresentationTarget(b)
                && backend.presentationTargetCountForTesting() == 1
                && backend.compositionDiagnosticsForTesting().lastDisconnectResult == static_cast<std::int32_t>(E_FAIL)
                && SUCCEEDED(probe.reserve(composition.window)),
                "disconnect failure completes logical/native removal")) return false;
        const auto afterDetach = backend.compositionDiagnosticsForTesting();
        const bool retired = require(afterDetach.capabilityIdentity == 0
                && afterDetach.trustState == CompositionTrustForTesting::Trusted
                && backend.presentationTargetInfo(a).deviceGeneration == generation,
            "failed final composition disconnect returns to trusted-empty on the same D3D generation");
        if (!retired)
            std::cerr << "Observed old capability still active after failed disconnect: "
                << (afterDetach.capabilityIdentity != 0) << '\n';
        if (!require(backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable
                && backend.presentationTargetInfo(a).ready,
                "failed composition disconnect leaves ordinary target independently usable")) return false;
        const auto fresh = backend.attachPresentationTarget(compositionFor(composition.window));
        if (!require(fresh.isValid() && fresh != b,
                "same HWND reattaches after failed final disconnect")) return false;
        const auto afterReattach = backend.compositionDiagnosticsForTesting();
        const bool recreated = require(afterReattach.capabilityCreationCount
                == afterDetach.capabilityCreationCount + 1,
            "same HWND reattachment uses a fresh composition capability");
        if (!recreated)
            std::cerr << "Observed capability creations after failed-disconnect reattach: "
                << afterReattach.capabilityCreationCount << "; expected: "
                << afterDetach.capabilityCreationCount + 1 << '\n';
        if (!retired || !recreated) return false;
    }
    return true;
}

bool verifySharedCompositionFailureQuarantinesPeers()
{
    TestWindow ordinary{320, 200, 100}, removed{300, 190, 460}, peer{240, 160, 820};
    if (!require(ordinary.window && removed.window && peer.window,
            "shared disconnect HWND preconditions")) return false;
    GraphicsBackend backend;
    const auto ordinaryId = backend.initialize({ordinary.window});
    const auto removedId = backend.attachPresentationTarget(compositionFor(removed.window));
    const auto peerId = backend.attachPresentationTarget(compositionFor(peer.window));
    if (!require(ordinaryId.isValid() && removedId.isValid() && peerId.isValid()
            && backend.presentationTargetInfo(ordinaryId).ready
            && backend.presentationTargetInfo(peerId).ready,
            "ordinary and two composition targets are initially ready")) return false;
    const auto d3dGeneration = backend.presentationTargetInfo(ordinaryId).deviceGeneration;
    if (!require(backend.setPresentationPacingTarget(peerId)
            && backend.preferredPacingTargetForTesting() == peerId,
            "composition peer is preferred pacing target")) return false;

    // The disconnect setters and Commit have been accepted; the required wait fails
    // while the ordinary D3D11 presentation target and both surviving HWNDs remain valid.
    backend.failNextCompositionOperationForTesting(CompositionFailurePoint::DisconnectWait);
    if (!require(backend.detachPresentationTarget(removedId)
            && backend.compositionDiagnosticsForTesting().lastDisconnectResult
                == static_cast<std::int32_t>(E_FAIL)
            && backend.presentationTargetCountForTesting() == 2
            && IsWindow(ordinary.window) && IsWindow(removed.window) && IsWindow(peer.window)
            && backend.presentationTargetInfo(ordinaryId).ready
            && backend.presentationTargetInfo(ordinaryId).deviceGeneration == d3dGeneration
            && backend.preferredPacingTargetForTesting() == peerId,
            "failed shared disconnect preserves healthy ordinary target and pacing intent")) return false;

    const auto peerInfo = backend.presentationTargetInfo(peerId);
    const auto selected = backend.presentationTargetInfo();
    const auto ordinaryInfo = backend.presentationTargetInfo(ordinaryId);
    const bool peerUnavailable = require(!peerInfo.ready,
        "shared disconnect failure makes surviving composition peer effectively unavailable");
    const bool ordinaryFallback = require(selected.clientWidth == ordinaryInfo.clientWidth,
        "shared disconnect failure selects ordinary target as usable pacing fallback");
    if (!peerUnavailable || !ordinaryFallback)
        std::cerr << "Observed peer ready: " << peerInfo.ready
            << "; selected width: " << selected.clientWidth
            << "; ordinary width: " << ordinaryInfo.clientWidth << '\n';
    return peerUnavailable && ordinaryFallback;
}

bool verifyRequiredDisconnectSetterFailureContainsPeers()
{
    for (const auto stage : {CompositionFailurePoint::DisconnectSetRoot,
            CompositionFailurePoint::DisconnectSetContent})
    {
        TestWindow ordinary{320, 200, 100}, removed{300, 190, 460}, peer{240, 160, 820};
        GraphicsBackend backend;
        const auto ordinaryId = backend.initialize({ordinary.window});
        const auto removedId = backend.attachPresentationTarget(compositionFor(removed.window));
        const auto peerId = backend.attachPresentationTarget(compositionFor(peer.window));
        if (!require(ordinaryId.isValid() && removedId.isValid() && peerId.isValid()
                && backend.setPresentationPacingTarget(peerId),
                "setter-failure target and pacing preconditions")) return false;
        const auto before = backend.compositionDiagnosticsForTesting();
        const auto generation = backend.presentationTargetInfo(ordinaryId).deviceGeneration;
        backend.failNextCompositionOperationForTesting(stage);
        if (!require(backend.detachPresentationTarget(removedId),
                "logical detach completes after required setter failure")) return false;
        const auto after = backend.compositionDiagnosticsForTesting();
        if (!require(after.lastDisconnectResult == static_cast<std::int32_t>(E_FAIL)
                && after.commitCount == before.commitCount + 1
                && after.waitCount == before.waitCount + 1
                && backend.hasCurrentGraphicsGenerationForTesting()
                && !backend.pendingGraphicsRecoveryForTesting()
                && backend.presentationTargetInfo(ordinaryId).deviceGeneration == generation
                && IsWindow(ordinary.window) && IsWindow(removed.window) && IsWindow(peer.window),
                "setter failure precedes successful Commit/Wait on healthy D3D and valid HWNDs"))
            return false;
        if (!require(after.trustState == CompositionTrustForTesting::UntrustedPending
                && !backend.presentationTargetInfo(peerId).ready
                && backend.presentationTargetInfo(ordinaryId).ready
                && backend.preferredPacingTargetForTesting() == peerId
                && backend.pacingPresentationTargetForTesting() == ordinaryId
                && !backend.attachPresentationTarget(compositionFor(removed.window)).isValid(),
                "required setter failure quarantines surviving DComp and forbids trusted rebind"))
            return false;
        const auto peerPresents = backend.presentationTargetDiagnosticsForTesting(peerId).presentAttemptCount;
        const auto commits = after.commitCount;
        const auto waits = after.waitCount;
        backend.failNextCompositionOperationForTesting(CompositionFailurePoint::CapabilityCreation);
        if (!require(backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable
                && controlPresentedResult(backend, ordinaryId),
                "ordinary target remains independently usable after setter failure")) return false;
        backend.renderPrepared({});
        if (!require(backend.presentationTargetDiagnosticsForTesting(peerId).presentAttemptCount
                    == peerPresents
                && backend.compositionDiagnosticsForTesting().commitCount == commits
                && backend.compositionDiagnosticsForTesting().waitCount == waits
                && backend.compositionDiagnosticsForTesting().trustState
                    == CompositionTrustForTesting::UntrustedSpent
                && backend.compositionDiagnosticsForTesting().armedFailurePoint
                    == CompositionFailurePoint::None,
                "contained old domain makes no later peer Present or transaction")) return false;
    }
    return true;
}

bool verifyDisconnectSetterDeviceLostUsesGenerationRecovery()
{
    using DesktopSceneEngine::Rendering::Detail::GenerationReplacementResult;
    TestWindow ordinary{320, 200, 100}, removed{300, 190, 460}, peer{240, 160, 820};
    GraphicsBackend backend;
    const auto ordinaryId = backend.initialize({ordinary.window});
    const auto removedId = backend.attachPresentationTarget(compositionFor(removed.window));
    const auto peerId = backend.attachPresentationTarget(compositionFor(peer.window));
    if (!require(ordinaryId.isValid() && removedId.isValid() && peerId.isValid(),
            "DeviceLost setter target preconditions")) return false;
    const auto generation = backend.presentationTargetInfo(ordinaryId).deviceGeneration;
    if (!require(backend.forceNextCompositionOperationResultForTesting(
                CompositionFailurePoint::DisconnectSetContent,
                static_cast<std::int32_t>(DXGI_ERROR_DEVICE_REMOVED))
            && backend.detachPresentationTarget(removedId),
            "required disconnect setter reports deterministic DeviceLost")) return false;
    if (!require(!backend.hasCurrentGraphicsGenerationForTesting()
            && backend.pendingGraphicsRecoveryForTesting()
            && backend.compositionDiagnosticsForTesting().trustState
                == CompositionTrustForTesting::Unavailable
            && backend.presentationTargetCountForTesting() == 2U
            && !backend.pendingTargetLocalRecoveryForTesting(peerId),
            "DeviceLost takes Slice 5 generation path, not healthy shared containment"))
        return false;
    if (!require(backend.recoverGraphicsGeneration(nullptr)
                == GenerationReplacementResult::GenerationAvailable
            && backend.presentationTargetInfo(ordinaryId).ready
            && backend.presentationTargetInfo(peerId).ready
            && backend.presentationTargetInfo(ordinaryId).deviceGeneration > generation
            && backend.compositionDiagnosticsForTesting().trustState
                == CompositionTrustForTesting::Trusted
            && !backend.pendingTargetLocalRecoveryForTesting(peerId),
            "fresh D3D generation does not inherit a target-local recovery edge"))
        return false;
    return true;
}

bool verifyContainedDomainFinalDetachResetsToTrustedEmpty()
{
    TestWindow ordinary{320, 200, 100}, removed{300, 190, 460}, peer{240, 160, 820};
    GraphicsBackend backend;
    const auto ordinaryId = backend.initialize({ordinary.window});
    const auto removedId = backend.attachPresentationTarget(compositionFor(removed.window));
    const auto peerId = backend.attachPresentationTarget(compositionFor(peer.window));
    if (!require(ordinaryId.isValid() && removedId.isValid() && peerId.isValid(),
            "contained zero-target reset preconditions")) return false;
    const auto before = backend.compositionDiagnosticsForTesting();
    const auto generation = backend.presentationTargetInfo(ordinaryId).deviceGeneration;
    backend.failNextCompositionOperationForTesting(CompositionFailurePoint::DisconnectWait);
    if (!require(backend.detachPresentationTarget(removedId)
            && backend.compositionDiagnosticsForTesting().trustState
                == CompositionTrustForTesting::UntrustedPending
            && backend.compositionDiagnosticsForTesting().capabilityIdentity
                == before.capabilityIdentity
            && !backend.presentationTargetInfo(peerId).ready
            && backend.presentationTargetInfo(ordinaryId).ready,
            "failed required wait contains a surviving logical DComp peer")) return false;

    if (!require(backend.detachPresentationTarget(peerId)
            && backend.presentationTargetCountForTesting() == 1U,
            "final logical DComp target detaches from contained domain")) return false;
    const auto empty = backend.compositionDiagnosticsForTesting();
    if (!require(empty.trustState == CompositionTrustForTesting::Trusted
            && empty.capabilityIdentity == 0U
            && empty.capabilityCreationCount == before.capabilityCreationCount
            && backend.hasCurrentGraphicsGenerationForTesting()
            && !backend.pendingGraphicsRecoveryForTesting()
            && backend.presentationTargetInfo(ordinaryId).ready
            && backend.presentationTargetInfo(ordinaryId).deviceGeneration == generation,
            "zero DComp targets retire contained capability on the same healthy D3D generation"))
        return false;

    const auto freshId = backend.attachPresentationTarget(compositionFor(peer.window));
    const auto fresh = backend.compositionDiagnosticsForTesting();
    return require(freshId.isValid() && freshId != peerId && freshId != removedId
            && backend.presentationTargetInfo(freshId).ready
            && !backend.pendingTargetLocalRecoveryForTesting(freshId)
            && fresh.trustState == CompositionTrustForTesting::Trusted
            && fresh.capabilityIdentity != 0U
            && fresh.capabilityCreationCount == empty.capabilityCreationCount + 1U
            && backend.presentationTargetInfo(freshId).deviceGeneration == generation
            && backend.presentationTargetInfo(ordinaryId).deviceGeneration == generation,
        "fresh attachment lazily creates a new trusted capability without replacing D3D");
}

bool verifySharedCompositionFailureRequiresFreshDomain()
{
    TestWindow ordinary{320, 200, 100}, removed{300, 190, 460}, peer{240, 160, 820};
    BindingProbe probe;
    if (!require(ordinary.window && removed.window && peer.window,
            "structural recovery HWND preconditions")) return false;
    GraphicsBackend backend;
    const auto ordinaryId = backend.initialize({ordinary.window});
    const auto removedId = backend.attachPresentationTarget(compositionFor(removed.window));
    const auto peerId = backend.attachPresentationTarget(compositionFor(peer.window));
    if (!require(ordinaryId.isValid() && removedId.isValid() && peerId.isValid(),
            "structural recovery targets attach")) return false;
    const auto before = backend.compositionDiagnosticsForTesting();
    const auto d3dGeneration = backend.presentationTargetInfo(ordinaryId).deviceGeneration;
    const auto peerGeneration = backend.presentationTargetInfo(peerId).targetGeneration;
    if (!require(before.capabilityIdentity != 0 && before.capabilityCreationCount == 1,
            "one shared composition capability exists before disconnect failure")) return false;

    backend.failNextCompositionOperationForTesting(CompositionFailurePoint::DisconnectWait);
    if (!require(backend.detachPresentationTarget(removedId)
            && backend.compositionDiagnosticsForTesting().lastDisconnectResult
                == static_cast<std::int32_t>(E_FAIL)
            && IsWindow(ordinary.window) && IsWindow(removed.window) && IsWindow(peer.window)
            && backend.presentationTargetInfo(ordinaryId).deviceGeneration == d3dGeneration,
            "required disconnect wait fails without D3D11 device loss")) return false;
    const auto pending = backend.compositionDiagnosticsForTesting();
    if (!require(backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable
            && backend.presentationTargetInfo(ordinaryId).ready
            && backend.presentationTargetInfo(ordinaryId).deviceGeneration == d3dGeneration,
            "next frame retains independently usable ordinary target and D3D generation")) return false;

    const auto after = backend.compositionDiagnosticsForTesting();
    const bool freshDomain = require(after.capabilityIdentity != 0
            && after.capabilityCreationCount == before.capabilityCreationCount + 1
            && after.trustState == CompositionTrustForTesting::Trusted
            && after.commitCount == pending.commitCount + 1U
            && after.waitCount == pending.waitCount + 1U
            && backend.presentationTargetInfo(peerId).deviceGeneration == d3dGeneration
            && backend.presentationTargetInfo(peerId).targetGeneration == peerGeneration + 1U
            && backend.presentationTargetInfo(peerId).ready
            && backend.presentationTargetDiagnosticsForTesting(peerId).compositionTreeSubmitted
            && IsWindow(peer.window) && SUCCEEDED(probe.result)
            && FAILED(probe.reserve(peer.window)) && SUCCEEDED(probe.reserve(removed.window)),
        "next eligible frame proves a fresh same-HWND binding without old-domain transactions or D3D replacement");
    if (!freshDomain)
        std::cerr << "Observed capability creations: " << after.capabilityCreationCount
            << "; expected: " << before.capabilityCreationCount + 1
            << "; old capability still active: "
            << (after.capabilityIdentity == before.capabilityIdentity) << '\n';
    return freshDomain;
}

bool verifySharedRecoveryAuthorizationIsConsumedOnce()
{
    TestWindow ordinary{320, 200, 100}, removed{300, 190, 460}, peer{240, 160, 820};
    if (!require(ordinary.window && removed.window && peer.window,
            "shared recovery attempt HWND preconditions")) return false;
    GraphicsBackend backend;
    const auto ordinaryId = backend.initialize({ordinary.window});
    const auto removedId = backend.attachPresentationTarget(compositionFor(removed.window));
    const auto peerId = backend.attachPresentationTarget(compositionFor(peer.window));
    const auto initial = backend.compositionDiagnosticsForTesting();
    if (!require(ordinaryId.isValid() && removedId.isValid() && peerId.isValid()
            && initial.trustState == CompositionTrustForTesting::Trusted
            && initial.capabilityIdentity != 0 && initial.capabilityCreationCount == 1,
            "one trusted shared capability exists before recovery-attempt trigger")) return false;
    const auto d3dGeneration = backend.presentationTargetInfo(ordinaryId).deviceGeneration;
    const auto generationCreations = backend.graphicsGenerationCreationCountForTesting();

    backend.failNextCompositionOperationForTesting(CompositionFailurePoint::DisconnectWait);
    if (!require(backend.detachPresentationTarget(removedId)
            && IsWindow(ordinary.window) && IsWindow(removed.window) && IsWindow(peer.window)
            && backend.compositionDiagnosticsForTesting().lastDisconnectResult
                == static_cast<std::int32_t>(E_FAIL),
            "healthy-D3D required disconnect wait fails with a valid surviving peer")) return false;
    const auto pending = backend.compositionDiagnosticsForTesting();
    if (!require(pending.trustState == CompositionTrustForTesting::UntrustedPending
            && pending.capabilityIdentity == initial.capabilityIdentity
            && !backend.presentationTargetInfo(peerId).ready
            && backend.presentationTargetInfo(ordinaryId).ready
            && backend.presentationTargetInfo(ordinaryId).deviceGeneration == d3dGeneration
            && backend.hasCurrentGraphicsGenerationForTesting()
            && !backend.pendingGraphicsRecoveryForTesting(),
            "shared containment starts Pending without D3D11 generation loss")) return false;

    // The old capability is already present. The ordinary frame has no reason
    // to create a DComp capability, so this one-shot failure belongs to the
    // future first structural-recovery attempt, not normal target work.
    backend.failNextCompositionOperationForTesting(CompositionFailurePoint::CapabilityCreation);
    if (!require(backend.compositionDiagnosticsForTesting().armedFailurePoint
            == CompositionFailurePoint::CapabilityCreation,
            "one-shot first-recovery capability failure is armed after containment")) return false;
    const bool firstFrameReady = require(
        backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable
            && backend.presentationTargetInfo(ordinaryId).ready
            && backend.presentationTargetInfo(ordinaryId).deviceGeneration == d3dGeneration,
        "first eligible frame retains the ordinary target and D3D generation");
    const auto afterFirst = backend.compositionDiagnosticsForTesting();
    const bool consumed = require(afterFirst.trustState == CompositionTrustForTesting::UntrustedSpent,
        "first eligible frame consumes the one shared recovery authorization before native work");
    if (!consumed)
        std::cerr << "Observed trust after first eligible frame: "
            << compositionTrustName(afterFirst.trustState)
            << "; expected: UntrustedSpent\n";
    const bool failureConsumed = require(afterFirst.armedFailurePoint
            == CompositionFailurePoint::None,
        "first recovery-native operation consumes the armed one-shot capability failure");
    if (!failureConsumed)
        std::cerr << "Observed capability failure still armed after first frame\n";
    const bool oldDomainRetired = require(afterFirst.capabilityIdentity == 0
            && afterFirst.capabilityCreationCount == initial.capabilityCreationCount
            && afterFirst.commitCount == pending.commitCount
            && afterFirst.waitCount == pending.waitCount
            && !backend.presentationTargetDiagnosticsForTesting(peerId).hasCompositionTarget,
        "failed first capability-creation attempt leaves no old or fresh DComp capability");
    if (!oldDomainRetired)
        std::cerr << "Observed capability after first frame: present="
            << (afterFirst.capabilityIdentity != 0) << ", creations="
            << afterFirst.capabilityCreationCount << "; expected: absent, creations="
            << initial.capabilityCreationCount << '\n';

    bool laterOrdinaryFramesReady = true;
    for (int frame = 0; frame < 2; ++frame)
    {
        laterOrdinaryFramesReady = require(
            backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable
                && backend.presentationTargetInfo(ordinaryId).ready
                && backend.presentationTargetInfo(ordinaryId).deviceGeneration == d3dGeneration,
            "later frames retain the ordinary target without D3D replacement")
            && laterOrdinaryFramesReady;
    }
    const auto afterLater = backend.compositionDiagnosticsForTesting();
    const bool noRetry = require(afterLater.trustState == CompositionTrustForTesting::UntrustedSpent
            && afterLater.capabilityIdentity == 0
            && afterLater.capabilityCreationCount == initial.capabilityCreationCount
            && afterLater.armedFailurePoint == CompositionFailurePoint::None
            && backend.graphicsGenerationCreationCountForTesting() == generationCreations,
        "later frames cannot retry the spent shared recovery attempt");
    if (!noRetry)
        std::cerr << "Observed trust after later frames: "
            << compositionTrustName(afterLater.trustState)
            << ", capability present=" << (afterLater.capabilityIdentity != 0)
            << ", creations=" << afterLater.capabilityCreationCount
            << "; expected: UntrustedSpent, absent, creations="
            << initial.capabilityCreationCount << '\n';
    return firstFrameReady && consumed && failureConsumed && oldDomainRetired
        && laterOrdinaryFramesReady && noRetry;
}

struct SharedRecoveryFixture
{
    TestWindow ordinary{240, 160, 100}, removed{240, 160, 380},
        first{240, 160, 660}, second{240, 160, 940};
    BindingProbe probe;
    GraphicsBackend backend;
    DesktopSceneEngine::PresentationTargetId ordinaryId{}, removedId{}, firstId{}, secondId{};
    GraphicsBackend::CompositionDiagnostics pending{};
    GraphicsBackend::PresentationTargetInfo ordinaryBefore{}, firstBefore{}, secondBefore{};
    std::uint64_t uploadsBefore = 0U;
    static constexpr std::array<unsigned char, 4> pixel{31, 193, 79, 255};

    bool initialize()
    {
        ordinaryId = backend.initialize({ordinary.window});
        removedId = backend.attachPresentationTarget(compositionFor(removed.window));
        firstId = backend.attachPresentationTarget(compositionFor(first.window));
        secondId = backend.attachPresentationTarget(compositionFor(second.window));
        return require(ordinaryId.isValid() && removedId.isValid() && firstId.isValid()
                && secondId.isValid() && SUCCEEDED(probe.result) && uploadPixel(backend, pixel),
            "shared recovery fixture uses real HWNDs and one shared media texture");
    }
    bool makeFirstLocallyPending()
    {
        if (!require(backend.forceNextDxgiPresentResultForTesting(firstId,
                static_cast<std::int32_t>(E_FAIL))
                && controlPresentedResult(backend, ordinaryId)
                && controlPresentedResult(backend, removedId)
                && controlPresentedResult(backend, secondId)
                && backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable,
            "real published composition target reaches a healthy Present failure")) return false;
        backend.renderPrepared({});
        return require(backend.presentationTargetInfo(firstId).failed
                && backend.pendingTargetLocalRecoveryForTesting(firstId)
                && backend.compositionDiagnosticsForTesting().trustState
                    == CompositionTrustForTesting::Trusted,
            "healthy Present failure leaves a preexisting one-shot 7A edge");
    }
    bool makeBothLocallyPending()
    {
        if (!require(backend.forceNextDxgiPresentResultForTesting(firstId,
                static_cast<std::int32_t>(E_FAIL))
                && backend.forceNextDxgiPresentResultForTesting(secondId,
                    static_cast<std::int32_t>(E_FAIL))
                && controlPresentedResult(backend, ordinaryId)
                && controlPresentedResult(backend, removedId)
                && backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable,
            "both published peers reach healthy presentation failures")) return false;
        backend.renderPrepared({});
        return require(backend.pendingTargetLocalRecoveryForTesting(firstId)
                && backend.pendingTargetLocalRecoveryForTesting(secondId),
            "both logical peers carry genuine preexisting local authority");
    }
    bool contain()
    {
        backend.failNextCompositionOperationForTesting(CompositionFailurePoint::DisconnectWait);
        if (!require(backend.detachPresentationTarget(removedId),
            "shared recovery fixture detaches the containment trigger")) return false;
        pending = backend.compositionDiagnosticsForTesting();
        ordinaryBefore = backend.presentationTargetInfo(ordinaryId);
        firstBefore = backend.presentationTargetInfo(firstId);
        secondBefore = backend.presentationTargetInfo(secondId);
        uploadsBefore = backend.mediaUploadCountForTesting();
        return require(pending.trustState == CompositionTrustForTesting::UntrustedPending
                && pending.capabilityIdentity != 0U && ordinaryBefore.ready
                && !firstBefore.ready && !secondBefore.ready,
            "required disconnect failure contains both logical composition survivors");
    }
    bool ordinaryContinues()
    {
        const auto info = backend.presentationTargetInfo(ordinaryId);
        return require(info.ready && info.deviceGeneration == ordinaryBefore.deviceGeneration
                && info.targetGeneration == ordinaryBefore.targetGeneration
                && backend.hasCurrentMediaRealizationForTesting()
                && backend.mediaUploadCountForTesting() == uploadsBefore,
            "healthy shared recovery preserves ordinary resources, D3D generation and media");
    }
};

bool verifyCanvasOutsideCompositionRecovery()
{
    using DesktopSceneEngine::CanvasOutsideInstruction;
    using DesktopSceneEngine::CanvasOutsideMode;
    SharedRecoveryFixture fixture;
    auto& backend = fixture.backend;
    if (!fixture.initialize() || !backend.setPresentationPacingTarget(fixture.firstId)) return false;
    const auto initialFirst = backend.presentationTargetInfo(fixture.firstId);
    const auto initialSecond = backend.presentationTargetInfo(fixture.secondId);
    DesktopSceneEngine::Rendering::RenderData frame;
    frame.canvasAspect = 16.0 / 9.0;
    frame.presentation.destination = {0.25F, 0.25F, 0.5F, 0.5F};
    const auto sample = [&](CanvasOutsideMode mode, int y, std::uint32_t expected)
    {
        frame.canvasOutside = CanvasOutsideInstruction{fixture.firstId, mode};
        for (const auto id : {fixture.ordinaryId, fixture.firstId, fixture.secondId})
            if (!backend.captureNextPresentationPixelForTesting(id, 120, y)
                || !controlPresentedResult(backend, id)) return false;
        backend.renderPrepared(frame);
        const auto addressed = backend.presentationTargetDiagnosticsForTesting(fixture.firstId);
        if (!require(addressed.capturedPixelValid && addressed.capturedPixelRgba == expected,
            "fresh frame policy reaches the recovered DComp framebuffer")) return false;
        for (const auto id : {fixture.ordinaryId, fixture.secondId})
        {
            const auto peer = backend.presentationTargetDiagnosticsForTesting(id);
            if (!require(peer.capturedPixelValid && peer.capturedPixelRgba == 0xFF191919U,
                "recovery never spreads the addressed policy to an ordinary or DComp peer")) return false;
        }
        return true;
    };
    if (backend.beginFrame() != FramePreparationResult::ReadyTargetsAvailable
        || !sample(CanvasOutsideMode::Transparent, 5, 0U)) return false;
    if (!fixture.makeFirstLocallyPending()
        || backend.beginFrame() != FramePreparationResult::ReadyTargetsAvailable
        || !require(backend.presentationTargetInfo(fixture.firstId).ready
                && backend.presentationTargetInfo(fixture.firstId).targetGeneration
                    == initialFirst.targetGeneration + 1U
                && backend.presentationTargetInfo(fixture.firstId).deviceGeneration
                    == initialFirst.deviceGeneration
                && backend.presentationTargetInfo(fixture.secondId).targetGeneration
                    == initialSecond.targetGeneration
                && !backend.pendingTargetLocalRecoveryForTesting(fixture.firstId),
            "7A same-HWND one-shot recovery retains its original authority and shared D3D generation")
        || !sample(CanvasOutsideMode::Opaque, 5, 0xFF191919U)
        || !sample(CanvasOutsideMode::Transparent, 40, 0U)) return false;
    if (!fixture.contain()
        || backend.beginFrame() != FramePreparationResult::ReadyTargetsAvailable) return false;
    const auto recovered = backend.compositionDiagnosticsForTesting();
    if (!require(recovered.trustState == CompositionTrustForTesting::Trusted
            && recovered.capabilityCreationCount == fixture.pending.capabilityCreationCount + 1U
            && recovered.commitCount == fixture.pending.commitCount + 1U
            && recovered.waitCount == fixture.pending.waitCount + 1U
            && backend.presentationTargetInfo(fixture.firstId).targetGeneration
                == fixture.firstBefore.targetGeneration + 1U
            && backend.presentationTargetInfo(fixture.secondId).targetGeneration
                == fixture.secondBefore.targetGeneration + 1U
            && backend.presentationTargetInfo(fixture.firstId).deviceGeneration
                == initialFirst.deviceGeneration && fixture.ordinaryContinues(),
        "7B structural recovery retains one shared Commit/Wait and unchanged media/device authority")
        || !sample(CanvasOutsideMode::Transparent, 5, 0U)
        || !sample(CanvasOutsideMode::Transparent, 40, 0U)) return false;
    frame.canvasOutside.reset();
    if (!backend.captureNextPresentationPixelForTesting(fixture.firstId, 120, 5)
        || !controlPresentedResult(backend, fixture.firstId)) return false;
    backend.renderPrepared(frame);
    const auto last = backend.presentationTargetDiagnosticsForTesting(fixture.firstId);
    return require(last.capturedPixelValid && last.capturedPixelRgba == 0xFF191919U
            && backend.compositionDiagnosticsForTesting().commitCount == recovered.commitCount
            && backend.compositionDiagnosticsForTesting().waitCount == recovered.waitCount
            && backend.mediaUploadCountForTesting() == 1U,
        "nullopt after recovery is Opaque, with no retained policy, Commit/Wait or duplicated media upload");
}

bool verifySharedStageALocalAuthorityIsSpent()
{
    for (const auto failure : {CompositionFailurePoint::SwapChainCreation,
            CompositionFailurePoint::RenderTargetViewCreation})
    {
        SharedRecoveryFixture fixture;
        auto& backend = fixture.backend;
        if (!fixture.initialize() || !fixture.makeFirstLocallyPending() || !fixture.contain())
            return false;
        backend.failNextCompositionOperationForTesting(failure);
        if (!require(backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable
                && backend.compositionDiagnosticsForTesting().armedFailurePoint
                    == CompositionFailurePoint::None
                && backend.compositionDiagnosticsForTesting().capabilityCreationCount
                    == fixture.pending.capabilityCreationCount + 1U,
            "C3 reaches real Stage A after fresh capability construction")) return false;
        if (!require(backend.presentationTargetInfo(fixture.firstId).failed
                && !backend.pendingTargetLocalRecoveryForTesting(fixture.firstId)
                && backend.presentationTargetInfo(fixture.firstId).targetGeneration
                    == fixture.firstBefore.targetGeneration
                && SUCCEEDED(fixture.probe.reserve(fixture.first.window)),
            "C3 drawable Stage-A failure spends the old edge without a replacement")) return false;
        backend.failNextCompositionOperationForTesting(CompositionFailurePoint::SwapChainCreation);
        for (int frame = 0; frame < 2; ++frame)
            if (!require(backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable
                    && backend.presentationTargetInfo(fixture.firstId).failed
                    && !backend.pendingTargetLocalRecoveryForTesting(fixture.firstId)
                    && backend.compositionDiagnosticsForTesting().armedFailurePoint
                        == CompositionFailurePoint::SwapChainCreation,
                "C3 unchanged-size later frames cannot self-rearm the failed local attempt"))
                return false;
        if (!fixture.ordinaryContinues()) return false;
    }
    return true;
}

bool verifySharedStageALocalFailureExcludesOnlyThatTarget()
{
    SharedRecoveryFixture fixture;
    auto& backend = fixture.backend;
    if (!fixture.initialize() || !fixture.contain()) return false;
    backend.failNextCompositionOperationForTesting(CompositionFailurePoint::RenderTargetViewCreation);
    if (!require(backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable
            && backend.compositionDiagnosticsForTesting().armedFailurePoint
                == CompositionFailurePoint::None,
        "E1 reaches the first drawable Stage-A RTV failure")) return false;
    const auto after = backend.compositionDiagnosticsForTesting();
    return require(backend.presentationTargetInfo(fixture.firstId).failed
            && !backend.pendingTargetLocalRecoveryForTesting(fixture.firstId)
            && SUCCEEDED(fixture.probe.reserve(fixture.first.window))
            && backend.presentationTargetInfo(fixture.secondId).ready
            && !backend.pendingTargetLocalRecoveryForTesting(fixture.secondId)
            && backend.presentationTargetInfo(fixture.secondId).targetGeneration
                == fixture.secondBefore.targetGeneration + 1U
            && FAILED(fixture.probe.reserve(fixture.second.window))
            && after.trustState == CompositionTrustForTesting::Trusted
            && after.commitCount == fixture.pending.commitCount + 1U
            && after.waitCount == fixture.pending.waitCount + 1U,
        "E1 Stage-A local exclusion lets the other drawable peer complete the shared batch")
        && fixture.ordinaryContinues();
}

bool verifySharedZeroSizeLocalAuthority(bool hadLocalEdge)
{
    SharedRecoveryFixture fixture;
    auto& backend = fixture.backend;
    if (!fixture.initialize()
        || (hadLocalEdge && !fixture.makeFirstLocallyPending())
        || !fixture.contain()) return false;
    if (!require(fixture.first.resize(0, 0) && fixture.second.resize(0, 0),
        "C1/C2 native zero size is observed during the shared attempt")) return false;
    backend.failNextCompositionOperationForTesting(CompositionFailurePoint::SwapChainCreation);
    if (!require(backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable,
        "C1/C2 real shared attempt retains ordinary rendering")) return false;
    const auto after = backend.compositionDiagnosticsForTesting();
    const auto first = backend.presentationTargetInfo(fixture.firstId);
    if (!require(after.capabilityCreationCount == fixture.pending.capabilityCreationCount + 1U
            && after.armedFailurePoint == CompositionFailurePoint::SwapChainCreation
            && first.suspended && first.clientWidth == 0 && first.clientHeight == 0
            && backend.presentationTargetInfo(fixture.secondId).suspended
            && backend.pendingTargetLocalRecoveryForTesting(fixture.firstId) == hadLocalEdge
            && !backend.pendingTargetLocalRecoveryForTesting(fixture.secondId)
            && SUCCEEDED(fixture.probe.reserve(fixture.first.window))
            && SUCCEEDED(fixture.probe.reserve(fixture.second.window)),
        "C1/C2 zero-size observation performs no target construction and preserves edge truth"))
        return false;
    if (!require(after.trustState == CompositionTrustForTesting::Trusted
            && after.capabilityIdentity != 0U
            && after.commitCount == fixture.pending.commitCount
            && after.waitCount == fixture.pending.waitCount,
        hadLocalEdge ? "C1 clean Empty A returns Trusted with the preexisting edge deferred"
            : "C2 clean Empty A returns Trusted without minting a local edge")) return false;

    // The still-armed construction fault must now be consumed by exactly one
    // ordinary 7A reconstruction: deferred authority (C1) or real size edge (C2).
    if (!require(fixture.first.resize(240, 160)
            && backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable
            && backend.presentationTargetInfo(fixture.firstId).failed
            && !backend.pendingTargetLocalRecoveryForTesting(fixture.firstId)
            && backend.compositionDiagnosticsForTesting().armedFailurePoint
                == CompositionFailurePoint::None
            && backend.compositionDiagnosticsForTesting().trustState
                == CompositionTrustForTesting::Trusted,
        "C1/C2 later drawable size consumes one normal 7A construction attempt")) return false;
    backend.failNextCompositionOperationForTesting(CompositionFailurePoint::SwapChainCreation);
    for (int frame = 0; frame < 2; ++frame)
        if (!require(backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable
                && !backend.pendingTargetLocalRecoveryForTesting(fixture.firstId)
                && backend.presentationTargetInfo(fixture.firstId).failed
                && backend.compositionDiagnosticsForTesting().armedFailurePoint
                    == CompositionFailurePoint::SwapChainCreation,
            "C1/C2 failed later 7A attempt does not self-rearm")) return false;
    return fixture.ordinaryContinues();
}

bool verifySharedCleanEmptyA()
{
    for (const bool invalidPeer : {false, true})
    {
        SharedRecoveryFixture fixture;
        auto& backend = fixture.backend;
        if (!fixture.initialize() || !fixture.contain()) return false;
        if (invalidPeer)
            fixture.second.destroy();
        else if (!fixture.second.resize(0, 0))
            return false;
        backend.failNextCompositionOperationForTesting(CompositionFailurePoint::SwapChainCreation);
        if (!require(backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable,
            "F1 zero-size/excluded working set reaches real Stage A")) return false;
        const auto after = backend.compositionDiagnosticsForTesting();
        if (!require(after.armedFailurePoint == CompositionFailurePoint::None
                && backend.presentationTargetInfo(fixture.firstId).failed
                && !backend.pendingTargetLocalRecoveryForTesting(fixture.firstId)
                && (invalidPeer ? backend.presentationTargetInfo(fixture.secondId).failed
                    : backend.presentationTargetInfo(fixture.secondId).suspended)
                && !backend.pendingTargetLocalRecoveryForTesting(fixture.secondId)
                && SUCCEEDED(fixture.probe.reserve(fixture.first.window)),
            "F1 all Stage-A survivors are zero-size or locally excluded without native bindings"))
            return false;
        if (!require(after.trustState == CompositionTrustForTesting::Trusted
                && after.capabilityIdentity != 0U
                && after.capabilityCreationCount == fixture.pending.capabilityCreationCount + 1U
                && after.commitCount == fixture.pending.commitCount
                && after.waitCount == fixture.pending.waitCount,
            "F1 clean Empty A publishes only a fresh capability, with no Commit/Wait")) return false;
        if (!fixture.ordinaryContinues()) return false;
    }
    return true;
}

bool verifySharedStageBFailureAbortsWholeAttempt(bool singleDrawable)
{
    // VisualCreation is first: a real HWND binding has been accepted before
    // this deterministic failure. The remaining cases include the first call.
    for (const auto failure : {CompositionFailurePoint::VisualCreation,
            CompositionFailurePoint::TargetCreation, CompositionFailurePoint::SetContent,
            CompositionFailurePoint::SetRoot})
    {
        SharedRecoveryFixture fixture;
        auto& backend = fixture.backend;
        if (!fixture.initialize() || !fixture.makeBothLocallyPending() || !fixture.contain())
            return false;
        if (singleDrawable && !fixture.second.resize(0, 0)) return false;
        backend.failNextCompositionOperationForTesting(failure);
        if (!require(backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable,
            "E2/F2 fresh candidate Stage B retains independent ordinary rendering")) return false;
        const auto after = backend.compositionDiagnosticsForTesting();
        if (!require(after.armedFailurePoint == CompositionFailurePoint::None
                && after.capabilityCreationCount == fixture.pending.capabilityCreationCount + 1U,
            "E2/F2 real Stage B reaches its selected shared-domain operation")) return false;
        if (!require(after.trustState == CompositionTrustForTesting::UntrustedSpent
                && after.capabilityIdentity == 0U
                && after.commitCount == fixture.pending.commitCount
                && after.waitCount == fixture.pending.waitCount
                && !backend.presentationTargetInfo(fixture.firstId).ready
                && !backend.presentationTargetInfo(fixture.secondId).ready
                && backend.presentationTargetInfo(fixture.firstId).targetGeneration
                    == fixture.firstBefore.targetGeneration
                && backend.presentationTargetInfo(fixture.secondId).targetGeneration
                    == fixture.secondBefore.targetGeneration
                && !backend.presentationTargetDiagnosticsForTesting(fixture.firstId).hasCompositionTarget
                && !backend.presentationTargetDiagnosticsForTesting(fixture.secondId).hasCompositionTarget
                && !backend.pendingTargetLocalRecoveryForTesting(fixture.firstId)
                && backend.pendingTargetLocalRecoveryForTesting(fixture.secondId) == singleDrawable
                && SUCCEEDED(fixture.probe.reserve(fixture.first.window))
                && SUCCEEDED(fixture.probe.reserve(fixture.second.window)),
            singleDrawable ? "F2 dirty Empty B is not clean Empty A: release only, no publication"
                : "E2 all Stage A precedes the first Stage-B failure; no peer publishes")) return false;
        backend.failNextCompositionOperationForTesting(CompositionFailurePoint::CapabilityCreation);
        for (int frame = 0; frame < 2; ++frame)
            if (!require(backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable
                    && backend.compositionDiagnosticsForTesting().trustState
                        == CompositionTrustForTesting::UntrustedSpent
                    && backend.compositionDiagnosticsForTesting().armedFailurePoint
                        == CompositionFailurePoint::CapabilityCreation
                    && backend.compositionDiagnosticsForTesting().capabilityCreationCount
                        == after.capabilityCreationCount
                    && backend.compositionDiagnosticsForTesting().commitCount == after.commitCount
                    && backend.compositionDiagnosticsForTesting().waitCount == after.waitCount,
                "E2/F2 failed fresh domain has no cleanup transaction or later automatic retry"))
                return false;
        if (!fixture.ordinaryContinues()) return false;
    }
    return true;
}

bool verifySharedRecoveryTransactionFailure(CompositionFailurePoint failure)
{
    SharedRecoveryFixture fixture;
    auto& backend = fixture.backend;
    if (!fixture.initialize() || !fixture.contain()) return false;
    backend.failNextCompositionOperationForTesting(failure);
    if (!require(backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable,
        "G1/G2 coordinated recovery failure preserves ordinary frame availability")) return false;
    const auto after = backend.compositionDiagnosticsForTesting();
    if (!require(after.capabilityCreationCount == fixture.pending.capabilityCreationCount + 1U
            && after.armedFailurePoint == CompositionFailurePoint::None
            && after.commitCount == fixture.pending.commitCount + 1U
            && after.waitCount == fixture.pending.waitCount
                + (failure == CompositionFailurePoint::RecoveryWait ? 1U : 0U),
        failure == CompositionFailurePoint::RecoveryCommit
            ? "G1 one RecoveryCommit failure is consumed, with zero recovery Wait"
            : "G2 one RecoveryWait failure is consumed after exactly one Commit")) return false;
    if (!require(after.trustState == CompositionTrustForTesting::UntrustedSpent
            && after.capabilityIdentity == 0U
            && !backend.presentationTargetInfo(fixture.firstId).ready
            && !backend.presentationTargetInfo(fixture.secondId).ready
            && backend.presentationTargetInfo(fixture.firstId).targetGeneration
                == fixture.firstBefore.targetGeneration
            && backend.presentationTargetInfo(fixture.secondId).targetGeneration
                == fixture.secondBefore.targetGeneration
            && !backend.presentationTargetDiagnosticsForTesting(fixture.firstId).hasCompositionTarget
            && !backend.presentationTargetDiagnosticsForTesting(fixture.secondId).hasCompositionVisual
            && SUCCEEDED(fixture.probe.reserve(fixture.first.window))
            && SUCCEEDED(fixture.probe.reserve(fixture.second.window)),
        "G1/G2 failed coordinated proof publishes nothing and releases every fresh HWND binding"))
        return false;
    backend.failNextCompositionOperationForTesting(CompositionFailurePoint::CapabilityCreation);
    for (int frame = 0; frame < 2; ++frame)
        if (!require(backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable
                && backend.compositionDiagnosticsForTesting().trustState
                    == CompositionTrustForTesting::UntrustedSpent
                && backend.compositionDiagnosticsForTesting().armedFailurePoint
                    == CompositionFailurePoint::CapabilityCreation
                && backend.compositionDiagnosticsForTesting().capabilityCreationCount
                    == after.capabilityCreationCount
                && backend.compositionDiagnosticsForTesting().commitCount == after.commitCount
                && backend.compositionDiagnosticsForTesting().waitCount == after.waitCount,
            "G1/G2 later frames issue no cleanup transaction, second Wait, or second capability"))
            return false;
    return fixture.ordinaryContinues();
}

bool verifySharedAtomicPublication()
{
    for (const bool compositionPaces : {false, true})
    {
        SharedRecoveryFixture fixture;
        auto& backend = fixture.backend;
        if (!fixture.initialize()
            || !backend.setPresentationPacingTarget(
                compositionPaces ? fixture.firstId : fixture.ordinaryId)
            || !fixture.contain()) return false;
        const auto preferred = backend.preferredPacingTargetForTesting();
        const auto nextId = backend.nextPresentationTargetValueForTesting();
        const auto generationCreations = backend.graphicsGenerationCreationCountForTesting();
        if (!require(backend.pacingPresentationTargetForTesting() == fixture.ordinaryId,
            "G3 shared containment derives ordinary fallback without changing pacing intent")) return false;
        backend.failNextCompositionOperationForTesting(CompositionFailurePoint::AttachCommit);
        if (!require(backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable,
            "G3 all drawable peers traverse the coordinated recovery batch")) return false;
        const auto after = backend.compositionDiagnosticsForTesting();
        if (!require(after.capabilityCreationCount == fixture.pending.capabilityCreationCount + 1U
                && after.commitCount == fixture.pending.commitCount + 1U
                && after.waitCount == fixture.pending.waitCount + 1U
                && after.armedFailurePoint == CompositionFailurePoint::AttachCommit,
            "G3 one fresh device performs exactly one Commit/Wait, never per-target AttachCommit"))
            return false;
        if (!require(after.trustState == CompositionTrustForTesting::Trusted
                && after.capabilityIdentity != 0U
                && backend.presentationTargetInfo(fixture.firstId).ready
                && backend.presentationTargetInfo(fixture.secondId).ready
                && !backend.pendingTargetLocalRecoveryForTesting(fixture.firstId)
                && !backend.pendingTargetLocalRecoveryForTesting(fixture.secondId)
                && backend.presentationTargetInfo(fixture.firstId).targetGeneration
                    == fixture.firstBefore.targetGeneration + 1U
                && backend.presentationTargetInfo(fixture.secondId).targetGeneration
                    == fixture.secondBefore.targetGeneration + 1U
                && backend.presentationTargetInfo(fixture.firstId).deviceGeneration
                    == fixture.ordinaryBefore.deviceGeneration
                && backend.presentationTargetInfo(fixture.secondId).deviceGeneration
                    == fixture.ordinaryBefore.deviceGeneration
                && backend.presentationTargetDiagnosticsForTesting(fixture.firstId).compositionTreeSubmitted
                && backend.presentationTargetDiagnosticsForTesting(fixture.secondId).compositionTreeSubmitted
                && FAILED(fixture.probe.reserve(fixture.first.window))
                && FAILED(fixture.probe.reserve(fixture.second.window))
                && backend.presentationTargetCountForTesting() == 3U
                && backend.nextPresentationTargetValueForTesting() == nextId
                && backend.graphicsGenerationCreationCountForTesting() == generationCreations
                && backend.preferredPacingTargetForTesting() == preferred
                && backend.pacingPresentationTargetForTesting() == preferred,
            "G3 both eligible peers publish together in original slots, with stable identity and pacing intent"))
            return false;
        if (!fixture.ordinaryContinues()) return false;
        if (!require(backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable
                && backend.compositionDiagnosticsForTesting().commitCount == after.commitCount
                && backend.compositionDiagnosticsForTesting().waitCount == after.waitCount
                && backend.compositionDiagnosticsForTesting().armedFailurePoint
                    == CompositionFailurePoint::AttachCommit,
            "G3 following frame does not reconstruct or resubmit recovered peers")) return false;
        for (const auto id : {fixture.ordinaryId, fixture.firstId, fixture.secondId})
            if (!capture(backend, id) || !controlPresentedResult(backend, id)) return false;
        backend.renderPrepared({});
        for (const auto id : {fixture.ordinaryId, fixture.firstId, fixture.secondId})
            if (!pixelMatches(backend, id, SharedRecoveryFixture::pixel)) return false;
        const auto ordinary = backend.presentationTargetDiagnosticsForTesting(fixture.ordinaryId);
        const auto first = backend.presentationTargetDiagnosticsForTesting(fixture.firstId);
        const auto second = backend.presentationTargetDiagnosticsForTesting(fixture.secondId);
        if (!require(second.lastPresentSyncInterval == 0U
                && second.lastPresentFlags == DXGI_PRESENT_DO_NOT_WAIT
                && (compositionPaces ? first.lastPresentSyncInterval == 1U
                    && first.lastPresentFlags == 0U
                    && ordinary.lastPresentSyncInterval == 0U
                    && ordinary.lastPresentFlags == DXGI_PRESENT_DO_NOT_WAIT
                    && ordinary.lastPresentOrder < second.lastPresentOrder
                    && second.lastPresentOrder < first.lastPresentOrder
                    : ordinary.lastPresentSyncInterval == 1U
                    && first.lastPresentSyncInterval == 0U
                    && first.lastPresentFlags == DXGI_PRESENT_DO_NOT_WAIT
                    && first.lastPresentOrder < second.lastPresentOrder
                    && second.lastPresentOrder < ordinary.lastPresentOrder),
            "G3 rendering retains descriptor order for non-pacing peers and one intended VSync authority"))
            return false;
        if (!fixture.ordinaryContinues()) return false;
    }
    return true;
}

bool verifySharedRecoveryDeviceLossUsesGenerationRecovery()
{
    using DesktopSceneEngine::Rendering::Detail::GenerationReplacementResult;
    using DesktopSceneEngine::Rendering::Detail::MediaView;
    for (const auto failure : {CompositionFailurePoint::CapabilityCreation,
            CompositionFailurePoint::SwapChainCreation, CompositionFailurePoint::RenderTargetViewCreation,
            CompositionFailurePoint::TargetCreation, CompositionFailurePoint::VisualCreation,
            CompositionFailurePoint::SetContent, CompositionFailurePoint::SetRoot,
            CompositionFailurePoint::RecoveryCommit, CompositionFailurePoint::RecoveryWait})
    {
        SharedRecoveryFixture fixture;
        auto& backend = fixture.backend;
        if (!fixture.initialize() || !backend.setPresentationPacingTarget(fixture.firstId)
            || !fixture.contain()) return false;
        const auto creations = backend.graphicsGenerationCreationCountForTesting();
        if (!require(backend.forceNextCompositionOperationResultForTesting(failure,
                    static_cast<std::int32_t>(DXGI_ERROR_DEVICE_REMOVED))
                && backend.beginFrame() == FramePreparationResult::DeviceLost
                && !backend.hasCurrentGraphicsGenerationForTesting()
                && backend.pendingGraphicsRecoveryForTesting()
                && backend.graphicsGenerationCreationCountForTesting() == creations
                && backend.presentationTargetCountForTesting() == 3U
                && backend.preferredPacingTargetForTesting() == fixture.firstId
                && SUCCEEDED(fixture.probe.reserve(fixture.first.window))
                && SUCCEEDED(fixture.probe.reserve(fixture.second.window)),
            "every recovery-native DeviceLost result retires local fresh ownership before Slice 5"))
            return false;
        // These existing diagnostics are lifetime totals, including retired
        // generations, not counters reset on a new D3D generation.
        const auto retired = backend.compositionDiagnosticsForTesting();
        if (!require(retired.trustState == CompositionTrustForTesting::Unavailable
                && retired.capabilityIdentity == 0U
                && retired.capabilityCreationCount == fixture.pending.capabilityCreationCount
                    + (failure == CompositionFailurePoint::CapabilityCreation ? 0U : 1U)
                && retired.commitCount == fixture.pending.commitCount
                    + (failure == CompositionFailurePoint::RecoveryCommit
                        || failure == CompositionFailurePoint::RecoveryWait ? 1U : 0U)
                && retired.waitCount == fixture.pending.waitCount
                    + (failure == CompositionFailurePoint::RecoveryWait ? 1U : 0U),
            "DeviceLost retirement adds no cleanup Commit/Wait to the lifetime diagnostics"))
            return false;
        const MediaView media{SharedRecoveryFixture::pixel.data(), 1, 1, 4, 4,
            SharedRecoveryFixture::pixel.size()};
        if (!require(backend.recoverGraphicsGeneration(&media)
                    == GenerationReplacementResult::GenerationAvailable
                && backend.compositionDiagnosticsForTesting().trustState
                    == CompositionTrustForTesting::Trusted
                && backend.compositionDiagnosticsForTesting().capabilityCreationCount
                    == retired.capabilityCreationCount + 1U
                && backend.graphicsGenerationCreationCountForTesting() == creations + 1U
                && backend.presentationTargetInfo(fixture.ordinaryId).deviceGeneration
                    != fixture.ordinaryBefore.deviceGeneration
                && backend.presentationTargetInfo(fixture.ordinaryId).ready
                && backend.presentationTargetInfo(fixture.firstId).ready
                && backend.presentationTargetInfo(fixture.secondId).ready
                && backend.pacingPresentationTargetForTesting() == fixture.firstId
                && backend.hasCurrentMediaRealizationForTesting()
                && backend.mediaUploadCountForTesting() == fixture.uploadsBefore + 1U,
            "fresh Slice-5 generation starts Trusted and rebuilds the same logical targets/media once"))
            return false;
    }
    return true;
}

bool verifySpentSharedDomainReauthorizationBoundaries()
{
    using DesktopSceneEngine::Rendering::Detail::GenerationReplacementResult;
    using DesktopSceneEngine::Rendering::Detail::MediaView;
    for (const bool replaceGeneration : {false, true})
    {
        SharedRecoveryFixture fixture;
        auto& backend = fixture.backend;
        if (!fixture.initialize() || !backend.setPresentationPacingTarget(fixture.firstId)
            || !fixture.contain()) return false;
        backend.failNextCompositionOperationForTesting(CompositionFailurePoint::RecoveryWait);
        if (!require(backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable
                && backend.compositionDiagnosticsForTesting().trustState
                    == CompositionTrustForTesting::UntrustedSpent,
            "reauthorization starts from a genuinely spent failed fresh domain")) return false;
        const auto spent = backend.compositionDiagnosticsForTesting();
        if (replaceGeneration)
        {
            if (!require(backend.forceDeviceLostOnTargetDetachForTesting(fixture.ordinaryId)
                    && backend.detachPresentationTarget(fixture.ordinaryId)
                    && !backend.hasCurrentGraphicsGenerationForTesting()
                    && backend.pendingGraphicsRecoveryForTesting(),
                "later real generation-lifecycle edge retires UntrustedSpent")) return false;
            const MediaView media{SharedRecoveryFixture::pixel.data(), 1, 1, 4, 4,
                SharedRecoveryFixture::pixel.size()};
            if (!require(backend.recoverGraphicsGeneration(&media)
                        == GenerationReplacementResult::GenerationAvailable
                    && backend.compositionDiagnosticsForTesting().trustState
                        == CompositionTrustForTesting::Trusted
                    && backend.presentationTargetInfo(fixture.firstId).deviceGeneration
                        != fixture.ordinaryBefore.deviceGeneration
                    && backend.presentationTargetInfo(fixture.firstId).ready
                    && backend.presentationTargetInfo(fixture.secondId).ready
                    && backend.preferredPacingTargetForTesting() == fixture.firstId,
                "UntrustedSpent never crosses a fresh D3D GraphicsGeneration boundary")) return false;
        }
        else
        {
            if (!require(backend.detachPresentationTarget(fixture.firstId)
                    && backend.compositionDiagnosticsForTesting().trustState
                        == CompositionTrustForTesting::UntrustedSpent
                    && backend.detachPresentationTarget(fixture.secondId),
                "spent domain stays contained until its final logical DComp target leaves")) return false;
            const auto empty = backend.compositionDiagnosticsForTesting();
            if (!require(empty.trustState == CompositionTrustForTesting::Trusted
                    && empty.capabilityIdentity == 0U
                    && empty.commitCount == spent.commitCount && empty.waitCount == spent.waitCount
                    && SUCCEEDED(fixture.probe.reserve(fixture.first.window))
                    && SUCCEEDED(fixture.probe.reserve(fixture.second.window)),
                "spent zero-target lifecycle resets to Trusted-empty without cleanup transactions"))
                return false;
            const auto freshId = backend.attachPresentationTarget(compositionFor(fixture.first.window));
            if (!require(freshId.isValid() && freshId != fixture.firstId
                    && backend.presentationTargetInfo(freshId).ready
                    && backend.compositionDiagnosticsForTesting().capabilityCreationCount
                        == empty.capabilityCreationCount + 1U,
                "explicit fresh logical attachment reauthorizes capability creation, not automatic retry"))
                return false;
            if (!fixture.ordinaryContinues()) return false;
        }
    }
    return true;
}

bool verifyNativePositiveSizeIsNotEmptyViewport()
{
    TestWindow ordinary{240, 160, 100}, removed{240, 160, 380}, inset{240, 160, 660};
    GraphicsBackend backend;
    BindingProbe probe;
    const auto ordinaryId = backend.initialize({ordinary.window});
    const auto removedId = backend.attachPresentationTarget(compositionFor(removed.window));
    const auto insetId = backend.attachPresentationTarget({inset.window, 160, Composition});
    if (!require(ordinaryId.isValid() && removedId.isValid() && insetId.isValid()
            && backend.presentationTargetInfo(insetId).suspended
            && backend.presentationTargetDiagnosticsForTesting(insetId).compositionTreeSubmitted,
        "positive native client size with a zero viewport still owns a normal DComp binding")) return false;
    const auto before = backend.presentationTargetInfo(insetId);
    backend.failNextCompositionOperationForTesting(CompositionFailurePoint::DisconnectWait);
    if (!backend.detachPresentationTarget(removedId)) return false;
    const auto pending = backend.compositionDiagnosticsForTesting();
    if (!require(backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable,
        "positive-native zero-viewport target participates in real coordinated recovery")) return false;
    const auto after = backend.compositionDiagnosticsForTesting();
    const auto info = backend.presentationTargetInfo(insetId);
    return require(after.trustState == CompositionTrustForTesting::Trusted
            && after.capabilityCreationCount == pending.capabilityCreationCount + 1U
            && after.commitCount == pending.commitCount + 1U
            && after.waitCount == pending.waitCount + 1U
            && info.suspended && !info.ready && info.clientWidth == 240 && info.clientHeight == 160
            && info.viewportHeight == 0 && info.targetGeneration == before.targetGeneration + 1U
            && backend.presentationTargetDiagnosticsForTesting(insetId).compositionTreeSubmitted
            && SUCCEEDED(probe.result) && FAILED(probe.reserve(inset.window))
            && !backend.pendingTargetLocalRecoveryForTesting(insetId),
        "bottom inset is preserved: positive native size performs Stage A/B and is not Clean Empty A");
}

bool verifyCompositionOnlyRecoveryAndShutdownFence()
{
    for (const bool failCapability : {false, true})
    {
        TestWindow removed{240, 160, 100}, peer{240, 160, 380};
        BindingProbe probe;
        GraphicsBackend backend;
        const auto removedId = backend.initialize(compositionFor(removed.window));
        const auto peerId = backend.attachPresentationTarget(compositionFor(peer.window));
        if (!require(removedId.isValid() && peerId.isValid() && SUCCEEDED(probe.result),
            "composition-only recovery preconditions")) return false;
        backend.failNextCompositionOperationForTesting(CompositionFailurePoint::DisconnectWait);
        if (!backend.detachPresentationTarget(removedId)) return false;
        if (failCapability)
            backend.failNextCompositionOperationForTesting(CompositionFailurePoint::CapabilityCreation);
        if (!require(backend.beginFrame() == (failCapability
                ? FramePreparationResult::NoReadyTargets : FramePreparationResult::ReadyTargetsAvailable)
                && backend.compositionDiagnosticsForTesting().trustState == (failCapability
                    ? CompositionTrustForTesting::UntrustedSpent : CompositionTrustForTesting::Trusted)
                && backend.presentationTargetInfo(peerId).ready == !failCapability,
            "shared eligibility never requires an ordinary target or global drawable fallback")) return false;
        backend.shutdown();
        if (!require(backend.beginFrame() == FramePreparationResult::GraphicsUnavailable
                && !backend.hasCurrentGraphicsGenerationForTesting()
                && !backend.pendingGraphicsRecoveryForTesting()
                && backend.presentationTargetCountForTesting() == 0U
                && SUCCEEDED(probe.reserve(peer.window)),
            "shutdown closes native ownership and fences any subsequent beginFrame recovery")) return false;
    }
    return true;
}

bool verifyCompositionPresentFailuresAndDeviceLoss()
{
    for (const bool lost : {false, true})
    {
        TestWindow composition, ordinary{320, 200, 800}, unattached{160, 100, 1200};
        BindingProbe probe;
        GraphicsBackend backend;
        const auto a = backend.initialize(compositionFor(composition.window));
        const auto b = backend.attachPresentationTarget({ordinary.window});
        constexpr std::array<unsigned char, 4> pixel{23, 179, 61, 255};
        if (!require(a.isValid() && b.isValid() && SUCCEEDED(probe.result)
                && uploadPixel(backend, pixel) && backend.setPresentationPacingTarget(b)
                && backend.forceNextDxgiPresentResultForTesting(a, static_cast<std::int32_t>(E_FAIL),
                    static_cast<std::int32_t>(lost ? DXGI_ERROR_DEVICE_REMOVED : S_OK))
                && controlPresentedResult(backend, b)
                && backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable,
                "composition presentation failure preconditions")) return false;
        const auto generation = backend.presentationTargetInfo(a).deviceGeneration;
        backend.renderPrepared({});
        if (!lost)
        {
            if (!require(backend.presentationTargetInfo(a).failed && backend.presentationTargetInfo(b).ready
                    && backend.presentationTargetDiagnosticsForTesting(b).drawCount == 1
                    && backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable
                    && backend.presentationTargetInfo(a).ready,
                    "healthy-device composition Present failure recovers locally once")) return false;
        }
        else
        {
            const auto commits = backend.compositionDiagnosticsForTesting().commitCount;
            const auto c = backend.attachPresentationTarget(
                compositionFor(unattached.window));
            if (!require(backend.presentationTargetDiagnosticsForTesting(b).drawCount == 0
                    && !backend.hasCurrentGraphicsGenerationForTesting()
                    && backend.pendingGraphicsRecoveryForTesting()
                    && backend.beginFrame() == FramePreparationResult::GraphicsUnavailable
                    && !uploadPixel(backend, pixel) && c.isValid()
                    && backend.presentationTargetCountForTesting() == 3U
                    && backend.graphicsRecoveryAttemptCountForTesting() == 0U
                    && backend.updateImage(pixel.data(), 1, 1, 4, 4, pixel.size())
                        == DesktopSceneEngine::Rendering::Detail::MediaUpdateResult::GraphicsUnavailable,
                    "device loss stops GPU work while a new logical attachment joins the pending reconstruction")) return false;
            backend.renderPrepared({});
            backend.shutdown();
            if (!require(backend.compositionDiagnosticsForTesting().commitCount == commits
                    && backend.compositionDiagnosticsForTesting().capabilityIdentity == 0
                    && SUCCEEDED(probe.reserve(composition.window)),
                    "latched loss releases COM objects without stale composition tree calls")) return false;
            const auto fresh = backend.initialize(compositionFor(composition.window));
            if (!require(fresh.isValid() && fresh != a && fresh != b
                    && backend.presentationTargetInfo(fresh).deviceGeneration > generation
                    && backend.compositionDiagnosticsForTesting().capabilityCreationCount == 2,
                    "explicit reinitialization has a fresh device-scoped capability and fresh IDs")) return false;
        }
    }
    return true;
}

void pumpMessages()
{
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

int runHumanGate()
{
    TestWindow composition{320, 200, 100, L"L6 Slice 1 Composition - green/cyan"};
    TestWindow ordinary{320, 200, 460, L"L6 Slice 1 Ordinary - green/cyan"};
    GraphicsBackend backend;
    const auto c = backend.initialize(compositionFor(composition.window));
    const auto a = backend.attachPresentationTarget({ordinary.window});
    if (!require(c.isValid() && a.isValid(), "human gate targets")) return 1;
    std::cout << "Human gate: left composition and right ordinary HWND should alternate green/cyan. No desktop shell integration." << std::endl;
    const ULONGLONG start = GetTickCount64();
    ULONGLONG uploadedPhase = ~ULONGLONG{0};
    while (GetTickCount64() - start < 90000)
    {
        pumpMessages();
        const ULONGLONG phase = (GetTickCount64() - start) / 1000;
        const bool cyan = phase % 2 != 0;
        const std::array<unsigned char, 4> pixel{13, 219, static_cast<unsigned char>(cyan ? 219 : 13), 255};
        if (phase != uploadedPhase)
        {
            if (!uploadPixel(backend, pixel)) return 1;
            uploadedPhase = phase;
        }
        if (backend.beginFrame() != FramePreparationResult::ReadyTargetsAvailable) return 1;
        backend.renderPrepared({});
    }
    return 0;
}
}

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--human-gate") return runHumanGate();
    if (argc == 2 && std::string_view(argv[1]) == "--alpha-contract")
        return verifyOpaquePresentationFramebuffer() ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--red-containment")
        return verifySharedCompositionFailureQuarantinesPeers() ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--red-structural-recovery")
        return verifySharedCompositionFailureRequiresFreshDomain() ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--red-zero-target-reset")
        return verifyMixedRenderingAndLifecycle() ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--red-failed-final-disconnect")
        return verifyDisconnectFailures() ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--green-7b-contained-zero-target-reset")
        return verifyContainedDomainFinalDetachResetsToTrustedEmpty() ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--green-7a-dcomp-handoff")
        return verifyPublishedTargetSharedMutationFailureHandsOffToContainment() ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--green-7a-dcomp-same-hwnd")
        return verifyTrustedSameHwndReconstructionWaitsForOldBinding() ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--red-one-shared-attempt")
        return verifySharedRecoveryAuthorizationIsConsumedOnce() ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--red-7b-c3")
        return verifySharedStageALocalAuthorityIsSpent() ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--red-7b-e1")
        return verifySharedStageALocalFailureExcludesOnlyThatTarget() ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--red-7b-c1")
        return verifySharedZeroSizeLocalAuthority(true) ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--red-7b-c2")
        return verifySharedZeroSizeLocalAuthority(false) ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--red-7b-f1")
        return verifySharedCleanEmptyA() ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--red-7b-e2")
        return verifySharedStageBFailureAbortsWholeAttempt(false) ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--red-7b-f2")
        return verifySharedStageBFailureAbortsWholeAttempt(true) ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--red-7b-g1")
        return verifySharedRecoveryTransactionFailure(CompositionFailurePoint::RecoveryCommit) ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--red-7b-g2")
        return verifySharedRecoveryTransactionFailure(CompositionFailurePoint::RecoveryWait) ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--red-7b-g3")
        return verifySharedAtomicPublication() ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--green-7b-device-lost")
        return verifySharedRecoveryDeviceLossUsesGenerationRecovery() ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--green-7b-reauthorization")
        return verifySpentSharedDomainReauthorizationBoundaries() ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--green-7b-native-inset")
        return verifyNativePositiveSizeIsNotEmptyViewport() ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--green-7b-composition-only")
        return verifyCompositionOnlyRecoveryAndShutdownFence() ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--green-7a-disconnect-setter-barrier")
        return verifyRequiredDisconnectSetterFailureContainsPeers() ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--green-7a-disconnect-setter-device-lost")
        return verifyDisconnectSetterDeviceLostUsesGenerationRecovery() ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--red-7a-initial-rollback-peer")
        return verifyFailedInitialRollbackContainsPublishedPeer() ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--red-7a-initial-rollback-empty")
        return verifyFailedInitialRollbackResetsEmptyCapability() ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--red-7a-initial-rollback-device-lost")
        return verifyFailedInitialRollbackDeviceLostKeepsCandidateUnpublished() ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--red-7a-invalid-reconcile")
        return verifyInvalidEndpointFailedDisconnectContainsPeer(false) ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--red-7a-invalid-detach")
        return verifyInvalidEndpointFailedDisconnectContainsPeer(true) ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--red-7a-invalid-empty")
        return verifyInvalidEndpointFailedDisconnectResetsEmptyCapability(false)
            && verifyInvalidEndpointFailedDisconnectResetsEmptyCapability(true) ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--red-7a-invalid-device-lost")
        return verifyInvalidEndpointDisconnectDeviceLost(false)
            && verifyInvalidEndpointDisconnectDeviceLost(true) ? 0 : 1;
    const bool success = verifyCanvasOutsideModeSwitching() && verifyCanvasOutsideCompositionRecovery()
        && verifyOpaquePresentationFramebuffer()
        && verifyNativeCompositionAttachment() && verifyEngineForwarding()
        && verifyAttachRollback(false) && verifyAttachRollback(true)
        && verifyFailedInitialRollbackContainsPublishedPeer()
        && verifyFailedInitialRollbackResetsEmptyCapability()
        && verifyFailedInitialRollbackDeviceLostKeepsCandidateUnpublished()
        && verifyInvalidEndpointFailedDisconnectContainsPeer(false)
        && verifyInvalidEndpointFailedDisconnectContainsPeer(true)
        && verifyInvalidEndpointFailedDisconnectResetsEmptyCapability(false)
        && verifyInvalidEndpointFailedDisconnectResetsEmptyCapability(true)
        && verifyInvalidEndpointDisconnectDeviceLost(false)
        && verifyInvalidEndpointDisconnectDeviceLost(true)
        && verifyMixedRenderingAndLifecycle()
        && verifyResizeAndUnavailableIsolation() && verifyInitiallySuspendedComposition()
        && verifyPublishedTargetSharedMutationFailureHandsOffToContainment()
        && verifyTrustedSameHwndReconstructionWaitsForOldBinding()
        && verifyDisconnectFailures() && verifyCompositionPresentFailuresAndDeviceLoss()
        && verifySharedCompositionFailureQuarantinesPeers()
        && verifyRequiredDisconnectSetterFailureContainsPeers()
        && verifyDisconnectSetterDeviceLostUsesGenerationRecovery()
        && verifyContainedDomainFinalDetachResetsToTrustedEmpty()
        && verifySharedRecoveryAuthorizationIsConsumedOnce()
        && verifySharedCompositionFailureRequiresFreshDomain()
        && verifySharedStageALocalAuthorityIsSpent()
        && verifySharedStageALocalFailureExcludesOnlyThatTarget()
        && verifySharedZeroSizeLocalAuthority(true)
        && verifySharedZeroSizeLocalAuthority(false)
        && verifySharedCleanEmptyA()
        && verifySharedStageBFailureAbortsWholeAttempt(false)
        && verifySharedStageBFailureAbortsWholeAttempt(true)
        && verifySharedRecoveryTransactionFailure(CompositionFailurePoint::RecoveryCommit)
        && verifySharedRecoveryTransactionFailure(CompositionFailurePoint::RecoveryWait)
        && verifySharedAtomicPublication()
        && verifySharedRecoveryDeviceLossUsesGenerationRecovery()
        && verifySpentSharedDomainReauthorizationBoundaries()
        && verifyNativePositiveSizeIsNotEmptyViewport()
        && verifyCompositionOnlyRecoveryAndShutdownFence();
    if (success) std::cout << "Composition target contracts passed: native forwarding/rollback, shared GPU pixels, mixed pacing, resize, detach and device-loss safety.\n";
    return success ? 0 : 1;
}
