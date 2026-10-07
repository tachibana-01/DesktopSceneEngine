#include "SceneRuntime.h"

#include <DesktopSceneEngine/Domain/EntityId.h>

#include <iostream>
#include <string>

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
    DesktopSceneEngine::Runtime::SceneRuntime runtime{};

    if (!require(!runtime.hasActiveScene(), "SceneRuntime should start without an active Scene"))
    {
        return 1;
    }

    const DesktopSceneEngine::Domain::EntityId firstScene{
        std::string{"scene-1"}
    };
    runtime.setActiveScene(firstScene);

    if (!require(runtime.hasActiveScene(), "SceneRuntime should have an active Scene"))
    {
        return 1;
    }

    if (!require(runtime.activeSceneId() == firstScene, "First active Scene ID should be returned"))
    {
        return 1;
    }

    const DesktopSceneEngine::Domain::EntityId secondScene{
        std::string{"scene-2"}
    };
    runtime.setActiveScene(secondScene);

    if (!require(runtime.activeSceneId() == secondScene, "Second active Scene ID should replace the first"))
    {
        return 1;
    }

    runtime.setActiveScene(secondScene);
    if (!require(runtime.activeSceneId() == secondScene, "Repeated Scene assignment should be safe"))
    {
        return 1;
    }

    runtime.clearActiveScene();
    if (!require(!runtime.hasActiveScene(), "SceneRuntime should clear the active Scene"))
    {
        return 1;
    }

    runtime.clearActiveScene();
    return require(!runtime.hasActiveScene(), "Repeated Scene clearing should be safe") ? 0 : 1;
}
