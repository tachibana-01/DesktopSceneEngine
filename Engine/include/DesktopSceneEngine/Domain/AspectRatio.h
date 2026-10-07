#pragma once

#include <numeric>
#include <optional>

namespace DesktopSceneEngine::Domain
{

// A positive logical aspect ratio. It remains a value type so it can describe
// Canvas and presentation semantics without referring to any display or GPU.
struct AspectRatio
{
    int numerator = 16;
    int denominator = 9;

    [[nodiscard]] constexpr bool isValid() const noexcept
    {
        return numerator > 0 && denominator > 0;
    }

    [[nodiscard]] static std::optional<AspectRatio> from(
        int numerator,
        int denominator
    ) noexcept
    {
        if (numerator <= 0 || denominator <= 0)
        {
            return std::nullopt;
        }

        const int divisor = std::gcd(numerator, denominator);
        return AspectRatio{numerator / divisor, denominator / divisor};
    }

    friend constexpr bool operator==(const AspectRatio&, const AspectRatio&) = default;
};

}
