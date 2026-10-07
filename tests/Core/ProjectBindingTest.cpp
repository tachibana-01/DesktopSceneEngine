#include <DesktopSceneEngine/Domain/Crop.h>
#include <DesktopSceneEngine/Domain/EntityId.h>
#include <DesktopSceneEngine/Domain/Layout.h>
#include <DesktopSceneEngine/Domain/Playback.h>
#include <DesktopSceneEngine/Domain/Project.h>
#include <DesktopSceneEngine/Domain/Scene.h>
#include <DesktopSceneEngine/Engine.h>

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

DesktopSceneEngine::Domain::Project makeProject()
{
    const DesktopSceneEngine::Domain::EntityId sceneId{
        std::string{"scene-1"}
    };
    const DesktopSceneEngine::Domain::Scene scene{
        sceneId,
        DesktopSceneEngine::Domain::EntityId{std::string{"media-1"}},
        DesktopSceneEngine::Domain::Crop{},
        DesktopSceneEngine::Domain::Layout{},
        DesktopSceneEngine::Domain::Playback{false, 1.25F, 0.75F}
    };

    return DesktopSceneEngine::Domain::Project{
        DesktopSceneEngine::Domain::Library{},
        {scene},
        {}
    };
}

}

int main()
{
    const DesktopSceneEngine::Domain::Project project = makeProject();
    const DesktopSceneEngine::Domain::EntityId sceneId{
        std::string{"scene-1"}
    };
    const DesktopSceneEngine::Domain::EntityId invalidSceneId{
        std::string{"missing-scene"}
    };

    DesktopSceneEngine::Engine engine{};

    if (!require(
            !engine.bindProject(project, sceneId),
            "Binding before initialization should fail"))
    {
        return 1;
    }

    engine.initialize();

    if (!require(
            engine.bindProject(project, sceneId),
            "Valid Project binding should succeed"))
    {
        return 1;
    }

    if (!require(!engine.isPaused(), "A newly bound project should be playing")
        || !require(engine.playbackTime() == 0.0F,
            "A newly bound project should start at zero playback time")
        || !require(engine.playbackSpeed() == 1.25F,
            "Playback speed query should expose the bound configuration")
        || !require(engine.sceneCount() == 1U,
            "Scene count query should expose the bound project")
        || !require(engine.activeSceneIndex() == 0U,
            "Active scene query should expose the bound scene")
        || !require(engine.mediaKind()
                == DesktopSceneEngine::Engine::MediaKind::Unknown,
            "Missing media should report an unknown media kind"))
    {
        return 1;
    }

    if (!require(
            !engine.bindProject(project, sceneId),
            "Duplicate Project binding should fail"))
    {
        return 1;
    }

    engine.detachProject();
    engine.detachProject();

    if (!require(
            engine.bindProject(project, sceneId),
            "Binding after detach should succeed"))
    {
        return 1;
    }

    engine.detachProject();

    DesktopSceneEngine::Engine invalidBindingEngine{};
    invalidBindingEngine.initialize();

    if (!require(
            !invalidBindingEngine.bindProject(project, invalidSceneId),
            "Invalid active Scene ID should fail"))
    {
        return 1;
    }

    if (!require(
            invalidBindingEngine.bindProject(project, sceneId),
            "Valid binding should succeed after invalid binding"))
    {
        return 1;
    }

    engine.bindProject(project, sceneId);
    engine.shutdown();
    engine.initialize();

    if (!require(
            engine.bindProject(project, sceneId),
            "Binding should succeed after shutdown and reinitialize"))
    {
        return 1;
    }

    engine.shutdown();
    invalidBindingEngine.shutdown();

    return 0;
}
