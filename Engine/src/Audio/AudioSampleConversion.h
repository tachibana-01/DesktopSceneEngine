#pragma once

#include <cstdint>
#include <span>

namespace DesktopSceneEngine::Audio
{

[[nodiscard]] bool convertF32SamplesToS16(
    std::span<const float> source,
    std::span<std::int16_t> destination
) noexcept;

[[nodiscard]] bool convertS16SamplesToF32(
    std::span<const std::int16_t> source,
    std::span<float> destination
) noexcept;

}
