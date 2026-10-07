#include "SceneRuntime.h"

namespace DesktopSceneEngine
{
namespace Runtime
{

void SceneRuntime::setActiveScene(const Domain::EntityId& sceneId)
{
    activeSceneId_ = sceneId;
}

void SceneRuntime::clearActiveScene()
{
    activeSceneId_ = Domain::EntityId{};
}

bool SceneRuntime::hasActiveScene() const noexcept
{
    return activeSceneId_.isValid();
}

const Domain::EntityId& SceneRuntime::activeSceneId() const noexcept
{
    return activeSceneId_;
}

}
}
