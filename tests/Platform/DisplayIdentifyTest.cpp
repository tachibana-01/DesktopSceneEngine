#include <DesktopSceneEngine/Platform/Display.h>

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

}

int main()
{
    using DesktopSceneEngine::Platform::DisplayIdentifyStatus;
    using DesktopSceneEngine::DisplayId;
    using DesktopSceneEngine::Platform::identifyDisplay;

    return require(
        identifyDisplay(DisplayId{}, 0U) == DisplayIdentifyStatus::InvalidRequest,
        "Identify must reject an invalid neutral request before creating native UI")
        ? 0 : 1;
}
