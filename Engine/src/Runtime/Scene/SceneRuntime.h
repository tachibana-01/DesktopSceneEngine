#pragma once

#include <DesktopSceneEngine/Domain/EntityId.h>

namespace DesktopSceneEngine
{
namespace Runtime
{

class SceneRuntime
{
public:

    void setActiveScene(const Domain::EntityId& sceneId);

    void clearActiveScene();

    [[nodiscard]] bool hasActiveScene() const noexcept;

    [[nodiscard]] const Domain::EntityId& activeSceneId() const noexcept;

private:

    Domain::EntityId activeSceneId_{};
};

}
}
