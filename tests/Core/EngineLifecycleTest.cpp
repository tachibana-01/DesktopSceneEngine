#include <DesktopSceneEngine/Engine.h>

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
    DesktopSceneEngine::Engine engine;

    if (!require(!engine.isInitialized(), "Engine should start uninitialized"))
    {
        return 1;
    }

    engine.initialize();
    if (!require(engine.isInitialized(), "Engine should initialize"))
    {
        return 1;
    }

    engine.initialize();
    if (!require(engine.isInitialized(), "Repeated initialize should be safe"))
    {
        return 1;
    }

    engine.update();
    if (!require(engine.isInitialized(), "Update should preserve initialized state"))
    {
        return 1;
    }

    engine.shutdown();
    if (!require(!engine.isInitialized(), "Engine should shut down"))
    {
        return 1;
    }

    engine.shutdown();
    if (!require(!engine.isInitialized(), "Repeated shutdown should be safe"))
    {
        return 1;
    }

    return 0;
}
