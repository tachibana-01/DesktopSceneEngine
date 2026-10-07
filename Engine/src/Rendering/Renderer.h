#pragma once

#include "GraphicsBackend.h"
#include "RenderData.h"
#include "WindowTarget.h"

#include <cstddef>

namespace DesktopSceneEngine::Rendering
{

class Renderer
{
public:

    Renderer() = default;
    ~Renderer() = default;

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;

    [[nodiscard]] PresentationTargetId initialize(const WindowTarget& target);

    [[nodiscard]] PresentationTargetId attachPresentationTarget(
        const WindowTarget& target);

    [[nodiscard]] bool detachPresentationTarget(
        PresentationTargetId id) noexcept;

    [[nodiscard]] bool setPresentationPacingTarget(
        PresentationTargetId id) noexcept;

    [[nodiscard]] Detail::MediaUpdateResult updateImage(
        const void* imageData,
        int width,
        int height,
        int channels,
        std::size_t rowStride,
        std::size_t byteSize
    );

    [[nodiscard]] Detail::MediaUpdateResult updateFrame(
        const void* frameData,
        int width,
        int height,
        int channels,
        std::size_t rowStride,
        std::size_t byteSize
    );

    void clearImage() noexcept;

    [[nodiscard]] bool prepareFrame(
        const Detail::MediaView* currentMedia = nullptr);

    void renderPrepared(const RenderData& data);

    void render(
        const RenderData& data,
        const Detail::MediaView* currentMedia = nullptr);

    [[nodiscard]] GraphicsBackend::PresentationTargetInfo
        presentationTargetInfo() const noexcept;

    [[nodiscard]] GraphicsBackend::PresentationTargetInfo
        presentationTargetInfo(PresentationTargetId id) const noexcept;

    void shutdown() noexcept;

private:

    friend class CanvasTargetFitTestAccess;

    GraphicsBackend backend_{};
    bool initialized_ = false;
};

}
