#include <DesktopSceneEngine/Domain/DisplayOwner.h>

#include <iostream>

namespace
{

using DesktopSceneEngine::DisplayId;
using DesktopSceneEngine::Domain::DisplayOwner;

bool require(bool condition, const char* message)
{
    if (condition)
    {
        return true;
    }

    std::cerr << "Test failed: " << message << std::endl;
    return false;
}

}

int main()
{
    const DisplayId target = DisplayId::fromTargetDevicePath("target-device-path-a");
    const DisplayId otherTarget = DisplayId::fromTargetDevicePath("target-device-path-b");
    const auto owner = DisplayOwner::fromDisplayId(target);
    const auto equalOwner = DisplayOwner::fromDisplayId(
        DisplayId::fromTargetDevicePath("target-device-path-a"));
    const auto otherOwner = DisplayOwner::fromDisplayId(otherTarget);

    return require(owner.has_value(),
               "A valid TargetDevicePath identity must create a DisplayOwner")
            && require(!DisplayOwner::fromDisplayId(
                    DisplayId::fromSourceDeviceName("\\\\.\\DISPLAY1")).has_value(),
                "A SourceDeviceName identity must not create a DisplayOwner")
            && require(!DisplayOwner::fromDisplayId(DisplayId{}).has_value(),
                "An invalid DisplayId must not create a DisplayOwner")
            && require(owner->displayId() == target,
                "DisplayOwner must expose its exact immutable DisplayId")
            && require(equalOwner.has_value() && *owner == *equalOwner,
                "Owners containing equal identities must compare equal")
            && require(otherOwner.has_value() && *owner != *otherOwner,
                "Owners containing different identities must compare unequal")
        ? 0
        : 1;
}
