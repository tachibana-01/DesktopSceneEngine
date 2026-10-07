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
    DirectCompositionSwapChain
};

enum class CanvasOutsideMode : std::uint8_t
{
    Opaque,
    Transparent
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
