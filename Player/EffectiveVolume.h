#pragma once

#include <algorithm>
#include <cmath>

namespace DesktopSceneEngine::Player
{

[[nodiscard]] inline double calculateEffectiveVolume(
    double playlistVolume,
    double itemVolume
) noexcept
{
    const auto normalized = [](double volume)
    {
        return std::isfinite(volume)
            ? std::clamp(volume, 0.0, 1.0)
            : 1.0;
    };

    return normalized(playlistVolume)
        * normalized(itemVolume);
}

}
