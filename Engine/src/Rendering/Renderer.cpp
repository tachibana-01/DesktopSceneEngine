#include "Renderer.h"

#include <cstdint>
#include <cstdio>

namespace DesktopSceneEngine::Rendering
{

namespace
{

std::uint64_t updateFrameCalls = 0;

bool shouldLog(std::uint64_t count) noexcept
{
    return count == 1 || count % 25 == 0;
}

}

PresentationTargetId Renderer::initialize(const WindowTarget& target)
{
    if (initialized_)
    {
        return {};
    }

    const PresentationTargetId id = backend_.initialize(target);
    if (!id.isValid())
    {
        return {};
    }

    initialized_ = true;
    return id;
}

PresentationTargetId Renderer::attachPresentationTarget(
    const WindowTarget& target)
{
    return initialized_ ? backend_.attachPresentationTarget(target)
                        : PresentationTargetId{};
}

bool Renderer::detachPresentationTarget(PresentationTargetId id) noexcept
{
    return initialized_ && backend_.detachPresentationTarget(id);
}

bool Renderer::setPresentationPacingTarget(PresentationTargetId id) noexcept
{
    return initialized_ && backend_.setPresentationPacingTarget(id);
}

Detail::MediaUpdateResult Renderer::updateImage(
    const void* imageData,
    int width,
    int height,
    int channels,
    std::size_t rowStride,
    std::size_t byteSize
)
{
    if (!initialized_)
    {
        return Detail::MediaUpdateResult::GraphicsUnavailable;
    }

    return backend_.updateImage(
        imageData, width, height, channels, rowStride, byteSize);
}

Detail::MediaUpdateResult Renderer::updateFrame(
    const void* frameData,
    int width,
    int height,
    int channels,
    std::size_t rowStride,
    std::size_t byteSize
)
{
    ++updateFrameCalls;
    if (shouldLog(updateFrameCalls))
    {
        std::fprintf(
            stderr,
            "[Renderer] updateFrame=%llu size=%dx%d stride=%zu bytes=%zu\n",
            static_cast<unsigned long long>(updateFrameCalls),
            width,
            height,
            rowStride,
            byteSize);
        std::fflush(stderr);
    }

    if (!initialized_)
    {
        return Detail::MediaUpdateResult::GraphicsUnavailable;
    }

    return backend_.updateTexture(
        frameData, width, height, channels, rowStride, byteSize);
}

void Renderer::clearImage() noexcept
{
    backend_.clearImage();
}

bool Renderer::prepareFrame(const Detail::MediaView* currentMedia)
{
    if (!initialized_)
        return false;
    (void)backend_.recoverGraphicsGeneration(currentMedia);
    return backend_.beginFrame()
        == Detail::FramePreparationResult::ReadyTargetsAvailable;
}

void Renderer::renderPrepared(const RenderData& data)
{
    if (!initialized_)
    {
        return;
    }

    backend_.renderPrepared(data);
}

void Renderer::render(
    const RenderData& data,
    const Detail::MediaView* currentMedia)
{
    if (!prepareFrame(currentMedia))
    {
        return;
    }

    renderPrepared(data);
}

GraphicsBackend::PresentationTargetInfo
Renderer::presentationTargetInfo() const noexcept
{
    return backend_.presentationTargetInfo();
}

GraphicsBackend::PresentationTargetInfo
Renderer::presentationTargetInfo(PresentationTargetId id) const noexcept
{
    return backend_.presentationTargetInfo(id);
}

void Renderer::shutdown() noexcept
{
    backend_.shutdown();
    initialized_ = false;
}

}
