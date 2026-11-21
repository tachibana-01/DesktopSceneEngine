#pragma once

#include <cstdint>

namespace DesktopSceneEngine
{

namespace Rendering
{

class GraphicsBackend;

}

enum class PresentationTransport : std::uint8_t
{
    HwndSwapChain,
    DirectCompositionSwapChain,
    // Windowless content only. Its consumer owns the composition tree.
    HostedCompositionContent
};

struct PresentationExtent
{
    int width = 0;
    int height = 0;
};

// Allocation pixels only: no UI placement, layout, clip or native window.
// A zero dimension suspends rendering until a positive extent is requested.
struct HostedCompositionTarget
{
    PresentationExtent extent{};
};

enum class CanvasOutsideMode : std::uint8_t
{
    Opaque,
    Transparent
};

enum class PresentationUploadResult
{
    Updated,
    ResourceFailure,
    DeviceLost,
    GraphicsUnavailable
};

class PresentationTargetId
{
public:

    constexpr PresentationTargetId() noexcept = default;

    [[nodiscard]] constexpr bool isValid() const noexcept
    {
        return value_ != 0U;
    }

    friend constexpr bool operator==(
        PresentationTargetId,
        PresentationTargetId) noexcept = default;

private:

    explicit constexpr PresentationTargetId(std::uint64_t value) noexcept
        : value_(value)
    {
    }

    std::uint64_t value_ = 0U;

    friend class Rendering::GraphicsBackend;
};

// Frame-local input. targetId must belong to the Engine receiving the frame.
struct CanvasOutsideInstruction
{
    PresentationTargetId targetId{};
    CanvasOutsideMode mode = CanvasOutsideMode::Opaque;
};

}
