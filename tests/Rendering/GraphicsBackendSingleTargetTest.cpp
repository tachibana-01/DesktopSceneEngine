#include "GraphicsBackend.h"
#include "Renderer.h"
#include "WindowTarget.h"

#include <Windows.h>

#include <iostream>

namespace
{

class TestWindow final
{
public:

    TestWindow(int clientWidth, int clientHeight)
        : instance_(GetModuleHandleW(nullptr))
    {
        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = DefWindowProcW;
        windowClass.hInstance = instance_;
        windowClass.lpszClassName = ClassName;
        classRegistered_ = RegisterClassW(&windowClass) != 0
            || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
        if (!classRegistered_)
        {
            return;
        }

        RECT windowRectangle{0, 0, clientWidth, clientHeight};
        if (AdjustWindowRectEx(
                &windowRectangle,
                WS_OVERLAPPEDWINDOW,
                FALSE,
                0) == FALSE)
        {
            return;
        }

        window_ = CreateWindowExW(
            0,
            ClassName,
            L"DesktopSceneEngine graphics test",
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            windowRectangle.right - windowRectangle.left,
            windowRectangle.bottom - windowRectangle.top,
            nullptr,
            nullptr,
            instance_,
            nullptr);
    }

    ~TestWindow()
    {
        if (window_ != nullptr)
        {
            DestroyWindow(window_);
        }

        if (classRegistered_)
        {
            UnregisterClassW(ClassName, instance_);
        }
    }

    TestWindow(const TestWindow&) = delete;
    TestWindow& operator=(const TestWindow&) = delete;

    [[nodiscard]] HWND handle() const noexcept
    {
        return window_;
    }

    [[nodiscard]] bool resizeClient(int clientWidth, int clientHeight) noexcept
    {
        RECT windowRectangle{0, 0, clientWidth, clientHeight};
        if (window_ == nullptr
            || AdjustWindowRectEx(
                &windowRectangle,
                WS_OVERLAPPEDWINDOW,
                FALSE,
                0) == FALSE)
        {
            return false;
        }

        return SetWindowPos(
            window_,
            nullptr,
            0,
            0,
            windowRectangle.right - windowRectangle.left,
            windowRectangle.bottom - windowRectangle.top,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != FALSE;
    }

    void destroy() noexcept
    {
        if (window_ != nullptr)
        {
            DestroyWindow(window_);
            window_ = nullptr;
        }
    }

private:

    static constexpr wchar_t ClassName[] =
        L"DesktopSceneEngineGraphicsBackendSingleTargetTestWindow";

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    bool classRegistered_ = false;
};

bool require(bool condition, const char* message)
{
    if (condition)
    {
        return true;
    }

    std::cerr << "Test failed: " << message << std::endl;
    return false;
}

bool verifyFailedTargetRecoveryPolicy()
{
    using DesktopSceneEngine::Rendering::Detail::PresentationTargetState;
    using DesktopSceneEngine::Rendering::Detail::
        shouldContinuePresentationTargetReconciliation;

    return require(
               !shouldContinuePresentationTargetReconciliation(
                   PresentationTargetState::Failed,
                    true,
                    false),
               "failed target without authority remains quarantined")
        && require(
            shouldContinuePresentationTargetReconciliation(
                PresentationTargetState::Failed,
                true,
                true),
            "failed target with pending authority may reconcile at the same size")
        && require(
            !shouldContinuePresentationTargetReconciliation(
                PresentationTargetState::Failed,
                false,
                true),
            "zero-size target defers a pending recovery authority");
}

bool verifyGraphicsOperationClassification()
{
    using DesktopSceneEngine::Rendering::Detail::GraphicsOperationResult;
    using DesktopSceneEngine::Rendering::Detail::classifyGraphicsOperationResult;

    return require(
               classifyGraphicsOperationResult(
                   static_cast<std::int32_t>(E_FAIL),
                   static_cast<std::int32_t>(S_OK))
                   == GraphicsOperationResult::ResourceFailure,
               "healthy-device native failure must remain resource-local")
        && require(
            classifyGraphicsOperationResult(
                static_cast<std::int32_t>(E_FAIL),
                static_cast<std::int32_t>(DXGI_ERROR_DEVICE_REMOVED))
                == GraphicsOperationResult::DeviceLost,
            "removed-device reason must escalate an otherwise local failure")
        && require(
            classifyGraphicsOperationResult(
                static_cast<std::int32_t>(DXGI_ERROR_DEVICE_RESET),
                static_cast<std::int32_t>(S_OK))
                == GraphicsOperationResult::DeviceLost,
            "device-reset HRESULT must classify as generation loss")
        && require(
            classifyGraphicsOperationResult(
                static_cast<std::int32_t>(DXGI_ERROR_DEVICE_HUNG),
                static_cast<std::int32_t>(S_OK))
                == GraphicsOperationResult::DeviceLost,
            "device-hung HRESULT must classify as generation loss")
        && require(
            classifyGraphicsOperationResult(
                static_cast<std::int32_t>(DXGI_ERROR_DRIVER_INTERNAL_ERROR),
                static_cast<std::int32_t>(S_OK))
                == GraphicsOperationResult::DeviceLost,
            "driver-internal HRESULT must classify as generation loss")
        && require(
            classifyGraphicsOperationResult(
                static_cast<std::int32_t>(S_OK),
                static_cast<std::int32_t>(S_OK))
                == GraphicsOperationResult::Succeeded,
            "successful operation on a healthy device must remain successful");
}

bool verifyPreparedFrameUsesCurrentNativeSize()
{
    TestWindow window{480, 270};
    if (!require(window.handle() != nullptr, "prepared-frame test HWND should be created"))
    {
        return false;
    }

    DesktopSceneEngine::Rendering::WindowTarget target{};
    target.handle = reinterpret_cast<void*>(window.handle());

    DesktopSceneEngine::Rendering::Renderer renderer{};
    if (!require(renderer.initialize(target).isValid(), "renderer should initialize"))
    {
        return false;
    }

    bool success = require(
        window.resizeClient(320, 320),
        "prepared-frame test HWND should resize without a manual notification");
    success = require(
            renderer.prepareFrame(),
            "frame preparation should reconcile the current native target")
        && success;

    const auto currentInfo = renderer.presentationTargetInfo();
    success = require(
            currentInfo.viewportWidth == 320 && currentInfo.viewportHeight == 320,
            "prepared frame must expose current native dimensions before render data is built")
        && success;

    renderer.renderPrepared({});
    renderer.shutdown();
    return success;
}

bool verifyPartialResizeFailurePreservesRealizedBufferSize()
{
    constexpr int InitialClientWidth = 420;
    constexpr int InitialClientHeight = 240;
    constexpr int FailedResizeWidth = 640;
    constexpr int FailedResizeHeight = 360;

    TestWindow window{InitialClientWidth, InitialClientHeight};
    if (!require(window.handle() != nullptr, "partial-resize test HWND should be created"))
    {
        return false;
    }

    DesktopSceneEngine::Rendering::WindowTarget target{};
    target.handle = reinterpret_cast<void*>(window.handle());

    DesktopSceneEngine::Rendering::GraphicsBackend backend{};
    if (!require(
            backend.initialize(target).isValid(),
            "partial-resize backend should initialize"))
    {
        return false;
    }

    const auto initialInfo = backend.presentationTargetInfo();
    const std::uint64_t initialResizeCount =
        backend.successfulPresentationResizeCountForTesting();
    backend.failNextPresentationRenderTargetViewCreationForTesting();

    bool success = require(
        window.resizeClient(FailedResizeWidth, FailedResizeHeight),
        "partial-resize HWND should resize from A to B");
    success = require(
            backend.beginFrame()
                == DesktopSceneEngine::Rendering::Detail::FramePreparationResult::
                    NoReadyTargets,
            "forced RTV failure after ResizeBuffers(B) should fail frame preparation")
        && success;

    const auto failedInfo = backend.presentationTargetInfo();
    success = require(
            failedInfo.clientWidth == FailedResizeWidth
                && failedInfo.clientHeight == FailedResizeHeight,
            "successful ResizeBuffers(B) must immediately commit realized buffer size B")
        && success;
    success = require(
            !failedInfo.ready && !failedInfo.suspended,
            "RTV failure must leave the resized target failed rather than ready")
        && success;
    success = require(
            failedInfo.targetGeneration == initialInfo.targetGeneration,
            "partial resize must not advance fully-ready target generation")
        && success;
    success = require(
            backend.successfulPresentationResizeCountForTesting()
                == initialResizeCount + 1U,
            "ResizeBuffers(B) success should be observed before forced RTV failure")
        && success;

    success = require(
            backend.beginFrame()
                == DesktopSceneEngine::Rendering::Detail::FramePreparationResult::
                    ReadyTargetsAvailable,
            "one pending local authority reconstructs the RTV at unchanged native B")
        && success;
    const auto sameSizeRecovered = backend.presentationTargetInfo();
    success = require(sameSizeRecovered.ready
            && sameSizeRecovered.targetGeneration > failedInfo.targetGeneration,
            "same-size RTV recovery advances the target generation") && success;
    success = require(
            backend.successfulPresentationResizeCountForTesting()
                == initialResizeCount + 1U,
            "same-size RTV recovery does not repeat ResizeBuffers")
        && success;

    success = require(
            window.resizeClient(InitialClientWidth, InitialClientHeight),
            "partial-resize HWND should return from B to A")
        && success;
    success = require(
            backend.beginFrame()
                == DesktopSceneEngine::Rendering::Detail::FramePreparationResult::
                    ReadyTargetsAvailable,
            "changed native A should recover only after rebuilding drawable resources")
        && success;

    const auto recoveredInfo = backend.presentationTargetInfo();
    success = require(
            recoveredInfo.ready
                && recoveredInfo.clientWidth == InitialClientWidth
                && recoveredInfo.clientHeight == InitialClientHeight,
            "recovered target should become ready with realized buffer size A")
        && success;
    success = require(
            backend.successfulPresentationResizeCountForTesting()
                == initialResizeCount + 2U,
            "B-to-A recovery must perform a real ResizeBuffers(A)")
        && success;
    success = require(
            recoveredInfo.targetGeneration > initialInfo.targetGeneration,
            "target generation should advance only after RTV recovery reaches Ready")
        && success;

    backend.shutdown();
    return success;
}

bool verifyNativeClientSizeIsAuthoritative()
{
    constexpr int NativeClientWidth = 360;
    constexpr int NativeClientHeight = 200;
    TestWindow window{NativeClientWidth, NativeClientHeight};
    if (!require(window.handle() != nullptr, "test HWND should be created"))
    {
        return false;
    }

    DesktopSceneEngine::Rendering::WindowTarget target{};
    target.handle = reinterpret_cast<void*>(window.handle());

    DesktopSceneEngine::Rendering::GraphicsBackend backend{};
    if (!require(
            backend.initialize(target).isValid(),
            "graphics backend should initialize"))
    {
        return false;
    }

    const auto initialInfo = backend.presentationTargetInfo();
    bool success = require(initialInfo.clientWidth == NativeClientWidth
            && initialInfo.clientHeight == NativeClientHeight,
        "realized target dimensions must come from the HWND client rectangle");

    constexpr int ResizedClientWidth = 500;
    constexpr int ResizedClientHeight = 280;
    success = require(
            window.resizeClient(ResizedClientWidth, ResizedClientHeight),
            "test HWND client area should resize")
        && success;
    success = require(backend.beginFrame()
            == DesktopSceneEngine::Rendering::Detail::FramePreparationResult::
                ReadyTargetsAvailable,
            "native resize reconciliation should produce a ready frame")
        && success;
    const auto resizedInfo = backend.presentationTargetInfo();
    success = require(resizedInfo.clientWidth == ResizedClientWidth
            && resizedInfo.clientHeight == ResizedClientHeight,
        "beginFrame must reconcile a native resize without a manual resize call")
        && success;
    success = require(
            resizedInfo.deviceGeneration == initialInfo.deviceGeneration,
            "target recreation must preserve shared device lifetime")
        && success;
    success = require(
            resizedInfo.targetGeneration > initialInfo.targetGeneration,
            "target recreation must advance only the target resource generation")
        && success;

    constexpr unsigned char WhitePixel[] = {255U, 255U, 255U, 255U};
    success = require(
            backend.updateImage(WhitePixel, 1, 1, 4, 4U, sizeof(WhitePixel))
                == DesktopSceneEngine::Rendering::Detail::MediaUpdateResult::Updated,
            "shared media texture should upload once")
        && success;
    success = require(backend.beginFrame()
            == DesktopSceneEngine::Rendering::Detail::FramePreparationResult::
                ReadyTargetsAvailable,
            "resized target should remain renderable")
        && success;
    DesktopSceneEngine::Rendering::RenderData renderData{};
    renderData.clearColor[0] = 0.1F;
    renderData.clearColor[1] = 0.1F;
    renderData.clearColor[2] = 0.1F;
    renderData.clearColor[3] = 1.0F;
    backend.renderPrepared(renderData);

    success = require(window.resizeClient(0, 0),
            "test HWND should enter a zero-client-size state")
        && success;
    success = require(backend.beginFrame()
            == DesktopSceneEngine::Rendering::Detail::FramePreparationResult::
                NoReadyTargets,
            "zero-size target must suspend presentation")
        && success;
    const auto suspendedInfo = backend.presentationTargetInfo();
    success = require(suspendedInfo.suspended && !suspendedInfo.ready,
            "zero-size target must report suspended without becoming ready")
        && success;

    success = require(
            window.resizeClient(ResizedClientWidth, ResizedClientHeight),
            "test HWND should restore from zero size")
        && success;
    success = require(backend.beginFrame()
            == DesktopSceneEngine::Rendering::Detail::FramePreparationResult::
                ReadyTargetsAvailable,
            "restored native target must resume presentation")
        && success;
    const auto restoredInfo = backend.presentationTargetInfo();
    success = require(restoredInfo.ready && !restoredInfo.suspended,
            "restored native target must report ready")
        && success;
    success = require(
            restoredInfo.deviceGeneration == initialInfo.deviceGeneration,
            "suspend and resume must preserve shared device lifetime")
        && success;

    window.destroy();
    success = require(backend.beginFrame()
            == DesktopSceneEngine::Rendering::Detail::FramePreparationResult::
                NoReadyTargets,
            "destroyed borrowed HWND must fail safely")
        && success;
    success = require(!backend.presentationTargetInfo().ready,
            "destroyed borrowed HWND must not retain a ready target")
        && success;
    return success;
}

}

int main()
{
    return verifyFailedTargetRecoveryPolicy()
            && verifyGraphicsOperationClassification()
            && verifyPreparedFrameUsesCurrentNativeSize()
            && verifyPartialResizeFailurePreservesRealizedBufferSize()
            && verifyNativeClientSizeIsAuthoritative()
        ? 0
        : 1;
}
