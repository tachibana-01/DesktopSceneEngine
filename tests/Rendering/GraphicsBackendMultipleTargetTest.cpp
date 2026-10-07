#include "GraphicsBackend.h"
#include "RenderData.h"
#include "WindowTarget.h"

#include <DesktopSceneEngine/Engine.h>

#include <Windows.h>
#include <dxgi.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

namespace
{

class TestWindows final
{
public:
    TestWindows() : instance_(GetModuleHandleW(nullptr))
    {
        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = DefWindowProcW;
        windowClass.hInstance = instance_;
        windowClass.lpszClassName = ClassName;
        registered_ = RegisterClassW(&windowClass) != 0;
    }

    ~TestWindows()
    {
        for (HWND window : windows_)
        {
            if (window != nullptr)
            {
                DestroyWindow(window);
            }
        }
        if (registered_)
        {
            UnregisterClassW(ClassName, instance_);
        }
    }

    [[nodiscard]] HWND create(int width, int height,
        DWORD style = WS_OVERLAPPEDWINDOW)
    {
        RECT rectangle{0, 0, width, height};
        if (!registered_ || AdjustWindowRectEx(
                &rectangle, style, FALSE, 0) == FALSE)
        {
            return nullptr;
        }
        HWND window = CreateWindowExW(
            0, ClassName, L"DSE multi-target test", style,
            CW_USEDEFAULT, CW_USEDEFAULT,
            rectangle.right - rectangle.left, rectangle.bottom - rectangle.top,
            nullptr, nullptr, instance_, nullptr);
        if (window != nullptr)
        {
            ShowWindow(window, SW_SHOWNOACTIVATE);
            UpdateWindow(window);
            windows_.push_back(window);
        }
        return window;
    }

    [[nodiscard]] bool resize(HWND window, int width, int height,
        DWORD style = WS_OVERLAPPEDWINDOW) const noexcept
    {
        RECT rectangle{0, 0, width, height};
        return window != nullptr
            && AdjustWindowRectEx(&rectangle, style, FALSE, 0) != FALSE
            && SetWindowPos(
                window, nullptr, 0, 0,
                rectangle.right - rectangle.left,
                rectangle.bottom - rectangle.top,
                SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != FALSE;
    }

    void destroy(HWND window) noexcept
    {
        const auto found = std::find(windows_.begin(), windows_.end(), window);
        if (found != windows_.end() && *found != nullptr)
        {
            DestroyWindow(*found);
            *found = nullptr;
        }
    }

private:
    static constexpr wchar_t ClassName[] = L"DSEGraphicsBackendMultipleTargetTest";
    HINSTANCE instance_ = nullptr;
    std::vector<HWND> windows_{};
    bool registered_ = false;
};

bool require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "Test failed: " << message << std::endl;
    }
    return condition;
}

DesktopSceneEngine::Rendering::WindowTarget targetFor(HWND window) noexcept
{
    return {reinterpret_cast<void*>(window), 0};
}

bool verifyClassification()
{
    using namespace DesktopSceneEngine::Rendering::Detail;
    const auto category = [](HRESULT result, HRESULT reason = S_OK)
    {
        return classifyPresentResult(
            static_cast<std::int32_t>(result),
            static_cast<std::int32_t>(reason));
    };
    return require(category(S_OK) == PresentResultCategory::Presented, "S_OK")
        && require(category(DXGI_STATUS_OCCLUDED) == PresentResultCategory::Occluded,
            "occluded status")
        && require(category(DXGI_ERROR_WAS_STILL_DRAWING)
                == PresentResultCategory::BusySkipped, "busy skip")
        && require(category(
                DXGI_ERROR_WAS_STILL_DRAWING,
                DXGI_ERROR_DEVICE_REMOVED) == PresentResultCategory::DeviceLost,
            "removed reason overrides busy skip")
        && require(category(S_OK, DXGI_ERROR_DEVICE_REMOVED)
                == PresentResultCategory::DeviceLost,
            "removed reason overrides nominal Present success")
        && require(category(DXGI_STATUS_OCCLUDED, DXGI_ERROR_DEVICE_REMOVED)
                == PresentResultCategory::DeviceLost,
            "removed reason overrides DXGI occlusion status")
        && require(category(DXGI_ERROR_DEVICE_REMOVED)
                == PresentResultCategory::DeviceLost, "device removed")
        && require(category(DXGI_ERROR_DEVICE_RESET)
                == PresentResultCategory::DeviceLost, "device reset")
        && require(category(E_FAIL) == PresentResultCategory::TargetFailed,
            "healthy-device target failure")
        && require(category(E_FAIL, DXGI_ERROR_DEVICE_REMOVED)
                == PresentResultCategory::DeviceLost, "removed reason")
        && require(presentationTargetStateAfterPresent(
                PresentationTargetState::Ready, PresentResultCategory::Occluded, true)
                == PresentationTargetState::Occluded, "Ready to Occluded")
        && require(presentationTargetStateAfterPresent(
                PresentationTargetState::Occluded,
                PresentResultCategory::BusySkipped, true)
                == PresentationTargetState::Occluded, "inconclusive probe")
        && require(presentationTargetStateAfterPresent(
                PresentationTargetState::Occluded,
                PresentResultCategory::Presented, true)
                == PresentationTargetState::Ready, "successful probe");
}

bool verifyLifecycle()
{
    using DesktopSceneEngine::Rendering::Detail::FramePreparationResult;
    using DesktopSceneEngine::Rendering::GraphicsBackend;

    TestWindows windows{};
    const HWND windowA = windows.create(480, 270);
    const HWND windowB = windows.create(320, 240);
    if (!require(windowA != nullptr && windowB != nullptr, "create two HWNDs"))
    {
        return false;
    }

    GraphicsBackend backend{};
    const auto idA = backend.initialize(targetFor(windowA));
    bool ok = require(idA.isValid(), "attach A");
    const auto initialA = backend.presentationTargetInfo(idA);
    const auto nextBeforeFailure = backend.nextPresentationTargetValueForTesting();
    DesktopSceneEngine::Rendering::WindowTarget invalid{};
    invalid.handle = reinterpret_cast<void*>(static_cast<std::uintptr_t>(1U));
    ok = require(!backend.attachPresentationTarget(invalid).isValid(), "reject invalid HWND") && ok;
    ok = require(backend.nextPresentationTargetValueForTesting() == nextBeforeFailure,
            "failed attach consumes no ID") && ok;

    const auto idB = backend.attachPresentationTarget(targetFor(windowB));
    ok = require(idB.isValid() && idB != idA, "attach distinct B") && ok;
    const auto initialB = backend.presentationTargetInfo(idB);
    ok = require(backend.presentationTargetCountForTesting() == 2U,
            "two target records") && ok;
    ok = require(initialB.clientWidth == 320 && initialB.clientHeight == 240,
            "independent B dimensions") && ok;
    ok = require(initialA.deviceGeneration == initialB.deviceGeneration,
            "one shared device generation") && ok;
    ok = require(!backend.attachPresentationTarget(targetFor(windowA)).isValid(),
            "reject duplicate HWND") && ok;
    ok = require(backend.presentationTargetCountForTesting() == 2U,
            "duplicate rejection is transactional") && ok;

    constexpr unsigned char Pixel[] = {255U, 255U, 255U, 255U};
    ok = require(backend.updateImage(Pixel, 1, 1, 4, 4U, sizeof(Pixel))
            == DesktopSceneEngine::Rendering::Detail::MediaUpdateResult::Updated,
            "upload shared image") && ok;
    ok = require(backend.setPresentationPacingTarget(idB), "set B pacing") && ok;
    ok = require(
            backend.forceNextDxgiPresentResultForTesting(
                idA, static_cast<std::int32_t>(S_OK))
                && backend.forceNextDxgiPresentResultForTesting(
                    idB, static_cast<std::int32_t>(S_OK)),
            "control first presentation results deterministically")
        && ok;
    ok = require(backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable,
            "prepare A+B") && ok;
    backend.renderPrepared({});
    const auto drawA = backend.presentationTargetDiagnosticsForTesting(idA);
    const auto drawB = backend.presentationTargetDiagnosticsForTesting(idB);
    ok = require(drawA.drawCount == 1U && drawB.drawCount == 1U,
            "draw once per Ready target") && ok;
    ok = require(drawA.presentAttemptCount == 1U && drawB.presentAttemptCount == 1U,
            "one Present per drawn target") && ok;
    ok = require(backend.mediaUploadCountForTesting() == 1U,
            "one shared media upload") && ok;
    ok = require(drawA.lastPresentSyncInterval == 0U
            && (drawA.lastPresentFlags & DXGI_PRESENT_DO_NOT_WAIT) != 0U,
            "secondary is nonblocking") && ok;
    ok = require(drawB.lastPresentSyncInterval == 1U
            && drawB.lastPresentFlags == 0U
            && drawA.lastPresentOrder < drawB.lastPresentOrder,
            "pacing Present is blocking and last") && ok;

    ok = require(
            backend.forceNextDxgiPresentResultForTesting(
                idA, static_cast<std::int32_t>(DXGI_STATUS_OCCLUDED))
                && backend.forceNextDxgiPresentResultForTesting(
                    idB, static_cast<std::int32_t>(S_OK)),
            "control target-local occlusion")
        && ok;
    ok = require(backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable,
            "prepare before target-local occlusion") && ok;
    backend.renderPrepared({});
    ok = require(backend.presentationTargetInfo(idA).occluded
            && backend.presentationTargetInfo(idB).ready,
            "A occlusion leaves B ready") && ok;

    ok = require(
            backend.forceNextDxgiPresentResultForTesting(
                idA, static_cast<std::int32_t>(DXGI_STATUS_OCCLUDED)),
            "control inconclusive A occlusion-status check") && ok;
    ok = require(backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable,
            "B remains ready during A occlusion-status check") && ok;
    ok = require(backend.presentationTargetInfo(idA).occluded
            && backend.presentationTargetInfo(idB).ready,
            "occluded A remains target-local") && ok;

    ok = require(
            backend.forceNextDxgiPresentResultForTesting(
                idA, static_cast<std::int32_t>(S_OK)),
            "control successful A occlusion-status check") && ok;
    ok = require(backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable,
            "successful A occlusion-status check keeps frame ready") && ok;
    ok = require(backend.presentationTargetInfo(idA).ready
            && backend.presentationTargetInfo(idB).ready,
            "successful occlusion-status check restores only A") && ok;

    ok = require(backend.setPresentationPacingTarget(idA),
            "select A pacing before suspension") && ok;
    ok = require(windows.resize(windowA, 0, 0), "suspend A") && ok;
    ok = require(backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable,
            "suspended A leaves B ready") && ok;
    ok = require(backend.presentationTargetInfo(idA).suspended
            && backend.presentationTargetInfo(idB).ready,
            "suspension is target-local") && ok;
    const auto readyFallback = backend.presentationTargetInfo();
    ok = require(readyFallback.ready
            && readyFallback.clientWidth == initialB.clientWidth
            && readyFallback.clientHeight == initialB.clientHeight,
            "shared frame preparation selects ready B geometry") && ok;
    ok = require(backend.pacingPresentationTargetForTesting() == idA,
            "temporary suspension preserves explicit pacing selection") && ok;

    ok = require(windows.resize(windowA, 640, 360), "resize A") && ok;
    ok = require(backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable,
            "prepare after A resize") && ok;
    const auto resizedA = backend.presentationTargetInfo(idA);
    const auto unchangedB = backend.presentationTargetInfo(idB);
    ok = require(resizedA.clientWidth == 640 && resizedA.clientHeight == 360
            && resizedA.targetGeneration > initialA.targetGeneration,
            "A realizes independent resize") && ok;
    ok = require(unchangedB.targetGeneration == initialB.targetGeneration,
            "B generation unchanged") && ok;
    ok = require(resizedA.deviceGeneration == initialA.deviceGeneration,
            "resize preserves device") && ok;

    ok = require(backend.setPresentationPacingTarget(idA),
            "select A for deterministic resize failure") && ok;
    backend.failNextPresentationRenderTargetViewCreationForTesting();
    ok = require(windows.resize(windowA, 800, 450),
            "resize A for deterministic RTV failure") && ok;
    ok = require(backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable,
            "A resize failure leaves B ready") && ok;
    ok = require(backend.presentationTargetInfo(idA).failed
            && backend.presentationTargetInfo(idB).ready,
            "A resize failure is target-local") && ok;
    ok = require(backend.presentationTargetInfo(idA).deviceGeneration
            == initialA.deviceGeneration,
            "target-local resize failure preserves shared device") && ok;
    ok = require(backend.pacingPresentationTargetForTesting() == idB,
            "failed pacing A falls back to oldest ready B") && ok;
    ok = require(backend.preferredPacingTargetForTesting() == idA,
            "target-local failure preserves preferred pacing intent") && ok;
    ok = require(backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable
            && backend.presentationTargetInfo(idA).ready
            && backend.presentationTargetInfo(idB).ready,
            "one same-size RTV recovery restores A while B remains ready") && ok;
    ok = require(windows.resize(windowA, 640, 360),
            "change A size to recover deterministic RTV failure") && ok;
    ok = require(backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable,
            "A resize transition recovers beside ready B") && ok;
    ok = require(backend.presentationTargetInfo(idA).ready
            && backend.presentationTargetInfo(idB).ready,
            "A recovers without changing B readiness") && ok;
    ok = require(backend.preferredPacingTargetForTesting() == idA
            && backend.pacingPresentationTargetForTesting() == idA,
            "preferred Ready target automatically resumes pacing") && ok;

    const auto beforeDestroyA = backend.presentationTargetInfo(idA);
    const auto beforeDestroyB = backend.presentationTargetInfo(idB);
    ok = require(beforeDestroyA.ready && beforeDestroyB.ready,
            "isolation precondition keeps A and B ready") && ok;

    windows.destroy(windowA);
    const auto afterDestroyPreparation = backend.beginFrame();
    ok = require(afterDestroyPreparation == FramePreparationResult::ReadyTargetsAvailable,
            "A failure leaves B ready") && ok;
    ok = require(backend.presentationTargetInfo(idA).failed
            && backend.presentationTargetInfo(idB).ready,
            "failure is target-local") && ok;
    const auto bDraws = backend.presentationTargetDiagnosticsForTesting(idB).drawCount;
    backend.renderPrepared({});
    ok = require(backend.presentationTargetDiagnosticsForTesting(idB).drawCount
            == bDraws + 1U, "B still draws after A failure") && ok;

    ok = require(backend.detachPresentationTarget(idA), "detach failed A") && ok;
    ok = require(!backend.preferredPacingTargetForTesting().isValid(),
            "detaching preferred descriptor clears pacing preference") && ok;
    ok = require(!backend.detachPresentationTarget(idA), "stale detach is safe") && ok;
    const HWND windowC = windows.create(300, 200);
    const auto idC = backend.attachPresentationTarget(targetFor(windowC));
    ok = require(idC.isValid() && idC != idA && idC != idB,
            "issued IDs are never reused") && ok;
    ok = require(backend.presentationTargetInfo(idC).deviceGeneration
            == initialA.deviceGeneration, "attach C reuses device") && ok;
    ok = require(backend.detachPresentationTarget(idB), "detach pacing B") && ok;
    ok = require(backend.pacingPresentationTargetForTesting() == idC,
            "oldest Ready pacing fallback") && ok;
    ok = require(backend.detachPresentationTarget(idC), "detach final C") && ok;
    ok = require(backend.presentationTargetCountForTesting() == 0U
            && backend.beginFrame() == FramePreparationResult::NoReadyTargets
            && backend.hasCurrentGraphicsGenerationForTesting()
            && !backend.pendingGraphicsRecoveryForTesting()
            && backend.graphicsRecoveryAttemptCountForTesting() == 0U,
            "zero Ready targets retain a healthy generation without recovery") && ok;
    backend.renderPrepared({});

    const HWND windowD = windows.create(400, 225);
    const auto idD = backend.attachPresentationTarget(targetFor(windowD));
    ok = require(idD.isValid() && idD != idA && idD != idB && idD != idC,
            "reattach uses fresh ID") && ok;
    ok = require(backend.presentationTargetInfo(idD).deviceGeneration
            == initialA.deviceGeneration, "reattach preserves device") && ok;
    ok = require(backend.mediaUploadCountForTesting() == 1U,
            "zero-target period preserves media") && ok;
    ok = require(backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable,
            "reattached target becomes Ready") && ok;
    backend.renderPrepared({});
    ok = require(backend.presentationTargetDiagnosticsForTesting(idD).drawCount == 1U,
            "reattached target draws retained media") && ok;
    backend.shutdown();

    const HWND windowE = windows.create(360, 240);
    const auto idE = backend.initialize(targetFor(windowE));
    ok = require(idE.isValid()
            && idE != idA && idE != idB && idE != idC && idE != idD,
            "presentation target IDs remain fresh after reinitialization") && ok;
    ok = require(backend.presentationTargetInfo(idE).deviceGeneration
            > initialA.deviceGeneration,
            "reinitialization creates a new shared device generation") && ok;
    backend.shutdown();
    return ok;
}

bool verifyTargetFailureIsolation()
{
    using DesktopSceneEngine::Rendering::Detail::FramePreparationResult;
    using DesktopSceneEngine::Rendering::GraphicsBackend;

    TestWindows windows{};
    const HWND windowA = windows.create(480, 270);
    const HWND windowB = windows.create(320, 240);
    if (!require(windowA != nullptr && windowB != nullptr,
            "create target-failure HWNDs"))
    {
        return false;
    }

    GraphicsBackend backend{};
    const auto idA = backend.initialize(targetFor(windowA));
    const auto idB = backend.attachPresentationTarget(targetFor(windowB));
    bool ok = require(idA.isValid() && idB.isValid(),
        "attach target-failure presentation targets");
    constexpr unsigned char Pixel[] = {255U, 255U, 255U, 255U};
    ok = require(backend.updateImage(Pixel, 1, 1, 4, 4U, sizeof(Pixel))
            == DesktopSceneEngine::Rendering::Detail::MediaUpdateResult::Updated,
            "upload target-failure shared image") && ok;
    ok = require(backend.setPresentationPacingTarget(idB),
            "set target-failure pacing target") && ok;
    ok = require(
            backend.forceNextDxgiPresentResultForTesting(
                idA, static_cast<std::int32_t>(E_FAIL))
                && backend.forceNextDxgiPresentResultForTesting(
                    idB, static_cast<std::int32_t>(S_OK)),
            "control target-local presentation failure")
        && ok;
    ok = require(backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable,
            "prepare target-local failure frame") && ok;
    backend.renderPrepared({});

    ok = require(backend.presentationTargetInfo(idA).failed
            && backend.presentationTargetInfo(idB).ready,
            "presentation failure is target-local") && ok;
    const auto diagnosticsB = backend.presentationTargetDiagnosticsForTesting(idB);
    ok = require(diagnosticsB.drawCount == 1U
            && diagnosticsB.presentAttemptCount == 1U,
            "B draws and presents after A presentation failure") && ok;
    ok = require(backend.pacingPresentationTargetForTesting() == idB,
            "B retains presentation pacing") && ok;
    ok = require(!backend.setPresentationPacingTarget(idA),
            "failed A cannot become pacing target") && ok;
    ok = require(backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable,
            "one local recovery attempt restores A without disrupting ready B") && ok;
    ok = require(backend.presentationTargetInfo(idA).ready
            && backend.presentationTargetInfo(idB).ready
            && backend.pacingPresentationTargetForTesting() == idB,
            "healthy local A recovery preserves B and preferred pacing") && ok;
    backend.shutdown();
    return ok;
}

bool verifyDeviceRemovalStopsSharedRendering()
{
    using DesktopSceneEngine::Rendering::Detail::FramePreparationResult;
    using DesktopSceneEngine::Rendering::GraphicsBackend;

    TestWindows windows{};
    const HWND windowA = windows.create(480, 270);
    const HWND windowB = windows.create(320, 240);
    const HWND windowC = windows.create(300, 200);
    if (!require(windowA != nullptr && windowB != nullptr && windowC != nullptr,
            "create device-removal HWNDs"))
    {
        return false;
    }

    GraphicsBackend backend{};
    const auto idA = backend.initialize(targetFor(windowA));
    const auto idB = backend.attachPresentationTarget(targetFor(windowB));
    bool ok = require(idA.isValid() && idB.isValid(),
        "attach device-removal presentation targets");
    constexpr unsigned char Pixel[] = {255U, 255U, 255U, 255U};
    ok = require(backend.updateImage(Pixel, 1, 1, 4, 4U, sizeof(Pixel))
            == DesktopSceneEngine::Rendering::Detail::MediaUpdateResult::Updated,
            "upload device-removal shared image") && ok;
    ok = require(backend.setPresentationPacingTarget(idB),
            "set device-removal pacing target") && ok;
    ok = require(
            backend.forceNextDxgiPresentResultForTesting(
                idA,
                static_cast<std::int32_t>(E_FAIL),
                static_cast<std::int32_t>(DXGI_ERROR_DEVICE_REMOVED))
                && backend.forceNextDxgiPresentResultForTesting(
                    idB, static_cast<std::int32_t>(S_OK)),
            "control D3D11 device-removal state")
        && ok;
    ok = require(backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable,
            "prepare device-removal frame") && ok;
    backend.renderPrepared({});

    ok = require(!backend.hasCurrentGraphicsGenerationForTesting()
            && backend.pendingGraphicsRecoveryForTesting()
            && backend.graphicsGenerationCreationCountForTesting() == 1U
            && backend.graphicsGenerationDestructionCountForTesting() == 1U,
            "Present-time device loss destroys the old generation and records one pending edge") && ok;
    ok = require(backend.presentationTargetCountForTesting() == 2U
            && backend.preferredPacingTargetForTesting() == idB
            && !backend.presentationTargetInfo(idA).ready
            && !backend.presentationTargetInfo(idB).ready,
            "generation loss preserves logical target identities/order/preference without stale realizations") && ok;
    ok = require(backend.beginFrame() == FramePreparationResult::GraphicsUnavailable,
            "pending recovery has no stale generation available to frame preparation") && ok;
    ok = require(backend.updateImage(Pixel, 1, 1, 4, 4U, sizeof(Pixel))
            == DesktopSceneEngine::Rendering::Detail::MediaUpdateResult::GraphicsUnavailable,
            "pending recovery has no stale shared media realization") && ok;
    ok = require(backend.updateTexture(Pixel, 1, 1, 4, 4U, sizeof(Pixel))
            == DesktopSceneEngine::Rendering::Detail::MediaUpdateResult::GraphicsUnavailable,
            "device-removal state rejects shared media texture update") && ok;
    backend.renderPrepared({});
    ok = require(backend.pendingGraphicsRecoveryForTesting()
            && backend.graphicsGenerationCreationCountForTesting() == 1U,
            "non-recovery operations do not consume or multiply the pending edge") && ok;
    backend.shutdown();
    return ok;
}

bool verifyTransactionalGenerationReconstruction()
{
    using DesktopSceneEngine::Rendering::Detail::FramePreparationResult;
    using DesktopSceneEngine::Rendering::Detail::GenerationReplacementResult;
    using DesktopSceneEngine::Rendering::GraphicsBackend;

    TestWindows windows{};
    const HWND windowA = windows.create(480, 270);
    const HWND windowB = windows.create(320, 240);
    if (!require(windowA != nullptr && windowB != nullptr,
            "create reconstruction HWNDs"))
        return false;

    GraphicsBackend backend{};
    const auto idA = backend.initialize(targetFor(windowA));
    const auto idB = backend.attachPresentationTarget(targetFor(windowB));
    if (!require(idA.isValid() && idB.isValid()
            && backend.setPresentationPacingTarget(idB),
            "prepare reconstruction descriptors and preference"))
        return false;
    const auto oldGeneration = backend.presentationTargetInfo(idA).deviceGeneration;
    if (!require(backend.forceNextDxgiPresentResultForTesting(
                idA, static_cast<std::int32_t>(E_FAIL),
                static_cast<std::int32_t>(DXGI_ERROR_DEVICE_REMOVED))
            && backend.forceNextDxgiPresentResultForTesting(
                idB, static_cast<std::int32_t>(S_OK))
            && backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable,
            "detect reconstruction loss through production Present path"))
        return false;
    backend.renderPrepared({});

    backend.failNextTargetRealizationForTesting(idB);
    if (!require(backend.recoverGraphicsGeneration(nullptr)
                == GenerationReplacementResult::GenerationAvailable,
            "one pending edge constructs and publishes a healthy candidate"))
        return false;

    const auto recoveredA = backend.presentationTargetInfo(idA);
    const auto recoveredB = backend.presentationTargetInfo(idB);
    bool ok = require(backend.hasCurrentGraphicsGenerationForTesting()
            && !backend.pendingGraphicsRecoveryForTesting()
            && backend.graphicsGenerationCreationCountForTesting() == 2U
            && backend.graphicsGenerationDestructionCountForTesting() == 1U,
            "candidate publication replaces exactly one destroyed generation");
    ok = require(recoveredA.ready && recoveredA.deviceGeneration > oldGeneration
            && recoveredB.failed
            && recoveredB.deviceGeneration == recoveredA.deviceGeneration,
            "target slots reconstruct independently beneath one replacement generation") && ok;
    ok = require(backend.presentationTargetCountForTesting() == 2U
            && backend.preferredPacingTargetForTesting() == idB
            && backend.pacingPresentationTargetForTesting() == idA,
            "partial reconstruction preserves IDs/order/preference and derives A as pacer") && ok;
    ok = require(!backend.hasCurrentMediaRealizationForTesting(),
            "a healthy replacement generation does not require media realization") && ok;
    const auto failedTargetGeneration = recoveredB.targetGeneration;
    ok = require(backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable
            && backend.presentationTargetInfo(idB).ready
            && backend.presentationTargetInfo(idB).targetGeneration
                > failedTargetGeneration
            && backend.preferredPacingTargetForTesting() == idB
            && backend.pacingPresentationTargetForTesting() == idB,
            "unchanged native size consumes one deferred reconstruction authority") && ok;
    ok = require(backend.forceNextDxgiPresentResultForTesting(
                idA, static_cast<std::int32_t>(S_OK))
            && backend.forceNextDxgiPresentResultForTesting(
                idB, static_cast<std::int32_t>(S_OK)),
            "control both targets after partial reconstruction recovery") && ok;
    backend.renderPrepared({});
    ok = require(backend.presentationTargetDiagnosticsForTesting(idA).presentAttemptCount == 1U
            && backend.presentationTargetDiagnosticsForTesting(idB).presentAttemptCount == 1U,
            "both independently reconstructed targets present") && ok;
    ok = require(windows.resize(windowB, 640, 360)
            && backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable
            && backend.presentationTargetInfo(idB).ready
            && backend.presentationTargetInfo(idB).targetGeneration
                > failedTargetGeneration
            && backend.preferredPacingTargetForTesting() == idB
            && backend.pacingPresentationTargetForTesting() == idB,
            "genuine later native-size transition reconciles the preferred target") && ok;
    backend.shutdown();
    return ok;
}

bool verifyFailedReconstructionZeroSizeRoundTrip()
{
    using DesktopSceneEngine::Rendering::Detail::FramePreparationResult;
    using DesktopSceneEngine::Rendering::Detail::GenerationReplacementResult;
    using DesktopSceneEngine::Rendering::GraphicsBackend;

    TestWindows windows{};
    const HWND windowA = windows.create(480, 270);
    const HWND windowB = windows.create(320, 240);
    if (!require(windowA != nullptr && windowB != nullptr,
            "create zero-size reconstruction HWNDs"))
        return false;

    GraphicsBackend backend{};
    const auto idA = backend.initialize(targetFor(windowA));
    const auto idB = backend.attachPresentationTarget(targetFor(windowB));
    if (!require(idA.isValid() && idB.isValid()
            && backend.setPresentationPacingTarget(idB),
            "prepare zero-size reconstruction descriptors and preference"))
        return false;
    if (!require(backend.forceNextDxgiPresentResultForTesting(
                idA, static_cast<std::int32_t>(E_FAIL),
                static_cast<std::int32_t>(DXGI_ERROR_DEVICE_REMOVED))
            && backend.forceNextDxgiPresentResultForTesting(
                idB, static_cast<std::int32_t>(S_OK))
            && backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable,
            "detect zero-size reconstruction loss through production Present path"))
        return false;
    backend.renderPrepared({});

    backend.failNextTargetRealizationForTesting(idB);
    if (!require(backend.recoverGraphicsGeneration(nullptr)
                == GenerationReplacementResult::GenerationAvailable,
            "publish partial generation before zero-size round trip"))
        return false;

    const auto failedTargetGeneration =
        backend.presentationTargetInfo(idB).targetGeneration;
    bool ok = require(backend.presentationTargetInfo(idA).ready
            && backend.presentationTargetInfo(idB).failed
            && backend.preferredPacingTargetForTesting() == idB
            && backend.pacingPresentationTargetForTesting() == idA,
        "zero-size round trip begins with stable preferred failed target");
    ok = require(windows.resize(windowB, 0, 0)
            && backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable
            && backend.presentationTargetInfo(idB).suspended
            && backend.presentationTargetInfo(idB).targetGeneration
                == failedTargetGeneration
            && backend.preferredPacingTargetForTesting() == idB
            && backend.pacingPresentationTargetForTesting() == idB
            && backend.presentationTargetInfo().clientWidth
                == backend.presentationTargetInfo(idA).clientWidth,
            "real zero size defers the pending authority while A remains usable") && ok;
    ok = require(backend.forceNextDxgiPresentResultForTesting(
                idA, static_cast<std::int32_t>(S_OK)),
            "control surviving target during failed zero-size state") && ok;
    backend.renderPrepared({});
    ok = require(backend.presentationTargetDiagnosticsForTesting(idA).presentAttemptCount == 1U
            && backend.presentationTargetDiagnosticsForTesting(idB).presentAttemptCount == 0U,
            "zero-size failed target does not present") && ok;
    ok = require(windows.resize(windowB, 320, 240)
            && backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable
            && backend.presentationTargetInfo(idB).ready
            && backend.presentationTargetInfo(idB).targetGeneration
                > failedTargetGeneration
            && backend.preferredPacingTargetForTesting() == idB
            && backend.pacingPresentationTargetForTesting() == idB,
            "restored original size reconciles from the remembered zero observation") && ok;
    backend.shutdown();
    return ok;
}

bool verifyHealthyLocalFailureGetsOneSameSizeAttempt()
{
    using DesktopSceneEngine::Rendering::Detail::FramePreparationResult;
    using DesktopSceneEngine::Rendering::GraphicsBackend;

    TestWindows windows{};
    const HWND window = windows.create(320, 240);
    if (!require(window != nullptr, "create healthy-local-failure HWND"))
        return false;
    GraphicsBackend backend{};
    const auto id = backend.initialize(targetFor(window));
    const auto before = backend.presentationTargetInfo(id);
    RECT nativeBefore{};
    if (!require(id.isValid() && before.ready && GetClientRect(window, &nativeBefore)
            && nativeBefore.right - nativeBefore.left == 320
            && nativeBefore.bottom - nativeBefore.top == 240,
            "ordinary target starts Ready at a drawable native size")) return false;

    if (!require(backend.forceNextDxgiPresentResultForTesting(
                id, static_cast<std::int32_t>(E_FAIL), static_cast<std::int32_t>(S_OK))
            && backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable,
            "arm a healthy target-local Present failure")) return false;
    backend.renderPrepared({});
    const auto failed = backend.presentationTargetInfo(id);
    if (!require(failed.failed && backend.presentationTargetCountForTesting() == 1U
            && backend.hasCurrentGraphicsGenerationForTesting()
            && !backend.pendingGraphicsRecoveryForTesting()
            && failed.deviceGeneration == before.deviceGeneration
            && failed.targetGeneration == before.targetGeneration && IsWindow(window),
            "healthy Present failure retains the logical slot and D3D generation")) return false;

    RECT nativeAfter{};
    if (!require(GetClientRect(window, &nativeAfter)
            && nativeAfter.right - nativeAfter.left == nativeBefore.right - nativeBefore.left
            && nativeAfter.bottom - nativeAfter.top == nativeBefore.bottom - nativeBefore.top,
            "native size remains unchanged before the authorized frame")) return false;
    const auto frame = backend.beginFrame();
    const auto after = backend.presentationTargetInfo(id);
    const bool attempted = require(frame == FramePreparationResult::ReadyTargetsAvailable
            && after.ready && after.deviceGeneration == before.deviceGeneration
            && after.targetGeneration > failed.targetGeneration,
        "healthy local failure authorizes one same-size next-frame RTV reconstruction");
    if (!attempted)
        std::cerr << "Observed same-size local recovery: frame="
            << static_cast<int>(frame) << ", ready=" << after.ready
            << ", failed=" << after.failed << ", target generation="
            << after.targetGeneration << "; expected Ready and generation > "
            << failed.targetGeneration << '\n';
    return attempted;
}

bool verifyGenerationReconstructionDefersOneSameSizeAttempt()
{
    using DesktopSceneEngine::Rendering::Detail::FramePreparationResult;
    using DesktopSceneEngine::Rendering::Detail::GenerationReplacementResult;
    using DesktopSceneEngine::Rendering::GraphicsBackend;

    TestWindows windows{};
    const HWND survivorWindow = windows.create(480, 270);
    const HWND failedWindow = windows.create(320, 240);
    if (!require(survivorWindow && failedWindow,
            "create generation-deferred ordinary HWNDs")) return false;
    GraphicsBackend backend{};
    const auto survivorId = backend.initialize(targetFor(survivorWindow));
    const auto failedId = backend.attachPresentationTarget(targetFor(failedWindow));
    const auto oldGeneration = backend.presentationTargetInfo(survivorId).deviceGeneration;
    RECT nativeBefore{};
    if (!require(survivorId.isValid() && failedId.isValid()
            && backend.presentationTargetInfo(failedId).ready
            && GetClientRect(failedWindow, &nativeBefore)
            && nativeBefore.right - nativeBefore.left == 320
            && nativeBefore.bottom - nativeBefore.top == 240,
            "two established ordinary targets start drawable")) return false;

    if (!require(backend.forceNextDxgiPresentResultForTesting(
                survivorId, static_cast<std::int32_t>(E_FAIL),
                static_cast<std::int32_t>(DXGI_ERROR_DEVICE_REMOVED))
            && backend.forceNextDxgiPresentResultForTesting(
                failedId, static_cast<std::int32_t>(S_OK))
            && backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable,
            "force proven D3D11 generation loss")) return false;
    backend.renderPrepared({});
    if (!require(!backend.hasCurrentGraphicsGenerationForTesting()
            && backend.pendingGraphicsRecoveryForTesting(),
            "DeviceLost retires the old GraphicsGeneration")) return false;

    if (!require(backend.forceNextTargetRealizationResultForTesting(
                failedId, static_cast<std::int32_t>(E_FAIL),
                static_cast<std::int32_t>(S_OK))
            && backend.recoverGraphicsGeneration(nullptr)
                == GenerationReplacementResult::GenerationAvailable,
            "replacement generation publishes despite one healthy local realization failure"))
        return false;
    const auto survivor = backend.presentationTargetInfo(survivorId);
    const auto failed = backend.presentationTargetInfo(failedId);
    if (!require(backend.hasCurrentGraphicsGenerationForTesting()
            && !backend.pendingGraphicsRecoveryForTesting()
            && backend.presentationTargetCountForTesting() == 2U
            && survivor.ready && failed.failed
            && survivor.deviceGeneration > oldGeneration
            && failed.deviceGeneration == survivor.deviceGeneration
            && IsWindow(survivorWindow) && IsWindow(failedWindow),
            "healthy replacement generation retains the failed logical target")) return false;

    RECT nativeAfter{};
    if (!require(GetClientRect(failedWindow, &nativeAfter)
            && nativeAfter.right - nativeAfter.left == nativeBefore.right - nativeBefore.left
            && nativeAfter.bottom - nativeAfter.top == nativeBefore.bottom - nativeBefore.top,
            "failed target native size is unchanged after generation replacement")) return false;
    const auto frame = backend.beginFrame();
    const auto after = backend.presentationTargetInfo(failedId);
    const bool attempted = require(frame == FramePreparationResult::ReadyTargetsAvailable
            && after.ready && after.deviceGeneration == survivor.deviceGeneration
            && after.targetGeneration > failed.targetGeneration,
        "successful generation replacement defers one same-size target-local reconstruction");
    if (!attempted)
        std::cerr << "Observed deferred same-size recovery: frame="
            << static_cast<int>(frame) << ", ready=" << after.ready
            << ", failed=" << after.failed << ", target generation="
            << after.targetGeneration << "; expected Ready and generation > "
            << failed.targetGeneration << '\n';
    return attempted;
}

bool verifyFailedSameSizeAttemptDoesNotSelfRearm()
{
    using DesktopSceneEngine::Rendering::Detail::FramePreparationResult;
    using DesktopSceneEngine::Rendering::GraphicsBackend;

    TestWindows windows{};
    const HWND window = windows.create(320, 240);
    if (!require(window != nullptr, "create spent-authority ordinary HWND"))
        return false;
    GraphicsBackend backend{};
    const auto id = backend.initialize(targetFor(window));
    const auto initial = backend.presentationTargetInfo(id);
    if (!require(id.isValid() && initial.ready
            && backend.forceNextDxgiPresentResultForTesting(
                id, static_cast<std::int32_t>(E_FAIL), static_cast<std::int32_t>(S_OK))
            && backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable,
            "arm a healthy local failure for one spent-authority attempt")) return false;
    backend.renderPrepared({});
    const auto failed = backend.presentationTargetInfo(id);
    if (!require(failed.failed && failed.targetGeneration == initial.targetGeneration,
            "healthy failure preserves the target generation before reconstruction")) return false;

    // The one-shot injection must be consumed by the first same-size RTV
    // reconstruction. A later real resize succeeding proves that attempt took
    // place; remaining Failed across intervening frames proves no self-retry.
    backend.failNextPresentationRenderTargetViewCreationForTesting();
    const auto firstFrame = backend.beginFrame();
    const auto first = backend.presentationTargetInfo(id);
    if (!require(firstFrame == FramePreparationResult::NoReadyTargets
            && first.failed && first.targetGeneration == failed.targetGeneration,
            "injected same-size reconstruction failure leaves target Failed")) return false;
    for (int frame = 0; frame < 2; ++frame)
    {
        if (!require(backend.beginFrame() == FramePreparationResult::NoReadyTargets
                && backend.presentationTargetInfo(id).failed
                && backend.presentationTargetInfo(id).targetGeneration
                    == failed.targetGeneration,
                "spent authority does not retry on later same-size frames")) return false;
    }
    return require(windows.resize(window, 640, 360)
            && backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable
            && backend.presentationTargetInfo(id).ready
            && backend.presentationTargetInfo(id).targetGeneration
                > failed.targetGeneration
            && backend.presentationTargetDiagnosticsForTesting(id).successfulResizeCount == 1U,
        "real size transition reauthorizes once after the injected failure was consumed");
}

bool verifyDetachEndsPendingLocalRecovery()
{
    using DesktopSceneEngine::Rendering::Detail::FramePreparationResult;
    using DesktopSceneEngine::Rendering::GraphicsBackend;

    TestWindows windows{};
    const HWND window = windows.create(320, 240);
    if (!require(window != nullptr, "create pending-detach ordinary HWND"))
        return false;
    GraphicsBackend backend{};
    const auto oldId = backend.initialize(targetFor(window));
    if (!require(oldId.isValid()
            && backend.forceNextDxgiPresentResultForTesting(
                oldId, static_cast<std::int32_t>(E_FAIL), static_cast<std::int32_t>(S_OK))
            && backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable,
            "establish healthy local failure before detach")) return false;
    backend.renderPrepared({});
    if (!require(backend.presentationTargetInfo(oldId).failed
            && backend.detachPresentationTarget(oldId),
            "detach removes a failed slot with pending local authority")) return false;
    const auto freshId = backend.attachPresentationTarget(targetFor(window));
    const auto fresh = backend.presentationTargetInfo(freshId);
    return require(freshId.isValid() && freshId != oldId && fresh.ready
            && backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable
            && backend.presentationTargetInfo(freshId).targetGeneration
                == fresh.targetGeneration
            && backend.presentationTargetCountForTesting() == 1U,
        "fresh attachment has a fresh logical ID and no inherited recovery attempt");
}

bool verifyZeroSizeDefersLocalRecoveryOpportunity()
{
    using DesktopSceneEngine::Rendering::Detail::FramePreparationResult;
    using DesktopSceneEngine::Rendering::GraphicsBackend;

    TestWindows windows{};
    const HWND window = windows.create(320, 240, WS_POPUP);
    if (!require(window != nullptr, "create zero-deferral ordinary HWND"))
        return false;
    GraphicsBackend backend{};
    const auto id = backend.initialize(targetFor(window));
    const auto before = backend.presentationTargetInfo(id);
    if (!require(id.isValid() && before.ready
            && backend.forceNextDxgiPresentResultForTesting(
                id, static_cast<std::int32_t>(E_FAIL), static_cast<std::int32_t>(S_OK))
            && backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable,
            "prepare a healthy local failure before zero size")) return false;
    backend.renderPrepared({});
    const auto failed = backend.presentationTargetInfo(id);
    if (!require(failed.failed && !backend.pendingGraphicsRecoveryForTesting()
            && backend.pendingTargetLocalRecoveryForTesting(id)
            && failed.deviceGeneration == before.deviceGeneration,
            "local failure is pending without D3D11 device loss")) return false;

    RECT zeroSize{};
    const bool resizedToZero = windows.resize(window, 0, 0, WS_POPUP);
    const bool queriedZero = GetClientRect(window, &zeroSize) != FALSE;
    if (!require(resizedToZero && queriedZero
            && zeroSize.right - zeroSize.left == 0
            && zeroSize.bottom - zeroSize.top == 0,
            "observe actual zero native client size"))
    {
        std::cerr << "Observed zero-size setup: resize=" << resizedToZero
            << ", query=" << queriedZero << ", client="
            << zeroSize.right - zeroSize.left << 'x'
            << zeroSize.bottom - zeroSize.top << '\n';
        return false;
    }
    const auto zeroFrame = backend.beginFrame();
    const auto deferred = backend.presentationTargetInfo(id);
    if (!require(zeroFrame == FramePreparationResult::NoReadyTargets
            && deferred.suspended && deferred.targetGeneration == failed.targetGeneration
            && backend.pendingTargetLocalRecoveryForTesting(id)
            && deferred.deviceGeneration == failed.deviceGeneration,
            "zero size makes no drawable local reconstruction attempt")) return false;

    RECT drawableAgain{};
    if (!require(windows.resize(window, 320, 240, WS_POPUP)
            && GetClientRect(window, &drawableAgain)
            && drawableAgain.right - drawableAgain.left == 320
            && drawableAgain.bottom - drawableAgain.top == 240,
            "restore the original drawable native size")) return false;
    const auto resumedFrame = backend.beginFrame();
    const auto resumed = backend.presentationTargetInfo(id);
    return require(resumedFrame == FramePreparationResult::ReadyTargetsAvailable
            && resumed.ready && resumed.targetGeneration > deferred.targetGeneration
            && !backend.pendingTargetLocalRecoveryForTesting(id)
            && resumed.deviceGeneration == before.deviceGeneration,
            "zero-to-drawable transition consumes the preserved pending authority");
}

bool verifyZeroSizeDoesNotConsumeAuthorizedNativeAttempt()
{
    using DesktopSceneEngine::Rendering::Detail::FramePreparationResult;
    using DesktopSceneEngine::Rendering::GraphicsBackend;

    TestWindows windows{};
    const HWND window = windows.create(320, 240, WS_POPUP);
    if (!require(window != nullptr, "create zero-size one-shot HWND"))
        return false;
    GraphicsBackend backend{};
    const auto id = backend.initialize(targetFor(window));
    if (!require(id.isValid()
            && backend.forceNextDxgiPresentResultForTesting(
                id, static_cast<std::int32_t>(E_FAIL), static_cast<std::int32_t>(S_OK))
            && backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable,
            "establish pending authority before zero-size deferral")) return false;
    backend.renderPrepared({});
    const auto failed = backend.presentationTargetInfo(id);
    if (!require(failed.failed && backend.pendingTargetLocalRecoveryForTesting(id),
            "local failure creates the pending authority before zero-size deferral"))
        return false;
    backend.failNextPresentationRenderTargetViewCreationForTesting();

    if (!require(windows.resize(window, 0, 0, WS_POPUP)
            && backend.beginFrame() == FramePreparationResult::NoReadyTargets
            && backend.presentationTargetInfo(id).suspended
            && backend.pendingTargetLocalRecoveryForTesting(id)
            && backend.presentationTargetInfo(id).targetGeneration == failed.targetGeneration,
            "zero size defers the pending native attempt and injected RTV failure"))
        return false;
    if (!require(windows.resize(window, 320, 240, WS_POPUP)
            && backend.beginFrame() == FramePreparationResult::NoReadyTargets
            && backend.presentationTargetInfo(id).failed
            && !backend.pendingTargetLocalRecoveryForTesting(id)
            && backend.presentationTargetInfo(id).targetGeneration == failed.targetGeneration,
            "restored drawable size reaches and consumes the injected first attempt"))
        return false;
    if (!require(backend.beginFrame() == FramePreparationResult::NoReadyTargets
            && backend.presentationTargetInfo(id).failed,
            "failed resumed attempt does not self-rearm")) return false;
    return require(windows.resize(window, 640, 360, WS_POPUP)
            && backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable
            && backend.presentationTargetInfo(id).ready
            && backend.presentationTargetInfo(id).targetGeneration > failed.targetGeneration,
        "later real size change succeeds because zero deferred and drawable consumed the injection");
}

bool verifyMediaFailureSemantics()
{
    using DesktopSceneEngine::Rendering::Detail::GenerationReplacementResult;
    using DesktopSceneEngine::Rendering::Detail::MediaUpdateResult;
    using DesktopSceneEngine::Rendering::Detail::MediaView;
    using DesktopSceneEngine::Rendering::GraphicsBackend;

    TestWindows windows{};
    const HWND window = windows.create(480, 270);
    if (!require(window != nullptr, "create media-failure HWND"))
        return false;

    GraphicsBackend backend{};
    const auto id = backend.initialize(targetFor(window));
    constexpr unsigned char Pixel[] = {13U, 219U, 13U, 255U};
    const MediaView media{Pixel, 1, 1, 4, 4U, sizeof(Pixel)};
    if (!require(id.isValid()
            && backend.updateTexture(Pixel, 1, 1, 4, 4U, sizeof(Pixel))
                == MediaUpdateResult::Updated,
            "media-failure test starts with one healthy media realization"))
        return false;

    const auto initialGeneration = backend.presentationTargetInfo(id).deviceGeneration;
    const auto initialUploads = backend.mediaUploadCountForTesting();
    backend.forceNextMediaUpdateResultForTesting(
        static_cast<std::int32_t>(E_FAIL), static_cast<std::int32_t>(S_OK));
    bool ok = require(
        backend.updateTexture(Pixel, 1, 1, 4, 4U, sizeof(Pixel))
            == MediaUpdateResult::ResourceFailure,
        "healthy-device ordinary media failure is classified as ResourceFailure");
    ok = require(backend.hasCurrentGraphicsGenerationForTesting()
            && !backend.pendingGraphicsRecoveryForTesting()
            && backend.hasCurrentMediaRealizationForTesting()
            && backend.mediaUploadCountForTesting() == initialUploads
            && backend.presentationTargetInfo(id).deviceGeneration == initialGeneration,
        "ordinary ResourceFailure preserves generation, prior media, and upload count") && ok;

    backend.forceNextMediaUpdateResultForTesting(
        static_cast<std::int32_t>(E_FAIL),
        static_cast<std::int32_t>(DXGI_ERROR_DEVICE_REMOVED));
    ok = require(backend.updateTexture(Pixel, 1, 1, 4, 4U, sizeof(Pixel))
            == MediaUpdateResult::DeviceLost
            && !backend.hasCurrentGraphicsGenerationForTesting()
            && backend.pendingGraphicsRecoveryForTesting(),
        "ordinary upload-time DeviceLost enters the shared generation-loss path") && ok;

    backend.forceNextMediaUpdateResultForTesting(
        static_cast<std::int32_t>(E_FAIL), static_cast<std::int32_t>(S_OK));
    ok = require(backend.recoverGraphicsGeneration(&media)
            == GenerationReplacementResult::GenerationAvailable
            && backend.hasCurrentGraphicsGenerationForTesting()
            && !backend.pendingGraphicsRecoveryForTesting()
            && !backend.hasCurrentMediaRealizationForTesting()
            && backend.graphicsRecoveryAttemptCountForTesting() == 1U,
        "recovery-time ResourceFailure publishes a healthy generation without media") && ok;

    backend.forceNextMediaUpdateResultForTesting(
        static_cast<std::int32_t>(E_FAIL),
        static_cast<std::int32_t>(DXGI_ERROR_DEVICE_REMOVED));
    ok = require(backend.updateImage(Pixel, 1, 1, 4, 4U, sizeof(Pixel))
            == MediaUpdateResult::DeviceLost
            && !backend.hasCurrentGraphicsGenerationForTesting()
            && backend.pendingGraphicsRecoveryForTesting(),
        "image upload DeviceLost also enters the shared generation-loss path") && ok;

    backend.forceNextMediaUpdateResultForTesting(
        static_cast<std::int32_t>(E_FAIL),
        static_cast<std::int32_t>(DXGI_ERROR_DEVICE_REMOVED));
    ok = require(backend.recoverGraphicsGeneration(&media)
            == GenerationReplacementResult::GenerationUnavailable
            && !backend.hasCurrentGraphicsGenerationForTesting()
            && !backend.pendingGraphicsRecoveryForTesting()
            && backend.graphicsRecoveryAttemptCountForTesting() == 2U,
        "candidate media DeviceLost invalidates the candidate and consumes the edge") && ok;
    backend.shutdown();
    return ok;
}

bool verifyGraphicsUnavailableSteadyState()
{
    using DesktopSceneEngine::Rendering::Detail::FramePreparationResult;
    using DesktopSceneEngine::Rendering::Detail::GenerationReplacementResult;
    using DesktopSceneEngine::Rendering::Detail::MediaUpdateResult;
    using DesktopSceneEngine::Rendering::GraphicsBackend;

    TestWindows windows{};
    const HWND windowA = windows.create(480, 270);
    const HWND windowB = windows.create(320, 240);
    const HWND windowC = windows.create(300, 200);
    if (!require(windowA && windowB && windowC,
            "create GraphicsUnavailable HWNDs"))
        return false;

    GraphicsBackend backend{};
    const auto idA = backend.initialize(targetFor(windowA));
    if (!require(idA.isValid()
            && backend.forceNextDxgiPresentResultForTesting(
                idA, static_cast<std::int32_t>(E_FAIL),
                static_cast<std::int32_t>(DXGI_ERROR_DEVICE_REMOVED))
            && backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable,
            "GraphicsUnavailable loss preconditions"))
        return false;
    backend.renderPrepared({});

    backend.failNextGraphicsGenerationCreationForTesting();
    bool ok = require(backend.recoverGraphicsGeneration(nullptr)
            == GenerationReplacementResult::GenerationUnavailable
            && !backend.hasCurrentGraphicsGenerationForTesting()
            && !backend.pendingGraphicsRecoveryForTesting()
            && backend.graphicsRecoveryAttemptCountForTesting() == 1U
            && backend.graphicsGenerationCreationCountForTesting() == 1U,
        "one failed automatic attempt reaches GraphicsUnavailable") ;

    constexpr unsigned char Pixel[] = {13U, 219U, 13U, 255U};
    for (int index = 0; index < 3; ++index)
    {
        ok = require(backend.recoverGraphicsGeneration(nullptr)
                == GenerationReplacementResult::GenerationUnavailable
                && backend.beginFrame() == FramePreparationResult::GraphicsUnavailable
                && backend.updateImage(Pixel, 1, 1, 4, 4U, sizeof(Pixel))
                    == MediaUpdateResult::GraphicsUnavailable,
            "GraphicsUnavailable operations do not authorize another attempt") && ok;
        backend.renderPrepared({});
    }

    const auto idB = backend.attachPresentationTarget(targetFor(windowB));
    ok = require(idB.isValid() && idB != idA
            && backend.presentationTargetCountForTesting() == 2U
            && backend.setPresentationPacingTarget(idB)
            && backend.preferredPacingTargetForTesting() == idB,
        "logical attachment and pacing preference remain available without a generation") && ok;
    ok = require(backend.detachPresentationTarget(idA)
            && backend.presentationTargetCountForTesting() == 1U
            && backend.preferredPacingTargetForTesting() == idB,
        "logical detach remains available without a generation") && ok;
    const auto idC = backend.attachPresentationTarget(targetFor(windowC));
    ok = require(idC.isValid() && idC != idA && idC != idB
            && backend.detachPresentationTarget(idB)
            && !backend.preferredPacingTargetForTesting().isValid()
            && backend.presentationTargetCountForTesting() == 1U,
        "fresh no-generation attachments retain monotonic IDs and preferred detach semantics") && ok;
    ok = require(backend.graphicsRecoveryAttemptCountForTesting() == 1U
            && backend.graphicsGenerationCreationCountForTesting() == 1U
            && !backend.pendingGraphicsRecoveryForTesting(),
        "ticks, media, attach, detach, and pacing do not retry graphics") && ok;
    backend.shutdown();
    return ok;
}

bool verifyCanvasOutsideInvalidAndRetiredInstructions()
{
    using namespace DesktopSceneEngine;
    using Rendering::Detail::FramePreparationResult;
    using Rendering::GraphicsBackend;
    TestWindows windows;
    const HWND ordinary = windows.create(320, 320, WS_POPUP);
    const HWND composition = windows.create(320, 320, WS_POPUP);
    const HWND peer = windows.create(320, 320, WS_POPUP);
    GraphicsBackend backend;
    const auto a = backend.initialize(targetFor(ordinary));
    const auto b = backend.attachPresentationTarget({composition, 0,
        PresentationTransport::DirectCompositionSwapChain});
    const auto c = backend.attachPresentationTarget({peer, 0,
        PresentationTransport::DirectCompositionSwapChain});
    if (!require(a.isValid() && b.isValid() && c.isValid(),
        "invalid-instruction real target preconditions")) return false;
    constexpr std::array<unsigned char, 4> media{13, 219, 13, 0};
    if (backend.updateTexture(media.data(), 1, 1, 4, 4, media.size())
        != Rendering::Detail::MediaUpdateResult::Updated) return false;
    Rendering::RenderData frame;
    frame.canvasAspect = 16.0 / 9.0;
    frame.clearColor[0] = frame.clearColor[1] = frame.clearColor[3] = 0.0F;
    frame.clearColor[2] = 1.0F;
    const auto sample = [&](PresentationTargetId addressed, CanvasOutsideMode mode,
                            bool explicitInstruction, bool reject)
    {
        frame.canvasOutside = explicitInstruction
            ? std::optional{CanvasOutsideInstruction{addressed, mode}} : std::nullopt;
        std::array<GraphicsBackend::PresentationTargetDiagnostics, 3> before;
        const std::array ids{a, b, c};
        for (std::size_t i = 0; i < ids.size(); ++i)
        {
            before[i] = backend.presentationTargetDiagnosticsForTesting(ids[i]);
            if ((!reject || ids[i] != addressed)
                && (!backend.captureNextPresentationPixelForTesting(ids[i], 160, 5)
                    || !backend.forceNextDxgiPresentResultForTesting(ids[i], static_cast<std::int32_t>(S_OK))))
                return false;
        }
        if (backend.beginFrame() != FramePreparationResult::ReadyTargetsAvailable) return false;
        backend.renderPrepared(frame);
        const std::uint32_t expected = frame.clearColor[2] == 1.0F ? 0xFFFF0000U : 0xFF0000FFU;
        for (std::size_t i = 0; i < ids.size(); ++i)
        {
            const auto after = backend.presentationTargetDiagnosticsForTesting(ids[i]);
            const auto info = backend.presentationTargetInfo(ids[i]);
            const bool skipped = reject && ids[i] == addressed;
            if (!require(after.presentAttemptCount == before[i].presentAttemptCount + (skipped ? 0U : 1U)
                    && after.drawCount == before[i].drawCount + (skipped ? 0U : 1U)
                    && info.ready && !info.failed
                    && !backend.pendingTargetLocalRecoveryForTesting(ids[i])
                    && after.capturedPixelValid
                    && after.capturedPixelRgba == (skipped ? before[i].capturedPixelRgba : expected),
                "invalid explicit input skips only its addressed submission without redirect or recovery")) return false;
        }
        return true;
    };
    if (!sample({}, CanvasOutsideMode::Opaque, false, false)
        || !sample({}, CanvasOutsideMode::Transparent, true, false)
        || !sample({}, static_cast<CanvasOutsideMode>(255), true, false)) return false;
    const auto generation = backend.presentationTargetInfo(a).deviceGeneration;
    const auto domain = backend.compositionDiagnosticsForTesting();
    frame.clearColor[0] = 1.0F; frame.clearColor[2] = 0.0F;
    if (!sample(a, CanvasOutsideMode::Transparent, true, true)
        || !sample(b, static_cast<CanvasOutsideMode>(255), true, true)) return false;
    if (!require(backend.presentationTargetInfo(a).deviceGeneration == generation
            && backend.graphicsGenerationCreationCountForTesting() == 1U
            && !backend.pendingGraphicsRecoveryForTesting()
            && backend.compositionDiagnosticsForTesting().commitCount == domain.commitCount
            && backend.compositionDiagnosticsForTesting().waitCount == domain.waitCount,
        "caller errors create no D3D11 device-removal state or DComp recovery edge")) return false;
    if (!backend.detachPresentationTarget(b)) return false;
    const auto fresh = backend.attachPresentationTarget({composition, 0,
        PresentationTransport::DirectCompositionSwapChain});
    if (!require(fresh.isValid() && fresh != b, "same-HWND attachment receives a fresh logical ID")) return false;
    frame.canvasOutside = CanvasOutsideInstruction{b, CanvasOutsideMode::Transparent};
    if (!backend.captureNextPresentationPixelForTesting(fresh, 160, 5)
        || !backend.forceNextDxgiPresentResultForTesting(fresh, static_cast<std::int32_t>(S_OK))
        || backend.beginFrame() != FramePreparationResult::ReadyTargetsAvailable) return false;
    backend.renderPrepared(frame);
    const auto unaddressed = backend.presentationTargetDiagnosticsForTesting(fresh);
    if (!require(unaddressed.capturedPixelValid && unaddressed.capturedPixelRgba == 0xFF0000FFU,
        "retired/missing target instruction cannot redirect to a fresh same-HWND target")) return false;
    frame.canvasOutside = CanvasOutsideInstruction{fresh, CanvasOutsideMode::Transparent};
    if (!backend.captureNextPresentationPixelForTesting(fresh, 160, 5)
        || !backend.forceNextDxgiPresentResultForTesting(fresh, static_cast<std::int32_t>(S_OK))) return false;
    backend.renderPrepared(frame);
    const auto addressed = backend.presentationTargetDiagnosticsForTesting(fresh);
    return require(addressed.capturedPixelValid && addressed.capturedPixelRgba == 0U
            && backend.mediaUploadCountForTesting() == 1U,
        "only an exact current target ID enables Transparent, without a second media upload");
}

bool verifyCanvasOutsideGenerationAndSizeRecovery()
{
    using namespace DesktopSceneEngine;
    using Rendering::Detail::FramePreparationResult;
    using Rendering::Detail::GenerationReplacementResult;
    TestWindows windows;
    const HWND ordinary = windows.create(320, 320, WS_POPUP);
    const HWND composition = windows.create(320, 320, WS_POPUP);
    Rendering::GraphicsBackend backend;
    const auto a = backend.initialize(targetFor(ordinary));
    const auto b = backend.attachPresentationTarget({composition, 0,
        PresentationTransport::DirectCompositionSwapChain});
    if (!require(a.isValid() && b.isValid() && backend.setPresentationPacingTarget(b),
        "generation policy target preconditions")) return false;
    Rendering::RenderData frame;
    frame.canvasAspect = 16.0 / 9.0;
    frame.canvasOutside = CanvasOutsideInstruction{b, CanvasOutsideMode::Transparent};
    frame.clearColor[0] = frame.clearColor[1] = frame.clearColor[3] = 0.0F;
    frame.clearColor[2] = 1.0F;
    const auto sample = [&](int y, std::uint32_t expected)
    {
        // Native size reconciliation must precede a readback request for a
        // target that was suspended at the end of the previous frame.
        if (!require(backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable,
                "reconcile current native target sizes before readback")
            || !require(backend.captureNextPresentationPixelForTesting(b, 160, y),
                "request readback only after the addressed target is usable")
            || !backend.forceNextDxgiPresentResultForTesting(a, static_cast<std::int32_t>(S_OK))
            || !backend.forceNextDxgiPresentResultForTesting(b, static_cast<std::int32_t>(S_OK))) return false;
        backend.renderPrepared(frame);
        const auto d = backend.presentationTargetDiagnosticsForTesting(b);
        return require(d.capturedPixelValid && d.capturedPixelRgba == expected,
            "clear realizes the current policy after native size or D3D generation reconstruction");
    };
    if (!sample(5, 0U) || !sample(160, 0U)) return false;
    const auto ordinaryBefore = backend.presentationTargetInfo(a);
    if (!windows.resize(composition, 0, 0, WS_POPUP)
        || backend.beginFrame() != FramePreparationResult::ReadyTargetsAvailable) return false;
    if (!backend.forceNextDxgiPresentResultForTesting(a, static_cast<std::int32_t>(S_OK))) return false;
    backend.renderPrepared(frame);
    const auto suspended = backend.presentationTargetInfo(b);
    const auto continuing = backend.presentationTargetInfo(a);
    const auto pacedPeer = backend.presentationTargetDiagnosticsForTesting(a);
    if (!require(suspended.suspended && continuing.ready
            && continuing.targetGeneration == ordinaryBefore.targetGeneration
            && backend.preferredPacingTargetForTesting() == b
            && pacedPeer.lastPresentSyncInterval == 1U && pacedPeer.lastPresentFlags == 0U,
        "suspended addressed target preserves intent while its usable peer performs the paced Present")) return false;
    if (!windows.resize(composition, 320, 320, WS_POPUP) || !sample(5, 0U)) return false;
    if (!require(backend.forceNextDxgiPresentResultForTesting(a, static_cast<std::int32_t>(E_FAIL),
                static_cast<std::int32_t>(DXGI_ERROR_DEVICE_REMOVED)),
        "existing deterministic D3D error-path test control arms real Present classification")) return false;
    backend.renderPrepared(frame);
    if (!require(!backend.hasCurrentGraphicsGenerationForTesting() && backend.pendingGraphicsRecoveryForTesting(),
            "D3D11 device-removal state follows the closed shared generation path")
        || backend.recoverGraphicsGeneration(nullptr) != GenerationReplacementResult::GenerationAvailable
        || !sample(5, 0U) || !sample(160, 0U)) return false;
    const auto recovered = backend.presentationTargetDiagnosticsForTesting(b);
    return require(backend.presentationTargetInfo(b).deviceGeneration > ordinaryBefore.deviceGeneration
            && backend.presentationTargetInfo(a).deviceGeneration == backend.presentationTargetInfo(b).deviceGeneration
            && recovered.drawCount == 0U
            && backend.graphicsGenerationCreationCountForTesting() == 2U
            && backend.graphicsGenerationDestructionCountForTesting() == 1U
            && backend.mediaUploadCountForTesting() == 0U && backend.pacingPresentationTargetForTesting() == b,
        "one fresh shared generation restores empty-media clear and pacing");
}

bool verifyEnginePublicPresentationTargetApi()
{
    TestWindows windows{};
    const HWND windowA = windows.create(480, 270);
    const HWND windowB = windows.create(320, 240);
    if (!require(windowA != nullptr && windowB != nullptr,
            "create Engine API HWNDs"))
    {
        return false;
    }

    DesktopSceneEngine::Engine engine{};
    const DesktopSceneEngine::Engine::ExternalWindowTarget targetA{
        reinterpret_cast<void*>(windowA)};
    const DesktopSceneEngine::Engine::ExternalWindowTarget targetB{
        reinterpret_cast<void*>(windowB)};
    const auto idA = engine.initialize(targetA);
    bool ok = require(engine.isInitialized() && idA.isValid(),
        "Engine initializes one presentation target");
    const auto idB = engine.attachPresentationTarget(targetB);
    ok = require(idB.isValid() && idB != idA,
            "Engine attaches a second presentation target") && ok;
    ok = require(!engine.attachPresentationTarget(targetA).isValid(),
            "Engine rejects a duplicate presentation window") && ok;
    ok = require(engine.setPresentationPacingTarget(idB),
            "Engine selects the pacing presentation target") && ok;
    engine.update();
    engine.render();
    ok = require(engine.detachPresentationTarget(idA),
            "Engine detaches the first presentation target") && ok;
    ok = require(!engine.detachPresentationTarget(idA),
            "Engine rejects a stale presentation target ID") && ok;
    ok = require(engine.detachPresentationTarget(idB),
            "Engine detaches the second presentation target") && ok;
    engine.shutdown();
    ok = require(!engine.isInitialized(),
            "Engine shuts down after all presentation targets detach") && ok;
    return ok;
}

}

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--red-7a-local-same-size")
        return verifyHealthyLocalFailureGetsOneSameSizeAttempt() ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--red-7a-generation-deferred")
        return verifyGenerationReconstructionDefersOneSameSizeAttempt() ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--red-7a-spent-local")
        return verifyFailedSameSizeAttemptDoesNotSelfRearm() ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--characterize-7a-zero-size")
        return verifyZeroSizeDefersLocalRecoveryOpportunity() ? 0 : 1;
    return verifyCanvasOutsideInvalidAndRetiredInstructions()
        && verifyCanvasOutsideGenerationAndSizeRecovery()
        && verifyClassification()
        && verifyLifecycle()
        && verifyTargetFailureIsolation()
        && verifyDeviceRemovalStopsSharedRendering()
        && verifyTransactionalGenerationReconstruction()
        && verifyFailedReconstructionZeroSizeRoundTrip()
        && verifyHealthyLocalFailureGetsOneSameSizeAttempt()
        && verifyGenerationReconstructionDefersOneSameSizeAttempt()
        && verifyFailedSameSizeAttemptDoesNotSelfRearm()
        && verifyDetachEndsPendingLocalRecovery()
        && verifyZeroSizeDefersLocalRecoveryOpportunity()
        && verifyZeroSizeDoesNotConsumeAuthorizedNativeAttempt()
        && verifyMediaFailureSemantics()
        && verifyGraphicsUnavailableSteadyState()
        && verifyEnginePublicPresentationTargetApi()
        ? 0
        : 1;
}
