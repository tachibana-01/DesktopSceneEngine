#pragma once

#include <DesktopSceneEngine/HostedCompositionContent.h>
#include <DesktopSceneEngine/PresentationTarget.h>
#include <DesktopSceneEngine/Presentation/PresentationGeometry.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace DesktopSceneEngine
{

class Engine;

enum class PresentationFrameKind { Image, Video };

// Owning RGBA pixels. The producer publishes this as a const shared object.
struct PresentationFrame
{
    PresentationFrameKind kind = PresentationFrameKind::Image;
    int width = 0;
    int height = 0;
    std::size_t rowStride = 0;
    std::vector<std::byte> pixels;
};

// One detached view of a control binding. An absent frame clears media.
struct PresentationInput
{
    std::shared_ptr<const PresentationFrame> frame;
    Presentation::ResolvedPresentationGeometry geometry;
    double canvasAspect = 0.0;
};

namespace Rendering
{
class Renderer;
}

namespace Platform::Windows
{
class DesktopWindowHost;
}

// Owns graphics and window targets on the presentation thread. Keep calls,
// lease access, and destruction on that same thread.
class EnginePresentation
{
public:
    // The caller retains ownership of the external window until its target
    // has been detached or presentation has shut down.
    struct ExternalWindowTarget
    {
        void* handle = nullptr;
        PresentationTransport transport = PresentationTransport::HwndSwapChain;
    };

    EnginePresentation();
    ~EnginePresentation();

    EnginePresentation(const EnginePresentation&) = delete;
    EnginePresentation& operator=(const EnginePresentation&) = delete;

    void initialize();
    [[nodiscard]] PresentationTargetId initialize(const ExternalWindowTarget& target);
    [[nodiscard]] PresentationTargetId initialize(const HostedCompositionTarget& target);
    [[nodiscard]] bool isInitialized() const noexcept;

    [[nodiscard]] PresentationTargetId attachPresentationTarget(const ExternalWindowTarget& target);
    [[nodiscard]] PresentationTargetId attachPresentationTarget(const HostedCompositionTarget& target);
    [[nodiscard]] bool setHostedPresentationExtent(PresentationTargetId id, PresentationExtent extent) noexcept;
    [[nodiscard]] CompositionContentLease acquireHostedCompositionContent(PresentationTargetId id) const noexcept;
    [[nodiscard]] bool detachPresentationTarget(PresentationTargetId id) noexcept;
    [[nodiscard]] bool setPresentationPacingTarget(PresentationTargetId id) noexcept;

    // Realizes the input on this thread and draws when a target is ready.
    // Updated describes media realization only. It is not a Present receipt.
    [[nodiscard]] PresentationUploadResult present(const PresentationInput& input,
        std::optional<CanvasOutsideInstruction> outside = std::nullopt);
    // Used when attaching an output. True requires a successful Present of
    // this frame on the named target. No other target is submitted here.
    [[nodiscard]] bool presentAndConfirm(const PresentationInput& input, CanvasOutsideInstruction target);

    [[nodiscard]] bool processEvents() noexcept;
    [[nodiscard]] bool consumeKeyPress(unsigned int virtualKey) noexcept;
    [[nodiscard]] bool consumeVolumeSlider(float& volume) noexcept;
    void setVolumeControl(float volume) noexcept;
    void setStatusText(const std::string& text) noexcept;

    void shutdown() noexcept;

private:
    friend class Engine;

    [[nodiscard]] PresentationTargetId initializeWithWindowHost(PresentationTransport transport);
    [[nodiscard]] PresentationUploadResult presentFrame(const PresentationInput& input,
        std::optional<CanvasOutsideInstruction> outside, bool* targetPresented);

    std::unique_ptr<Rendering::Renderer> renderer_;
    std::unique_ptr<Platform::Windows::DesktopWindowHost> windowHost_;
    std::shared_ptr<const PresentationFrame> realizedFrame_;
    std::uint64_t realizedDeviceGeneration_ = 0;
    bool initialized_ = false;
};

}
