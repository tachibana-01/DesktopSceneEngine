#pragma once

#include <DesktopSceneEngine/PresentationTarget.h>

#include "RenderData.h"
#include "WindowTarget.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace DesktopSceneEngine::Rendering
{

namespace Detail
{

enum class PresentationTargetState
{
    Detached,
    Ready,
    Suspended,
    Occluded,
    Failed
};

enum class FramePreparationResult
{
    ReadyTargetsAvailable,
    NoReadyTargets,
    DeviceLost,
    GraphicsUnavailable
};

enum class MediaUpdateResult
{
    Updated,
    ResourceFailure,
    DeviceLost,
    GraphicsUnavailable
};

enum class GraphicsOperationResult
{
    Succeeded,
    ResourceFailure,
    DeviceLost
};

enum class GenerationReplacementResult
{
    GenerationAvailable,
    GenerationUnavailable
};

struct MediaView
{
    const void* data = nullptr;
    int width = 0;
    int height = 0;
    int channels = 0;
    std::size_t rowStride = 0U;
    std::size_t byteSize = 0U;
};

enum class PresentResultCategory
{
    Presented,
    Occluded,
    BusySkipped,
    TargetFailed,
    DeviceLost
};

// Internal deterministic D3D/DComp error-path test control. A selected stage
// fails before its native operation; all earlier successful operations are real.
enum class CompositionFailurePoint
{
    None,
    CapabilityCreation,
    SwapChainCreation,
    RenderTargetViewCreation,
    TargetCreation,
    VisualCreation,
    SetContent,
    SetRoot,
    AttachCommit,
    DisconnectSetRoot,
    DisconnectSetContent,
    DisconnectCommit,
    DisconnectWait,
    RecoveryCommit,
    RecoveryWait
};

[[nodiscard]] PresentResultCategory classifyPresentResult(
    std::int32_t presentResult,
    std::int32_t deviceRemovedReason
) noexcept;

[[nodiscard]] GraphicsOperationResult classifyGraphicsOperationResult(
    std::int32_t operationResult,
    std::int32_t deviceRemovedReason
) noexcept;

[[nodiscard]] constexpr PresentationTargetState presentationTargetStateAfterPresent(
    PresentationTargetState currentState,
    PresentResultCategory result,
    bool hasDrawableResources
) noexcept
{
    switch (result)
    {
    case PresentResultCategory::Presented:
        return hasDrawableResources
            ? PresentationTargetState::Ready
            : PresentationTargetState::Failed;
    case PresentResultCategory::Occluded:
        return PresentationTargetState::Occluded;
    case PresentResultCategory::BusySkipped:
    case PresentResultCategory::DeviceLost:
        return currentState;
    case PresentResultCategory::TargetFailed:
        return PresentationTargetState::Failed;
    }

    return PresentationTargetState::Failed;
}

[[nodiscard]] constexpr bool shouldContinuePresentationTargetReconciliation(
    PresentationTargetState state,
    bool hasDrawableNativeSize,
    bool pendingTargetLocalRecovery
) noexcept
{
    return state != PresentationTargetState::Failed
        || (hasDrawableNativeSize && pendingTargetLocalRecovery);
}

}

class GraphicsBackend
{
public:

    struct PresentationTargetInfo
    {
        int clientWidth = 0;
        int clientHeight = 0;
        int viewportWidth = 0;
        int viewportHeight = 0;
        std::uint64_t deviceGeneration = 0U;
        std::uint64_t targetGeneration = 0U;
        bool ready = false;
        bool suspended = false;
        bool occluded = false;
        bool failed = false;
    };

    struct PresentationTargetDiagnostics
    {
        std::uint64_t drawCount = 0U;
        std::uint64_t presentAttemptCount = 0U;
        std::uint64_t lastPresentOrder = 0U;
        unsigned int lastPresentSyncInterval = 0U;
        unsigned int lastPresentFlags = 0U;
        PresentationTransport transport = PresentationTransport::HwndSwapChain;
        bool hasCompositionTarget = false;
        bool hasCompositionVisual = false;
        bool compositionTreeSubmitted = false;
        std::uint64_t successfulResizeCount = 0U;
        std::uint32_t capturedPixelRgba = 0U;
        bool capturedPixelValid = false;
        // Non-owning observations for native resource stability tests only.
        std::uintptr_t deviceIdentity = 0U;
        std::uintptr_t swapChainIdentity = 0U;
        std::uintptr_t compositionTargetIdentity = 0U;
        std::uintptr_t compositionVisualIdentity = 0U;
    };

    struct CompositionDiagnostics
    {
        enum class TrustState { Unavailable, Trusted, UntrustedPending, UntrustedSpent };

        std::uintptr_t capabilityIdentity = 0U;
        std::uint64_t capabilityCreationCount = 0U;
        std::uint64_t commitCount = 0U;
        std::uint64_t waitCount = 0U;
        std::int32_t lastDisconnectResult = 0;
        TrustState trustState = TrustState::Unavailable;
        Detail::CompositionFailurePoint armedFailurePoint = Detail::CompositionFailurePoint::None;
        Detail::CompositionFailurePoint armedRollbackFailurePoint = Detail::CompositionFailurePoint::None;
    };

    GraphicsBackend();
    ~GraphicsBackend();

    GraphicsBackend(const GraphicsBackend&) = delete;
    GraphicsBackend& operator=(const GraphicsBackend&) = delete;
    GraphicsBackend(GraphicsBackend&&) = delete;
    GraphicsBackend& operator=(GraphicsBackend&&) = delete;

    [[nodiscard]] PresentationTargetId initialize(const WindowTarget& target);

    [[nodiscard]] PresentationTargetId attachPresentationTarget(
        const WindowTarget& target);

    [[nodiscard]] bool detachPresentationTarget(
        PresentationTargetId id) noexcept;

    [[nodiscard]] bool setPresentationPacingTarget(
        PresentationTargetId id) noexcept;

    [[nodiscard]] Detail::FramePreparationResult beginFrame();

    [[nodiscard]] Detail::GenerationReplacementResult recoverGraphicsGeneration(
        const Detail::MediaView* media);

    void renderPrepared(const RenderData& data);

    [[nodiscard]] Detail::MediaUpdateResult updateImage(
        const void* imageData,
        int width,
        int height,
        int channels,
        std::size_t rowStride,
        std::size_t byteSize
    );

    [[nodiscard]] Detail::MediaUpdateResult updateTexture(
        const void* frameData,
        int width,
        int height,
        int channels,
        std::size_t rowStride,
        std::size_t byteSize
    );

    void clearImage() noexcept;

    [[nodiscard]] PresentationTargetInfo presentationTargetInfo() const noexcept;

    [[nodiscard]] PresentationTargetInfo presentationTargetInfo(
        PresentationTargetId id) const noexcept;

    [[nodiscard]] PresentationTargetDiagnostics
        presentationTargetDiagnosticsForTesting(
            PresentationTargetId id) const noexcept;

    [[nodiscard]] std::size_t presentationTargetCountForTesting() const noexcept;

    [[nodiscard]] PresentationTargetId pacingPresentationTargetForTesting()
        const noexcept;

    [[nodiscard]] PresentationTargetId preferredPacingTargetForTesting()
        const noexcept;

    [[nodiscard]] bool hasCurrentGraphicsGenerationForTesting() const noexcept;

    [[nodiscard]] bool pendingGraphicsRecoveryForTesting() const noexcept;

    [[nodiscard]] bool pendingTargetLocalRecoveryForTesting(
        PresentationTargetId id) const noexcept;

    [[nodiscard]] std::uint64_t graphicsGenerationCreationCountForTesting()
        const noexcept;

    [[nodiscard]] std::uint64_t graphicsGenerationDestructionCountForTesting()
        const noexcept;

    [[nodiscard]] std::uint64_t graphicsRecoveryAttemptCountForTesting()
        const noexcept;

    [[nodiscard]] bool hasCurrentMediaRealizationForTesting() const noexcept;

    void failNextTargetRealizationForTesting(PresentationTargetId id) noexcept;

    [[nodiscard]] bool forceNextTargetRealizationResultForTesting(
        PresentationTargetId id,
        std::int32_t operationResult,
        std::int32_t deviceRemovedReason = 0) noexcept;

    [[nodiscard]] bool forceDeviceLostOnTargetDetachForTesting(
        PresentationTargetId id) noexcept;

    void failNextGraphicsGenerationCreationForTesting() noexcept;

    void forceNextMediaUpdateResultForTesting(
        std::int32_t operationResult,
        std::int32_t deviceRemovedReason = 0) noexcept;

    [[nodiscard]] std::uint64_t mediaUploadCountForTesting() const noexcept;

    [[nodiscard]] std::uint64_t nextPresentationTargetValueForTesting()
        const noexcept;

    [[nodiscard]] PresentationTargetId nextPresentationTargetIdForTesting()
        const noexcept;

    [[nodiscard]] bool forceNextDxgiPresentResultForTesting(
        PresentationTargetId id,
        std::int32_t presentResult,
        std::int32_t deviceRemovedReason = 0) noexcept;

    void failNextPresentationRenderTargetViewCreationForTesting() noexcept;

    void failNextCompositionOperationForTesting(
        Detail::CompositionFailurePoint point) noexcept;

    [[nodiscard]] bool forceNextCompositionOperationResultForTesting(
        Detail::CompositionFailurePoint point,
        std::int32_t operationResult) noexcept;

    // A distinct, one-shot disconnect fault can accompany a construction fault.
    [[nodiscard]] bool forceNextCompositionRollbackResultForTesting(
        Detail::CompositionFailurePoint point,
        std::int32_t operationResult) noexcept;

    [[nodiscard]] CompositionDiagnostics compositionDiagnosticsForTesting()
        const noexcept;

    // Opt-in GPU readback of the target's rendered center pixel before Present.
    // No staging allocation/readback occurs on the normal frame path.
    [[nodiscard]] bool captureNextPresentationPixelForTesting(
        PresentationTargetId id) noexcept;

    // Select a native RTV pixel, including gutters outside the draw viewport.
    [[nodiscard]] bool captureNextPresentationPixelForTesting(
        PresentationTargetId id, int x, int y) noexcept;

    [[nodiscard]] std::uint64_t
    successfulPresentationResizeCountForTesting() const noexcept;

    void shutdown() noexcept;

private:

    struct Impl;

    std::unique_ptr<Impl> impl_{};
};

}
