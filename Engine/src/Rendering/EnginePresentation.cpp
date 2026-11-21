#include <DesktopSceneEngine/EnginePresentation.h>

#include "../Platform/Windows/DesktopWindowHost.h"
#include "Renderer.h"

namespace DesktopSceneEngine
{

EnginePresentation::EnginePresentation()
    : renderer_(std::make_unique<Rendering::Renderer>())
{
}

EnginePresentation::~EnginePresentation()
{
    shutdown();
}

void EnginePresentation::initialize()
{
    if (initialized_) return;

    windowHost_ = std::make_unique<Platform::Windows::DesktopWindowHost>();
    windowHost_->initialize();
    if (!initializeWithWindowHost(PresentationTransport::HwndSwapChain).isValid())
        windowHost_.reset();
}

PresentationTargetId EnginePresentation::initialize(const ExternalWindowTarget& target)
{
    if (initialized_) return {};

    windowHost_ = std::make_unique<Platform::Windows::DesktopWindowHost>();
    windowHost_->initializeExternal(target.handle);
    const auto id = initializeWithWindowHost(target.transport);
    if (!id.isValid()) windowHost_.reset();
    return id;
}

PresentationTargetId EnginePresentation::initialize(const HostedCompositionTarget& target)
{
    if (initialized_) return {};

    const auto id = renderer_->initialize(target);
    initialized_ = id.isValid();
    if (!initialized_) renderer_->shutdown();
    return id;
}

bool EnginePresentation::isInitialized() const noexcept
{
    return initialized_;
}

PresentationTargetId EnginePresentation::initializeWithWindowHost(PresentationTransport transport)
{
    if (!windowHost_->isInitialized()) return {};

    auto target = windowHost_->getWindowTarget();
    target.transport = transport;
    const auto id = renderer_->initialize(target);
    initialized_ = id.isValid();
    if (!initialized_) renderer_->shutdown();
    return id;
}

PresentationTargetId EnginePresentation::attachPresentationTarget(const ExternalWindowTarget& target)
{
    if (!initialized_ || target.handle == nullptr) return {};

    Rendering::WindowTarget windowTarget{};
    windowTarget.handle = target.handle;
    windowTarget.transport = target.transport;
    return renderer_->attachPresentationTarget(windowTarget);
}

PresentationTargetId EnginePresentation::attachPresentationTarget(const HostedCompositionTarget& target)
{
    return initialized_ ? renderer_->attachPresentationTarget(target) : PresentationTargetId{};
}

bool EnginePresentation::setHostedPresentationExtent(PresentationTargetId id, PresentationExtent extent) noexcept
{
    return initialized_ && renderer_->setHostedPresentationExtent(id, extent);
}

CompositionContentLease EnginePresentation::acquireHostedCompositionContent(PresentationTargetId id) const noexcept
{
    return initialized_ ? renderer_->acquireHostedCompositionContent(id) : CompositionContentLease{};
}

bool EnginePresentation::detachPresentationTarget(PresentationTargetId id) noexcept
{
    return initialized_ && renderer_->detachPresentationTarget(id);
}

bool EnginePresentation::setPresentationPacingTarget(PresentationTargetId id) noexcept
{
    return initialized_ && renderer_->setPresentationPacingTarget(id);
}

PresentationUploadResult EnginePresentation::present(const PresentationInput& input,
    std::optional<CanvasOutsideInstruction> outside)
{
    return presentFrame(input, outside, nullptr);
}

bool EnginePresentation::presentAndConfirm(const PresentationInput& input, CanvasOutsideInstruction target)
{
    if (!target.targetId.isValid()) return false;
    bool presented = false;
    (void)presentFrame(input, target, &presented);
    return presented;
}

PresentationUploadResult EnginePresentation::presentFrame(const PresentationInput& input,
    std::optional<CanvasOutsideInstruction> outside, bool* targetPresented)
{
    if (!initialized_) return PresentationUploadResult::GraphicsUnavailable;

    // Recreate graphics first, then explicitly realize this media. During
    // recovery, Renderer::prepareFrame(media) cannot distinguish a healthy
    // replacement device from a failed realization of the supplied media.
    const bool ready = renderer_->prepareFrame();
    const auto generation = renderer_->deviceGeneration();
    if (!generation) return PresentationUploadResult::GraphicsUnavailable;

    if (!input.frame)
    {
        renderer_->clearImage();
        realizedFrame_.reset();
        realizedDeviceGeneration_ = 0;
    }
    else if (input.frame != realizedFrame_ || generation != realizedDeviceGeneration_)
    {
        const auto& frame = *input.frame;
        const auto result = frame.kind == PresentationFrameKind::Image
            ? renderer_->updateImage(frame.pixels.data(), frame.width, frame.height, 4,
                frame.rowStride, frame.pixels.size())
            : renderer_->updateFrame(frame.pixels.data(), frame.width, frame.height, 4,
                frame.rowStride, frame.pixels.size());
        if (result != PresentationUploadResult::Updated) return result;
        realizedFrame_ = input.frame;
        realizedDeviceGeneration_ = generation;
    }
    if (ready)
    {
        Rendering::RenderData data{};
        data.presentation = input.geometry;
        data.canvasAspect = input.canvasAspect;
        data.canvasOutside = outside;
        const bool presented = renderer_->renderPrepared(data, targetPresented ? outside->targetId : PresentationTargetId{});
        if (targetPresented) *targetPresented = presented;
    }
    return PresentationUploadResult::Updated;
}

bool EnginePresentation::processEvents() noexcept
{
    return windowHost_ ? windowHost_->processMessages() : initialized_;
}

bool EnginePresentation::consumeKeyPress(unsigned int virtualKey) noexcept
{
    return windowHost_ && windowHost_->consumeKeyPress(virtualKey);
}

bool EnginePresentation::consumeVolumeSlider(float& volume) noexcept
{
    return windowHost_ && windowHost_->consumeVolumeSlider(volume);
}

void EnginePresentation::setVolumeControl(float volume) noexcept
{
    if (windowHost_) windowHost_->setVolumeControl(volume);
}

void EnginePresentation::setStatusText(const std::string& text) noexcept
{
    if (windowHost_) windowHost_->setStatusText(text);
}

void EnginePresentation::shutdown() noexcept
{
    renderer_->shutdown();
    windowHost_.reset();
    realizedFrame_.reset();
    realizedDeviceGeneration_ = 0;
    initialized_ = false;
}

}
