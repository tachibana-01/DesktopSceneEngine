#include "EffectiveVolume.h"

#include <cmath>
#include <iostream>

namespace
{

bool require(bool condition, const char* message)
{
    if (condition)
    {
        return true;
    }

    std::cerr << "Test failed: " << message << std::endl;
    return false;
}

bool nearlyEqual(double left, double right)
{
    return std::fabs(left - right) <= 0.000001;
}

bool verifyEffectiveVolumeFormula()
{
    using DesktopSceneEngine::Player::calculateEffectiveVolume;

    return require(
               nearlyEqual(calculateEffectiveVolume(1.0, 1.0), 1.0),
               "Default playlist and item factors should preserve full output")
        && require(
            nearlyEqual(calculateEffectiveVolume(0.5, 0.5), 0.25),
            "Playlist and item 50% should produce 25% output")
        && require(
            nearlyEqual(calculateEffectiveVolume(0.8, 0.5), 0.4),
            "Playlist and item factors should multiply")
        && require(
            nearlyEqual(calculateEffectiveVolume(0.5, 0.0), 0.0),
            "A zero item factor should produce zero gain")
        && require(
            nearlyEqual(
                calculateEffectiveVolume(0.73, 1.0),
                0.73),
            "The default item factor should preserve the playlist master volume");
}

}

int main()
{
    return verifyEffectiveVolumeFormula() ? 0 : 1;
}
